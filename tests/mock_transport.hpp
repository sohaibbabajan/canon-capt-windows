// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "capt/protocol.hpp"

#include <algorithm>
#include <stdexcept>

// In-memory USB transport stand-in. Models partial byte I/O and failures only;
// it does not model USB packets, endpoint policies, timing or a real printer.
class MockTransport final : public capt::Transport {
public:
    enum class Failure { none, zero_progress, overreport, io_error };
    capt::Bytes incoming;
    capt::Bytes outgoing;
    std::vector<std::size_t> read_requests;
    std::vector<std::size_t> write_requests;
    std::size_t offset = 0;
    std::size_t read_chunk = 65535;
    std::size_t write_chunk = 65535;
    Failure read_failure = Failure::none;
    Failure write_failure = Failure::none;

    std::size_t read_some(std::uint8_t* data, std::size_t size) override
    {
        read_requests.push_back(size);
        if (read_failure != Failure::none)
            return fail(read_failure, size);
        const auto count = std::min({size, read_chunk, incoming.size() - offset});
        if (count != 0)
            std::copy_n(incoming.data() + offset, count, data);
        offset += count;
        return count;
    }

    std::size_t write_some(const std::uint8_t* data, std::size_t size) override
    {
        write_requests.push_back(size);
        if (write_failure != Failure::none)
            return fail(write_failure, size);
        const auto count = std::min(size, write_chunk);
        outgoing.insert(outgoing.end(), data, data + count);
        return count;
    }

private:
    static std::size_t fail(Failure failure, std::size_t requested)
    {
        if (failure == Failure::io_error)
            throw std::runtime_error("synthetic transport error");
        return failure == Failure::overreport ? requested + 1 : 0;
    }
};
