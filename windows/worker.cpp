// SPDX-License-Identifier: GPL-3.0-or-later
#include "worker.hpp"
#include <algorithm>
#include <vector>

namespace capt::win {
std::wstring executable_path()
{
    std::vector<wchar_t> path(32768);
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) throw std::runtime_error("Cannot locate executable");
    return {path.data(), length};
}

void Worker::start(const std::wstring& executable, const std::wstring& mode, HANDLE document, HANDLE cancel)
{
    if (active()) throw std::logic_error("A diagnostic is already active");
    if (mode != L"--print" && mode != L"--inspect" && mode != L"--status" && mode != L"--self-test" && mode != L"--watchdog-test")
        throw std::invalid_argument("Unknown worker mode");
    if (cancel && mode != L"--print") throw std::invalid_argument("Cancellation handle requires a print worker");
    if (executable.empty() || executable.find(L'"') != std::wstring::npos)
        throw std::invalid_argument("Invalid executable path");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE raw_read = nullptr, raw_write = nullptr;
    require(CreatePipe(&raw_read, &raw_write, &security, 0), "Create report pipe");
    Handle read(raw_read), write(raw_write);
    require(SetHandleInformation(read.get(), HANDLE_FLAG_INHERIT, 0), "Limit pipe inheritance");
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &security, OPEN_EXISTING, 0, nullptr));
    if (!input) fail("Open child input");
    if (document) {
        HANDLE copy = nullptr;
        require(DuplicateHandle(GetCurrentProcess(), document, GetCurrentProcess(), &copy,
                                GENERIC_READ, TRUE, 0), "Pass document to worker");
        input.reset(copy);
    }
    Handle cancel_copy;
    if (cancel) {
        HANDLE copy = nullptr;
        require(DuplicateHandle(GetCurrentProcess(), cancel, GetCurrentProcess(), &copy,
                                SYNCHRONIZE, TRUE, 0), "Pass job cancellation to worker");
        cancel_copy.reset(copy);
    }
    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    if (!attribute_bytes) fail("Size process attributes");
    std::vector<std::uint64_t> attributes((attribute_bytes + 7) / 8);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    require(InitializeProcThreadAttributeList(list, 1, 0, &attribute_bytes), "Initialize process attributes");
    struct DeleteList { LPPROC_THREAD_ATTRIBUTE_LIST p; ~DeleteList() { DeleteProcThreadAttributeList(p); } } cleanup{list};
    HANDLE inherited[] = {write.get(), input.get(), cancel_copy.get()};
    require(UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                      inherited, sizeof(HANDLE) * (cancel ? 3u : 2u), nullptr, nullptr), "Limit child handles");
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) fail("Create diagnostic job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (mode == L"--print") {
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit = 1;
        limits.ProcessMemoryLimit = 768u * 1024u * 1024u;
    }
    require(SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)), "Set worker lifetime");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = write.get();
    startup.StartupInfo.hStdError = write.get();
    startup.StartupInfo.hStdInput = input.get();
    startup.lpAttributeList = list;
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + executable + L"\" " + mode;
    if (cancel) command += L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(cancel_copy.get()));
    require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                           EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW,
                           nullptr, nullptr, &startup.StartupInfo, &process), "Start diagnostic worker");
    Handle child(process.hProcess), thread(process.hThread);
    // The child cannot access devices until attached to its lifetime job.
    if (!AssignProcessToJobObject(job.get(), child.get())) {
        const auto error = GetLastError();
        TerminateProcess(child.get(), 1);
        fail("Attach diagnostic worker to job", error);
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        const auto error = GetLastError();
        TerminateJobObject(job.get(), 1);
        fail("Resume diagnostic worker", error);
    }
    pipe_ = std::move(read);
    process_ = std::move(child);
    job_ = std::move(job);
}

std::string Worker::read_available()
{
    if (!active()) return {};
    DWORD available = 0;
    if (!PeekNamedPipe(pipe_.get(), nullptr, 0, nullptr, &available, nullptr)) {
        if (GetLastError() == ERROR_BROKEN_PIPE) return {};
        fail("Inspect diagnostic report pipe");
    }
    if (!available) return {};
    std::string bytes(std::min<DWORD>(available, 8192), '\0');
    DWORD read = 0;
    require(ReadFile(pipe_.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr), "Read diagnostic report");
    bytes.resize(read);
    return bytes;
}
bool Worker::exited() const
{
    if (!active()) return true;
    const auto result = WaitForSingleObject(process_.get(), 0);
    if (result == WAIT_FAILED) fail("Observe diagnostic worker");
    return result == WAIT_OBJECT_0;
}
DWORD Worker::exit_code() const
{
    if (!active() || !exited()) throw std::logic_error("Worker has not exited");
    DWORD code = 0;
    require(GetExitCodeProcess(process_.get(), &code), "Read diagnostic exit status");
    return code;
}
void Worker::stop() { if (active()) require(TerminateJobObject(job_.get(), ERROR_CANCELLED), "Stop diagnostic worker"); }
void Worker::clear() { job_.reset(); process_.reset(); pipe_.reset(); }
} // namespace capt::win
