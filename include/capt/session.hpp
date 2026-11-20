// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "capt/control.hpp"
#include "capt/hiscoa.hpp"
#include "capt/raster.hpp"
#include "capt/status.hpp"

#include <functional>
#include <utility>

namespace capt {
enum class Command : std::uint16_t {
    extended_status = 0xA0A8, identify = 0xA1A1, job_begin = 0xA2A0,
    start = 0xA3A2, print_data = 0xC0A0, print_end = 0xC0A4,
    page_parameters = 0xD0A0, page_init = 0xD0A1, page_reset = 0xD0A2,
    hiscoa_parameters = 0xD0A4, multi_parameters = 0xD0A9,
    start_2 = 0xE0A2, start_1 = 0xE0A3, start_3 = 0xE0A4,
    upload = 0xE0A5, fire = 0xE0A7, job_end = 0xE0A9,
    lbp6000_setup = 0xE0BA, job_setup = 0xE1A1, gpio = 0xE1A2
};

struct CommandPolicy {
    bool reply;
    std::size_t min_request;
    std::size_t max_request;
    std::size_t min_reply_payload;
};
// Deliberate allow-list for the implemented LBP6000 sequence. Unknown commands
// and raw identification (0xA1A0) have no policy and are rejected.
CommandPolicy command_policy(Command command);

struct JobTimestamp {
    unsigned year = 2000; // Gregorian calendar; serialized as years since 1900.
    unsigned month = 1;   // 1..12; serialized zero based, matching the reference.
    unsigned day = 1;
    unsigned hour = 0;
    unsigned minute = 0;
    unsigned second = 0;
};

enum class PlainMedia : std::uint8_t { a4 = 0x02, letter = 0x0D };
struct PageParameters {
    std::uint16_t paper_width = 0;   // Reference CUPS imageable width, in pixels.
    std::uint16_t paper_height = 0;  // Reference CUPS imageable height, in pixels.
    std::uint16_t raster_width = 0;
    std::uint16_t raster_rows = 0;
    std::uint16_t line_size = 0;     // Packed bytes per line, including any padding.
    std::uint16_t margin_left = 0;   // Reference CUPS margin field, in integer points.
    std::uint16_t margin_top = 0;
    PlainMedia media = PlainMedia::a4;
    std::uint8_t density = 7;
    bool toner_save = false;
};

// Reference-derived, experimental plain-paper/600dpi profile. These serializers
// do not establish model compatibility, physical geometry or firmware semantics.
Bytes format_job_setup(const JobTimestamp& time, std::uint16_t job,
                       std::uint16_t page, std::uint8_t flags);
Bytes format_page_parameters(const PageParameters& page);

struct SessionOptions {
    Milliseconds transaction_timeout{15000};
    Milliseconds operation_timeout{120000};
    Milliseconds cancel_timeout{5000};
    Milliseconds poll_interval{1000};
    Milliseconds startup_delay{1000}; // May be zero in tests/validated profiles.
    std::size_t max_reply_size = 4096;
    // Explicit per-command overrides only. No LBP6000 BCD quirk is presumed.
    std::vector<std::pair<Command, ReplyLength>> reply_quirks;
    hiscoa::Parameters compression;
};

enum class SessionState {
    idle, starting_job, job_open, starting_page, page_open, sending_band,
    awaiting_page, finishing_job, cancelling, faulted
};

class PaperOut : public std::runtime_error {
public:
    PaperOut() : std::runtime_error("CAPT printer reports paper out") {}
};

// One instance per exclusively owned physical transport. Calls are serialized;
// concurrent/reentrant operations fail with logic_error instead of interleaving.
// Any failure after an operation starts faults the job; there is no page replay.
class Lbp6000Session {
public:
    Lbp6000Session(SessionTransport& transport, Runtime& runtime, SessionOptions options = {});
    SessionState state() const noexcept { return state_.load(); }
    void start_job(const JobTimestamp& time, const Cancellation& cancellation);
    void begin_page(const PageParameters& page, const Cancellation& cancellation);
    void send_band(const RasterBand& band, const Cancellation& cancellation);
    void end_page(const Cancellation& cancellation);
    void finish_job(const Cancellation& cancellation);
    // Bounded reference cleanup, using a fresh internal cancellation token.
    // Refuses I/O after an interrupted/malformed transaction lost framing.
    // Destruction performs no I/O. Cleanup cannot retract already printed pages.
    void cancel_job();

private:
    using Work = std::function<void(const IoContext&)>;
    void perform(SessionState expected, SessionState active, SessionState success,
                 const Cancellation& cancellation, const Work& preflight, const Work& action);
    Bytes execute(Command command, const Bytes& payload, const IoContext& operation);
    ExtendedStatus poll(const IoContext& operation);
    ExtendedStatus wait_for(const IoContext& operation,
                            const std::function<bool(const ExtendedStatus&)>& predicate);
    ExtendedStatus ready(const IoContext& operation);
    ExtendedStatus writable(const IoContext& operation);
    void setup(std::uint8_t flags, std::uint16_t page, const IoContext& operation);
    void clear_job();

    SessionTransport& transport_;
    Runtime& runtime_;
    SessionOptions options_;
    std::atomic_flag active_ = ATOMIC_FLAG_INIT;
    std::atomic<SessionState> state_{SessionState::idle};
    bool synchronized_ = true;
    std::optional<std::uint16_t> job_;
    JobTimestamp time_;
    PageParameters page_;
    std::size_t rows_sent_ = 0;
    std::uint16_t previous_decoding_ = 0;
    std::uint16_t last_page_ = 0;
};
} // namespace capt
