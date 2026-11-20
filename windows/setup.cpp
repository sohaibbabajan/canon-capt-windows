// SPDX-License-Identifier: GPL-3.0-or-later
#include "queue.hpp"
#include "worker.hpp"

#include <commctrl.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tcpxcv.h>
#include <winspool.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <memory>
#include <string_view>
#include <vector>

namespace {
using namespace capt::win;
namespace fs = std::filesystem;
constexpr wchar_t pdf_driver[] = L"Microsoft Print To PDF";
constexpr wchar_t installation_marker[] = L"installation-id";
constexpr std::string_view installation_id = "Independent Canon CAPT ARM64 queue\n";
constexpr wchar_t preferences[] = L"Software\\IndependentCanonCAPT";
bool arabic = false;

const wchar_t* local(const wchar_t* english, const wchar_t* translated)
{
    return arabic ? translated : english;
}

UINT reading_direction()
{
    return arabic ? MB_RIGHT | MB_RTLREADING : 0;
}

const wchar_t* title()
{
    return local(L"Canon LBP6000 CAPT Setup", L"إعداد طابعة Canon LBP6000 CAPT");
}

bool prefer_arabic()
{
    DWORD language = 0, bytes = sizeof(language);
    if (RegGetValueW(HKEY_CURRENT_USER, preferences, L"Arabic", RRF_RT_REG_DWORD,
                     nullptr, &language, &bytes) == ERROR_SUCCESS)
        return language == 1;
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_ARABIC;
}

void remember_language()
{
    const DWORD language = arabic ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, preferences, L"Arabic", REG_DWORD, &language, sizeof(language));
}

std::wstring widen(const std::string& text)
{
    const auto count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (count <= 1) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), count);
    result.pop_back();
    return result;
}

void show_result(const wchar_t* english, const wchar_t* translated)
{
    MessageBoxW(nullptr, local(english, translated), title(), MB_OK | MB_ICONINFORMATION | reading_direction());
}

struct ScCloser { void operator()(SC_HANDLE value) const { CloseServiceHandle(value); } };
using ServiceHandle = std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, ScCloser>;
struct PrinterCloser { void operator()(HANDLE value) const { ClosePrinter(value); } };
using PrinterHandle = std::unique_ptr<void, PrinterCloser>;

fs::path target_directory()
{
    wchar_t program_files[MAX_PATH]{};
    const auto result = SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, program_files);
    if (FAILED(result)) throw std::runtime_error("Cannot locate Program Files");
    return fs::path(program_files) / L"Independent Canon CAPT";
}

bool same(std::wstring_view first, const wchar_t* second)
{
    if (!second) return false;
    return CompareStringOrdinal(first.data(), static_cast<int>(first.size()), second, -1, TRUE) == CSTR_EQUAL;
}

PrinterHandle open_printer(std::wstring name, ACCESS_MASK access, bool optional = false)
{
    PRINTER_DEFAULTSW defaults{};
    defaults.DesiredAccess = access;
    HANDLE printer = nullptr;
    if (OpenPrinterW(name.data(), &printer, &defaults)) return PrinterHandle(printer);
    const auto code = GetLastError();
    if (optional && (code == ERROR_INVALID_PRINTER_NAME || code == ERROR_PRINTER_NOT_FOUND)) return {};
    fail("Open Windows print queue or port monitor", code);
}

void require_pdf_driver()
{
    DWORD bytes = 0, count = 0;
    if (!EnumPrinterDriversW(nullptr, nullptr, 2, nullptr, 0, &bytes, &count) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("List inbox Windows printer drivers");
    std::vector<BYTE> data(bytes);
    if (bytes)
        require(EnumPrinterDriversW(nullptr, nullptr, 2, data.data(), bytes, &bytes, &count), "List Windows printer drivers");
    const auto* drivers = reinterpret_cast<const DRIVER_INFO_2W*>(data.data());
    for (DWORD i = 0; i < count; ++i)
        if (same(pdf_driver, drivers[i].pName)) return;
    throw std::runtime_error(arabic
        ? u8"فعّل ميزة Microsoft Print to PDF من نافذة ميزات Windows، ثم شغّل برنامج الإعداد مرة أخرى."
        : "Enable Microsoft Print to PDF in Windows Features, then rerun Setup.");
}

bool port_exists()
{
    DWORD bytes = 0, count = 0;
    if (!EnumPortsW(nullptr, 1, nullptr, 0, &bytes, &count) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("List Windows print ports");
    std::vector<BYTE> data(bytes);
    if (bytes)
        require(EnumPortsW(nullptr, 1, data.data(), bytes, &bytes, &count), "List Windows print ports");
    const auto* ports = reinterpret_cast<const PORT_INFO_1W*>(data.data());
    for (DWORD i = 0; i < count; ++i)
        if (same(port_name, ports[i].pName)) return true;
    return false;
}

template<class Data>
void xcv(HANDLE handle, const wchar_t* name, Data& data, BYTE* output = nullptr, DWORD output_size = 0)
{
    DWORD needed = 0, status = 0;
    require(XcvDataW(handle, name, reinterpret_cast<BYTE*>(&data), sizeof(data),
                     output, output_size, &needed, &status), "Contact Windows TCP/IP port monitor");
    if (status != NO_ERROR) fail("Configure or inspect Canon CAPT print port", status);
}

constexpr wchar_t spool_protocol_queue[] = L"capt";

void add_port()
{
    auto monitor = open_printer(L",XcvMonitor Standard TCP/IP Port", SERVER_ACCESS_ADMINISTER);
    PORT_DATA_1 config{};
    std::copy_n(port_name, std::size(port_name), config.sztPortName);
    std::copy_n(host_name, std::size(host_name), config.sztHostAddress);
    std::copy_n(host_name, std::size(host_name), config.sztIPAddress);
    std::copy_n(spool_protocol_queue, std::size(spool_protocol_queue), config.sztQueue);
    config.dwVersion = 1;
    config.cbSize = sizeof(config);
    config.dwProtocol = PROTOCOL_LPR_TYPE;
    config.dwDoubleSpool = TRUE;
    config.dwPortNumber = lpr_port;
    xcv(monitor.get(), L"AddPort", config);
}

void verify_port()
{
    auto port = open_printer(std::wstring(L",XcvPort ") + port_name, SERVER_ACCESS_ADMINISTER);
    CONFIG_INFO_DATA_1 query{};
    query.dwVersion = 1;
    PORT_DATA_1 config{};
    xcv(port.get(), L"GetConfigInfo", query, reinterpret_cast<BYTE*>(&config), sizeof(config));
    if (config.dwProtocol != PROTOCOL_LPR_TYPE || config.dwPortNumber != lpr_port ||
        config.dwDoubleSpool != TRUE || config.dwSNMPEnabled ||
        !same(host_name, config.sztHostAddress) || !same(spool_protocol_queue, config.sztQueue))
        throw std::runtime_error("The existing CAPT-named port has different settings. Setup left it in place.");
}

void delete_port()
{
    auto monitor = open_printer(L",XcvMonitor Standard TCP/IP Port", SERVER_ACCESS_ADMINISTER);
    DELETE_PORT_DATA_1 config{};
    config.dwVersion = 1;
    std::copy_n(port_name, std::size(port_name), config.psztPortName);
    xcv(monitor.get(), L"DeletePort", config);
}

PrinterHandle add_queue()
{
    HANDLE token = nullptr;
    require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &token),
            "Open setup token");
    Handle privileges(token);
    TOKEN_PRIVILEGES desired{}, previous{};
    desired.PrivilegeCount = 1;
    desired.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    require(LookupPrivilegeValueW(nullptr, SE_LOAD_DRIVER_NAME, &desired.Privileges[0].Luid),
            "Find the Windows printer installation privilege");
    DWORD bytes = 0;
    require(AdjustTokenPrivileges(token, FALSE, &desired, sizeof(previous), &previous, &bytes),
            "Enable the Windows printer installation privilege");
    if (GetLastError() != ERROR_SUCCESS) fail("Enable the Windows printer installation privilege");
    struct Restore {
        HANDLE token;
        TOKEN_PRIVILEGES& previous;
        ~Restore() { AdjustTokenPrivileges(token, FALSE, &previous, 0, nullptr, nullptr); }
    } restore{token, previous};

    PRINTER_INFO_2W printer{};
    printer.pPrinterName = const_cast<wchar_t*>(queue_name);
    printer.pPortName = const_cast<wchar_t*>(port_name);
    printer.pDriverName = const_cast<wchar_t*>(pdf_driver);
    printer.pComment = const_cast<wchar_t*>(queue_marker);
    printer.pPrintProcessor = const_cast<wchar_t*>(L"winprint");
    printer.pDatatype = const_cast<wchar_t*>(L"RAW");
    printer.Attributes = PRINTER_ATTRIBUTE_LOCAL | PRINTER_ATTRIBUTE_QUEUED;
    PrinterHandle added(AddPrinterW(nullptr, 2, reinterpret_cast<BYTE*>(&printer)));
    if (!added) fail("Add Canon CAPT queue using Microsoft Print to PDF");
    return added;
}

void set_a4(HANDLE printer)
{
    auto* name = const_cast<wchar_t*>(queue_name);
    const auto size = DocumentPropertiesW(nullptr, printer, name, nullptr, nullptr, 0);
    if (size < static_cast<LONG>(sizeof(DEVMODEW))) throw std::runtime_error("Cannot inspect queue paper defaults");
    std::vector<BYTE> data(static_cast<std::size_t>(size));
    auto* mode = reinterpret_cast<DEVMODEW*>(data.data());
    if (DocumentPropertiesW(nullptr, printer, name, mode, nullptr, DM_OUT_BUFFER) != IDOK)
        throw std::runtime_error("Cannot read queue paper defaults");
    mode->dmFields &= ~static_cast<DWORD>(DM_PAPERLENGTH | DM_PAPERWIDTH | DM_FORMNAME);
    mode->dmFields |= DM_PAPERSIZE | DM_ORIENTATION;
    mode->dmPaperSize = DMPAPER_A4;
    mode->dmOrientation = DMORIENT_PORTRAIT;
    if (DocumentPropertiesW(nullptr, printer, name, mode, mode, DM_IN_BUFFER | DM_OUT_BUFFER) != IDOK ||
        mode->dmPaperSize != DMPAPER_A4)
        throw std::runtime_error("Windows PDF renderer did not accept A4 paper");
    PRINTER_INFO_8W defaults{mode};
    require(SetPrinterW(printer, 8, reinterpret_cast<BYTE*>(&defaults), 0), "Save A4 as the queue default");
}

void verify_queue(HANDLE printer)
{
    DWORD bytes = 0;
    if (!GetPrinterW(printer, 2, nullptr, 0, &bytes) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("Inspect queue before removing");
    std::vector<BYTE> data(bytes);
    require(GetPrinterW(printer, 2, data.data(), bytes, &bytes), "Inspect queue before removing");
    const auto& details = *reinterpret_cast<const PRINTER_INFO_2W*>(data.data());
    if (!same(queue_marker, details.pComment) || !same(port_name, details.pPortName))
        throw std::runtime_error("The queue with this name belongs to a different installation.");
    if (details.cJobs) throw std::runtime_error(arabic
        ? u8"انتظر إلى أن تنتهي المستندات في قائمة انتظار الطباعة قبل إزالة الطابعة."
        : "Wait for queued documents to finish before removing the printer.");
}

std::wstring service_command(const fs::path& directory)
{
    return L"\"" + (directory / worker_name).wstring() + L"\" --service";
}

ServiceHandle open_service(SC_HANDLE manager)
{
    ServiceHandle service(OpenServiceW(manager, service_name, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG |
                                         SERVICE_STOP | DELETE));
    if (!service && GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST) fail("Open Canon CAPT service");
    return service;
}

void verify_service(SC_HANDLE service, const fs::path& directory)
{
    DWORD bytes = 0;
    if (!QueryServiceConfigW(service, nullptr, 0, &bytes) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("Inspect CAPT service");
    std::vector<BYTE> data(bytes);
    auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(data.data());
    require(QueryServiceConfigW(service, config, bytes, &bytes), "Inspect CAPT service");
    if (!same(service_command(directory), config->lpBinaryPathName) ||
        !same(service_account, config->lpServiceStartName))
        throw std::runtime_error("The existing CAPT-named service belongs to a different installation.");
}

DWORD current_service_state(SC_HANDLE service)
{
    SERVICE_STATUS status{};
    require(QueryServiceStatus(service, &status), "Read queue service state");
    return status.dwCurrentState;
}

void wait_service(SC_HANDLE service, DWORD goal)
{
    const auto start = GetTickCount64();
    for (;;) {
        const auto state = current_service_state(service);
        if (state == goal) return;
        if ((goal == SERVICE_RUNNING && state == SERVICE_STOPPED) || GetTickCount64() - start >= 15000)
            throw std::runtime_error("CAPT service did not reach the requested state. Check whether local port 515 is in use.");
        Sleep(100);
    }
}

void remove_service(SC_HANDLE service)
{
    if (current_service_state(service) != SERVICE_STOPPED) {
        SERVICE_STATUS state{};
        if (!ControlService(service, SERVICE_CONTROL_STOP, &state) && GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
            fail("Stop CAPT queue service");
        wait_service(service, SERVICE_STOPPED);
    }
    require(DeleteService(service), "Remove CAPT queue service");
}

void secured_directory(const fs::path& directory, const wchar_t* acl)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(acl, SDDL_REVISION_1, &descriptor, nullptr),
            "Prepare installation folder permissions");
    struct Free { PSECURITY_DESCRIPTOR value; ~Free() { LocalFree(value); } } free{descriptor};
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    require(CreateDirectoryW(directory.c_str(), &security), "Create protected installation folder");
}

void remove_files(const fs::path& directory)
{
    const auto attributes = GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return;
        fail("Read installation folder", error);
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("The CAPT installation folder was replaced. Setup left it in place.");
    for (const auto& entry : fs::directory_iterator(directory)) {
        const auto name = entry.path().filename().wstring();
        if (!same(name, worker_name) && !same(name, pdfium_name) && !same(name, state_dir_name) &&
            !same(name, jobs_dir_name) && !same(name, installation_marker))
            throw std::runtime_error("The CAPT folder contains an unexpected file. Setup left the folder in place.");
    }
    for (const auto* child : {state_dir_name, jobs_dir_name}) {
        const auto info = GetFileAttributesW((directory / child).c_str());
        if (info != INVALID_FILE_ATTRIBUTES &&
            (!(info & FILE_ATTRIBUTE_DIRECTORY) || (info & FILE_ATTRIBUTE_REPARSE_POINT)))
            throw std::runtime_error("A CAPT data folder was replaced. Setup left it in place.");
    }
    fs::remove(directory / worker_name);
    fs::remove(directory / pdfium_name);
    fs::remove(directory / state_dir_name / report_name);
    fs::remove(directory / state_dir_name);
    fs::remove(directory / jobs_dir_name);
    fs::remove(directory / installation_marker);
    fs::remove(directory);
}

void copy_payload(const fs::path& source, const fs::path& destination)
{
    Handle input(CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!input) fail("Open a CAPT package file");
    BY_HANDLE_FILE_INFORMATION details{};
    require(GetFileInformationByHandle(input.get(), &details), "Inspect package file");
    if ((details.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        details.nFileSizeHigh || !details.nFileSizeLow || details.nFileSizeLow > 32 * 1024 * 1024)
        throw std::runtime_error("The package file is invalid. Extract the entire ZIP before running Setup.");
    // Create the destination with inherited Program Files permissions.
    // CopyFile can carry security information from the source.
    Handle output(CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!output) fail("Create protected CAPT program file");
    std::array<BYTE, 32768> block;
    DWORD remaining = details.nFileSizeLow;
    while (remaining) {
        const auto requested = std::min<DWORD>(remaining, static_cast<DWORD>(block.size()));
        DWORD read = 0, written = 0;
        require(ReadFile(input.get(), block.data(), requested, &read, nullptr), "Read CAPT package file");
        if (read != requested) throw std::runtime_error("Incomplete CAPT package file");
        require(WriteFile(output.get(), block.data(), read, &written, nullptr), "Install CAPT program file");
        if (written != read) throw std::runtime_error("Incomplete program file write");
        remaining -= written;
    }
}

std::string last_report(const fs::path& directory)
{
    std::ifstream input(directory / state_dir_name / report_name, std::ios::binary);
    std::string data(32768, '\0');
    input.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<std::size_t>(input.gcount()));
    return data.empty() ? "No CAPT report is available yet." : data;
}

void install(SC_HANDLE manager, const fs::path& directory)
{
    if (fs::exists(directory) || open_service(manager) ||
        open_printer(queue_name, PRINTER_ACCESS_USE, true) || port_exists())
        throw std::runtime_error(arabic
            ? u8"الطابعة مثبتة بالفعل أو اسمها مستخدم. اختر «إزالة الطابعة» قبل إعادة التثبيت."
            : "A CAPT installation or name already exists. Use Remove printer before reinstalling.");
    require_pdf_driver();
    const auto source = fs::path(executable_path()).parent_path();
    secured_directory(directory, L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;AU)(A;OICI;GRGX;;;LS)");
    ServiceHandle service;
    PrinterHandle queue;
    bool port = false;
    try {
        std::ofstream marker(directory / installation_marker, std::ios::binary);
        marker << installation_id;
        marker.close();
        if (!marker) throw std::runtime_error("Cannot record ownership of the installation folder");
        secured_directory(directory / state_dir_name,
            L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;LS)(A;OICI;GRGX;;;AU)");
        secured_directory(directory / jobs_dir_name,
            L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;LS)");
        for (const auto* file : {worker_name, pdfium_name})
            copy_payload(source / file, directory / file);
        const auto command = service_command(directory);
        service.reset(CreateServiceW(manager, service_name, L"Canon LBP6000 CAPT USB Print Queue",
            SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_CHANGE_CONFIG | DELETE, SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, command.c_str(), nullptr, nullptr,
            L"Spooler\0", service_account, nullptr));
        if (!service) fail("Install CAPT queue service");
        wchar_t privileges[] = L"SeChangeNotifyPrivilege\0";
        SERVICE_REQUIRED_PRIVILEGES_INFOW required{privileges};
        require(ChangeServiceConfig2W(service.get(), SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO, &required),
                "Restrict the service privileges");
        require(StartServiceW(service.get(), 0, nullptr), "Start CAPT queue service");
        wait_service(service.get(), SERVICE_RUNNING);
        add_port();
        port = true;
        queue = add_queue();
        set_a4(queue.get());
    } catch (const std::exception& error) {
        std::string message(error.what());
        if (service && !port) message += "\n" + last_report(directory);
        try {
            if (queue) require(DeletePrinter(queue.get()), "Roll back CAPT print queue");
            queue.reset();
            if (port) delete_port();
            if (service) remove_service(service.get());
            service.reset();
            remove_files(directory);
        } catch (const std::exception& rollback) {
            message += "\nRollback stopped: ";
            message += rollback.what();
        }
        throw std::runtime_error(message);
    }
}

void uninstall(SC_HANDLE manager, const fs::path& directory)
{
    if (fs::exists(directory)) {
        std::ifstream marker(directory / installation_marker, std::ios::binary);
        std::string value(installation_id.size() + 1, '\0');
        marker.read(value.data(), static_cast<std::streamsize>(value.size()));
        value.resize(static_cast<std::size_t>(marker.gcount()));
        if (value != installation_id)
            throw std::runtime_error("Setup could not verify ownership of the CAPT installation folder.");
    }
    auto service = open_service(manager);
    if (service) verify_service(service.get(), directory);
    auto printer = open_printer(queue_name, PRINTER_ACCESS_ADMINISTER | PRINTER_ACCESS_USE, true);
    if (printer) verify_queue(printer.get());
    const bool port = port_exists();
    if (port) verify_port();
    if (printer) require(DeletePrinter(printer.get()), "Remove Canon CAPT print queue");
    printer.reset();
    if (port) delete_port();
    if (service) remove_service(service.get());
    service.reset();
    remove_files(directory);
}

void elevate(const wchar_t* mode)
{
    const auto self = executable_path();
    const std::wstring argument = std::wstring(mode) + (arabic ? L" ar" : L" en");
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOASYNC;
    launch.lpVerb = L"runas";
    launch.lpFile = self.c_str();
    launch.lpParameters = argument.c_str();
    launch.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&launch) && GetLastError() != ERROR_CANCELLED) fail("Start elevated CAPT setup");
}

void menu()
{
    constexpr int setup = 100, remove = 101, report = 102, cancel = 103, language = 104;
    int selected;
    do {
        const TASKDIALOG_BUTTON buttons[] = {
            {setup, local(L"Install printer", L"تثبيت الطابعة")},
            {report, local(L"View last job report", L"عرض تقرير آخر مهمة طباعة")},
            {cancel, local(L"Cancel active USB job", L"إلغاء مهمة الطباعة الجارية عبر USB")},
            {remove, local(L"Remove printer", L"إزالة الطابعة")},
            {language, local(L"العربية", L"English")}, {IDCANCEL, local(L"Close", L"إغلاق")},
        };
        TASKDIALOGCONFIG dialog{};
        dialog.cbSize = sizeof(dialog);
        dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS_NO_ICON;
        if (arabic) dialog.dwFlags |= TDF_RTL_LAYOUT;
        dialog.pszWindowTitle = title();
        dialog.pszMainInstruction = L"Canon LBP6000 / LBP6000B";
        dialog.pszContent = local(L"Install the printer to print from Windows apps.",
                                  L"ثبّت الطابعة للطباعة من تطبيقات Windows.");
        dialog.cButtons = static_cast<UINT>(std::size(buttons));
        dialog.pButtons = buttons;
        dialog.nDefaultButton = setup;
        if (FAILED(TaskDialogIndirect(&dialog, &selected, nullptr, nullptr)))
            throw std::runtime_error("Cannot show CAPT setup");
        if (selected == language) {
            arabic = !arabic;
            remember_language();
        }
    } while (selected == language);
    if (selected == setup) elevate(L"--install");
    if (selected == cancel) elevate(L"--cancel");
    if (selected == remove && MessageBoxW(nullptr,
            local(L"Remove the CAPT printer and stop any active CAPT job?",
                  L"هل تريد إزالة الطابعة وإيقاف مهمة الطباعة الجارية؟"), title(),
            MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION | reading_direction()) == IDYES)
        elevate(L"--remove");
    if (selected == report) {
        const auto text = last_report(target_directory());
        if (text == "No CAPT report is available yet.") {
            show_result(L"No CAPT report is available yet.", L"لا يوجد تقرير طباعة بعد.");
        } else {
            const auto details = widen(text);
            MessageBoxW(nullptr, details.c_str(), local(L"CAPT report (Ctrl+C to copy)",
                  L"تقرير الطباعة (للنسخ، اضغط Ctrl+C)"), MB_OK);
        }
    }
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command, int)
{
    arabic = prefer_arabic();
    try {
        std::wstring_view mode(command);
        if (mode.empty()) {
            menu();
            return 0;
        }
        const auto separator = mode.find(L' ');
        if (separator != std::wstring_view::npos) {
            const auto language = mode.substr(separator + 1);
            if (language != L"ar" && language != L"en") return 2;
            arabic = language == L"ar";
            mode = mode.substr(0, separator);
        }
        if (mode != L"--install" && mode != L"--remove" && mode != L"--cancel") return 2;
        ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
        if (!manager) fail("Open Service Control Manager as administrator");
        Handle lock(CreateMutexW(nullptr, FALSE, L"Global\\IndependentCanonCaptSetup"));
        if (!lock) fail("Open setup lock");
        const auto acquired = WaitForSingleObject(lock.get(), 0);
        if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED)
            throw std::runtime_error("Another CAPT Setup action is running.");
        struct Release { HANDLE value; ~Release() { ReleaseMutex(value); } } release{lock.get()};
        if (mode == L"--install") {
            install(manager.get(), target_directory());
            show_result(L"Installed. In an app's Print dialog, select Canon LBP6000 CAPT.",
                        L"تم التثبيت. اختر Canon LBP6000 CAPT من نافذة الطباعة في التطبيق.");
        } else if (mode == L"--remove") {
            uninstall(manager.get(), target_directory());
            show_result(L"The Canon CAPT queue was removed.", L"تمت إزالة طابعة Canon CAPT.");
        } else {
            ServiceHandle service(OpenServiceW(manager.get(), service_name, SERVICE_USER_DEFINED_CONTROL |
                                                                         SERVICE_QUERY_CONFIG));
            if (!service) fail("Open CAPT service to cancel the current job");
            verify_service(service.get(), target_directory());
            SERVICE_STATUS state{};
            require(ControlService(service.get(), cancel_job_control, &state), "Request cancellation of active USB job");
            show_result(L"Cancellation requested. View the last job report for the result.",
                        L"تم إرسال طلب الإلغاء. راجع تقرير آخر مهمة طباعة لمعرفة النتيجة.");
        }
        return 0;
    } catch (const std::exception& error) {
        const auto text = std::wstring(local(L"Setup could not finish the requested action.\n\n",
                                              L"تعذّر إكمال الإجراء المطلوب.\n\n")) + widen(error.what());
        MessageBoxW(nullptr, text.c_str(), title(), MB_OK | MB_ICONERROR | reading_direction());
        return 1;
    }
}
