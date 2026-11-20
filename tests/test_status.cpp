// SPDX-License-Identifier: GPL-3.0-or-later
#include "capt/status.hpp"
#include "support.hpp"
#include "mock_transport.hpp"
#include "fixtures/canon_271a_empty_tray.hpp"

#include <iostream>

int main()
{
    try {
        MockTransport usb;
        usb.incoming = canon_271a_empty_tray_reply;
        usb.read_chunk = 64;
        capt::write_packet(usb, {0xA0A8, {}});
        CHECK((usb.outgoing == capt::Bytes{0xA8, 0xA0, 0x04, 0x00}));
        const auto captured = capt::read_reply(usb, 0xA0A8, 4096);
        CHECK(usb.offset == 92 && captured.payload.size() == 88);
        CHECK((usb.read_requests == std::vector<std::size_t>{6, 86, 22}));
        const auto empty_tray = capt::decode_extended_status(captured.payload);
        CHECK(empty_tray.words[0] == 0x8E33 && empty_tray.words[1] == 0);
        CHECK(empty_tray.paper_out() && empty_tray.processing() && empty_tray.uninitialized());
        CHECK(!empty_tray.printing() && !empty_tray.busy() && !empty_tray.buffer_full());
        CHECK(empty_tray.page_decoding == 0 && empty_tray.page_received == 0);
        CHECK(empty_tray.page_out == 0 && empty_tray.page_completed == 0);
        const capt::Bytes payload{
            0x34, 0x12, 0, 0, 0, 0, 0, 0,
            0x78, 0x56, 0xBC, 0x9A, 0xF0, 0xDE,
            1, 0, 2, 0, 3, 0, 4, 0, 0, 0,
            0x22, 0x11, 0, 0, 0x34, 0x12, 0x44, 0x33,
            0, 0, 5, 0, 0, 0, 0x66, 0x55};
        const auto status = capt::decode_extended_status(payload);
        CHECK((status.words == std::array<std::uint16_t, 7>{0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x1122, 0x3344, 0x5566}));
        CHECK(status.page_decoding == 1 && status.page_printing == 2 && status.page_out == 3);
        CHECK(status.page_completed == 4 && status.page_received == 5 && status.job == 0x1234);
        CHECK(!status.pages_in_job && !status.pages_printed && !status.event_counter);
        for (std::size_t size = 0; size < 40; ++size)
            expect_throw<std::runtime_error>([&] { capt::decode_extended_status(capt::Bytes(size)); });
        for (std::size_t size = 40; size <= 84; ++size) {
            auto extended = payload;
            extended.resize(size, 0xAB);
            const auto result = capt::decode_extended_status(extended);
            CHECK(result.words == status.words);
            CHECK(result.pages_in_job.has_value() == (size >= 42));
            CHECK(result.pages_printed.has_value() == (size >= 46));
            CHECK(result.event_counter.has_value() == (size >= 66));
            if (result.pages_in_job) CHECK(*result.pages_in_job == 0xABAB);
            if (result.pages_printed) CHECK(*result.pages_printed == 0xABAB);
            if (result.event_counter) CHECK(*result.event_counter == 0xABAB);
        }
        capt::ExtendedStatus flags;
        CHECK(!flags.busy() && !flags.buffer_full() && !flags.paper_out() && !flags.uninitialized());
        flags.words[0] = 0x00B7;
        CHECK(flags.busy() && flags.buffer_full() && flags.paper_out() && flags.uninitialized() && flags.processing());
        flags.words[0] = 0x10;
        CHECK(flags.uninitialized());
        flags.words[0] = 0x1000; // Reference's unrelated READY2 bit.
        CHECK(!flags.uninitialized());
        flags.words[0] = 0;
        flags.words[1] = 0x4084;
        CHECK(flags.paper_out() && flags.processing() && flags.printing());
        CHECK(!flags.busy() && !flags.buffer_full());
        std::cout << "Status fields, flags and all truncated/optional field boundaries passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
