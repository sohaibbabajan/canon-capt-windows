// SPDX-License-Identifier: GPL-3.0-or-later
// Field layout: captdriver/SPECS and src/capt-status.c. See NOTICE.md.
#include "capt/status.hpp"

#include <stdexcept>

namespace capt {
ExtendedStatus decode_extended_status(const Bytes& payload)
{
    if (payload.size() < 40)
        throw std::runtime_error("CAPT extended status needs at least 40 payload bytes");
    const auto word = [&](std::size_t offset) {
        return static_cast<std::uint16_t>(static_cast<unsigned>(payload[offset]) |
                                          (static_cast<unsigned>(payload[offset + 1]) << 8));
    };
    ExtendedStatus status;
    constexpr std::size_t offsets[]{0, 8, 10, 12, 24, 30, 38};
    for (std::size_t i = 0; i < status.words.size(); ++i)
        status.words[i] = word(offsets[i]);
    status.page_decoding = word(14);
    status.page_printing = word(16);
    status.page_out = word(18);
    status.page_completed = word(20);
    status.job = word(28);
    status.page_received = word(34);
    if (payload.size() >= 42) status.pages_in_job = word(40);
    if (payload.size() >= 46) status.pages_printed = word(44);
    if (payload.size() >= 66) status.event_counter = word(64);
    return status;
}
} // namespace capt
