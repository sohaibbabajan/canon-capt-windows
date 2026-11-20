// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace capt {
// Resource policy for this implementation, not an advertised printer limit.
constexpr std::size_t max_band_bytes = 16u * 1024u * 1024u;
} // namespace capt
