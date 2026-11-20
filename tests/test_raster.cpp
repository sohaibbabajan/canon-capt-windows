// SPDX-License-Identifier: GPL-3.0-or-later
#include "capt/raster.hpp"
#include "support.hpp"

#include <algorithm>
#include <iostream>
#include <limits>

using Bytes = std::vector<std::uint8_t>;

static void raster_bands()
{
    // Source padding contains black values and must never become image pixels.
    const Bytes pixels{0, 255, 0, 255, 0, 255, 0, 0,
                        255, 0, 0, 255, 255, 0, 0, 0,
                        0, 255, 0, 0, 255, 0, 0, 0,
                        255, 0, 255, 0, 255, 0};
    const capt::GrayImageView source{pixels.data(), pixels.size(), 6, 4, 8};
    const capt::MonoPageLayout page{13, 5, 3, {1, 1, 3, 1, 4, 2}};
    const auto first = capt::prepare_monochrome_band(source, page, 0, 2);
    CHECK(first.first_row == 0 && first.rows == 2 && first.line_size == 3);
    CHECK((first.pixels == Bytes{0, 0, 0, 0x18, 0, 0}));
    const auto middle = capt::prepare_monochrome_band(source, page, 2, 2);
    CHECK((middle.pixels == Bytes{0x0C, 0, 0, 0, 0, 0}));
    const auto last = capt::prepare_monochrome_band(source, page, 4, 2);
    CHECK(last.first_row == 4 && last.rows == 1);
    CHECK((last.pixels == Bytes{0, 0, 0}));

    const Bytes black(4, 0);
    const capt::GrayImageView row{black.data(), black.size(), 4, 1, 4};
    CHECK((capt::prepare_monochrome_band(row, {11, 1, 3, {0, 0, 7, 0, 4, 1}}, 0).pixels ==
           Bytes{0x01, 0xE0, 0}));
    CHECK((capt::prepare_monochrome_band({}, {9, 3, 2, {}}, 0).pixels == Bytes(6, 0)));
    CHECK((capt::prepare_monochrome_band(row, {4, 1, 1, {0, 0, 0, 0, 4, 1}}, 0, 1, 0).pixels ==
           Bytes{0}));

    // A tall page is produced one band at a time; no whole-page allocation.
    const auto tall = capt::prepare_monochrome_band({}, {65535, 65535, 8192, {}}, 65534);
    CHECK(tall.rows == 1 && tall.pixels.size() == 8192);
}

static void invalid_band_inputs()
{
    const Bytes pixels(24, 255);
    const capt::GrayImageView source{pixels.data(), pixels.size(), 6, 4, 6};
    const capt::MonoPageLayout valid{9, 5, 2, {0, 0, 1, 1, 6, 4}};
    for (const auto& bad : {capt::MonoPageLayout{0, 5, 2, {}},
                            capt::MonoPageLayout{9, 0, 2, {}},
                            capt::MonoPageLayout{65536, 5, 8192, {}},
                            capt::MonoPageLayout{9, 65536, 2, {}},
                            capt::MonoPageLayout{9, 5, 1, {}},
                            capt::MonoPageLayout{9, 5, 65536, {}},
                            capt::MonoPageLayout{9, 5, 2, {1, 0, 0, 0, 6, 4}},
                            capt::MonoPageLayout{9, 5, 2, {0, 1, 0, 0, 6, 4}},
                            capt::MonoPageLayout{9, 5, 2, {0, 0, 4, 0, 6, 4}},
                            capt::MonoPageLayout{9, 5, 2, {0, 0, 0, 2, 6, 4}},
                            capt::MonoPageLayout{9, 5, 2, {0, 0, 0, 0, 0, 4}},
                            capt::MonoPageLayout{9, 5, 2, {std::numeric_limits<std::size_t>::max(), 0, 0, 0, 1, 1}}})
        expect_throw<std::invalid_argument>([&] { capt::prepare_monochrome_band(source, bad, 0); });
    for (const auto& bad : {capt::GrayImageView{nullptr, 24, 6, 4, 6},
                            capt::GrayImageView{pixels.data(), 23, 6, 4, 6},
                            capt::GrayImageView{pixels.data(), 24, 6, 4, 5},
                            capt::GrayImageView{pixels.data(), 0, 0, 4, 0},
                            capt::GrayImageView{pixels.data(), 0, 0, 0, 1}})
        expect_throw<std::invalid_argument>([&] { capt::prepare_monochrome_band(bad, valid, 0); });
    expect_throw<std::invalid_argument>([&] { capt::prepare_monochrome_band(source, valid, 5); });
    expect_throw<std::invalid_argument>([&] { capt::prepare_monochrome_band(source, valid, 0, 0); });
    expect_throw<std::invalid_argument>([&] { capt::prepare_monochrome_band(source, valid, 0, 65536); });
    expect_throw<std::length_error>([&] {
        const capt::GrayImageView huge{pixels.data(), std::numeric_limits<std::size_t>::max(),
                                       1, 3, std::numeric_limits<std::size_t>::max()};
        capt::prepare_monochrome_band(huge, valid, 0);
    });
    expect_throw<std::length_error>([] {
        capt::prepare_monochrome_band({}, {65535, 65535, 65535, {}}, 0, 65535);
    });
}

static void ordered_dither_bands()
{
    const capt::MonoPageLayout page{13, 9, 2, {0, 0, 3, 1, 8, 8}};
    for (const auto tone : {0, 64, 128, 192, 255}) {
        const Bytes pixels(64, static_cast<std::uint8_t>(tone));
        const capt::GrayImageView source{pixels.data(), pixels.size(), 8, 8, 8};
        const auto dither = [&](std::size_t start, std::size_t rows) {
            return capt::prepare_monochrome_band(source, page, start, rows, 128,
                                                  capt::GrayConversion::ordered_dither).pixels;
        };
        const auto whole = dither(0, 9);
        auto split = dither(0, 3);
        const auto bottom = dither(3, 6);
        split.insert(split.end(), bottom.begin(), bottom.end());
        CHECK(whole == split);

        const auto bit = [&](std::size_t x, std::size_t y) {
            return (whole[y * 2 + x / 8] & (0x80u >> (x % 8))) != 0;
        };
        int ink = 0;
        for (std::size_t y = 0; y < 9; ++y) {
            for (std::size_t x = 0; x < 16; ++x) {
                if (y == 0 || x < 3 || x >= 11) CHECK(!bit(x, y));
                else if (bit(x, y)) ++ink;
            }
        }
        CHECK(ink == 64 - (tone + 1) / 4);
    }
}

int main()
{
    try {
        const Bytes samples{0, 255, 127, 128, 1, 254, 2, 200, 0};
        CHECK((capt::pack_grayscale_row(samples.data(), 8) == Bytes{0xAA}));
        CHECK((capt::pack_grayscale_row(samples.data(), 9) == Bytes{0xAA, 0x80}));
        CHECK((capt::pack_grayscale_row(samples.data(), 1) == Bytes{0x80}));
        CHECK(capt::pack_grayscale_row(nullptr, 0).empty());
        expect_throw<std::invalid_argument>([] { capt::pack_grayscale_row(nullptr, 1); });
        const Bytes white(17, 255), black(17, 0);
        CHECK((capt::pack_grayscale_row(white.data(), white.size()) == Bytes{0, 0, 0}));
        CHECK((capt::pack_grayscale_row(black.data(), black.size()) == Bytes{255, 255, 128}));
        CHECK((capt::pack_grayscale_row(black.data(), 1, 0) == Bytes{0}));
        const Bytes edges{254, 255, 127, 128};
        CHECK((capt::pack_grayscale_row(edges.data(), 4, 255) == Bytes{0xB0}));
        CHECK((capt::pack_grayscale_row(edges.data(), 4, 128) == Bytes{0x20}));

        for (std::size_t width = 0; width <= 129; ++width) {
            Bytes gray(width);
            for (std::size_t x = 0; x < width; ++x)
                gray[x] = static_cast<std::uint8_t>((x * 29 + width) & 0xFFu);
            const auto row = capt::pack_grayscale_row(gray.data(), width);
            CHECK(row.size() == (width + 7) / 8);
            for (std::size_t x = 0; x < row.size() * 8; ++x) {
                const bool black_pixel = (row[x / 8] & (0x80u >> (x % 8))) != 0;
                CHECK(black_pixel == (x < width && gray[x] < 128));
            }
        }
        raster_bands();
        ordered_dither_bands();
        invalid_band_inputs();
        std::cout << "Raster polarity, bit order, stride, placement, bands and bounds passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
