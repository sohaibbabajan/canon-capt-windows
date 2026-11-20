// SPDX-License-Identifier: GPL-3.0-or-later
// Protocol reference and attribution: NOTICE.md. New bounded implementation.
#include "capt/protocol.hpp"

#include <algorithm>
#include <stdexcept>

namespace capt {
namespace {
std::uint16_t little_word(const std::uint8_t* bytes)
{
    return static_cast<std::uint16_t>(static_cast<unsigned>(bytes[0]) |
                                       (static_cast<unsigned>(bytes[1]) << 8));
}

void read_exact(Transport& transport, std::uint8_t* data, std::size_t size)
{
    while (size != 0) {
        const auto count = transport.read_some(data, size);
        if (count == 0 || count > size)
            throw std::runtime_error("CAPT transport returned invalid read progress");
        data += count;
        size -= count;
    }
}

std::size_t reply_size(const std::uint8_t* bytes, ReplyLength encoding)
{
    if (encoding == ReplyLength::binary)
        return little_word(bytes);
    std::size_t result = 0;
    std::size_t place = 1;
    for (unsigned nibble = 0; nibble < 4; ++nibble) {
        const unsigned digit = (static_cast<unsigned>(bytes[nibble / 2]) >>
                                (4 * (nibble % 2))) & 0x0Fu;
        if (digit > 9)
            throw std::runtime_error("CAPT reply has invalid BCD length");
        result += digit * place;
        place *= 10;
    }
    return result;
}
} // namespace

Bytes encode_packet(const Packet& packet)
{
    if (packet.payload.size() > max_packet_size - 4)
        throw std::length_error("CAPT packet exceeds 16-bit total length");
    Bytes frame(4 + packet.payload.size());
    frame[0] = static_cast<std::uint8_t>(packet.command & 0xFFu);
    frame[1] = static_cast<std::uint8_t>(packet.command >> 8);
    frame[2] = static_cast<std::uint8_t>(frame.size() & 0xFFu);
    frame[3] = static_cast<std::uint8_t>(frame.size() >> 8);
    std::copy(packet.payload.begin(), packet.payload.end(), frame.begin() + 4);
    return frame;
}

void write_packet(Transport& transport, const Packet& packet)
{
    const auto frame = encode_packet(packet);
    std::size_t offset = 0;
    while (offset < frame.size()) {
        const auto requested = std::min<std::size_t>(4096, frame.size() - offset);
        const auto count = transport.write_some(frame.data() + offset, requested);
        if (count == 0 || count > requested)
            throw std::runtime_error("CAPT transport returned invalid write progress");
        offset += count;
    }
}

Packet read_reply(Transport& transport, std::uint16_t expected_command,
                  std::size_t max_total_length, ReplyLength encoding)
{
    if (max_total_length < 6 || max_total_length > max_packet_size)
        throw std::invalid_argument("CAPT reply limit must be in [6, 65535]");
    if (encoding != ReplyLength::binary && encoding != ReplyLength::bcd)
        throw std::invalid_argument("Unknown CAPT reply length encoding");
    Bytes frame(6);
    read_exact(transport, frame.data(), frame.size());
    if (little_word(frame.data()) != expected_command)
        throw std::runtime_error("CAPT reply command mismatch");
    const auto total = reply_size(frame.data() + 2, encoding);
    if (total < 6)
        throw std::runtime_error("CAPT reply is shorter than six bytes");
    if (total > max_total_length)
        throw std::length_error("CAPT reply exceeds configured limit");
    frame.resize(total);
    read_exact(transport, frame.data() + 6, total - 6);
    return {expected_command, Bytes(frame.begin() + 4, frame.end())};
}
} // namespace capt
