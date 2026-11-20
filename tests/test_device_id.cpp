// SPDX-License-Identifier: GPL-3.0-or-later
#include "capt/device_id.hpp"
#include "support.hpp"
#include "fixtures/canon_271a_identity.hpp"
#include <iostream>

namespace {
capt::Bytes identity(const std::string& text, bool little = false, bool nul = true)
{
    const auto size = text.size() + 2;
    capt::Bytes result{static_cast<std::uint8_t>(size >> 8), static_cast<std::uint8_t>(size)};
    if (little) std::swap(result[0], result[1]);
    result.insert(result.end(), text.begin(), text.end());
    if (nul) result.push_back(0);
    return result;
}
}
int main()
{
    try {
        for (const auto* model : {"LBP6000", "LBP6000B", "LBP6000/LBP6018"}) {
            for (bool little : {false, true}) for (bool nul : {false, true}) {
                auto bytes = identity(std::string("MFG:Canon;MDL:") + model + ";CMD:CAPT;", little, nul);
                const auto id = capt::decode_device_id(bytes);
                CHECK(capt::is_lbp6000_identity(id));
                CHECK(id.commands == "CAPT");
                CHECK(id.little_endian_length == little);
                for (std::size_t n = 0; n < bytes.size() - (nul ? 1u : 0u); ++n) {
                    expect_throw<std::invalid_argument>([&] {
                        capt::decode_device_id(capt::Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n)));
                    });
                }
            }
        }
        CHECK(capt::is_lbp6000_identity(capt::decode_device_id(
            identity(" manufacturer : canon inc. ; model : lbp6000b ;X:opaque:thing;"))));
        for (const auto* text : {"MFG:Canon;MDL:LBP6000evil;", "MFG:Other;MDL:LBP6000;",
                                 "MFG:Canon;MDL:LBP6018;", "MFG:Canon;MDL:LBP6000 B;"})
            CHECK(!capt::is_lbp6000_identity(capt::decode_device_id(identity(text))));
        for (const auto* text : {"", "MFG:Canon;", "MDL:LBP6000;", "MFG:;MDL:LBP6000;",
                "MFG:Canon;MDL:LBP6000", "MFG:Canon;MDL:LBP6000;MODEL:LBP6000;",
                "MFG:Canon;MANUFACTURER:Other;MDL:LBP6000;", "MFG:Canon;MDL:LBP6000;bad;",
                "MFG:Canon;MDL:LBP6000;CMD:CAPT;CMD:CAPT;", "MFG:Canon;MDL:LBP6000;\n"})
            expect_throw<std::invalid_argument>([&] { capt::decode_device_id(identity(text)); });
        auto bad = identity("MFG:Canon;MDL:LBP6000;");
        bad.push_back('X');
        expect_throw<std::invalid_argument>([&] { capt::decode_device_id(bad); });
        bad = identity("MFG:Canon;MDL:LBP6000;");
        bad[0] = 0xFF;
        expect_throw<std::invalid_argument>([&] { capt::decode_device_id(bad); });
        bad.assign(4095, 0);
        expect_throw<std::invalid_argument>([&] { capt::decode_device_id(bad); });
        // Exhaust every two-byte prefix against a fixed bounded response.
        bad = identity("MFG:Canon;MDL:LBP6000;");
        const auto length = bad.size() - 1;
        for (unsigned prefix = 0; prefix <= 65535; ++prefix) {
            bad[0] = static_cast<std::uint8_t>(prefix >> 8);
            bad[1] = static_cast<std::uint8_t>(prefix);
            if (prefix == length || prefix == length * 256) CHECK(capt::is_lbp6000_identity(capt::decode_device_id(bad)));
            else expect_throw<std::invalid_argument>([&] { capt::decode_device_id(bad); });
        }
        const capt::Bytes captured(canon_271a_identity.begin(), canon_271a_identity.end());
        const auto policy = capt::DeviceIdPolicy::canon_usb_04a9_271a;
        CHECK(capt::device_id_policy_for_usb_hardware_id(L"USB\\VID_04A9&PID_271A") == policy);
        CHECK(capt::device_id_policy_for_usb_hardware_id(L"usb\\vid_04a9&pid_271a") == policy);
        for (const auto* id : {L"", L"USB\\VID_04A9&PID_271B", L"USB\\VID_04A8&PID_271A",
                L"USB\\VID_04A9&PID_271A&REV_0100", L"xUSB\\VID_04A9&PID_271A", L"USB\\VID_04A9&PID_271AX",
                L"USB\\VID_04A9&PID_271A ", L"USB\\VID_04A9&PID_271A\\serial", L"\\\\?\\usb#vid_04a9&pid_271a"})
            CHECK(capt::device_id_policy_for_usb_hardware_id(id) == capt::DeviceIdPolicy::strict);
        // Strict callers still reject the anomalous host-side capture.
        expect_throw<std::invalid_argument>([&] { capt::decode_device_id(captured); });
        const auto actual = capt::decode_device_id(captured, policy);
        CHECK(actual.canon_duplicate_length_quirk);
        CHECK(!actual.little_endian_length);
        CHECK(actual.manufacturer == "CANON");
        CHECK(actual.model == "LBP6000/LBP6018");
        CHECK(actual.commands == "CAPT");
        CHECK(capt::is_lbp6000_identity(actual));
        CHECK(actual.text == "MFG:Canon;MDL:LBP6000/LBP6018;CMD:CAPT;VER:3.0;CLS:PRINTER;DES:Canon LBP6000/LBP6018");
        for (std::size_t n = 0; n < captured.size(); ++n) {
            auto truncated = capt::Bytes(captured.begin(), captured.begin() + static_cast<std::ptrdiff_t>(n));
            expect_throw<std::invalid_argument>([&] { capt::decode_device_id(truncated, policy); });
            if (n >= 2) {
                truncated[0] = truncated[1] = static_cast<std::uint8_t>(n);
                expect_throw<std::invalid_argument>([&] { capt::decode_device_id(truncated, policy); });
            }
        }
        // No generic length bypass, including in the opted-in device policy.
        bad = captured;
        for (unsigned prefix = 0; prefix <= 65535; ++prefix) {
            bad[0] = static_cast<std::uint8_t>(prefix >> 8);
            bad[1] = static_cast<std::uint8_t>(prefix);
            if (prefix == 0x5656) CHECK(capt::decode_device_id(bad, policy).canon_duplicate_length_quirk);
            else expect_throw<std::invalid_argument>([&] { capt::decode_device_id(bad, policy); });
        }
        for (const std::size_t position : {6u, 20u, 37u, 66u}) {
            bad = captured;
            bad[position] = 'X'; // manufacturer, model, command set or description
            expect_throw<std::invalid_argument>([&] { capt::decode_device_id(bad, policy); });
        }
        for (const auto* text : {"MFG:Canon;MDL:LBP6000/LBP6018;CMD:CAPT;MDL:LBP6000/LBP6018;DES:Canon LBP6000/LBP6018",
                                "MFG:Canon;MDL:LBP6000/LBP6018;CMD:CAPT;DES:Canon LBP6000/LBP6018;"}) {
            bad = identity(text, false, false);
            bad[0] = bad[1] = static_cast<std::uint8_t>(bad.size());
            expect_throw<std::invalid_argument>([&] { capt::decode_device_id(bad, policy); });
        }
        for (std::size_t i = 2; i < captured.size(); ++i) {
            bad = captured;
            bad[i] = 0;
            expect_throw<std::invalid_argument>([&] { capt::decode_device_id(bad, policy); });
        }
        bad = captured;
        bad.push_back(0);
        expect_throw<std::invalid_argument>([&] { capt::decode_device_id(bad, policy); });
        CHECK(!capt::decode_device_id(identity("MFG:Canon;MDL:LBP6000;"), policy).canon_duplicate_length_quirk);
        std::cout << "Device identity bounds and allow-list passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
