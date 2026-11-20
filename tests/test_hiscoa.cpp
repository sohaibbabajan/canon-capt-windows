// SPDX-License-Identifier: GPL-3.0-or-later
#include "capt/hiscoa.hpp"
#include "capt/raster.hpp"
#include "hiscoa_decoder.hpp"
#include "support.hpp"
#ifdef CAPT_HAS_REFERENCE
#include "reference_hiscoa.h"
#endif

#include <iostream>
#include <limits>

using Bytes = std::vector<std::uint8_t>;
using capt::hiscoa::EndMarker;
using capt::hiscoa::Parameters;

static std::size_t checked_bands = 0;
static std::array<std::size_t, 6> repeat_counts{};
static std::size_t prefix_count = 0;
static std::size_t stash_count = 0;

static test_hiscoa::Decoded verify(const Bytes& input, std::size_t line_size, std::size_t rows,
                                  const Parameters& p = {}, EndMarker end = EndMarker::band)
{
    const auto encoded = capt::hiscoa::compress_band({input.data(), input.size(), line_size, rows}, end, p);
    CHECK(encoded.size() % 4 == 0);
    CHECK(encoded.size() <= capt::hiscoa::maximum_compressed_size(input.size()));
    auto decoded = test_hiscoa::decode(encoded, input.size(), line_size, p);
    CHECK(decoded.pixels == input);
    CHECK(decoded.end == end);
    for (std::size_t i = 0; i < repeat_counts.size(); ++i)
        repeat_counts[i] += decoded.repeats[i];
    prefix_count += decoded.prefixes;
    stash_count += decoded.stash_reads;
#ifdef CAPT_HAS_REFERENCE
    Bytes reference(capt::hiscoa::maximum_compressed_size(input.size()), 0);
    const std::int32_t origins[]{p.origin_3, p.origin_5, p.origin_0, p.origin_2, p.origin_4};
    const auto size = capt_reference_hiscoa(reference.data(), reference.size(), input.data(),
                                           static_cast<unsigned>(line_size), static_cast<unsigned>(rows),
                                           origins, end == EndMarker::page ? 1 : 0);
    CHECK(size <= reference.size());
    reference.resize(size);
    CHECK(encoded == reference);
#endif
    ++checked_bands;
    return decoded;
}

static void exact_fixtures()
{
    // Manually assembled token/bit vectors, then XOR 0x43. No encoder generated
    // the expected bytes. Derivations are in docs/HISCOA.md; all are synthetic.
    struct Fixture { Bytes raw; Bytes encoded; EndMarker end; };
    const Fixture fixtures[]{
        {{0}, {0xBE, 0xBD, 0x7C, 0xBC}, EndMarker::band},
        {{0}, {0xBE, 0xBD, 0x3C, 0xBC}, EndMarker::page},
        {{0xAA}, {0x99, 0xEC, 0xA0, 0xBC}, EndMarker::band},
        {{0xAA, 0xAA}, {0x99, 0xE8, 0xBC, 0xCC}, EndMarker::band},
        {{0xAA, 0xAA, 0xAA}, {0x99, 0xED, 0x3C, 0x84}, EndMarker::band},
        {Bytes(9, 0xAA), {0x99, 0xED, 0x9C, 0xBB, 0xBC, 0xBC, 0xBC, 0xBC}, EndMarker::band},
        {Bytes(129, 0xAA), {0x99, 0xEC, 0x80, 0xFC, 0xBD, 0x7C, 0xBC, 0xBC}, EndMarker::band}
    };
    for (const auto& fixture : fixtures) {
        const auto actual = capt::hiscoa::compress_band(
            {fixture.raw.data(), fixture.raw.size(), fixture.raw.size(), 1}, fixture.end);
        CHECK(actual == fixture.encoded);
        verify(fixture.raw, fixture.raw.size(), 1, {}, fixture.end);
    }
    CHECK(verify(Bytes(9, 0xAA), 9, 1).end_aligned);
    CHECK((capt::hiscoa::format_parameters() ==
           std::array<std::uint8_t, 8>{1, 4, 1, 1, 0, 0xF9, 0, 0}));
    CHECK((capt::hiscoa::format_parameters({4, 12, 0, -5, 384}) ==
           std::array<std::uint8_t, 8>{4, 12, 1, 1, 0, 0xFB, 0x80, 1}));
}

static void repeats_and_stash()
{
    // Exercise every representable repeat remainder and all encoder prefix
    // ranges, including row-boundary and 639-byte match limits.
    for (std::size_t length = 2; length <= 640; ++length)
        verify(Bytes(length + 1, 0xAA), length + 1, 1);
    verify({1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8}, 8, 2);
    verify(Bytes(16, 0xAA), 8, 2); // L2/L0 swap, followed by a row repeat.
    Bytes period3(39), period4(51);
    for (std::size_t i = 0; i < period3.size(); ++i)
        period3[i] = static_cast<std::uint8_t>(i % 3 + 1);
    for (std::size_t i = 0; i < period4.size(); ++i)
        period4[i] = static_cast<std::uint8_t>(i % 4 + 1);
    verify(period3, 13, 3, {1, 4, 0, -7, 3});
    verify(period4, 17, 3); // L5/L3 swap is used by the following row.
    for (unsigned period : {15u, 16u, 17u, 256u}) {
        Bytes input(1000);
        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<std::uint8_t>(i % period);
        verify(input, 100, 10);
    }
    for (auto command : {0u, 2u, 3u, 4u, 5u})
        CHECK(repeat_counts[command] != 0);
    CHECK(prefix_count != 0 && stash_count != 0);
}

static void deterministic_bands()
{
    std::uint32_t random = 0x6000B123u; // Fixed xorshift32 seed.
    const Parameters parameters[]{{}, {4, 12, 0, -12, 340}, {127, 1, -128, 127, 32767}};
    for (const auto& p : parameters) {
        for (auto line_size : {1u, 2u, 7u, 8u, 16u, 64u, 595u, 640u}) {
            for (auto rows : {1u, 2u, 7u, 160u}) {
                for (unsigned kind = 0; kind < 4; ++kind) {
                    Bytes input(static_cast<std::size_t>(line_size) * rows);
                    for (auto& pixel : input) {
                        random ^= random << 13;
                        random ^= random >> 17;
                        random ^= random << 5;
                        pixel = kind == 0 ? 0 : kind == 1 ? 255 :
                            static_cast<std::uint8_t>(random & (kind == 2 ? 7u : 255u));
                    }
                    verify(input, line_size, rows, p, rows % 2 == 0 ? EndMarker::page : EndMarker::band);
                }
            }
        }
    }
}

static void capacity_and_validation()
{
    const Bytes input(129, 0xAA);
    const capt::hiscoa::BandView band{input.data(), input.size(), input.size(), 1};
    const auto encoded = capt::hiscoa::compress_band(band);
    CHECK(capt::hiscoa::compress_band(band, EndMarker::band, {}, encoded.size()) == encoded);
    expect_throw<std::length_error>([&] {
        capt::hiscoa::compress_band(band, EndMarker::band, {}, encoded.size() - 1);
    });
    expect_throw<std::length_error>([&] { capt::hiscoa::compress_band(band, EndMarker::band, {}, 0); });
    CHECK(capt::hiscoa::maximum_compressed_size(0) == 4);
    CHECK(capt::hiscoa::maximum_compressed_size(1) == 4);
    CHECK(capt::hiscoa::maximum_compressed_size(2) == 8);
    expect_throw<std::length_error>([] {
        capt::hiscoa::maximum_compressed_size(std::numeric_limits<std::size_t>::max());
    });
    for (const auto& bad : {
             capt::hiscoa::BandView{nullptr, 1, 1, 1},
             capt::hiscoa::BandView{input.data(), 0, 0, 1},
             capt::hiscoa::BandView{input.data(), 0, 1, 0},
             capt::hiscoa::BandView{input.data(), 1, 65536, 1},
             capt::hiscoa::BandView{input.data(), 1, 1, 65536},
             capt::hiscoa::BandView{input.data(), 128, 129, 1},
             capt::hiscoa::BandView{input.data(), 130, 129, 1}})
        expect_throw<std::invalid_argument>([&] { capt::hiscoa::compress_band(bad); });
    expect_throw<std::invalid_argument>([&] { capt::hiscoa::compress_band(band, static_cast<EndMarker>(2)); });
    expect_throw<std::length_error>([&] {
        capt::hiscoa::compress_band({input.data(), 16384u * 1025u, 16384, 1025});
    });
    for (const auto& bad : {Parameters{0, 4, 0, -7, 0}, Parameters{128, 4, 0, -7, 0},
                            Parameters{1, -1, 0, -7, 0}, Parameters{1, 128, 0, -7, 0},
                            Parameters{1, 4, -129, -7, 0}, Parameters{1, 4, 128, -7, 0},
                            Parameters{1, 4, 0, -129, 0}, Parameters{1, 4, 0, 128, 0},
                            Parameters{1, 4, 0, -7, -1}, Parameters{1, 4, 0, -7, 32768}}) {
        expect_throw<std::invalid_argument>([&] { capt::hiscoa::format_parameters(bad); });
        expect_throw<std::invalid_argument>([&] { capt::hiscoa::compress_band(band, EndMarker::band, bad); });
    }
    // The oracle must detect truncated/corrupt padding, output mismatch and
    // missing data instead of silently filling invented pixels.
    for (std::size_t size = 0; size < encoded.size(); ++size) {
        const Bytes truncated(encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(size));
        expect_throw<std::runtime_error>([&] { test_hiscoa::decode(truncated, 129, 129); });
    }
    auto corrupted = encoded;
    corrupted.back() ^= 1;
    expect_throw<std::runtime_error>([&] { test_hiscoa::decode(corrupted, 129, 129); });
    expect_throw<std::runtime_error>([&] { test_hiscoa::decode(encoded, 128, 129); });
}

static void raster_to_codec()
{
    const Bytes image{0, 255, 0, 255, 0, 255, 0, 255, 0,
                       255, 0, 255, 0, 255, 0, 255, 0, 255,
                       0, 0, 0, 0, 0, 0, 0, 0, 0};
    const capt::GrayImageView source{image.data(), image.size(), 9, 3, 9};
    const capt::MonoPageLayout page{13, 5, 3, {0, 0, 2, 1, 9, 3}};
    Bytes joined;
    for (std::size_t row = 0; row < page.height; row += 2) {
        const auto band = capt::prepare_monochrome_band(source, page, row, 2);
        const auto end = band.first_row + band.rows == page.height ? EndMarker::page : EndMarker::band;
        const auto decoded = verify(band.pixels, band.line_size, band.rows, {}, end);
        joined.insert(joined.end(), decoded.pixels.begin(), decoded.pixels.end());
    }
    CHECK((joined == Bytes{0, 0, 0, 0x2A, 0xA0, 0, 0x15, 0x40, 0,
                           0x3F, 0xE0, 0, 0, 0, 0}));
}

int main()
{
    try {
        exact_fixtures();
        repeats_and_stash();
        deterministic_bands();
        capacity_and_validation();
        raster_to_codec();
        std::cout << checked_bands << " Hi-SCoA bands decoded and verified";
#ifdef CAPT_HAS_REFERENCE
        std::cout << ", each byte-identical to the local C reference";
#endif
        std::cout << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
