// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
size_t capt_reference_hiscoa(uint8_t* output, size_t capacity,
                            const uint8_t* input, unsigned line_size, unsigned rows,
                            const int32_t origins[5], int last);
#ifdef __cplusplus
}
#endif
