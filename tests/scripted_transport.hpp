// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "capt/session.hpp"
#include "support.hpp"

#include <algorithm>
#include <functional>

class FakeRuntime final : public capt::Runtime {
public:
    capt::Instant current{};
    std::vector<capt::Instant> wakes;
    std::function<void()> on_wait;
    bool early_return = false;

    capt::Instant now() const override { return current; }
    void wait_until(capt::Instant wake, const capt::Cancellation& cancellation) override
    {
        wakes.push_back(wake);
        if (on_wait) on_wait();
        if (!early_return && !cancellation.requested()) current = wake;
    }
};

// Strict byte script: refuses a new command until the entire preceding reply
// is read. No device model or hardware compatibility is simulated or inferred.
class ScriptedTransport final : public capt::SessionTransport {
public:
    struct Step { capt::Bytes request; capt::Bytes reply; };
    std::vector<Step> steps;
    std::vector<std::uint16_t> commands;
    std::vector<capt::Instant> deadlines;
    std::function<void(const capt::IoContext&)> before_read;
    std::function<void(const capt::IoContext&)> after_write;
    std::size_t read_chunk = 65535;
    std::size_t write_chunk = 65535;
    std::size_t read_calls = 0;
    std::size_t write_calls = 0;
    bool bcd_status = false;

    void raw(std::uint16_t command, const capt::Bytes& request, const capt::Bytes& reply)
    {
        steps.push_back({capt::encode_packet({command, request}), reply});
    }
    void exchange(std::uint16_t command, const capt::Bytes& request, const capt::Bytes& payload = {0, 0})
    {
        auto reply = capt::encode_packet({command, payload});
        if (bcd_status && command == 0xA0A8) {
            CHECK(reply.size() == 44);
            reply[2] = 0x44;
        }
        raw(command, request, reply);
    }
    void write(std::uint16_t command, const capt::Bytes& request)
    {
        raw(command, request, {});
    }
    bool done() const { return step_ == steps.size() && written_ == 0 && read_ == 0; }

    std::size_t write_some(const std::uint8_t* data, std::size_t size,
                           const capt::IoContext& context) override
    {
        ++write_calls;
        observe(context);
        CHECK(step_ < steps.size());
        auto& step = steps[step_];
        CHECK(written_ < step.request.size()); // No next write before draining reply.
        const auto count = std::min({size, write_chunk, step.request.size() - written_});
        CHECK(std::equal(data, data + count, step.request.data() + written_));
        written_ += count;
        if (written_ == step.request.size()) {
            commands.push_back(static_cast<std::uint16_t>(static_cast<unsigned>(step.request[0]) |
                                (static_cast<unsigned>(step.request[1]) << 8)));
            if (step.reply.empty()) advance();
        }
        if (after_write) after_write(context);
        return count;
    }
    std::size_t read_some(std::uint8_t* data, std::size_t size,
                          const capt::IoContext& context) override
    {
        ++read_calls;
        observe(context);
        if (before_read) before_read(context);
        CHECK(step_ < steps.size());
        auto& step = steps[step_];
        CHECK(written_ == step.request.size() && !step.reply.empty());
        const auto count = std::min({size, read_chunk, step.reply.size() - read_});
        std::copy_n(step.reply.data() + read_, count, data);
        read_ += count;
        if (read_ == step.reply.size()) advance();
        return count;
    }

private:
    void observe(const capt::IoContext& context)
    {
        context.check();
        CHECK(context.remaining().count() > 0);
        deadlines.push_back(context.deadline);
    }
    void advance() { ++step_; written_ = 0; read_ = 0; }
    std::size_t step_ = 0;
    std::size_t written_ = 0;
    std::size_t read_ = 0;
};
