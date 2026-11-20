// SPDX-License-Identifier: GPL-3.0-or-later
#include "lpr.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>

namespace capt::printing {
namespace {
constexpr std::size_t control_limit = 16 * 1024;

std::string line(LprJob& job)
{
    std::string result;
    for (std::size_t i = 0; i < 255; ++i) {
        char ch{};
        job.read(&ch, 1);
        if (ch == '\n') return result;
        if (ch < ' ' || ch > '~') break;
        result.push_back(ch);
    }
    throw std::invalid_argument("Invalid or oversized LPR command");
}

std::size_t file_size(std::string_view argument, std::size_t limit)
{
    const auto separator = argument.find(' ');
    if (separator == std::string_view::npos || separator == 0 ||
        argument.size() == separator + 1 || argument.find(' ', separator + 1) != std::string_view::npos)
        throw std::invalid_argument("Invalid LPR size or filename");
    std::size_t size = 0;
    const auto end = argument.data() + separator;
    const auto result = std::from_chars(argument.data(), end, size);
    if (result.ec != std::errc{} || result.ptr != end || size == 0 || size > limit)
        throw std::invalid_argument("LPR file exceeds the allowed size or has an invalid length");
    return size;
}

void receive_file(LprJob& job, std::size_t size, bool data)
{
    std::array<char, 32 * 1024> block;
    while (size) {
        const auto count = std::min(block.size(), size);
        job.read(block.data(), count);
        if (data) job.write_document(block.data(), count);
        size -= count;
    }
    char trailer{};
    job.read(&trailer, 1);
    if (trailer != 0)
        throw std::runtime_error("LPR sender rejected the transfer or used an incorrect byte count");
}
}

bool receive_lpr_job(LprJob& job)
{
    try {
        std::uint8_t operation{};
        job.read(&operation, 1);
        if (operation != 2 || line(job) != lpr_queue)
            throw std::invalid_argument("Expected the CAPT LPR receive-job request");
        job.acknowledge(true);

        bool received_control = false;
        bool received_data = false;
        for (int file = 0; file < 2; ++file) {
            job.read(&operation, 1);
            const auto argument = line(job);
            if (operation == 1 && argument.empty()) return false;
            if ((operation != 2 && operation != 3) ||
                (operation == 2 && received_control) || (operation == 3 && received_data))
                throw std::invalid_argument("Unexpected or duplicate LPR file");
            const bool data = operation == 3;
            const auto size = file_size(argument, data ? max_pdf_size : control_limit);
            job.acknowledge(true);
            receive_file(job, size, data);
            if (data) {
                job.validate_document();
                received_data = true;
            } else {
                received_control = true;
            }
            job.acknowledge(true);
        }
        return true;
    } catch (...) {
        try { job.acknowledge(false); } catch (...) {}
        throw;
    }
}
}
