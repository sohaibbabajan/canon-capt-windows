// SPDX-License-Identifier: GPL-3.0-or-later
#include "worker.hpp"
#include <commdlg.h>
#include <algorithm>
#include <cstring>

namespace capt::win {
namespace {
constexpr int inspect_button = 101, status_button = 102, cancel_button = 103, save_button = 104, self_button = 105;
constexpr std::size_t report_limit = 256 * 1024;
struct App {
    HWND window = nullptr, output = nullptr;
    Worker worker;
    std::string report;
    ULONGLONG started = 0;
    bool stopping = false;
    bool failed = false;

    void append(const std::string& text)
    {
        if (text.size() > report_limit - report.size()) throw std::runtime_error("Diagnostic report exceeded 256 KiB");
        report += text;
        std::wstring wide(text.begin(), text.end());
        SendMessageW(output, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        SendMessageW(output, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(wide.c_str()));
    }
    void buttons(bool running)
    {
        for (int id : {inspect_button, status_button, self_button}) EnableWindow(GetDlgItem(window, id), !running);
        EnableWindow(GetDlgItem(window, cancel_button), running && !stopping);
    }
    void start(const std::wstring& mode)
    {
        worker.start(executable_path(), mode);
        report.clear();
        SetWindowTextW(output, L"");
        started = GetTickCount64();
        stopping = false;
        failed = false;
        buttons(true);
        append("Checking... The window remains available while the USB worker runs.\r\n");
    }
    void stop(const std::string& why)
    {
        if (!worker.active() || stopping) return;
        worker.stop();
        stopping = true;
        buttons(true);
        append("\r\n" + why + " Worker termination requested; waiting for Windows to release it.\r\n");
    }
    void tick()
    {
        if (!worker.active()) return;
        if (failed) {
            if (worker.exited()) { worker.clear(); buttons(false); }
            return;
        }
        const auto bytes = worker.read_available();
        if (!bytes.empty()) append(bytes);
        if (worker.exited()) {
            // Drain the remaining bounded report over later timer ticks.
            if (!bytes.empty()) return;
            const auto code = worker.exit_code();
            append("\r\nWorker exited with code " + std::to_string(code) +
                   (stopping ? ". Check interrupted; result is incomplete.\r\n" : ".\r\n"));
            worker.clear();
            buttons(false);
        } else if (!stopping && GetTickCount64() - started >= 30000)
            stop("The 30-second diagnostic limit was reached.");
    }
    void save()
    {
        wchar_t path[32768] = L"capt-connection-report.txt";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFilter = L"Text report (*.txt)\0*.txt\0\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = 32768;
        dialog.lpstrDefExt = L"txt";
        dialog.lpstrTitle = L"Save report (may contain printer serial numbers)";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog)) {
            const auto error = CommDlgExtendedError();
            if (error) throw std::runtime_error("Save dialog failed: " + std::to_string(error));
            return;
        }
        Handle file(CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) fail("Open report file");
        std::size_t pos = 0;
        while (pos < report.size()) {
            DWORD written = 0;
            require(WriteFile(file.get(), report.data() + pos, static_cast<DWORD>(report.size() - pos), &written, nullptr), "Save report");
            if (!written) throw std::runtime_error("Report file write made no progress");
            pos += written;
        }
    }
};
void layout(HWND window, HWND output)
{
    RECT bounds{};
    GetClientRect(window, &bounds);
    MoveWindow(output, 12, 105, std::max(100L, bounds.right - 24), std::max(100L, bounds.bottom - 117), TRUE);
}
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wp, lp);
    try {
        switch (message) {
        case WM_CREATE: {
            auto control = [&](const wchar_t* kind, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
                const auto child = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
                if (!child) fail("Create window control");
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
                return child;
            };
            control(L"STATIC", L"Canon CAPT connection check - development diagnostic, not an installed printer", 0, 12, 10, 850, 20, 0);
            control(L"STATIC", L"Connect and power on the printer. Close active print jobs. A status check does not print a page.", 0, 12, 34, 850, 20, 0);
            control(L"BUTTON", L"Inspect USB printers", WS_TABSTOP, 12, 64, 155, 28, inspect_button);
            control(L"BUTTON", L"Check Canon status", WS_TABSTOP, 177, 64, 155, 28, status_button);
            control(L"BUTTON", L"Cancel", WS_TABSTOP, 342, 64, 80, 28, cancel_button);
            control(L"BUTTON", L"Save report", WS_TABSTOP, 432, 64, 100, 28, save_button);
            control(L"BUTTON", L"Check this app", WS_TABSTOP, 542, 64, 115, 28, self_button);
            app->output = control(L"EDIT", L"", WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_TABSTOP,
                                  12, 105, 850, 430, 106);
            SendMessageW(app->output, EM_SETLIMITTEXT, report_limit, 0);
            app->append("Start with Inspect USB printers. Save the report if a check fails.\r\n"
                        "Check this app tests worker launch without accessing a printer.\r\n"
                        "No driver, queue, service, or USB binding is installed or changed by this diagnostic.\r\n");
            app->buttons(false);
            layout(window, app->output);
            if (!SetTimer(window, 1, 100, nullptr)) fail("Start diagnostic timer");
            return 0;
        }
        case WM_SIZE: if (app->output) layout(window, app->output); return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {720, 380}; return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
            case inspect_button: app->start(L"--inspect"); break;
            case status_button: app->start(L"--status"); break;
            case cancel_button: app->stop("Cancelled by user."); break;
            case save_button: app->save(); break;
            case self_button: app->start(L"--self-test"); break;
            }
            return 0;
        case WM_TIMER: app->tick(); return 0;
        case WM_DESTROY:
            KillTimer(window, 1);
            app->worker.clear();
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception& e) {
        // Keep a failed worker supervised, without reading a broken/full report
        // pipe again. Do not permit another check until Windows releases it.
        if (app->worker.active()) {
            try { app->worker.stop(); } catch (...) {} // close still closes the job
            app->stopping = true;
            app->failed = true;
            app->buttons(true);
        } else app->buttons(false);
        const std::string text = e.what();
        const std::wstring wide(text.begin(), text.end());
        MessageBoxW(window, wide.c_str(), L"Connection check failed", MB_OK | MB_ICONERROR);
        if (message == WM_CREATE) return -1;
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
}
} // namespace capt::win

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show)
{
    using namespace capt::win;
    // Only fixed, private worker modes. No arbitrary path or CAPT bytes accepted.
    const std::wstring mode(command);
    if (mode == L"--inspect") return run_probe(false);
    if (mode == L"--status") return run_probe(true);
    if (mode == L"--self-test") {
        constexpr char result[] = "Worker launch and report pipe passed. No USB access.\r\n";
        DWORD written = 0;
        return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), result, sizeof(result) - 1, &written, nullptr)
            && written == sizeof(result) - 1 ? 0 : 1;
    }
    if (mode == L"--watchdog-test") { Sleep(INFINITE); return 1; }
    if (!mode.empty()) return 2;
    App app;
    WNDCLASSW type{};
    type.lpfnWndProc = window_proc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    type.lpszClassName = L"IndependentCaptConnectionCheck";
    if (!RegisterClassW(&type)) return 1;
    const auto window = CreateWindowExW(0, type.lpszClassName, L"CAPT Connection Check", WS_OVERLAPPEDWINDOW,
                                       CW_USEDEFAULT, CW_USEDEFAULT, 910, 610, nullptr, nullptr, instance, &app);
    if (!window) return 1;
    ShowWindow(window, show);
    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}
