// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "capt/protocol.hpp"
#include <string>
#include <string_view>

namespace capt {
struct DeviceId {
    std::string manufacturer;
    std::string model;
    std::string commands;
    std::string text;
    bool little_endian_length = false;
    bool canon_duplicate_length_quirk = false;
};

enum class DeviceIdPolicy { strict, canon_usb_04a9_271a };
// One complete SetupAPI hardware-ID entry, not a symbolic path or substring.
DeviceIdPolicy device_id_policy_for_usb_hardware_id(std::wstring_view hardware_id);

// USBPRINT's returned bytes, not the output buffer's allocation size. Accepts
// an inclusive two-byte length in either byte order only if it exactly bounds
// the printable ID (plus optional trailing NULs). Never scans beyond the reply.
// The explicit Canon policy additionally accepts the narrowly validated
// duplicate-length/unterminated-DES variant captured on 2026-11-19. Callers
// must select that policy from an exact enumerated USB hardware ID, not text.
DeviceId decode_device_id(const Bytes& reply, DeviceIdPolicy policy = DeviceIdPolicy::strict);

// A narrow diagnostic allow-list, not a claim of printing compatibility.
bool is_lbp6000_identity(const DeviceId& id);
} // namespace capt
