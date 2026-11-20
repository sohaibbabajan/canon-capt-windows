// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2013 Alexey Galakhov <agalakhov@gmail.com>
// Modified 2026-11-16: C++17 adaptation of captdriver/src/hiscoa-compress.c.
// Added validated sizes/parameters, bounded output, size_t indexing and explicit
// errors. Preserves greedy token selection and reference padding. See NOTICE.md.
#include "capt/hiscoa.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace capt::hiscoa {
namespace {
void validate_parameters(const Parameters& p)
{
    if (p.origin_3 < 1 || p.origin_3 > 127 || p.origin_5 < 1 || p.origin_5 > 127 ||
        p.origin_0 < -128 || p.origin_0 > 127 || p.origin_2 < -128 || p.origin_2 > 127 ||
        p.origin_4 < 0 || p.origin_4 > 32767)
        throw std::invalid_argument("Unsupported or unrepresentable Hi-SCoA parameters");
}

class BitWriter {
public:
    BitWriter(std::size_t bound, std::size_t limit) : limit_(limit)
    {
        bytes_.reserve(std::min(bound, limit));
    }

    void push(std::uint32_t bits, unsigned count)
    {
        for (unsigned remaining = count; remaining != 0; --remaining) {
            if (used_ == 0) {
                if (bytes_.size() == limit_)
                    throw std::length_error("Hi-SCoA output limit exceeded");
                bytes_.push_back(0);
            }
            if (((bits >> (remaining - 1)) & 1u) != 0)
                bytes_.back() |= static_cast<std::uint8_t>(0x80u >> used_);
            used_ = (used_ + 1) % 8;
            ++bits_;
        }
    }

    std::vector<std::uint8_t> finish(EndMarker end)
    {
        push(0xFE, 8);
        push(static_cast<std::uint32_t>(end), 2);
        // Preserve upstream's extra all-ones word when already word aligned.
        push(0xFFFFFFFFu, static_cast<unsigned>(32 - bits_ % 32));
        for (auto& byte : bytes_)
            byte ^= 0x43;
        return std::move(bytes_);
    }

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t limit_;
    std::size_t bits_ = 0;
    unsigned used_ = 0;
};

unsigned floor_log2(unsigned value)
{
    unsigned result = 0;
    while (value > 1) {
        value >>= 1;
        ++result;
    }
    return result;
}

void write_number(BitWriter& writer, unsigned value)
{
    switch (value) {
    case 0: writer.push(0x3F, 6); return;
    case 1: writer.push(0, 2); return;
    case 2: writer.push(3, 3); return;
    case 3: writer.push(2, 3); return;
    default:
        const auto bits = floor_log2(value);
        writer.push(0xFFFFFFFEu, bits);
        writer.push(~static_cast<std::uint32_t>(value), bits);
    }
}

std::size_t resolve_origin(std::size_t line_size, std::int32_t offset)
{
    const auto distance = static_cast<std::int64_t>(line_size) + offset;
    return distance > 0 ? static_cast<std::size_t>(distance) : 0;
}

std::size_t match_length(BandView band, std::size_t position, std::size_t distance)
{
    if (distance == 0 || distance > position)
        return 0;
    const auto limit = std::min({band.size - position,
                                 band.line_size - position % band.line_size,
                                 std::size_t{639}});
    std::size_t count = 0;
    while (count < limit && band.data[position + count] == band.data[position + count - distance])
        ++count;
    return count;
}
} // namespace

std::size_t maximum_compressed_size(std::size_t input_bytes)
{
    // A literal costs at most 12 bits/byte; END costs 10, padding costs 1..32.
    if (input_bytes > (std::numeric_limits<std::size_t>::max() - 42) / 12)
        throw std::length_error("Hi-SCoA size bound overflows size_t");
    return ((input_bytes * 12 + 10) / 32 + 1) * 4;
}

std::array<std::uint8_t, 8> format_parameters(const Parameters& p)
{
    validate_parameters(p);
    return {static_cast<std::uint8_t>(p.origin_3), static_cast<std::uint8_t>(p.origin_5),
            1, 1, static_cast<std::uint8_t>(p.origin_0), static_cast<std::uint8_t>(p.origin_2),
            static_cast<std::uint8_t>(static_cast<std::uint32_t>(p.origin_4) & 0xFFu),
            static_cast<std::uint8_t>(static_cast<std::uint32_t>(p.origin_4) >> 8)};
}

std::vector<std::uint8_t> compress_band(BandView band, EndMarker end,
                                        const Parameters& parameters, std::size_t output_limit)
{
    validate_parameters(parameters);
    if (end != EndMarker::band && end != EndMarker::page)
        throw std::invalid_argument("Unknown Hi-SCoA end marker");
    if (band.line_size == 0 || band.line_size > 65535 || band.rows == 0 || band.rows > 65535)
        throw std::invalid_argument("Hi-SCoA dimensions must be in [1,65535]");
    if (band.rows > std::numeric_limits<std::size_t>::max() / band.line_size)
        throw std::length_error("Hi-SCoA dimensions overflow size_t");
    if (band.size != band.line_size * band.rows || band.data == nullptr)
        throw std::invalid_argument("Hi-SCoA input must contain exactly the declared band");
    if (band.size > max_band_bytes)
        throw std::length_error("Hi-SCoA band exceeds the memory policy");

    BitWriter writer(maximum_compressed_size(band.size), output_limit);
    std::array<std::size_t, 6> origins{
        resolve_origin(band.line_size, parameters.origin_0), 0,
        resolve_origin(band.line_size, parameters.origin_2),
        static_cast<std::size_t>(parameters.origin_3),
        static_cast<std::size_t>(parameters.origin_4),
        static_cast<std::size_t>(parameters.origin_5)};
    std::array<std::uint8_t, 16> stash{};
    std::size_t stash_size = 0;
    std::size_t position = 0;
    while (position < band.size) {
        unsigned best_command = 0;
        std::size_t best_length = 0;
        for (unsigned command = 0; command < origins.size(); ++command) {
            const auto count = match_length(band, position, origins[command]);
            if (count > best_length) {
                best_length = count;
                best_command = command;
            }
        }
        if (best_length > 1) {
            auto length = static_cast<unsigned>(best_length);
            if (length > 127) {
                const auto prefix = length / 128;
                const auto order = floor_log2(prefix);
                writer.push(0xFC, 8);
                writer.push(order, 2);
                writer.push(~static_cast<std::uint32_t>(prefix), order);
                length %= 128;
            }
            writer.push(0xFFFFFFFEu, best_command + 1);
            if (best_command == 2)
                writer.push(0, 1);
            write_number(writer, length);
            if (best_command == 2)
                std::swap(origins[0], origins[2]);
            if (best_command == 5)
                std::swap(origins[3], origins[5]);
            position += best_length;
            continue;
        }

        const auto byte = band.data[position++];
        auto index = std::size_t{0};
        while (index < stash_size && stash[index] != byte)
            ++index;
        if (index < stash_size) {
            writer.push(0x20u | static_cast<std::uint32_t>(15 - index), 6);
        } else {
            writer.push(byte == 0 ? 0xFDu : (0xD00u | byte), byte == 0 ? 8u : 12u);
            stash_size = std::min<std::size_t>(16, stash_size + 1);
            index = stash_size - 1;
        }
        for (std::size_t i = index; i != 0; --i)
            stash[i] = stash[i - 1];
        stash[0] = byte;
    }
    return writer.finish(end);
}
} // namespace capt::hiscoa
