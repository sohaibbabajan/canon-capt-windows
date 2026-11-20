// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace capt {
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t max_packet_size = 65535;

struct Packet {
    std::uint16_t command;
    Bytes payload;
};

enum class ReplyLength { binary, bcd };

// Every call must make bounded progress or throw. Real adapters must implement
// finite timeouts/cancellation; these helpers cannot interrupt a blocking call.
class Transport {
public:
    virtual ~Transport() = default;
    virtual std::size_t write_some(const std::uint8_t* data, std::size_t size) = 0;
    virtual std::size_t read_some(std::uint8_t* data, std::size_t size) = 0;
};

// Ordinary little-endian command frame. Throws length_error above 65535 bytes.
Bytes encode_packet(const Packet& packet);
// Writes only; no reply policy or device recovery is implied.
void write_packet(Transport& transport, const Packet& packet);
// Ordinary echoed-command replies only (minimum total length 6). Excludes raw
// identification replies. Length includes header; returned payload excludes it.
// No heuristic BCD detection, retries, session locking or overall deadline.
Packet read_reply(Transport& transport, std::uint16_t expected_command,
                  std::size_t max_total_length = max_packet_size,
                  ReplyLength encoding = ReplyLength::binary);
} // namespace capt
