// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace capt {
using Instant = std::chrono::steady_clock::time_point;
using Milliseconds = std::chrono::milliseconds;

class Cancelled : public std::runtime_error {
public:
    Cancelled() : std::runtime_error("CAPT operation cancelled") {}
};
class DeadlineExceeded : public std::runtime_error {
public:
    DeadlineExceeded() : std::runtime_error("CAPT operation deadline exceeded") {}
};

class Cancellation {
public:
    void request() noexcept { requested_.store(true); }
    bool requested() const noexcept { return requested_.load(); }
private:
    std::atomic<bool> requested_{false};
};

class Runtime {
public:
    virtual ~Runtime() = default;
    virtual Instant now() const = 0;
    // Return at wake or on cancellation; must not block past wake. An OS
    // implementation must arrange cancellation-aware waiting, not a long sleep.
    virtual void wait_until(Instant wake, const Cancellation& cancellation) = 0;
};

// Finite positive delays up to 24h; throws on invalid delay or clock overflow.
Instant deadline_after(Instant now, Milliseconds delay);

struct IoContext {
    Runtime& runtime;
    const Cancellation& cancellation;
    Instant deadline;

    void check() const;
    std::chrono::steady_clock::duration remaining() const;
    void pause(Milliseconds delay) const;
};

// Session transport contract, intentionally separate from the primitive
// Transport interface. An adapter MUST enforce context.deadline/cancellation
// inside blocking I/O, and must make positive bounded progress or throw.
class SessionTransport {
public:
    virtual ~SessionTransport() = default;
    virtual std::size_t write_some(const std::uint8_t* data, std::size_t size,
                                   const IoContext& context) = 0;
    virtual std::size_t read_some(std::uint8_t* data, std::size_t size,
                                  const IoContext& context) = 0;
};
} // namespace capt
