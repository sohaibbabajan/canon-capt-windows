// SPDX-License-Identifier: GPL-3.0-or-later
#include "queue.hpp"
#include "worker.hpp"
#include "../printing/document.hpp"
#include "../printing/lpr.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <thread>

namespace capt::win {
namespace {
void line(const std::string& text)
{
    const std::string message = text + "\r\n";
    DWORD count = 0;
    require(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message.data(), static_cast<DWORD>(message.size()),
                      &count, nullptr), "Write progress");
    if (count != message.size()) throw std::runtime_error("Incomplete progress report");
}

class Clock final : public Runtime {
    Instant now() const override { return std::chrono::steady_clock::now(); }
    void wait_until(Instant wake, const Cancellation& token) override
    {
        while (!token.requested() && now() < wake)
            std::this_thread::sleep_until(std::min(wake, now() + Milliseconds(25)));
    }
};

DWORD pdf_size(HANDLE file)
{
    LARGE_INTEGER size{};
    require(GetFileSizeEx(file, &size), "Read print data size");
    if (size.QuadPart < 10 || size.QuadPart > static_cast<LONGLONG>(printing::max_pdf_size))
        throw std::runtime_error("Print data size is outside the 64 MiB limit");
    return static_cast<DWORD>(size.QuadPart);
}

void seek(HANDLE file, DWORD offset)
{
    LARGE_INTEGER position{};
    position.QuadPart = offset;
    require(SetFilePointerEx(file, position, nullptr, FILE_BEGIN), "Seek print data");
}

void read_exact(HANDLE file, void* data, DWORD size)
{
    DWORD count = 0;
    require(ReadFile(file, data, size, &count, nullptr), "Read print data");
    if (count != size) throw std::runtime_error("Incomplete print data");
}

int render_job(bool test_page, HANDLE cancel_event)
{
    try {
        Cancellation token;
        Handle done(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!done) fail("Create print worker exit event");
        std::thread observer;
        if (cancel_event) {
            observer = std::thread([&] {
                HANDLE events[] = {done.get(), cancel_event};
                if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0) token.request();
            });
        }
        struct Join {
            HANDLE done;
            std::thread& observer;
            ~Join() { SetEvent(done); if (observer.joinable()) observer.join(); }
        } join{done.get(), observer};
        HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        validate_pdf_file(input);
        Bytes data(pdf_size(input));
        read_exact(input, data.data(), static_cast<DWORD>(data.size()));
        FPDF_InitLibrary();
        struct Library { ~Library() { FPDF_DestroyLibrary(); } } library;
        std::unique_ptr<fpdf_document_t__, decltype(&FPDF_CloseDocument)> document(
            FPDF_LoadMemDocument64(data.data(), data.size(), nullptr), FPDF_CloseDocument);
        if (!document) throw std::runtime_error("Cannot load the PDF from the print queue");
        if (test_page) printing::require_a4_test_page(document.get());
        return run_probe(true, [&](SessionTransport& usb) {
            SYSTEMTIME time{};
            GetLocalTime(&time);
            Clock clock;
            Lbp6000Session session(usb, clock);
            printing::print_document(document.get(), session,
                {time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond}, token, line,
                test_page ? GrayConversion::threshold : GrayConversion::ordered_dither);
            line("Printer completion received");
        }, test_page);
    } catch (const std::exception& error) {
        line(std::string("Print failed: ") + error.what());
        return 1;
    }
}
}

void validate_pdf_file(HANDLE file)
{
    const auto size = pdf_size(file);
    std::array<char, 5> signature;
    seek(file, 0);
    read_exact(file, signature.data(), static_cast<DWORD>(signature.size()));
    if (std::memcmp(signature.data(), "%PDF-", 5) != 0)
        throw std::runtime_error("Windows renderer delivered non-PDF print data");
    std::array<char, 8192> tail;
    const auto length = std::min(size, static_cast<DWORD>(tail.size()));
    seek(file, size - length);
    read_exact(file, tail.data(), length);
    std::string_view trailer(tail.data(), length);
    const auto end = trailer.find_last_not_of(std::string_view(" \t\r\n\f\0", 6));
    if (end == std::string_view::npos || end < 4 || trailer.substr(end - 4, 5) != "%%EOF")
        throw std::runtime_error("Print data is missing its final PDF marker");
    seek(file, 0);
}

int print_file(HANDLE file, std::ostream& log, ULONGLONG timeout_ms, HANDLE stop, HANDLE cancel)
{
    Handle request(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!request) fail("Create print cancellation event");
    Worker child;
    child.start(executable_path(), L"--print", file, request.get());
    const auto started = GetTickCount64();
    auto last_report = started;
    ULONGLONG cancel_requested = 0;
    for (;;) {
        const bool finished = child.exited();
        const auto bytes = child.read_available();
        log << bytes << std::flush;
        if (!bytes.empty()) last_report = GetTickCount64();
        if (finished && bytes.empty()) return static_cast<int>(child.exit_code());

        const auto now = GetTickCount64();
        if (!cancel_requested && ((stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) ||
            (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) ||
            now - started >= timeout_ms || now - last_report >= 130000)) {
            log << "Cancellation requested.\n";
            require(SetEvent(request.get()), "Request job cancellation");
            cancel_requested = now;
        }
        if (cancel_requested && now - cancel_requested >= 7000) {
            child.stop();
            log << "Print stopped. Check the printer before resubmitting.\n";
            return 1;
        }
        if (bytes.empty()) Sleep(30);
    }
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command, int)
{
    using namespace capt::win;
    const std::wstring_view mode(command);
    constexpr std::wstring_view prefix = L"--print ";
    if (mode.substr(0, prefix.size()) == prefix) {
        const auto number = mode.substr(prefix.size());
        if (number.empty() || number.size() > 20 || number.find_first_not_of(L"0123456789") != std::wstring_view::npos)
            return 2;
        try {
            const auto value = std::stoull(std::wstring(number));
            const auto event = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
#ifdef CAPT_QUEUE
            return render_job(false, event);
#else
            return render_job(true, event);
#endif
        } catch (...) { return 2; }
    }
#ifdef CAPT_QUEUE
    if (mode == L"--service") return run_queue_service();
    MessageBoxW(nullptr, L"Run CAPT-Setup.exe to install or inspect the printer queue.",
                L"Canon CAPT", MB_OK | MB_ICONINFORMATION);
    return 2;
#else
    try {
        if (!mode.empty()) return 2;
        if (MessageBoxW(nullptr, L"Print one A4 CAPT test sheet on the connected Canon?",
                        L"Canon LBP6000 USB test", MB_YESNO | MB_DEFBUTTON2) != IDYES) return 0;
        const auto sample = std::filesystem::path(executable_path()).parent_path() / L"Test-page.pdf";
        Handle file(CreateFileW(sample.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!file) fail("Open Test-page.pdf");
        std::ostringstream log;
        const int result = print_file(file.get(), log, 5 * 60 * 1000);
        log << "\nWorker exit " << result << '\n';
        const auto text = log.str();
        const std::wstring report(text.begin(), text.end());
        MessageBoxW(nullptr, report.c_str(), L"Canon CAPT print report (Ctrl+C to copy)", MB_OK);
        return result;
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "CAPT print failed", MB_ICONERROR);
        return 1;
    }
#endif
}
