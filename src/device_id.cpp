// SPDX-License-Identifier: GPL-3.0-or-later
#include "capt/device_id.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace capt {
namespace {
std::string normalized(std::string_view value)
{
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
    std::string result(value);
    for (auto& c : result) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return result;
}
}

DeviceIdPolicy device_id_policy_for_usb_hardware_id(std::wstring_view hardware_id)
{
    constexpr std::wstring_view expected = L"USB\\VID_04A9&PID_271A";
    if (hardware_id.size() != expected.size()) return DeviceIdPolicy::strict;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        auto c = hardware_id[i];
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
        if (c != expected[i]) return DeviceIdPolicy::strict;
    }
    return DeviceIdPolicy::canon_usb_04a9_271a;
}

DeviceId decode_device_id(const Bytes& reply, DeviceIdPolicy policy)
{
    if (policy != DeviceIdPolicy::strict && policy != DeviceIdPolicy::canon_usb_04a9_271a)
        throw std::invalid_argument("Unknown identity policy");
    if (reply.size() < 3 || reply.size() > 4094)
        throw std::invalid_argument("USBPRINT identity must contain 3..4094 returned bytes");
    auto end = std::find(reply.begin() + 2, reply.end(), 0);
    const auto extent = static_cast<std::size_t>(end - reply.begin());
    if (!std::all_of(end, reply.end(), [](std::uint8_t b) { return b == 0; }))
        throw std::invalid_argument("Nonzero data follows identity terminator");
    const std::size_t big = static_cast<std::size_t>(reply[0]) * 256 + reply[1];
    const std::size_t little = static_cast<std::size_t>(reply[1]) * 256 + reply[0];
    // Observed USBPRINT output: both prefix bytes equal the actual returned
    // length, followed by an unterminated DES field. This is a host-side capture;
    // it does not establish whether firmware or USBPRINT produced the anomaly.
    const bool duplicate = policy == DeviceIdPolicy::canon_usb_04a9_271a &&
        extent == reply.size() && extent <= 255 && reply[0] == extent && reply[1] == extent;
    if (big != extent && little != extent && !duplicate)
        throw std::invalid_argument("Identity length does not match returned text extent");
    for (auto p = reply.begin() + 2; p != end; ++p)
        if (*p < 0x20 || *p > 0x7E)
            throw std::invalid_argument("Identity contains non-printable ASCII");
    DeviceId result;
    result.little_endian_length = !duplicate && big != extent;
    result.canon_duplicate_length_quirk = duplicate;
    result.text.assign(reply.begin() + 2, end);
    bool manufacturer_seen = false, model_seen = false, commands_seen = false;
    bool captured_description_seen = false;
    std::size_t pos = 0;
    while (pos < result.text.size()) {
        const auto next = result.text.find(';', pos);
        const bool unterminated = next == std::string::npos;
        if (unterminated && !duplicate)
            throw std::invalid_argument("Unterminated identity field");
        const auto field_end = unterminated ? result.text.size() : next;
        const auto field = std::string_view(result.text).substr(pos, field_end - pos);
        pos = unterminated ? result.text.size() : next + 1;
        if (normalized(field).empty()) continue;
        const auto colon = field.find(':');
        if (colon == std::string_view::npos)
            throw std::invalid_argument("Identity field has no colon");
        const auto key = normalized(field.substr(0, colon));
        const auto value = normalized(field.substr(colon + 1));
        if (unterminated) {
            if (key != "DES" || value != "CANON LBP6000/LBP6018")
                throw std::invalid_argument("Canon quirk requires the complete captured final description");
            captured_description_seen = true;
        }
        auto assign_once = [&](bool& seen, std::string& target) {
            if (seen || value.empty()) throw std::invalid_argument("Duplicate or empty identity field");
            seen = true;
            target = value;
        };
        if (key == "MFG" || key == "MANUFACTURER") assign_once(manufacturer_seen, result.manufacturer);
        if (key == "MDL" || key == "MODEL") assign_once(model_seen, result.model);
        if (key == "CMD" || key == "COMMAND SET") assign_once(commands_seen, result.commands);
    }
    if (!manufacturer_seen || !model_seen)
        throw std::invalid_argument("Identity is missing manufacturer or model");
    if (duplicate && (!captured_description_seen || result.manufacturer != "CANON" ||
                      result.model != "LBP6000/LBP6018" || result.commands != "CAPT"))
        throw std::invalid_argument("Canon identity quirk does not match the captured device fields");
    return result;
}

bool is_lbp6000_identity(const DeviceId& id)
{
    const auto make = normalized(id.manufacturer);
    const auto model = normalized(id.model);
    return (make == "CANON" || make == "CANON INC." || make == "CANON INC") &&
        (model == "LBP6000" || model == "LBP6000B" || model == "LBP6000/LBP6018");
}
} // namespace capt
