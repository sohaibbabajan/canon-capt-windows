// SPDX-License-Identifier: GPL-3.0-or-later
#include "capt/protocol.hpp"
#include "mock_transport.hpp"
#include "support.hpp"

#include <iostream>
#include <string>

using capt::Bytes;
using capt::ReplyLength;

static void wire_fixtures()
{
    // Synthetic serialization vectors, not device captures. SPECS §1.1 and
    // prn_lbp2900.c's LBP6000 setup payload supply the independent expectations.
    CHECK((capt::encode_packet({0xD0A1, {}}) == Bytes{0xA1, 0xD0, 4, 0}));
    CHECK((capt::encode_packet({0xE0BA, {1, 0}}) == Bytes{0xBA, 0xE0, 6, 0, 1, 0}));
    MockTransport usb;
    usb.write_chunk = 1;
    capt::write_packet(usb, {0xE0BA, {1, 0}});
    CHECK((usb.outgoing == Bytes{0xBA, 0xE0, 6, 0, 1, 0}));
    CHECK(usb.read_requests.empty());

    Bytes payload(65531, 0xA5);
    const auto maximum = capt::encode_packet({0xC0A0, payload});
    CHECK(maximum.size() == 65535);
    CHECK(maximum[2] == 0xFF && maximum[3] == 0xFF);
    CHECK(maximum.back() == 0xA5);
    MockTransport large;
    capt::write_packet(large, {0xC0A0, payload});
    CHECK(large.outgoing == maximum);
    CHECK(large.write_requests.front() == 4096);
    CHECK(large.write_requests.size() == 16);
    for (auto requested : large.write_requests)
        CHECK(requested <= 4096);
    payload.push_back(0);
    expect_throw<std::length_error>([&] { capt::encode_packet({0xC0A0, payload}); });
}

static void reply_fixtures()
{
    MockTransport usb;
    usb.incoming = {0xA5, 0xE0, 6, 0, 0, 0}; // SPECS reply example.
    const auto reply = capt::read_reply(usb, 0xE0A5);
    CHECK(reply.command == 0xE0A5);
    CHECK((reply.payload == Bytes{0, 0})); // No header bytes in payload.
    CHECK((usb.read_requests == std::vector<std::size_t>{6}));

    MockTransport split;
    split.incoming = {0xA8, 0xA0, 10, 0, 1, 2, 3, 4, 5, 6};
    CHECK((capt::read_reply(split, 0xA0A8).payload == Bytes{1, 2, 3, 4, 5, 6}));
    CHECK((split.read_requests == std::vector<std::size_t>{6, 4}));

    MockTransport fragmented;
    fragmented.incoming = split.incoming;
    fragmented.read_chunk = 1;
    CHECK((capt::read_reply(fragmented, 0xA0A8).payload == Bytes{1, 2, 3, 4, 5, 6}));
    CHECK(fragmented.read_requests.size() == 10);

    MockTransport adjacent;
    adjacent.incoming = {0xA5, 0xE0, 6, 0, 0, 0, 0xA8, 0xA0, 6, 0, 1, 2};
    capt::read_reply(adjacent, 0xE0A5);
    CHECK(adjacent.offset == 6);
    CHECK((capt::read_reply(adjacent, 0xA0A8).payload == Bytes{1, 2}));
    CHECK(adjacent.offset == adjacent.incoming.size());

    MockTransport bcd;
    bcd.incoming = Bytes(57, 0xAA);
    bcd.incoming[0] = 0xA8;
    bcd.incoming[1] = 0xA0;
    bcd.incoming[2] = 0x57;
    bcd.incoming[3] = 0;
    CHECK(capt::read_reply(bcd, 0xA0A8, 57, ReplyLength::bcd).payload == Bytes(53, 0xAA));
    bcd.offset = 0;
    // Same digits would mean 87 with binary decoding; no auto-fallback.
    expect_throw<std::length_error>([&] { capt::read_reply(bcd, 0xA0A8, 57); });

    MockTransport long_bcd;
    long_bcd.incoming = Bytes(1234, 0xCC);
    long_bcd.incoming[0] = 0xA8;
    long_bcd.incoming[1] = 0xA0;
    long_bcd.incoming[2] = 0x34;
    long_bcd.incoming[3] = 0x12;
    CHECK(capt::read_reply(long_bcd, 0xA0A8, 1234, ReplyLength::bcd).payload == Bytes(1230, 0xCC));

    MockTransport maximum;
    maximum.incoming = Bytes(65535, 0xCC);
    maximum.incoming[0] = 0xA8;
    maximum.incoming[1] = 0xA0;
    maximum.incoming[2] = maximum.incoming[3] = 0xFF;
    CHECK(capt::read_reply(maximum, 0xA0A8).payload == Bytes(65531, 0xCC));
}

static void rejection_and_failures()
{
    MockTransport usb;
    usb.incoming = {0xA8, 0xA0, 6, 0, 0, 0};
    for (auto limit : {std::size_t{0}, std::size_t{5}, std::size_t{65536}})
        expect_throw<std::invalid_argument>([&] { capt::read_reply(usb, 0xA0A8, limit); });
    expect_throw<std::invalid_argument>([&] {
        capt::read_reply(usb, 0xA0A8, 100, static_cast<ReplyLength>(-1));
    });
    CHECK(usb.read_requests.empty());
    expect_throw<std::runtime_error>([&] { capt::read_reply(usb, 0xE0A5); });
    for (std::uint8_t total = 0; total < 6; ++total) {
        usb.offset = 0;
        usb.incoming[2] = total;
        expect_throw<std::runtime_error>([&] { capt::read_reply(usb, 0xA0A8); });
    }
    usb.incoming[2] = 7;
    usb.offset = 0;
    expect_throw<std::length_error>([&] { capt::read_reply(usb, 0xA0A8, 6); });
    CHECK(usb.offset == 6); // Rejected before reading a claimed oversized body.
    usb.incoming[2] = 0x6A;
    usb.offset = 0;
    expect_throw<std::runtime_error>([&] { capt::read_reply(usb, 0xA0A8, 100, ReplyLength::bcd); });
    usb.incoming[2] = 0x06;
    usb.incoming[3] = 0xA0;
    usb.offset = 0;
    expect_throw<std::runtime_error>([&] { capt::read_reply(usb, 0xA0A8, 100, ReplyLength::bcd); });

    for (const auto& truncated : {Bytes{0xA8, 0xA0, 6, 0, 0},
                                  Bytes{0xA8, 0xA0, 8, 0, 0, 0, 1}}) {
        MockTransport short_reply;
        short_reply.incoming = truncated;
        expect_throw<std::runtime_error>([&] { capt::read_reply(short_reply, 0xA0A8); });
    }
    for (auto failure : {MockTransport::Failure::zero_progress, MockTransport::Failure::overreport}) {
        MockTransport broken;
        broken.read_failure = broken.write_failure = failure;
        expect_throw<std::runtime_error>([&] { capt::write_packet(broken, {0xD0A1, {}}); });
        expect_throw<std::runtime_error>([&] { capt::read_reply(broken, 0xA0A8); });
    }
    MockTransport error;
    error.read_failure = error.write_failure = MockTransport::Failure::io_error;
    for (bool writing : {false, true}) {
        bool propagated = false;
        try {
            if (writing) capt::write_packet(error, {0xD0A1, {}});
            else capt::read_reply(error, 0xA0A8);
        } catch (const std::runtime_error& exception) {
            propagated = std::string(exception.what()) == "synthetic transport error";
        }
        CHECK(propagated);
    }
}

static void varied_fragmentation()
{
    // Deterministic varied payloads supplement fixed, independent byte fixtures.
    for (std::size_t size = 2; size <= 258; ++size) {
        Bytes payload(size);
        for (std::size_t i = 0; i < size; ++i)
            payload[i] = static_cast<std::uint8_t>((i * 37 + size) & 0xFFu);
        MockTransport usb;
        usb.incoming = capt::encode_packet({0xA0A8, payload});
        usb.read_chunk = size % 65 + 1;
        CHECK(capt::read_reply(usb, 0xA0A8).payload == payload);
        CHECK(usb.offset == usb.incoming.size());
    }
}

int main()
{
    try {
        wire_fixtures();
        reply_fixtures();
        rejection_and_failures();
        varied_fragmentation();
        std::cout << "Protocol fixtures, bounds, fragmentation and failures passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
