// SPDX-License-Identifier: GPL-3.0-or-later
// Separate test-only decoder based on captdriver/SPECS and the test decoder's
// prefix formula. Does not call or share encoder bit/number/match helpers.
#pragma once

#include "capt/hiscoa.hpp"

#include <stdexcept>
#include <utility>

namespace test_hiscoa {
using Bytes = std::vector<std::uint8_t>;

struct Decoded {
    Bytes pixels;
    capt::hiscoa::EndMarker end;
    std::array<std::size_t, 6> repeats{};
    std::size_t prefixes = 0;
    std::size_t stash_reads = 0;
    bool end_aligned = false;
};

class Reader {
public:
    explicit Reader(const Bytes& input) : input_(input) {}
    unsigned bit()
    {
        if (position_ / 8 >= input_.size())
            throw std::runtime_error("Truncated test Hi-SCoA bitstream");
        const auto value = ((static_cast<unsigned>(input_[position_ / 8]) ^ 0x43u) >>
                            (7 - position_ % 8)) & 1u;
        ++position_;
        return value;
    }
    unsigned number(unsigned count)
    {
        unsigned result = 0;
        while (count-- != 0)
            result = result * 2 + bit();
        return result;
    }
    std::size_t position() const { return position_; }
    void padding()
    {
        const auto count = 32 - position_ % 32;
        for (std::size_t i = 0; i < count; ++i) {
            if (bit() != 1)
                throw std::runtime_error("Invalid test Hi-SCoA padding");
        }
        if (position_ / 8 != input_.size())
            throw std::runtime_error("Trailing test Hi-SCoA bytes");
    }
private:
    const Bytes& input_;
    std::size_t position_ = 0;
};

inline unsigned repeat_number(Reader& reader)
{
    unsigned order = 0;
    while (order < 6 && reader.bit() != 0)
        ++order;
    if (order == 6)
        return 0;
    if (order == 0) {
        if (reader.bit() == 0)
            return 1;
        return reader.bit() != 0 ? 2u : 3u;
    }
    return (1u << (order + 2)) - 1 - reader.number(order + 1);
}

inline Decoded decode(const Bytes& input, std::size_t expected_size, std::size_t line_size,
                      const capt::hiscoa::Parameters& p = {})
{
    if (expected_size > capt::max_band_bytes)
        throw std::length_error("Test decoder output exceeds limit");
    Reader reader(input);
    Decoded result{{}, capt::hiscoa::EndMarker::band};
    result.pixels.reserve(expected_size);
    std::array<std::int64_t, 6> distances{
        static_cast<std::int64_t>(line_size) + p.origin_0, 0,
        static_cast<std::int64_t>(line_size) + p.origin_2,
        p.origin_3, p.origin_4, p.origin_5};
    Bytes stash;
    unsigned prefix = 0;
    bool pending_prefix = false;
    const auto no_prefix = [&] {
        if (pending_prefix)
            throw std::runtime_error("Hi-SCoA prefix must precede a long repeat");
    };
    const auto output = [&](std::uint8_t byte) {
        if (result.pixels.size() == expected_size)
            throw std::runtime_error("Test decoder output exceeds declared band");
        result.pixels.push_back(byte);
    };
    const auto save = [&](std::uint8_t byte) {
        stash.insert(stash.begin(), byte);
        if (stash.size() > 16)
            stash.pop_back();
    };
    const auto copy = [&](unsigned command) {
        const auto length = prefix + repeat_number(reader);
        const auto signed_distance = distances[command];
        if (signed_distance <= 0 || static_cast<std::uint64_t>(signed_distance) > result.pixels.size())
            throw std::runtime_error("Test decoder back-reference is unavailable");
        if (length > expected_size - result.pixels.size())
            throw std::runtime_error("Test decoder repeat exceeds declared band");
        const auto distance = static_cast<std::size_t>(signed_distance);
        for (unsigned i = 0; i < length; ++i)
            output(result.pixels[result.pixels.size() - distance]);
        ++result.repeats[command];
        prefix = 0;
        pending_prefix = false;
    };

    for (;;) {
        unsigned command = 0;
        while (command < 8 && reader.bit() != 0)
            ++command;
        switch (command) {
        case 0: copy(0); break;
        case 1: {
            no_prefix();
            const auto index = 15 - reader.number(4);
            if (index >= stash.size())
                throw std::runtime_error("Test decoder stash entry is unavailable");
            const auto byte = stash[index];
            stash.erase(stash.begin() + static_cast<std::ptrdiff_t>(index));
            save(byte);
            output(byte);
            ++result.stash_reads;
            break;
        }
        case 2:
            if (reader.bit() != 0) {
                no_prefix();
                const auto byte = static_cast<std::uint8_t>(reader.number(8));
                save(byte);
                output(byte);
            } else {
                copy(2);
                std::swap(distances[0], distances[2]);
            }
            break;
        case 3: copy(3); break;
        case 4: copy(4); break;
        case 5:
            copy(5);
            std::swap(distances[3], distances[5]);
            break;
        case 6:
            no_prefix();
            if (reader.bit() != 0) {
                save(0);
                output(0);
            } else {
                const auto order = reader.number(2);
                prefix = 128 * ((1u << (order + 1)) - 1 - reader.number(order));
                pending_prefix = true;
                ++result.prefixes;
            }
            break;
        case 7: {
            no_prefix();
            const auto marker = reader.number(2);
            if (marker > 1 || result.pixels.size() != expected_size)
                throw std::runtime_error("Invalid test Hi-SCoA END or output size");
            result.end = static_cast<capt::hiscoa::EndMarker>(marker);
            result.end_aligned = reader.position() % 32 == 0;
            reader.padding();
            return result;
        }
        case 8: no_prefix(); break; // Eight one bits: NOP.
        }
    }
}
} // namespace test_hiscoa
