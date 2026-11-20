// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "capt/session.hpp"
#include <fpdfview.h>
#include <functional>
#include <string>

namespace capt::printing {
// PDFium must be initialized and document must stay open throughout the call.
using Progress = std::function<void(const std::string&)>;
void require_a4_test_page(FPDF_DOCUMENT document);
void print_document(FPDF_DOCUMENT document, Lbp6000Session& session,
                    const JobTimestamp& timestamp, const Cancellation& cancel, const Progress& progress,
                    GrayConversion conversion = GrayConversion::threshold);
}
