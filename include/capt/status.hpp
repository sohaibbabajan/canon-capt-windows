// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "capt/protocol.hpp"

#include <array>
#include <optional>

namespace capt {
struct ExtendedStatus {
    std::array<std::uint16_t, 7> words{};
    std::uint16_t page_decoding = 0;
    std::uint16_t page_printing = 0;
    std::uint16_t page_out = 0;
    std::uint16_t page_completed = 0;
    std::uint16_t page_received = 0;
    std::uint16_t job = 0;
    std::optional<std::uint16_t> pages_in_job;
    std::optional<std::uint16_t> pages_printed;
    std::optional<std::uint16_t> event_counter;

    bool busy() const { return (words[0] & 0x0080u) != 0; }
    bool buffer_full() const { return (words[0] & 0x0004u) != 0; }
    bool uninitialized() const { return (words[0] & 0x0030u) != 0; }
    bool paper_out() const { return (words[0] & 0x0002u) != 0 || (words[1] & 0x4000u) != 0; }
    bool processing() const { return (words[0] & 0x0001u) != 0 || (words[1] & 0x0080u) != 0; }
    bool printing() const { return (words[1] & 0x0004u) != 0; }
};

// Payload only, excluding the CAPT header. Requires all first 40 bytes; optional
// tail fields are read only when complete. Unknown extension bytes are ignored.
ExtendedStatus decode_extended_status(const Bytes& payload);
} // namespace capt
