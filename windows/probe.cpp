// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.hpp"
#include "capt/device_id.hpp"
#include "capt/status.hpp"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <winioctl.h>
#include <initguid.h>
#include <usbprint.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace capt::win {
namespace {
void report(const std::string& text)
{
    const auto line = text + "\r\n";
    std::size_t sent = 0;
    while (sent < line.size()) {
        DWORD count = 0;
        require(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data() + sent,
                         static_cast<DWORD>(line.size() - sent), &count, nullptr), "report WriteFile");
        if (!count) throw std::runtime_error("Report pipe made no progress");
        sent += count;
    }
}
std::string hex(const Bytes& bytes)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto b : bytes) out << std::setw(2) << static_cast<unsigned>(b) << ' ';
    return out.str();
}

// Only use from a supervised worker process. Cancellation drainage may block
// in a broken device driver.
class ProbeIo final : public Transport, public SessionTransport {
public:
    explicit ProbeIo(const std::wstring& path)
        : device_(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr))
    { if (!device_) fail("Open USB printer exclusively"); }

    void start_transaction(bool trace = false) { deadline_ = GetTickCount64() + 5000; trace_ = trace; }
    Bytes identity()
    {
        start_transaction();
        Bytes bytes(4094);
        bytes.resize(transfer(bytes.data(), bytes.size(), Kind::identity));
        return bytes;
    }
    std::size_t read_some(std::uint8_t* data, std::size_t size) override
    { return transfer(data, size, Kind::read); }
    std::size_t write_some(const std::uint8_t* data, std::size_t size) override
    {
        // Private copy also keeps outgoing bytes alive until completion.
        Bytes copy(data, data + size);
        return transfer(copy.data(), copy.size(), Kind::write);
    }
    std::size_t write_some(const std::uint8_t* data, std::size_t size, const IoContext& ctx) override
    {
        if (size >= 4 && remaining_payload_ == 0) {
            const std::size_t length = data[2] + 256u * data[3];
            if (length >= 4) remaining_payload_ = length;
            last_command_ = static_cast<unsigned>(data[0]) + 256u * data[1];
        }
        try {
            set_context(ctx);
            Bytes copy(data, data + size);
            const auto written = transfer(copy.data(), copy.size(), Kind::write, &ctx);
            remaining_payload_ -= std::min(remaining_payload_, written);
            return written;
        } catch (const std::exception& e) { io_error(e); }
    }
    std::size_t read_some(std::uint8_t* data, std::size_t size, const IoContext& ctx) override
    {
        try {
            set_context(ctx);
            return transfer(data, size, Kind::read, &ctx);
        }
        catch (const std::exception& e) { io_error(e); }
    }
private:
    [[noreturn]] void io_error(const std::exception& e) const
    {
        std::ostringstream out;
        out << "USB command 0x" << std::hex << last_command_ << ": " << e.what();
        throw std::runtime_error(out.str());
    }
    void set_context(const IoContext& ctx)
    {
        const auto remaining = std::chrono::ceil<Milliseconds>(ctx.remaining()).count();
        deadline_ = GetTickCount64() + static_cast<ULONGLONG>(remaining);
    }
    unsigned last_command_ = 0;
    std::size_t remaining_payload_ = 0;
    enum class Kind { read, write, identity };
    std::size_t transfer(std::uint8_t* data, std::size_t size, Kind kind, const IoContext* ctx = nullptr)
    {
        if (ctx) ctx->check();
        if (size == 0 || size > 65535) throw std::invalid_argument("Invalid diagnostic transfer length");
        if (GetTickCount64() >= deadline_) throw DeadlineExceeded();
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event) fail("CreateEvent");
        OVERLAPPED operation{};
        operation.hEvent = event.get();
        DWORD transferred = 0;
        BOOL ok = FALSE;
        const auto count = static_cast<DWORD>(size);
        if (kind == Kind::read) ok = ReadFile(device_.get(), data, count, &transferred, &operation);
        if (kind == Kind::write) ok = WriteFile(device_.get(), data, count, &transferred, &operation);
        if (kind == Kind::identity)
            ok = DeviceIoControl(device_.get(), IOCTL_USBPRINT_GET_1284_ID, nullptr, 0,
                                 data, count, &transferred, &operation);
        if (!ok) {
            const auto code = GetLastError();
            if (code != ERROR_IO_PENDING) fail("USB I/O submission", code);
            // Keep OVERLAPPED, event and all buffers alive even after CancelIoEx.
            // The parent kills the worker if this drainage cannot finish.
            struct Drain {
                HANDLE device;
                OVERLAPPED* operation;
                bool pending = true;
                ~Drain() {
                    if (pending) {
                        CancelIoEx(device, operation);
                        DWORD ignored = 0;
                        GetOverlappedResult(device, operation, &ignored, TRUE);
                    }
                }
            } drain{device_.get(), &operation};
            while (true) {
                if (ctx) ctx->check();
                const auto now = GetTickCount64();
                if (now >= deadline_) throw DeadlineExceeded();
                const auto wait = WaitForSingleObject(event.get(), static_cast<DWORD>(std::min<ULONGLONG>(25, deadline_ - now)));
                if (wait == WAIT_TIMEOUT) continue;
                if (wait != WAIT_OBJECT_0) fail("Wait for USB I/O");
                ok = GetOverlappedResult(device_.get(), &operation, &transferred, FALSE);
                const auto completion_error = ok ? ERROR_SUCCESS : GetLastError();
                if (completion_error == ERROR_IO_INCOMPLETE) continue;
                drain.pending = false;
                if (!ok) fail("USB I/O completion", completion_error);
                break;
            }
        }
        if (transferred == 0 || transferred > size) throw std::runtime_error("Invalid USB transfer progress");
        if (trace_ && kind != Kind::identity)
            report(std::string(kind == Kind::read ? "USB RX chunk: " : "USB TX chunk: ") +
                   hex(Bytes(data, data + transferred)));
        if (ctx) ctx->check();
        if (GetTickCount64() >= deadline_) throw DeadlineExceeded();
        return transferred;
    }
    Handle device_;
    ULONGLONG deadline_ = 0;
    bool trace_ = false;
};

struct DeviceSet {
    HDEVINFO handle = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USBPRINT, nullptr, nullptr,
                                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    DeviceSet() { if (handle == INVALID_HANDLE_VALUE) fail("Enumerate USBPRINT interfaces"); }
    ~DeviceSet() { SetupDiDestroyDeviceInfoList(handle); }
};
std::wstring property(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property_id)
{
    DWORD type = 0, size = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, &device, property_id, &type, nullptr, 0, &size)) {
        const auto error = GetLastError();
        if (error == ERROR_INVALID_DATA) return L"(not reported)";
        if (error != ERROR_INSUFFICIENT_BUFFER) fail("Read device property length", error);
    }
    if (!size || size > 65536 || size % sizeof(wchar_t)) throw std::runtime_error("Invalid device property extent");
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 2, L'\0');
    require(SetupDiGetDeviceRegistryPropertyW(set, &device, property_id, &type,
            reinterpret_cast<PBYTE>(buffer.data()), size, nullptr), "Read device property");
    if (type != REG_SZ && type != REG_MULTI_SZ) throw std::runtime_error("Unexpected device property type");
    std::wstring result;
    const auto end = buffer.begin() + static_cast<std::ptrdiff_t>(size / sizeof(wchar_t));
    auto begin = buffer.begin();
    while (begin < end && *begin) {
        const auto stop = std::find(begin, end, L'\0');
        if (stop == end) throw std::runtime_error("Unterminated device property");
        if (!result.empty()) result += L" | ";
        result.append(begin, stop);
        begin = stop + 1;
        if (type == REG_SZ) break;
    }
    return result;
}
std::wstring instance_id(DEVINST instance)
{
    wchar_t buffer[MAX_DEVICE_ID_LEN]{};
    const auto result = CM_Get_Device_IDW(instance, buffer, MAX_DEVICE_ID_LEN, 0);
    if (result != CR_SUCCESS) throw std::runtime_error("CM_Get_Device_ID failed: " + std::to_string(result));
    return buffer;
}
}

int run_probe(bool status, const PrintTask& print, bool include_device_details)
{
    try {
        if (print) report(include_device_details ? "CAPT USB print worker 0.2" : "CAPT Windows queue 0.3.1");
        else report("CAPT Connection Check 0.1.1 / native Windows diagnostic / no print job");
#if defined(_M_ARM64) || defined(__aarch64__)
        report("Executable architecture: ARM64 (not ARM64EC)");
#else
        report("Executable architecture: non-ARM64 developer build");
#endif
        SYSTEMTIME time{};
        GetSystemTime(&time);
        report("UTC: " + std::to_string(time.wYear) + "-" + std::to_string(time.wMonth) + "-" +
               std::to_string(time.wDay) + " " + std::to_string(time.wHour) + ":" + std::to_string(time.wMinute));
        if (!print) report(status ? "Mode: identity and one CAPT A0A8 status request" : "Mode: USBPRINT discovery and IEEE 1284 identity only");
        if (include_device_details)
            report("Device paths and instance IDs below may contain serial numbers. Review before sharing.");
        DeviceSet devices;
        std::unique_ptr<ProbeIo> target;
        unsigned matches = 0, failures = 0, found = 0;
        for (DWORD index = 0; ; ++index) {
            SP_DEVICE_INTERFACE_DATA interface_data{};
            interface_data.cbSize = sizeof(interface_data);
            if (!SetupDiEnumDeviceInterfaces(devices.handle, nullptr, &GUID_DEVINTERFACE_USBPRINT, index, &interface_data)) {
                const auto error = GetLastError();
                if (error == ERROR_NO_MORE_ITEMS) break;
                fail("Read USBPRINT interface", error);
            }
            if (++found > 16) throw std::runtime_error("More than 16 USB printers; disconnect unrelated devices before checking");
            report("\r\nUSB printer " + std::to_string(found));
            try {
                DWORD size = 0;
                SetupDiGetDeviceInterfaceDetailW(devices.handle, &interface_data, nullptr, 0, &size, nullptr);
                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size > 65536)
                    throw std::runtime_error("Invalid device interface detail extent");
                // operator new storage is aligned for the Windows detail structure.
                std::vector<std::uint64_t> storage((size + 7u) / 8u);
                auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
                detail->cbSize = sizeof(*detail);
                SP_DEVINFO_DATA info{};
                info.cbSize = sizeof(info);
                require(SetupDiGetDeviceInterfaceDetailW(devices.handle, &interface_data, detail, size, nullptr, &info), "Read interface path");
                const auto path_capacity = (size - offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath)) / sizeof(wchar_t);
                const auto end = std::find(detail->DevicePath, detail->DevicePath + path_capacity, L'\0');
                if (end == detail->DevicePath + path_capacity) throw std::runtime_error("Unterminated USBPRINT path");
                const std::wstring path(detail->DevicePath, end);
                report("Description: " + escaped(property(devices.handle, info, SPDRP_DEVICEDESC)));
                const auto hardware_ids = property(devices.handle, info, SPDRP_HARDWAREID);
                report("Hardware IDs: " + escaped(hardware_ids));
                auto identity_policy = DeviceIdPolicy::strict;
                // property() joins MULTI_SZ entries with this separator. Match
                // complete entries only, never the opaque device-interface path.
                std::size_t begin = 0;
                while (begin < hardware_ids.size()) {
                    const auto separator = hardware_ids.find(L" | ", begin);
                    const auto stop = separator == std::wstring::npos ? hardware_ids.size() : separator;
                    if (device_id_policy_for_usb_hardware_id(std::wstring_view(hardware_ids).substr(begin, stop - begin))
                        == DeviceIdPolicy::canon_usb_04a9_271a)
                        identity_policy = DeviceIdPolicy::canon_usb_04a9_271a;
                    if (separator == std::wstring::npos) break;
                    begin = separator + 3;
                }
                report("Service: " + escaped(property(devices.handle, info, SPDRP_SERVICE)));
                if (include_device_details) {
                    report("Instance: " + escaped(instance_id(info.DevInst)));
                    DEVINST parent = 0;
                    if (CM_Get_Parent(&parent, info.DevInst, 0) == CR_SUCCESS)
                        report("Parent instance: " + escaped(instance_id(parent)));
                    report("Interface: " + escaped(path));
                }
                report("Opening exclusively; an active queue or another program may prevent this.");
                auto io = std::make_unique<ProbeIo>(path);
                const auto raw = io->identity();
                if (include_device_details)
                    report("IEEE 1284 bytes (" + std::to_string(raw.size()) + "): " + hex(raw));
                const auto id = decode_device_id(raw, identity_policy);
                if (include_device_details) report("IEEE 1284 text: " + id.text);
                if (id.canon_duplicate_length_quirk)
                    report("Identity format: observed 04A9:271A duplicate-length / final-DES quirk (explicit policy)");
                else report(std::string("Length prefix: ") + (id.little_endian_length ? "little endian" : "big endian"));
                if (is_lbp6000_identity(id)) {
                    ++matches;
                    report("Identity matches the LBP6000 diagnostic allow-list; printing compatibility remains unverified.");
                    if (status && !target) target = std::move(io);
                } else report("Identity is outside the allow-list; no CAPT commands sent.");
            } catch (const std::exception& e) { ++failures; report(std::string("Device check failed: ") + e.what()); }
        }
        if (!found) {
            report("No USBPRINT interface found. Connect and power on the printer, then check Device Manager. No driver binding was changed.");
            return 2;
        }
        if (!status) { report("\r\nIdentity inspection finished. No CAPT commands were sent."); return failures ? 2 : 0; }
        if (matches != 1 || failures || !target) {
            report("CAPT operation withheld: need exactly one verified target and no unresolved device checks. Review device errors above; disconnect unrelated printers only if multiple devices are listed.");
            return 2;
        }
        if (print) { print(*target); return 0; }
        report("\r\nCAPT TX: a8 a0 04 00 (status only)");
        target->start_transaction(true);
        write_packet(*target, Packet{0xA0A8, {}});
        const auto reply = read_reply(*target, 0xA0A8, 4096, ReplyLength::binary);
        report("CAPT RX: " + hex(encode_packet(reply)));
        const auto decoded = decode_extended_status(reply.payload);
        report("Status flags: busy=" + std::to_string(decoded.busy()) + " buffer_full=" + std::to_string(decoded.buffer_full()) +
               " paper_out=" + std::to_string(decoded.paper_out()) + " processing=" + std::to_string(decoded.processing()) +
               " printing=" + std::to_string(decoded.printing()));
        report("Counters: decoding=" + std::to_string(decoded.page_decoding) + " received=" + std::to_string(decoded.page_received) +
               " out=" + std::to_string(decoded.page_out) + " completed=" + std::to_string(decoded.page_completed));
        report("CAPT status exchange completed. This establishes one exchange only; no page was printed and no driver was installed.");
        return 0;
    } catch (const std::exception& e) {
        try { report(std::string("Check failed: ") + e.what()); } catch (...) {}
        return 1;
    }
}
} // namespace capt::win
