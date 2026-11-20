// SPDX-License-Identifier: GPL-3.0-or-later
#include "capt/control.hpp"

#include <algorithm>

namespace capt {
Instant deadline_after(Instant now, Milliseconds delay)
{
    if (delay.count() <= 0 || delay > std::chrono::hours(24))
        throw std::invalid_argument("CAPT delay must be positive and at most 24 hours");
    const auto duration = std::chrono::duration_cast<Instant::duration>(delay);
    if (now > Instant::max() - duration)
        throw std::overflow_error("CAPT deadline overflows the monotonic clock");
    return now + duration;
}

void IoContext::check() const
{
    if (cancellation.requested())
        throw Cancelled();
    if (runtime.now() >= deadline)
        throw DeadlineExceeded();
}

std::chrono::steady_clock::duration IoContext::remaining() const
{
    check();
    const auto now = runtime.now();
    if (now >= deadline)
        throw DeadlineExceeded();
    return deadline - now;
}

void IoContext::pause(Milliseconds delay) const
{
    check();
    const auto wake = std::min(deadline, deadline_after(runtime.now(), delay));
    runtime.wait_until(wake, cancellation);
    check();
    if (runtime.now() < wake)
        throw std::runtime_error("CAPT runtime returned before its wake time");
}
} // namespace capt
