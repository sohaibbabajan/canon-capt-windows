// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2013 Alexey Galakhov <agalakhov@gmail.com>
// Copyright (C) 2016 Alexei Gordeev <KP1533TM2@gmail.com>
// Modified 2026-11-16: portable C++ session adapted from captdriver's model
// operations, with deadlines, cancellation, validated state and no page replay.
// Reference provenance, deliberate differences and limitations: docs/SESSION.md.
#include "capt/session.hpp"

#include <algorithm>

namespace capt {
namespace {
class ActiveGuard {
public:
    explicit ActiveGuard(std::atomic_flag& flag) : flag_(flag)
    {
        if (flag_.test_and_set())
            throw std::logic_error("CAPT session operation already in progress");
    }
    ~ActiveGuard() { flag_.clear(); }
    ActiveGuard(const ActiveGuard&) = delete;
    ActiveGuard& operator=(const ActiveGuard&) = delete;
private:
    std::atomic_flag& flag_;
};

class CheckedTransport final : public Transport {
public:
    CheckedTransport(SessionTransport& transport, const IoContext& context, bool& synchronized)
        : transport_(transport), context_(context), synchronized_(synchronized) {}
    std::size_t write_some(const std::uint8_t* data, std::size_t size) override
    {
        context_.check();
        synchronized_ = false;
        const auto count = transport_.write_some(data, size, context_);
        context_.check();
        return count;
    }
    std::size_t read_some(std::uint8_t* data, std::size_t size) override
    {
        context_.check();
        synchronized_ = false;
        const auto count = transport_.read_some(data, size, context_);
        context_.check();
        return count;
    }
private:
    SessionTransport& transport_;
    const IoContext& context_;
    bool& synchronized_;
};

void put_word(Bytes& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::uint16_t word(const Bytes& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(static_cast<unsigned>(bytes[offset]) |
                                      (static_cast<unsigned>(bytes[offset + 1]) << 8));
}

Bytes gpio_init()
{
    Bytes payload(16, 0);
    payload[0] = 0x13;
    return payload;
}

void append_command(Bytes& bytes, Command command, const Bytes& payload)
{
    const auto frame = encode_packet({static_cast<std::uint16_t>(command), payload});
    bytes.insert(bytes.end(), frame.begin(), frame.end());
}
} // namespace

CommandPolicy command_policy(Command command)
{
    switch (command) {
    case Command::extended_status: return {true, 0, 0, 40};
    case Command::identify:
    case Command::start:
    case Command::start_1:
    case Command::start_2:
    case Command::start_3: return {true, 0, 0, 2};
    case Command::job_begin: return {true, 8, 8, 4};
    case Command::lbp6000_setup:
    case Command::fire:
    case Command::job_end: return {true, 2, 2, 2};
    case Command::upload:
    case Command::gpio: return {true, 16, 16, 2};
    case Command::job_setup: return {true, 72, 72, 2};
    case Command::print_data: return {false, 1, 0xFF00, 0};
    case Command::print_end:
    case Command::page_init:
    case Command::page_reset: return {false, 0, 0, 0};
    case Command::page_parameters: return {false, 40, 40, 0};
    case Command::hiscoa_parameters: return {false, 8, 8, 0};
    case Command::multi_parameters: return {false, 64, 64, 0};
    }
    throw std::invalid_argument("No policy for this CAPT command");
}

Bytes format_job_setup(const JobTimestamp& time, std::uint16_t job,
                       std::uint16_t page, std::uint8_t flags)
{
    constexpr unsigned month_days[]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (time.year < 1900 || time.year > 9999 || time.month < 1 || time.month > 12 ||
        time.hour > 23 || time.minute > 59 || time.second > 59)
        throw std::invalid_argument("Invalid CAPT job timestamp");
    auto days = month_days[time.month - 1];
    if (time.month == 2 && time.year % 4 == 0 && (time.year % 100 != 0 || time.year % 400 == 0))
        ++days;
    if (time.day < 1 || time.day > days || (flags != 1 && flags != 2 && flags != 4 && flags != 6))
        throw std::invalid_argument("Invalid CAPT job day or setup flags");
    Bytes payload(72, 0);
    put_word(payload, 4, page);
    payload[16] = flags;
    payload[17] = 1;
    put_word(payload, 18, job);
    // Opaque constants retained from the reference, not inferred local timezone.
    put_word(payload, 20, 0xFFC4);
    put_word(payload, 22, 0xFF88);
    put_word(payload, 24, static_cast<std::uint16_t>(time.year - 1900));
    payload[26] = static_cast<std::uint8_t>(time.month - 1);
    payload[27] = static_cast<std::uint8_t>(time.day);
    payload[28] = static_cast<std::uint8_t>(time.hour);
    payload[29] = static_cast<std::uint8_t>(time.minute);
    payload[30] = static_cast<std::uint8_t>(time.second);
    payload[31] = 1;
    return payload;
}

Bytes format_page_parameters(const PageParameters& page)
{
    if (page.paper_width == 0 || page.paper_height == 0 || page.raster_width == 0 || page.raster_rows == 0 ||
        page.margin_left >= page.paper_width || page.raster_width > page.paper_width ||
        page.margin_top >= page.paper_height || page.raster_rows > page.paper_height ||
        page.line_size < (static_cast<unsigned>(page.raster_width) + 7) / 8 || page.density > 15 ||
        (page.media != PlainMedia::a4 && page.media != PlainMedia::letter))
        throw std::invalid_argument("Invalid CAPT plain-page geometry or settings");
    Bytes payload{
        0, 0, 0x30, 0x2A, static_cast<std::uint8_t>(page.media), 0, 0, 0,
        static_cast<std::uint8_t>(static_cast<unsigned>(page.density) << 2), 0x1C, 0x1C, 0x1C,
        1, 0x11, 4, 0, 1, 1, 2, static_cast<std::uint8_t>(page.toner_save ? 1 : 0), 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0};
    put_word(payload, 22, page.margin_top);
    put_word(payload, 24, page.margin_left);
    put_word(payload, 26, page.line_size);
    put_word(payload, 28, page.raster_rows);
    put_word(payload, 30, page.paper_width);
    put_word(payload, 32, page.paper_height);
    return payload;
}

Lbp6000Session::Lbp6000Session(SessionTransport& transport, Runtime& runtime, SessionOptions options)
    : transport_(transport), runtime_(runtime), options_(std::move(options))
{
    for (const auto delay : {options_.transaction_timeout, options_.operation_timeout,
                             options_.cancel_timeout, options_.poll_interval})
        (void) deadline_after(Instant{}, delay);
    if (options_.startup_delay.count() < 0 || options_.startup_delay > std::chrono::hours(24))
        throw std::invalid_argument("Invalid CAPT startup delay");
    if (options_.max_reply_size < 44 || options_.max_reply_size > max_packet_size)
        throw std::invalid_argument("CAPT reply cap must be in [44,65535]");
    (void) hiscoa::format_parameters(options_.compression);
    std::vector<Command> seen;
    for (const auto& quirk : options_.reply_quirks) {
        if (!command_policy(quirk.first).reply ||
            (quirk.second != ReplyLength::binary && quirk.second != ReplyLength::bcd) ||
            std::find(seen.begin(), seen.end(), quirk.first) != seen.end())
            throw std::invalid_argument("Invalid or duplicate CAPT reply quirk");
        seen.push_back(quirk.first);
    }
}

void Lbp6000Session::perform(SessionState expected, SessionState active, SessionState success,
                            const Cancellation& cancellation, const Work& preflight, const Work& action)
{
    ActiveGuard guard(active_);
    if (state() != expected)
        throw std::logic_error("CAPT operation is invalid in the current session state");
    const IoContext operation{runtime_, cancellation, deadline_after(runtime_.now(), options_.operation_timeout)};
    operation.check();
    preflight(operation); // Invalid input/cancellation here leaves the prior state intact.
    operation.check();
    state_.store(active);
    try {
        action(operation);
        operation.check();
        state_.store(success);
    } catch (...) {
        state_.store(SessionState::faulted);
        throw;
    }
}

Bytes Lbp6000Session::execute(Command command, const Bytes& payload, const IoContext& operation)
{
    const auto policy = command_policy(command);
    if (payload.size() < policy.min_request || payload.size() > policy.max_request)
        throw std::invalid_argument("CAPT command payload violates its policy");
    if (!synchronized_)
        throw std::logic_error("CAPT transport framing was lost; reopen and recover the device");
    operation.check();
    const IoContext context{runtime_, operation.cancellation,
        std::min(operation.deadline, deadline_after(runtime_.now(), options_.transaction_timeout))};
    CheckedTransport checked(transport_, context, synchronized_);
    write_packet(checked, {static_cast<std::uint16_t>(command), payload});
    Bytes reply;
    if (policy.reply) {
        auto encoding = ReplyLength::binary;
        for (const auto& quirk : options_.reply_quirks) {
            if (quirk.first == command)
                encoding = quirk.second;
        }
        reply = read_reply(checked, static_cast<std::uint16_t>(command), options_.max_reply_size, encoding).payload;
    }
    synchronized_ = true;
    if (reply.size() < policy.min_reply_payload)
        throw std::runtime_error("CAPT reply payload is shorter than the command requires");
    return reply;
}

ExtendedStatus Lbp6000Session::poll(const IoContext& operation)
{
    return decode_extended_status(execute(Command::extended_status, {}, operation));
}

ExtendedStatus Lbp6000Session::wait_for(const IoContext& operation,
                                      const std::function<bool(const ExtendedStatus&)>& predicate)
{
    for (;;) {
        const auto status = poll(operation);
        if (predicate(status))
            return status;
        operation.pause(options_.poll_interval);
    }
}

ExtendedStatus Lbp6000Session::ready(const IoContext& operation)
{
    return wait_for(operation, [](const ExtendedStatus& status) { return !status.busy(); });
}

ExtendedStatus Lbp6000Session::writable(const IoContext& operation)
{
    return wait_for(operation, [](const ExtendedStatus& status) {
        return !status.busy() && !status.buffer_full();
    });
}

void Lbp6000Session::setup(std::uint8_t flags, std::uint16_t page, const IoContext& operation)
{
    (void) execute(Command::job_setup, format_job_setup(time_, *job_, page, flags), operation);
}

void Lbp6000Session::start_job(const JobTimestamp& time, const Cancellation& cancellation)
{
    perform(SessionState::idle, SessionState::starting_job, SessionState::job_open, cancellation,
        [&](const IoContext&) { (void) format_job_setup(time, 0, 0, 1); },
        [&](const IoContext& operation) {
            time_ = time;
            (void) execute(Command::identify, {}, operation);
            if (options_.startup_delay.count() != 0)
                operation.pause(options_.startup_delay);
            last_page_ = poll(operation).page_decoding;
            (void) execute(Command::start, {}, operation);
            const auto reply = execute(Command::job_begin, {0, 0, 0x1E, 0, 0, 0, 0, 0}, operation);
            job_ = word(reply, 2);
            (void) execute(Command::gpio, gpio_init(), operation);
            (void) ready(operation);
            (void) execute(Command::lbp6000_setup, {1, 0}, operation);
            (void) ready(operation);
            setup(1, 0, operation);
            (void) ready(operation);
        });
}

void Lbp6000Session::begin_page(const PageParameters& page, const Cancellation& cancellation)
{
    Bytes parameters;
    perform(SessionState::job_open, SessionState::starting_page, SessionState::page_open, cancellation,
        [&](const IoContext&) {
            append_command(parameters, Command::page_parameters, format_page_parameters(page));
            const auto compression = hiscoa::format_parameters(options_.compression);
            append_command(parameters, Command::hiscoa_parameters, Bytes(compression.begin(), compression.end()));
            append_command(parameters, Command::page_init, {});
            append_command(parameters, Command::page_reset, {});
        },
        [&](const IoContext& operation) {
            const auto status = poll(operation);
            if (status.uninitialized()) {
                (void) execute(Command::start_1, {}, operation);
                (void) execute(Command::start_2, {}, operation);
                (void) execute(Command::start_3, {}, operation);
                (void) ready(operation);
                (void) execute(Command::upload,
                    {0xEE, 0xDB, 0xEA, 0xAD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, operation);
                (void) ready(operation);
            }
            previous_decoding_ = writable(operation).page_decoding;
            (void) execute(Command::multi_parameters, parameters, operation);
            page_ = page;
            rows_sent_ = 0;
        });
}

void Lbp6000Session::send_band(const RasterBand& band, const Cancellation& cancellation)
{
    Bytes compressed;
    perform(SessionState::page_open, SessionState::sending_band, SessionState::page_open, cancellation,
        [&](const IoContext&) {
            if (band.first_row != rows_sent_ || band.rows == 0 || band.rows > page_.raster_rows - rows_sent_ ||
                band.line_size != page_.line_size)
                throw std::invalid_argument("CAPT band is out of order or exceeds the page");
            compressed = hiscoa::compress_band({band.pixels.data(), band.pixels.size(), band.line_size, band.rows},
                                               hiscoa::EndMarker::band, options_.compression);
        },
        [&](const IoContext& operation) {
            std::size_t offset = 0;
            while (offset < compressed.size()) {
                (void) writable(operation);
                const auto size = std::min<std::size_t>(0xFF00, compressed.size() - offset);
                const auto first = compressed.begin() + static_cast<std::ptrdiff_t>(offset);
                (void) execute(Command::print_data, Bytes(first, first + static_cast<std::ptrdiff_t>(size)), operation);
                offset += size;
            }
            rows_sent_ += band.rows;
        });
}

void Lbp6000Session::end_page(const Cancellation& cancellation)
{
    perform(SessionState::page_open, SessionState::awaiting_page, SessionState::job_open, cancellation,
        [&](const IoContext&) {
            if (rows_sent_ != page_.raster_rows)
                throw std::logic_error("CAPT page has not received all raster rows");
        },
        [&](const IoContext& operation) {
            (void) execute(Command::print_end, {}, operation);
            operation.pause(options_.poll_interval);
            const auto received = wait_for(operation, [&](const ExtendedStatus& status) {
                return !status.busy() && status.page_received == status.page_decoding &&
                       status.page_decoding != previous_decoding_;
            });
            const auto target = received.page_decoding;
            setup(2, target, operation);
            (void) ready(operation);
            Bytes page_number(2);
            put_word(page_number, 0, target);
            (void) execute(Command::fire, page_number, operation);
            (void) ready(operation);
            setup(6, target, operation);
            (void) wait_for(operation, [=](const ExtendedStatus& status) {
                if (!status.busy() && status.page_out == target) return true;
                // The reference checks feed failure after FIRE. PROCESSING1
                // is word 1 bit 7, distinct from the idle word 0 bit.
                if (status.paper_out() && !status.printing() && !(status.words[1] & 0x0080u))
                    throw PaperOut();
                return false;
            });
            last_page_ = target;
        });
}

void Lbp6000Session::finish_job(const Cancellation& cancellation)
{
    perform(SessionState::job_open, SessionState::finishing_job, SessionState::idle, cancellation,
        [](const IoContext&) {},
        [&](const IoContext& operation) {
            (void) wait_for(operation, [&](const ExtendedStatus& status) {
                return !status.busy() && status.page_completed == last_page_ && status.page_decoding == last_page_;
            });
            setup(4, last_page_, operation);
            Bytes job_number(2);
            put_word(job_number, 0, *job_);
            (void) execute(Command::job_end, job_number, operation);
            clear_job();
        });
}

void Lbp6000Session::cancel_job()
{
    ActiveGuard guard(active_);
    if (state() == SessionState::idle)
        return;
    if (!job_ || !synchronized_)
        throw std::logic_error("CAPT cleanup requires a known job and synchronized transport; reopen/recover device");
    Cancellation cleanup;
    const IoContext operation{runtime_, cleanup, deadline_after(runtime_.now(), options_.cancel_timeout)};
    state_.store(SessionState::cancelling);
    try {
        const auto status = poll(operation);
        (void) execute(Command::gpio, gpio_init(), operation);
        setup(4, status.page_completed, operation);
        Bytes job_number(2);
        put_word(job_number, 0, *job_);
        (void) execute(Command::job_end, job_number, operation);
        operation.check();
        clear_job();
        state_.store(SessionState::idle);
    } catch (...) {
        state_.store(SessionState::faulted);
        throw;
    }
}

void Lbp6000Session::clear_job()
{
    job_.reset();
    rows_sent_ = 0;
    previous_decoding_ = last_page_ = 0;
    page_ = {};
}
} // namespace capt
