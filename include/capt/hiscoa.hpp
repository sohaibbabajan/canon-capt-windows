// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "capt/limits.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace capt::hiscoa {
struct Parameters {
    std::int32_t origin_3 = 1;
    std::int32_t origin_5 = 4;
    std::int32_t origin_0 = 0;
    std::int32_t origin_2 = -7;
    std::int32_t origin_4 = 0;
};

enum class EndMarker : std::uint8_t { band = 0, page = 1 };

struct BandView {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t line_size; // Packed bytes per line, including row padding.
    std::size_t rows;
};

// Conservative maximum including END, XOR and the reference's 1..32 padding
// bits. Throws length_error if the calculation would overflow size_t.
std::size_t maximum_compressed_size(std::size_t input_bytes);

// Positive signed-byte L3/L5, signed-byte L0/L2, and nonnegative signed-word
// L4 (0 disables it). Invalid/unrepresentable parameters throw invalid_argument.
std::array<std::uint8_t, 8> format_parameters(const Parameters& parameters = {});

// Audited port of the reference's greedy band encoder. No CUPS/OS dependencies.
// Dimensions must be in [1,65535], size must equal line_size*rows, and input
// must not exceed capt::max_band_bytes. Invalid arguments throw invalid_argument;
// resource/output limits throw length_error. No partial output is returned.
// Nonpositive resolved L0/L2 distances are disabled, avoiding unsigned wrap.
std::vector<std::uint8_t> compress_band(
    BandView band, EndMarker end = EndMarker::band,
    const Parameters& parameters = {},
    std::size_t output_limit = std::numeric_limits<std::size_t>::max());
} // namespace capt::hiscoa
