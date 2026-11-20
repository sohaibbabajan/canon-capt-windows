// SPDX-License-Identifier: GPL-3.0-or-later
#include <winsock2.h>
#include <iphlpapi.h>
#include "queue.hpp"
#include "worker.hpp"
#include "../printing/lpr.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace capt::win {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t pending_limit = 3;
Handle stop_event;
Handle cancel_event;
std::atomic<bool> active_job = false;
SERVICE_STATUS_HANDLE registration{};

bool stopping()
{
    return WaitForSingleObject(stop_event.get(), 0) == WAIT_OBJECT_0;
}

void service_state(DWORD current, DWORD error = NO_ERROR)
{
    SERVICE_STATUS status{};
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = current;
    status.dwControlsAccepted = current == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    status.dwWin32ExitCode = error;
    if (current == SERVICE_START_PENDING || current == SERVICE_STOP_PENDING) {
        status.dwCheckPoint = 1;
        status.dwWaitHint = 12000;
    }
    SetServiceStatus(registration, &status);
}

void WINAPI control(DWORD command)
{
    if (command == SERVICE_CONTROL_STOP || command == SERVICE_CONTROL_SHUTDOWN)
        SetEvent(stop_event.get());
    if (command == cancel_job_control && active_job.load()) SetEvent(cancel_event.get());
}

struct Socket {
    SOCKET value;
    ~Socket() { if (value != INVALID_SOCKET) closesocket(value); }
};

struct ScHandle {
    SC_HANDLE value;
    ~ScHandle() { if (value) CloseServiceHandle(value); }
};

DWORD spooler_pid(SC_HANDLE spooler)
{
    SERVICE_STATUS_PROCESS state{};
    DWORD needed = 0;
    if (!QueryServiceStatusEx(spooler, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&state),
                              sizeof(state), &needed) || state.dwCurrentState != SERVICE_RUNNING)
        return 0;
    return state.dwProcessId;
}

bool from_spooler(const sockaddr_in& peer, SC_HANDLE spooler)
{
    const auto pid = spooler_pid(spooler);
    if (!pid || peer.sin_family != AF_INET || peer.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) return false;

    // Look up the client half of this exact IPv4 connection. An ordinary
    // loopback connection by another local process grants no print access.
    DWORD bytes = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!bytes) {
            if (GetExtendedTcpTable(nullptr, &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0)
                != ERROR_INSUFFICIENT_BUFFER) return false;
        }
        if (bytes < sizeof(DWORD) || bytes > 4 * 1024 * 1024) return false;
        std::vector<DWORD> buffer((static_cast<std::size_t>(bytes) + sizeof(DWORD) - 1) / sizeof(DWORD));
        const auto error = GetExtendedTcpTable(buffer.data(), &bytes, FALSE, AF_INET,
                                               TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        if (error == ERROR_INSUFFICIENT_BUFFER) continue;
        if (error != NO_ERROR || bytes < sizeof(DWORD)) return false;
        const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
        if (table->dwNumEntries > (bytes - sizeof(DWORD)) / sizeof(MIB_TCPROW_OWNER_PID)) return false;
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const auto& row = table->table[i];
            if (row.dwLocalAddr == peer.sin_addr.s_addr && row.dwRemoteAddr == htonl(INADDR_LOOPBACK) &&
                static_cast<unsigned short>(row.dwLocalPort) == peer.sin_port &&
                static_cast<unsigned short>(row.dwRemotePort) == htons(lpr_port))
                return row.dwOwningPid == pid && spooler_pid(spooler) == pid;
        }
        return false;
    }
    return false;
}

Handle temporary_document(const fs::path& directory)
{
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Cannot name staging file");
    wchar_t filename[40]{};
    if (!StringFromGUID2(guid, filename, static_cast<int>(std::size(filename))))
        throw std::runtime_error("Cannot format staging file name");
    Handle file(CreateFileW((directory / filename).c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
                            0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    if (!file) fail("Create private print staging file");
    return file;
}

class Connection final : public printing::LprJob {
public:
    Connection(SOCKET socket, const fs::path& staging) : socket_(socket), staging_(staging)
    {
        DWORD timeout = 1000;
        const auto value = reinterpret_cast<const char*>(&timeout);
        if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, value, sizeof(timeout)) != 0 ||
            setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, value, sizeof(timeout)) != 0)
            throw std::runtime_error("Cannot set LPR socket timeouts");
    }

    void read(void* output, std::size_t size) override
    {
        auto* data = static_cast<char*>(output);
        while (size) {
            const auto now = GetTickCount64();
            if (stopping() || now - last_input_ > 15000 || now - started_ > 120000)
                throw std::runtime_error("Print data transfer timed out or was stopped");
            if (begin_ == end_) {
                const int amount = recv(socket_, buffer_.data(), static_cast<int>(buffer_.size()), 0);
                if (amount == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT) continue;
                if (amount <= 0) throw std::runtime_error("Print port connection ended before submission");
                last_input_ = GetTickCount64();
                begin_ = 0;
                end_ = static_cast<std::size_t>(amount);
            }
            const auto count = std::min(size, end_ - begin_);
            std::copy_n(buffer_.data() + begin_, count, data);
            size -= count;
            data += count;
            begin_ += count;
        }
    }

    void write_document(const void* data, std::size_t size) override
    {
        if (!document_) document_ = temporary_document(staging_);
        DWORD count = 0;
        require(WriteFile(document_.get(), data, static_cast<DWORD>(size), &count, nullptr), "Stage print data");
        if (count != size) throw std::runtime_error("Incomplete print staging write");
    }

    void validate_document() override { validate_pdf_file(document_.get()); }

    void acknowledge(bool accepted) override
    {
        const char response = accepted ? 0 : 1;
        if (send(socket_, &response, 1, 0) != 1) throw std::runtime_error("Cannot acknowledge print data");
    }

    Handle take_document() { return std::move(document_); }

private:
    SOCKET socket_;
    const fs::path& staging_;
    ULONGLONG started_ = GetTickCount64();
    ULONGLONG last_input_ = started_;
    std::array<char, 32768> buffer_{};
    std::size_t begin_ = 0, end_ = 0;
    Handle document_;
};

class PrinterQueue {
public:
    explicit PrinterQueue(fs::path directory)
        : report_(std::move(directory) / state_dir_name / report_name), thread_([this] { print_pending(); }) {}

    ~PrinterQueue()
    {
        SetEvent(stop_event.get());
        wake_.notify_one();
        thread_.join();
    }

    bool full()
    {
        std::lock_guard lock(mutex_);
        return count_ >= pending_limit;
    }

    void submit(Handle file)
    {
        {
            std::lock_guard lock(mutex_);
            pending_[(next_ + count_) % pending_limit] = std::move(file);
            ++count_;
        }
        wake_.notify_one();
    }

    void report(const std::string& message)
    {
        std::lock_guard lock(report_mutex_);
        std::ofstream(report_, std::ios::trunc) << message;
    }

private:
    void print_pending()
    {
        for (;;) {
            Handle file;
            {
                std::unique_lock lock(mutex_);
                // Lock and periodic wake handle an SCM stop with an empty queue.
                wake_.wait_for(lock, std::chrono::milliseconds(250), [&] { return count_ != 0 || stopping(); });
                if (stopping()) return;
                if (!count_) continue;
                file = std::move(pending_[next_]);
                next_ = (next_ + 1) % pending_limit;
                --count_;
            }
            std::ostringstream log;
            try {
                require(ResetEvent(cancel_event.get()), "Reset job cancellation event");
                active_job.store(true);
                report("Print job running. Refresh this report after the printer stops.\n");
                const int result = print_file(file.get(), log, 20 * 60 * 1000, stop_event.get(), cancel_event.get());
                log << "\nWorker exit " << result << '\n';
            } catch (const std::exception& error) {
                log << "\nPrint worker failed: " << error.what() << '\n';
            }
            active_job.store(false);
            report(log.str());
        }
    }

    const fs::path report_;
    std::mutex mutex_, report_mutex_;
    std::condition_variable wake_;
    std::array<Handle, pending_limit> pending_;
    std::size_t next_ = 0, count_ = 0;
    std::thread thread_;
};

void listen_for_jobs(const fs::path& directory)
{
    WSADATA ws{};
    if (WSAStartup(MAKEWORD(2, 2), &ws) != 0) throw std::runtime_error("Winsock startup failed");
    struct Winsock { ~Winsock() { WSACleanup(); } } winsock;
    Socket listener{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    const BOOL exclusive = TRUE;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(lpr_port);
    if (listener.value == INVALID_SOCKET ||
        setsockopt(listener.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0 ||
        bind(listener.value, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener.value, 4) != 0)
        throw std::runtime_error("Cannot bind local print port 127.0.0.1:515 (Winsock " +
                                 std::to_string(WSAGetLastError()) + ")");
    const ScHandle manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
    if (!manager.value) fail("Open Service Control Manager");
    const ScHandle spooler{OpenServiceW(manager.value, L"Spooler", SERVICE_QUERY_STATUS)};
    if (!spooler.value) fail("Read Print Spooler service status");

    const auto staging = directory / jobs_dir_name;
    PrinterQueue printer(directory);
    printer.report("CAPT queue service ready. No completed job in this session.\n");
    service_state(SERVICE_RUNNING);
    while (!stopping()) {
        fd_set read{};
        FD_SET(listener.value, &read);
        timeval timeout{0, 250000};
        const int ready = select(0, &read, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) throw std::runtime_error("Print listener failed");
        if (ready == 0) continue;

        sockaddr_in peer{};
        int length = sizeof(peer);
        Socket client{accept(listener.value, reinterpret_cast<sockaddr*>(&peer), &length)};
        if (client.value == INVALID_SOCKET || length != sizeof(peer) || !from_spooler(peer, spooler.value))
            continue;
        try {
            Connection connection(client.value, staging);
            if (printer.full() || stopping()) {
                connection.acknowledge(false);
                continue;
            }
            if (printing::receive_lpr_job(connection)) printer.submit(connection.take_document());
        } catch (const std::exception& error) {
            printer.report(std::string("Queue transfer failed: ") + error.what() + '\n');
        }
    }
    service_state(SERVICE_STOP_PENDING);
}

void WINAPI service(DWORD, LPWSTR*)
{
    stop_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    cancel_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    registration = RegisterServiceCtrlHandlerW(service_name, control);
    if (!registration) return;
    service_state(SERVICE_START_PENDING);
    try {
        if (!stop_event || !cancel_event) throw std::runtime_error("Cannot create print service events");
        const auto directory = fs::path(executable_path()).parent_path();
        try {
            listen_for_jobs(directory);
        } catch (const std::exception& error) {
            std::ofstream(directory / state_dir_name / report_name) << "CAPT service failed: " << error.what() << '\n';
            throw;
        }
        service_state(SERVICE_STOPPED);
    } catch (...) {
        service_state(SERVICE_STOPPED, ERROR_GEN_FAILURE);
    }
}
}

int run_queue_service()
{
    SERVICE_TABLE_ENTRYW services[] = {{const_cast<LPWSTR>(service_name), service}, {nullptr, nullptr}};
    return StartServiceCtrlDispatcherW(services) ? 0 : 1;
}
}
