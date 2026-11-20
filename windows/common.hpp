// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include "capt/control.hpp"
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace capt::win {
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.release()) {}
    Handle& operator=(Handle&& other) noexcept { if (this != &other) reset(other.release()); return *this; }
    explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const noexcept { return value_; }
    HANDLE release() noexcept { return std::exchange(value_, nullptr); }
    void reset(HANDLE value = nullptr) noexcept { if (*this) CloseHandle(value_); value_ = value; }
private:
    HANDLE value_;
};

// Reports are ASCII with escaped UTF-16, avoiding terminal control characters
// and encoding ambiguity when a pipe splits a non-ASCII device description.
inline std::string escaped(const std::wstring& value)
{
    std::ostringstream out;
    for (const auto c : value) {
        if (c >= 32 && c <= 126 && c != L'\\') out << static_cast<char>(c);
        else out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c);
    }
    return out.str();
}
[[noreturn]] inline void fail(const char* operation, DWORD code = GetLastError())
{
    wchar_t message[512]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, message, 512, nullptr);
    throw std::runtime_error(std::string(operation) + " failed (Win32 " +
                             std::to_string(code) + "): " + escaped(message));
}
inline void require(BOOL success, const char* operation) { if (!success) fail(operation); }
using PrintTask = std::function<void(SessionTransport&)>;
int run_probe(bool status, const PrintTask& print = {}, bool include_device_details = true);
} // namespace capt::win
