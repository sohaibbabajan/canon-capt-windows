// SPDX-License-Identifier: GPL-3.0-or-later
#include "../printing/lpr.hpp"
#include "support.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace {
using capt::printing::receive_lpr_job;

class Script final : public capt::printing::LprJob {
public:
    explicit Script(std::string commands) : commands_(std::move(commands)) {}
    std::vector<bool> decisions;
    std::string document;
    bool valid = true;
    int validations = 0;

    void read(void* output, std::size_t count) override
    {
        if (count > commands_.size() - position_) throw std::runtime_error("Transfer disconnected");
        std::memcpy(output, commands_.data() + position_, count);
        position_ += count;
    }
    void write_document(const void* data, std::size_t count) override
    {
        document.append(static_cast<const char*>(data), count);
    }
    void validate_document() override
    {
        ++validations;
        if (!valid) throw std::runtime_error("Invalid PDF");
    }
    void acknowledge(bool accepted) override { decisions.push_back(accepted); }

private:
    const std::string commands_;
    std::size_t position_ = 0;
};

const auto control = std::string("\2" "3 cfA\nabc") + '\0';
const auto data = std::string("\3" "5 dfA\nabc") + '\0' + 'd' + '\0';
const std::string request = "\2capt\n";
}

int main()
{
    for (const auto& transfer : {control + data, data + control}) {
        Script job(request + transfer);
        CHECK(receive_lpr_job(job));
        CHECK(job.validations == 1 && job.document == std::string("abc\0d", 5));
        CHECK(job.decisions == std::vector<bool>({true, true, true, true, true}));
        for (std::size_t i = 0; i < transfer.size(); ++i) {
            Script incomplete(request + transfer.substr(0, i));
            expect_throw<std::runtime_error>([&] { receive_lpr_job(incomplete); });
            CHECK(!incomplete.decisions.empty() && !incomplete.decisions.back());
        }
    }
    Script abort(request + control + "\1\n");
    CHECK(!receive_lpr_job(abort));
    CHECK(abort.document.empty() && abort.validations == 0);
    for (const auto& input : {std::string("\2other\n"), request + control + control, request + data + data,
                              request + "\3" "67108865 dfA\n", request + "\3" "18446744073709551616 dfA\n",
                              request + "\3" "10junk dfA\n", request + "\3" "0 dfA\n",
                              request + "\3" "10 \n", request + "\4" "abc\n", request + "\3" + std::string(255, 'x')}) {
        Script wrong(input);
        expect_throw<std::invalid_argument>([&] { receive_lpr_job(wrong); });
        CHECK(!wrong.decisions.back());
    }
    Script bad_pdf(request + data + control);
    bad_pdf.valid = false;
    expect_throw<std::runtime_error>([&] { receive_lpr_job(bad_pdf); });
    CHECK(bad_pdf.validations == 1 && bad_pdf.decisions == std::vector<bool>({true, true, false}));
    Script sender_error(request + "\3" "3 dfA\nabc\1");
    expect_throw<std::runtime_error>([&] { receive_lpr_job(sender_error); });
    CHECK(sender_error.validations == 0 && !sender_error.decisions.back());
}
