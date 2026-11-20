// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "capt/limits.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace capt {
// Gray 0=black, 255=white. Black when sample<threshold. Output is MSB first,
// 1=black, with unused low bits zero. A null input is allowed only for width 0.
// Row-only primitive: no DPI, geometry, stride, banding or final halftoning.
std::vector<std::uint8_t> pack_grayscale_row(const std::uint8_t* gray,
                                          std::size_t width,
                                          std::uint8_t threshold = 128);

struct GrayImageView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t stride = 0; // Bytes between source rows; the last row needs width bytes.
};

struct ImagePlacement {
    std::size_t source_x = 0;
    std::size_t source_y = 0;
    std::size_t target_x = 0;
    std::size_t target_y = 0;
    std::size_t width = 0;
    std::size_t height = 0;
};

struct MonoPageLayout {
    std::size_t width;     // Output pixels, not points or bytes.
    std::size_t height;    // Output rows.
    std::size_t line_size; // Packed bytes/row, at least ceil(width/8).
    ImagePlacement image;  // Explicit source crop and destination rectangle.
};

struct RasterBand {
    std::size_t first_row;
    std::size_t rows;
    std::size_t line_size;
    std::vector<std::uint8_t> pixels;
};

enum class GrayConversion { threshold, ordered_dither };

// Produces at most requested_rows, stopping at page end. Rectangles must fit;
// no implicit cropping/scaling/centering. Margins, row padding and tail bits
// are white. A zero-size placement and empty source produce a blank band.
// Page width/height/line_size and requested_rows must be in [1,65535].
// Allocates only this band, capped by max_band_bytes. Throws invalid_argument
// for malformed views/geometry, length_error for arithmetic/resource overflow.
RasterBand prepare_monochrome_band(GrayImageView source, const MonoPageLayout& page,
                                  std::size_t first_row, std::size_t requested_rows = 160,
                                  std::uint8_t threshold = 128,
                                  GrayConversion conversion = GrayConversion::threshold);
} // namespace capt
