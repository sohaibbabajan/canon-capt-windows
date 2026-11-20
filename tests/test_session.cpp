// SPDX-License-Identifier: GPL-3.0-or-later
#include "capt/session.hpp"
#include "scripted_transport.hpp"
#include "support.hpp"

#include <iostream>
#include <limits>
#include <string>

using capt::Bytes;
using capt::Command;
using capt::Milliseconds;
using capt::SessionState;

namespace {
const capt::JobTimestamp timestamp{2026, 11, 16, 9, 8, 7};
const capt::PageParameters page{16, 4, 9, 2, 2, 3, 1, capt::PlainMedia::a4, 7, false};
const Bytes page_payload{
    0, 0, 0x30, 0x2A, 2, 0, 0, 0, 0x1C, 0x1C, 0x1C, 0x1C, 1, 0x11, 4, 0,
    1, 1, 2, 0, 0, 0, 1, 0, 3, 0, 2, 0, 2, 0, 16, 0, 4, 0, 0, 0, 1, 0, 0, 0};
const capt::RasterBand band{0, 2, 2, {0xAA, 0x80, 0x55, 0}};

capt::SessionOptions options()
{
    capt::SessionOptions result;
    result.startup_delay = Milliseconds(0);
    result.poll_interval = Milliseconds(2);
    result.transaction_timeout = Milliseconds(5);
    result.operation_timeout = Milliseconds(20);
    result.cancel_timeout = Milliseconds(5);
    return result;
}

void put(Bytes& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 255u);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

Bytes status(std::uint16_t decoding = 0, std::uint16_t received = 0,
             std::uint16_t out = 0, std::uint16_t completed = 0, std::uint16_t flags = 0)
{
    Bytes result(40, 0);
    put(result, 0, flags);
    put(result, 14, decoding);
    put(result, 18, out);
    put(result, 20, completed);
    put(result, 28, 0x1234);
    put(result, 34, received);
    return result;
}

Bytes job_payload(std::uint8_t flags, std::uint16_t page_number = 0)
{
    // Independent fixed timestamp/job fixture, with only flag and page varied.
    Bytes result{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  1, 1, 0x34, 0x12, 0xC4, 0xFF, 0x88, 0xFF,
                  0x7E, 0, 0x0A, 0x10, 9, 8, 7, 1};
    result.resize(72, 0);
    result[16] = flags;
    put(result, 4, page_number);
    return result;
}

void expect_status(ScriptedTransport& usb, const Bytes& payload = status())
{
    usb.exchange(0xA0A8, {}, payload);
}

void expect_start(ScriptedTransport& usb, std::uint16_t baseline = 0)
{
    usb.exchange(0xA1A1, {});
    expect_status(usb, status(baseline, baseline, baseline, baseline));
    usb.exchange(0xA3A2, {});
    usb.exchange(0xA2A0, {0, 0, 0x1E, 0, 0, 0, 0, 0}, {0, 0, 0x34, 0x12});
    Bytes gpio(16, 0); gpio[0] = 0x13;
    usb.exchange(0xE1A2, gpio);
    expect_status(usb);
    usb.exchange(0xE0BA, {1, 0});
    expect_status(usb);
    usb.exchange(0xE1A1, job_payload(1));
    expect_status(usb);
}

void expect_page_setup(ScriptedTransport& usb, const Bytes& page_bytes = page_payload)
{
    Bytes payload{0xA0, 0xD0, 44, 0};
    payload.insert(payload.end(), page_bytes.begin(), page_bytes.end());
    const Bytes tail{0xA4, 0xD0, 12, 0, 1, 4, 1, 1, 0, 0xF9, 0, 0,
                      0xA1, 0xD0, 4, 0, 0xA2, 0xD0, 4, 0};
    payload.insert(payload.end(), tail.begin(), tail.end());
    usb.write(0xD0A9, payload);
}

void expect_begin(ScriptedTransport& usb, std::uint16_t baseline = 0, bool uninitialized = false)
{
    expect_status(usb, status(baseline, baseline, baseline, baseline, uninitialized ? 0x30 : 0));
    if (uninitialized) {
        usb.exchange(0xE0A3, {});
        usb.exchange(0xE0A2, {});
        usb.exchange(0xE0A4, {});
        expect_status(usb);
        usb.exchange(0xE0A5, {0xEE, 0xDB, 0xEA, 0xAD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        expect_status(usb);
    }
    expect_status(usb, status(baseline, baseline, baseline, baseline));
    expect_page_setup(usb);
}

void expect_band(ScriptedTransport& usb, const capt::RasterBand& raster = band)
{
    const auto compressed = capt::hiscoa::compress_band(
        {raster.pixels.data(), raster.pixels.size(), raster.line_size, raster.rows});
    for (std::size_t offset = 0; offset < compressed.size();) {
        expect_status(usb);
        const auto count = std::min<std::size_t>(0xFF00, compressed.size() - offset);
        const auto first = compressed.begin() + static_cast<std::ptrdiff_t>(offset);
        usb.write(0xC0A0, Bytes(first, first + static_cast<std::ptrdiff_t>(count)));
        offset += count;
    }
}

void expect_end_page(ScriptedTransport& usb, std::uint16_t target = 1)
{
    usb.write(0xC0A4, {});
    expect_status(usb, status(target, target, 0, 0, 4));
    usb.exchange(0xE1A1, job_payload(2, target));
    // Buffer-full must not block the control commands that release the page.
    expect_status(usb, status(target, target, 0, 0, 4));
    usb.exchange(0xE0A7, {static_cast<std::uint8_t>(target & 255u), static_cast<std::uint8_t>(target >> 8)});
    expect_status(usb, status(target, target));
    usb.exchange(0xE1A1, job_payload(6, target));
    expect_status(usb, status(target, target, target));
}

void expect_finish(ScriptedTransport& usb, std::uint16_t target = 1)
{
    expect_status(usb, status(target, target, target, target));
    usb.exchange(0xE1A1, job_payload(4, target));
    usb.exchange(0xE0A9, {0x34, 0x12});
}

void expect_cancel(ScriptedTransport& usb, std::uint16_t completed = 0)
{
    expect_status(usb, status(0, 0, 0, completed));
    Bytes gpio(16, 0); gpio[0] = 0x13;
    usb.exchange(0xE1A2, gpio);
    usb.exchange(0xE1A1, job_payload(4, completed));
    usb.exchange(0xE0A9, {0x34, 0x12});
}

void serializers_and_policies()
{
    CHECK(capt::format_job_setup(timestamp, 0x1234, 0, 1) == job_payload(1));
    CHECK(capt::format_page_parameters(page) == page_payload);
    auto letter = page; letter.media = capt::PlainMedia::letter;
    auto expected_letter = page_payload; expected_letter[4] = 0x0D;
    CHECK(capt::format_page_parameters(letter) == expected_letter);
    // LBP6000 PPD + Ghostscript/CUPS at 600 dpi, mapped via reference paper.c.
    const capt::PageParameters a4{4738, 6793, 4738, 6793, 595, 13, 13, capt::PlainMedia::a4};
    const Bytes a4_payload{
        0, 0, 0x30, 0x2A, 2, 0, 0, 0, 0x1C, 0x1C, 0x1C, 0x1C, 1, 0x11, 4, 0,
        1, 1, 2, 0, 0, 0, 13, 0, 13, 0, 0x53, 0x02, 0x89, 0x1A, 0x82, 0x12,
        0x89, 0x1A, 0, 0, 1, 0, 0, 0};
    CHECK(capt::format_page_parameters(a4) == a4_payload);
    for (const auto& bad : {capt::JobTimestamp{1899, 1, 1}, capt::JobTimestamp{10000, 1, 1},
                            capt::JobTimestamp{2026, 0, 1}, capt::JobTimestamp{2026, 13, 1},
                            capt::JobTimestamp{2026, 2, 29}, capt::JobTimestamp{2100, 2, 29},
                            capt::JobTimestamp{2026, 4, 31}, capt::JobTimestamp{2026, 1, 0},
                            capt::JobTimestamp{2026, 1, 1, 24}, capt::JobTimestamp{2026, 1, 1, 0, 60},
                            capt::JobTimestamp{2026, 1, 1, 0, 0, 60}})
        expect_throw<std::invalid_argument>([&] { capt::format_job_setup(bad, 0, 0, 1); });
    CHECK(capt::format_job_setup({2000, 2, 29}, 0, 0, 1)[27] == 29);
    expect_throw<std::invalid_argument>([] { capt::format_job_setup(timestamp, 0, 0, 3); });
    for (auto command : {Command::print_data, Command::print_end, Command::page_parameters,
                         Command::page_init, Command::page_reset, Command::hiscoa_parameters,
                         Command::multi_parameters})
        CHECK(!capt::command_policy(command).reply);
    CHECK(capt::command_policy(Command::extended_status).min_reply_payload == 40);
    CHECK(capt::command_policy(Command::job_begin).min_reply_payload == 4);
    CHECK(capt::command_policy(Command::print_data).max_request == 0xFF00);
    expect_throw<std::invalid_argument>([] { capt::command_policy(static_cast<Command>(0xA1A0)); });
    expect_throw<std::invalid_argument>([] { capt::command_policy(static_cast<Command>(0xFFFF)); });
}

void successful_jobs()
{
    FakeRuntime clock;
    ScriptedTransport usb;
    usb.read_chunk = usb.write_chunk = 1;
    expect_start(usb);
    expect_begin(usb, 0, true);
    expect_band(usb);
    expect_end_page(usb);
    expect_begin(usb, 1);
    const capt::RasterBand first{0, 1, 2, {0xAA, 0x80}}, second{1, 1, 2, {0x55, 0}};
    expect_band(usb, first); expect_band(usb, second);
    expect_end_page(usb, 2);
    expect_finish(usb, 2);
    // Reuse only after an explicitly completed job, with no retained global state.
    expect_start(usb, 2); expect_cancel(usb, 2);
    capt::Cancellation token;
    capt::Lbp6000Session session(usb, clock, options());
    session.start_job(timestamp, token);
    CHECK(session.state() == SessionState::job_open);
    session.begin_page(page, token); session.send_band(band, token); session.end_page(token);
    session.begin_page(page, token);
    session.send_band(first, token); session.send_band(second, token); session.end_page(token);
    session.finish_job(token);
    CHECK(session.state() == SessionState::idle);
    session.start_job(timestamp, token); session.cancel_job();
    CHECK(session.state() == SessionState::idle && usb.done());
}

void state_and_input_guards()
{
    FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
    expect_start(usb); expect_begin(usb); expect_band(usb); expect_cancel(usb);
    capt::Lbp6000Session session(usb, clock, options());
    expect_throw<std::logic_error>([&] { session.begin_page(page, token); });
    expect_throw<std::logic_error>([&] { session.end_page(token); });
    CHECK(usb.write_calls == 0);
    session.start_job(timestamp, token);
    auto invalid = page; invalid.line_size = 1;
    const auto before = usb.write_calls;
    expect_throw<std::invalid_argument>([&] { session.begin_page(invalid, token); });
    CHECK(session.state() == SessionState::job_open && usb.write_calls == before);
    for (unsigned field = 0; field < 7; ++field) {
        invalid = page;
        switch (field) {
        case 0: invalid.paper_width = 0; break;
        case 1: invalid.raster_width = 17; break;
        case 2: invalid.margin_top = 4; break;
        case 3: invalid.raster_rows = 0; break;
        case 4: invalid.margin_left = 16; break;
        case 5: invalid.density = 16; break;
        case 6: invalid.media = static_cast<capt::PlainMedia>(3); break;
        }
        expect_throw<std::invalid_argument>([&] { session.begin_page(invalid, token); });
    }
    session.begin_page(page, token);
    const auto before_band = usb.write_calls;
    expect_throw<std::logic_error>([&] { session.end_page(token); });
    for (const auto& bad : {capt::RasterBand{1, 1, 2, {0, 0}}, capt::RasterBand{0, 0, 2, {}},
                            capt::RasterBand{0, 3, 2, Bytes(6)}, capt::RasterBand{0, 2, 1, {0, 0}},
                            capt::RasterBand{0, 2, 2, {0, 0, 0}}})
        expect_throw<std::invalid_argument>([&] { session.send_band(bad, token); });
    CHECK(usb.write_calls == before_band && session.state() == SessionState::page_open);
    session.send_band(band, token);
    const auto after_band = usb.write_calls;
    expect_throw<std::invalid_argument>([&] { session.send_band(band, token); });
    CHECK(usb.write_calls == after_band); // No duplicate band transmission.
    session.cancel_job();
    CHECK(usb.done());
}

void flow_control_and_stale_counters()
{
    FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
    expect_start(usb);
    expect_status(usb);
    expect_status(usb, status(0, 0, 0, 0, 4)); // Full before page setup.
    expect_status(usb, status(0, 0, 0, 0, 0x80)); // Busy after full clears.
    expect_status(usb);
    expect_page_setup(usb);
    expect_status(usb, status(0, 0, 0, 0, 4)); // Full before first band.
    expect_band(usb);
    usb.write(0xC0A4, {});
    expect_status(usb); expect_status(usb); // Stale equal zero counters must not fire.
    expect_status(usb, status(1, 1));
    usb.exchange(0xE1A1, job_payload(2, 1)); expect_status(usb, status(1, 1));
    usb.exchange(0xE0A7, {1, 0}); expect_status(usb, status(1, 1));
    usb.exchange(0xE1A1, job_payload(6, 1));
    expect_status(usb, status(1, 1)); expect_status(usb, status(1, 1, 1));
    expect_status(usb, status(1, 1, 1, 0)); // Page out is not completed-job evidence.
    expect_finish(usb);
    capt::Lbp6000Session session(usb, clock, options());
    session.start_job(timestamp, token); session.begin_page(page, token);
    session.send_band(band, token); session.end_page(token); session.finish_job(token);
    CHECK(usb.done() && clock.wakes.size() == 8);
}

void paper_out_and_cleanup()
{
    FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
    expect_start(usb); expect_status(usb); expect_status(usb, status(0, 0, 0, 0, 2));
    expect_page_setup(usb);
    expect_cancel(usb, 5);
    capt::Lbp6000Session session(usb, clock, options());
    session.start_job(timestamp, token);
    session.begin_page(page, token);
    CHECK(session.state() == SessionState::page_open);
    token.request(); // Cleanup has its own bounded token.
    session.cancel_job();
    CHECK(session.state() == SessionState::idle && usb.done());
}

void operation_timeout_and_safe_cancel()
{
    FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
    expect_start(usb); expect_status(usb);
    for (unsigned i = 0; i < 4; ++i) expect_status(usb, status(0, 0, 0, 0, 4));
    expect_cancel(usb);
    auto config = options(); config.operation_timeout = Milliseconds(7);
    capt::Lbp6000Session session(usb, clock, config);
    session.start_job(timestamp, token);
    expect_throw<capt::DeadlineExceeded>([&] { session.begin_page(page, token); });
    CHECK(clock.current == capt::Instant{} + Milliseconds(7));
    CHECK(session.state() == SessionState::faulted);
    session.cancel_job();
    CHECK(usb.done());
}

void transaction_timeout_and_lost_framing()
{
    for (unsigned failure = 0; failure < 4; ++failure) {
        FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
        usb.exchange(0xA1A1, {});
        usb.before_read = [&](const capt::IoContext& context) {
            CHECK(context.deadline == capt::Instant{} + Milliseconds(5));
            if (failure == 0) { clock.current = context.deadline; context.check(); }
            if (failure == 1) token.request();
            if (failure == 2) throw std::runtime_error("synthetic disconnect");
            if (failure == 3) clock.current = context.deadline + Milliseconds(1); // Late adapter return.
        };
        capt::Lbp6000Session session(usb, clock, options());
        if (failure == 1) expect_throw<capt::Cancelled>([&] { session.start_job(timestamp, token); });
        else if (failure == 2) expect_throw<std::runtime_error>([&] { session.start_job(timestamp, token); });
        else expect_throw<capt::DeadlineExceeded>([&] { session.start_job(timestamp, token); });
        CHECK(session.state() == SessionState::faulted);
        const auto calls = usb.read_calls + usb.write_calls;
        expect_throw<std::logic_error>([&] { session.cancel_job(); });
        expect_throw<std::logic_error>([&] { session.start_job(timestamp, token); });
        CHECK(usb.read_calls + usb.write_calls == calls);
    }

    // The same deadline spans all partial writes and reply reads; progress
    // must not restart the transaction timeout.
    FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
    usb.exchange(0xA1A1, {}); usb.write_chunk = 1;
    usb.after_write = [&](const capt::IoContext&) { clock.current += Milliseconds(1); };
    usb.before_read = [&](const capt::IoContext&) { clock.current += Milliseconds(1); };
    capt::Lbp6000Session session(usb, clock, options());
    expect_throw<capt::DeadlineExceeded>([&] { session.start_job(timestamp, token); });
    CHECK(clock.current == capt::Instant{} + Milliseconds(5));
    for (const auto deadline : usb.deadlines)
        CHECK(deadline == capt::Instant{} + Milliseconds(5));
}

void cancellation_boundaries()
{
    FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
    capt::Lbp6000Session session(usb, clock, options());
    token.request();
    expect_throw<capt::Cancelled>([&] { session.start_job(timestamp, token); });
    CHECK(session.state() == SessionState::idle && usb.write_calls == 0);
    capt::Cancellation next;
    expect_start(usb); expect_status(usb); expect_status(usb, status(0, 0, 0, 0, 4)); expect_cancel(usb);
    session.start_job(timestamp, next);
    clock.on_wait = [&] { next.request(); };
    expect_throw<capt::Cancelled>([&] { session.begin_page(page, next); });
    CHECK(session.state() == SessionState::faulted);
    clock.on_wait = {};
    session.cancel_job();
    CHECK(usb.done());

    // Cancellation halfway through a packet must not trigger cleanup or replay.
    FakeRuntime partial_clock; ScriptedTransport partial; capt::Cancellation partial_token;
    expect_start(partial); expect_begin(partial); expect_band(partial);
    capt::Lbp6000Session partial_session(partial, partial_clock, options());
    partial_session.start_job(timestamp, partial_token); partial_session.begin_page(page, partial_token);
    partial.write_chunk = 1;
    bool status_read = false;
    partial.before_read = [&](const capt::IoContext&) { status_read = true; };
    partial.after_write = [&](const capt::IoContext&) { if (status_read) partial_token.request(); };
    expect_throw<capt::Cancelled>([&] { partial_session.send_band(band, partial_token); });
    const auto calls = partial.write_calls;
    expect_throw<std::logic_error>([&] { partial_session.cancel_job(); });
    expect_throw<std::logic_error>([&] { partial_session.send_band(band, partial_token); });
    CHECK(partial.write_calls == calls);
}

void cancellation_after_fire()
{
    FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
    expect_start(usb); expect_begin(usb); expect_band(usb);
    usb.write(0xC0A4, {}); expect_status(usb, status(1, 1));
    usb.exchange(0xE1A1, job_payload(2, 1)); expect_status(usb);
    usb.exchange(0xE0A7, {1, 0}); expect_status(usb, status(1, 1, 0, 0, 0x80));
    expect_cancel(usb);
    capt::Lbp6000Session session(usb, clock, options());
    session.start_job(timestamp, token); session.begin_page(page, token); session.send_band(band, token);
    unsigned waits = 0;
    clock.on_wait = [&] { if (++waits == 2) token.request(); };
    expect_throw<capt::Cancelled>([&] { session.end_page(token); });
    CHECK(session.state() == SessionState::faulted);
    const auto count = usb.write_calls;
    expect_throw<std::logic_error>([&] { session.end_page(token); });
    CHECK(usb.write_calls == count);
    clock.on_wait = {};
    session.cancel_job();
    CHECK(usb.done());
    CHECK(std::count(usb.commands.begin(), usb.commands.end(), 0xC0A0) == 1);
    CHECK(std::count(usb.commands.begin(), usb.commands.end(), 0xE0A7) == 1);
}

void paper_out_after_fire()
{
    FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
    expect_start(usb); expect_begin(usb); expect_band(usb);
    usb.write(0xC0A4, {}); expect_status(usb, status(1, 1));
    usb.exchange(0xE1A1, job_payload(2, 1)); expect_status(usb);
    usb.exchange(0xE0A7, {1, 0}); expect_status(usb);
    usb.exchange(0xE1A1, job_payload(6, 1));
    auto feeding = status(1, 1, 0, 0, 3);
    put(feeding, 8, 0x0080);
    expect_status(usb, feeding); // Feed processing is still active.
    expect_status(usb, status(1, 1, 0, 0, 3)); // Feed processing stopped without page out.
    expect_cancel(usb);
    capt::Lbp6000Session session(usb, clock, options());
    session.start_job(timestamp, token); session.begin_page(page, token); session.send_band(band, token);
    expect_throw<capt::PaperOut>([&] { session.end_page(token); });
    CHECK(session.state() == SessionState::faulted);
    session.cancel_job();
    CHECK(usb.done());
    CHECK(std::count(usb.commands.begin(), usb.commands.end(), 0xC0A0) == 1);
    CHECK(std::count(usb.commands.begin(), usb.commands.end(), 0xE0A7) == 1);
}

void chunked_data_and_default_startup()
{
    FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
    expect_start(usb); expect_status(usb); expect_status(usb);
    const capt::PageParameters wide{8000, 50, 8000, 50, 1000, 0, 0};
    auto wide_payload = page_payload;
    put(wide_payload, 22, 0); put(wide_payload, 24, 0); put(wide_payload, 26, 1000);
    put(wide_payload, 28, 50); put(wide_payload, 30, 8000); put(wide_payload, 32, 50);
    expect_page_setup(usb, wide_payload);
    capt::RasterBand large{0, 50, 1000, Bytes(50000)};
    std::uint32_t random = 0x60001234;
    for (auto& pixel : large.pixels) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        pixel = static_cast<std::uint8_t>(random & 255u);
    }
    expect_band(usb, large); expect_cancel(usb);
    capt::Lbp6000Session session(usb, clock); // Actual default 1-second startup delay, on fake time.
    session.start_job(timestamp, token); session.begin_page(wide, token); session.send_band(large, token);
    session.cancel_job();
    CHECK(usb.done() && clock.wakes.front() == capt::Instant{} + Milliseconds(1000));
    CHECK(std::count(usb.commands.begin(), usb.commands.end(), 0xC0A0) > 1);
    bool saw_full_chunk = false;
    for (const auto& step : usb.steps) {
        if (step.request[0] == 0xA0 && step.request[1] == 0xC0) {
            CHECK(step.request.size() - 4 <= 0xFF00);
            if (!saw_full_chunk) {
                CHECK(step.request.size() - 4 == 0xFF00);
                CHECK(step.request[2] == 4 && step.request[3] == 0xFF);
                saw_full_chunk = true;
            }
        }
    }
    CHECK(saw_full_chunk);
}

void counter_wrap_and_cleanup_timeout()
{
    FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
    expect_start(usb, 0xFFFF); expect_begin(usb, 0xFFFF); expect_band(usb);
    expect_end_page(usb, 0); expect_finish(usb, 0);
    expect_start(usb);
    usb.exchange(0xA0A8, {}, status());
    capt::Lbp6000Session session(usb, clock, options());
    session.start_job(timestamp, token); session.begin_page(page, token); session.send_band(band, token);
    session.end_page(token); session.finish_job(token); session.start_job(timestamp, token);
    const auto cleanup_start = clock.current;
    usb.before_read = [&](const capt::IoContext& context) {
        CHECK(context.deadline == cleanup_start + Milliseconds(5));
        clock.current = context.deadline;
        context.check();
    };
    expect_throw<capt::DeadlineExceeded>([&] { session.cancel_job(); });
    CHECK(session.state() == SessionState::faulted);
    const auto count = usb.write_calls;
    expect_throw<std::logic_error>([&] { session.cancel_job(); });
    CHECK(usb.write_calls == count);
}

void malformed_replies()
{
    for (unsigned failure = 0; failure < 4; ++failure) {
        FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
        usb.exchange(0xA1A1, {});
        if (failure == 0) usb.exchange(0xA0A8, {}, Bytes(39));
        if (failure == 1) usb.raw(0xA0A8, {}, {0xA8, 0xA0, 45, 0, 0, 0});
        if (failure == 2) usb.raw(0xA0A8, {}, {0xA1, 0xA0, 6, 0, 0, 0});
        if (failure == 3) {
            expect_status(usb); usb.exchange(0xA3A2, {});
            usb.exchange(0xA2A0, {0, 0, 0x1E, 0, 0, 0, 0, 0}); // Job number absent.
        }
        auto config = options(); config.max_reply_size = 44;
        capt::Lbp6000Session session(usb, clock, config);
        expect_throw<std::exception>([&] { session.start_job(timestamp, token); });
        CHECK(session.state() == SessionState::faulted);
        CHECK(std::count(usb.commands.begin(), usb.commands.end(), 0xE0BA) == 0);
    }
}

void quirks_reentrancy_and_clock_guards()
{
    FakeRuntime clock; ScriptedTransport usb; capt::Cancellation token;
    usb.bcd_status = true;
    expect_start(usb); expect_cancel(usb);
    auto config = options(); config.reply_quirks = {{Command::extended_status, capt::ReplyLength::bcd}};
    capt::Lbp6000Session session(usb, clock, config);
    bool attempted = false;
    usb.after_write = [&](const capt::IoContext&) {
        if (!attempted) {
            attempted = true;
            expect_throw<std::logic_error>([&] { session.start_job(timestamp, token); });
        }
    };
    session.start_job(timestamp, token); session.cancel_job();
    CHECK(attempted && usb.done());
    for (unsigned bad = 0; bad < 6; ++bad) {
        config = options();
        if (bad == 0) config.poll_interval = Milliseconds(0);
        if (bad == 1) config.operation_timeout = Milliseconds(-1);
        if (bad == 2) config.max_reply_size = 43;
        if (bad == 3) config.reply_quirks = {{Command::print_end, capt::ReplyLength::bcd}};
        if (bad == 4) config.reply_quirks = {{Command::identify, capt::ReplyLength::bcd}, {Command::identify, capt::ReplyLength::binary}};
        if (bad == 5) config.reply_quirks = {{static_cast<Command>(0xFFFF), capt::ReplyLength::binary}};
        expect_throw<std::invalid_argument>([&] { capt::Lbp6000Session invalid(usb, clock, config); });
    }
    expect_throw<std::overflow_error>([] { capt::deadline_after(capt::Instant::max(), Milliseconds(1)); });
    expect_throw<std::invalid_argument>([] { capt::deadline_after(capt::Instant{}, std::chrono::hours(25)); });
    FakeRuntime early; early.early_return = true;
    const capt::IoContext context{early, token, capt::Instant{} + Milliseconds(5)};
    expect_throw<std::runtime_error>([&] { context.pause(Milliseconds(1)); });
}
} // namespace

int main()
{
    try {
        serializers_and_policies(); successful_jobs(); state_and_input_guards();
        flow_control_and_stale_counters(); paper_out_and_cleanup(); operation_timeout_and_safe_cancel();
        transaction_timeout_and_lost_framing(); cancellation_boundaries(); cancellation_after_fire(); paper_out_after_fire();
        chunked_data_and_default_startup(); counter_wrap_and_cleanup_timeout(); malformed_replies();
        quirks_reentrancy_and_clock_guards();
        std::cout << "Session scripts, states, deadlines, cancellation, flow control and cleanup passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
