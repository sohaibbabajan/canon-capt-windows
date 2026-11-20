// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only adapter: compiles the unchanged GPL reference encoder separately.
#include "reference_hiscoa.h"
#include "hiscoa-compress.h"

size_t capt_reference_hiscoa(uint8_t* output, size_t capacity,
                            const uint8_t* input, unsigned line_size, unsigned rows,
                            const int32_t origins[5], int last)
{
    const struct hiscoa_params parameters = {
        .origin_3 = origins[0], .origin_5 = origins[1], .origin_0 = origins[2],
        .origin_2 = origins[3], .origin_4 = origins[4]
    };
    return hiscoa_compress_band(output, capacity, input, line_size, rows,
                               last ? HISCOA_EOB_LAST : HISCOA_EOB_NORMAL, &parameters);
}
