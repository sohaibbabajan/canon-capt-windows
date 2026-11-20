// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace capt::printing {
inline constexpr std::size_t max_pdf_size = 64u * 1024u * 1024u;
inline constexpr char lpr_queue[] = "capt";

class LprJob {
public:
    virtual ~LprJob() = default;
    virtual void read(void* data, std::size_t size) = 0;
    virtual void write_document(const void* data, std::size_t size) = 0;
    virtual void validate_document() = 0;
    virtual void acknowledge(bool accepted) = 0;
};

// One LPR receive-job session. Returns true after both files were accepted;
// the caller then owns the job. Abort and device probes never submit a job.
bool receive_lpr_job(LprJob& job);
}
