// SPDX-License-Identifier: GPL-3.0-or-later
#include "capt/raster.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace capt {
namespace {
// Centered 4x4 Bayer thresholds. Index using page coordinates so a band's
// start row or an image crop cannot reset the screen phase.
constexpr std::uint8_t ordered_threshold[4][4] = {
    {8, 136, 40, 168}, {200, 72, 232, 104}, {56, 184, 24, 152}, {248, 120, 216, 88}};
}
std::vector<std::uint8_t> pack_grayscale_row(const std::uint8_t* gray,
                                          std::size_t width,
                                          std::uint8_t threshold)
{
    if (gray == nullptr && width != 0)
        throw std::invalid_argument("Nonempty grayscale row needs input pixels");
    // Division first avoids overflow in the customary (width + 7) / 8 formula.
    std::vector<std::uint8_t> row(width / 8 + (width % 8 != 0 ? 1u : 0u), 0);
    for (std::size_t x = 0; x < width; ++x) {
        if (gray[x] < threshold)
            row[x / 8] |= static_cast<std::uint8_t>(0x80u >> (x % 8));
    }
    return row;
}

RasterBand prepare_monochrome_band(GrayImageView source, const MonoPageLayout& page,
                                  std::size_t first_row, std::size_t requested_rows,
                                  std::uint8_t threshold, GrayConversion conversion)
{
    if (page.width == 0 || page.width > 65535 || page.height == 0 || page.height > 65535 ||
        page.line_size == 0 || page.line_size > 65535 || requested_rows == 0 || requested_rows > 65535 ||
        page.line_size < page.width / 8 + (page.width % 8 != 0 ? 1u : 0u) || first_row >= page.height)
        throw std::invalid_argument("Invalid monochrome page or band geometry");

    if (source.width == 0 || source.height == 0) {
        if (source.width != 0 || source.height != 0 || source.size != 0 || source.stride != 0)
            throw std::invalid_argument("Empty source needs zero dimensions, size and stride");
    } else {
        if (source.data == nullptr || source.stride < source.width)
            throw std::invalid_argument("Invalid grayscale source pointer or stride");
        if (source.height - 1 > (std::numeric_limits<std::size_t>::max() - source.width) / source.stride)
            throw std::length_error("Grayscale source extent overflows size_t");
        if ((source.height - 1) * source.stride + source.width > source.size)
            throw std::invalid_argument("Truncated grayscale source");
    }

    const auto& image = page.image;
    if ((image.width == 0) != (image.height == 0) ||
        image.source_x > source.width || image.width > source.width - image.source_x ||
        image.source_y > source.height || image.height > source.height - image.source_y ||
        image.target_x > page.width || image.width > page.width - image.target_x ||
        image.target_y > page.height || image.height > page.height - image.target_y)
        throw std::invalid_argument("Image placement exceeds source or destination");

    const auto rows = std::min(requested_rows, page.height - first_row);
    if (rows > max_band_bytes / page.line_size)
        throw std::length_error("Raster band exceeds the memory policy");
    RasterBand band{first_row, rows, page.line_size,
                    std::vector<std::uint8_t>(rows * page.line_size, 0)};
    for (std::size_t row = 0; row < rows; ++row) {
        const auto page_row = first_row + row;
        if (page_row < image.target_y || page_row - image.target_y >= image.height)
            continue;
        const auto source_row = image.source_y + page_row - image.target_y;
        const auto* pixels = source.data + source_row * source.stride + image.source_x;
        const auto& screen_row = ordered_threshold[page_row % 4];
        for (std::size_t x = 0; x < image.width; ++x) {
            const auto target_x = image.target_x + x;
            const auto tone = conversion == GrayConversion::ordered_dither ? screen_row[target_x % 4] : threshold;
            if (pixels[x] < tone)
                band.pixels[row * page.line_size + target_x / 8] |=
                    static_cast<std::uint8_t>(0x80u >> (target_x % 8));
        }
    }
    return band;
}
} // namespace capt
