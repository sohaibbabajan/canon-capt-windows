// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.hpp"
#include <algorithm>
#include <cmath>
#include <memory>

namespace capt::printing {
namespace {
struct Layout {
    PageParameters page;
    int sheet_width;
    int sheet_height;
    bool landscape;
};

Layout layout(double width, double height)
{
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
        throw std::invalid_argument("Invalid PDF page size");
    const auto short_side = std::min(width, height), long_side = std::max(width, height);
    const bool landscape = width > height;
    // Imageable dimensions, line lengths and integer margins from CUPS raster
    // headers generated with the reference LBP6000 PPD at 600 dpi.
    if (std::abs(short_side - 595.28) <= 3 && std::abs(long_side - 841.89) <= 3)
        return {{4738, 6793, 4738, 6793, 595, 13, 13, PlainMedia::a4}, 4960, 7016, landscape};
    if (std::abs(short_side - 612) <= 3 && std::abs(long_side - 792) <= 3)
        return {{4877, 6377, 4877, 6377, 612, 13, 13, PlainMedia::letter}, 5100, 6600, landscape};
    throw std::invalid_argument("Select A4 or US Letter in the print dialog");
}
}
void require_a4_test_page(FPDF_DOCUMENT document)
{
    FS_SIZEF dimensions{};
    if (FPDF_GetPageCount(document) != 1 ||
        !FPDF_GetPageSizeByIndexF(document, 0, &dimensions))
        throw std::invalid_argument("The USB test requires a single A4 page");
    const auto page = layout(dimensions.width, dimensions.height);
    if (page.landscape || page.page.media != PlainMedia::a4)
        throw std::invalid_argument("The USB test requires A4 portrait paper");
}

void print_document(FPDF_DOCUMENT doc, Lbp6000Session& session, const JobTimestamp& time,
                    const Cancellation& cancel, const Progress& progress, GrayConversion conversion)
{
    const int count = FPDF_GetPageCount(doc);
    if (count < 1 || count > 100) throw std::invalid_argument("Expected a PDF with 1 to 100 pages");
    std::vector<Layout> pages;
    for (int index=0; index<count; ++index) {
        FS_SIZEF dimensions{};
        if (!FPDF_GetPageSizeByIndexF(doc,index,&dimensions)) throw std::runtime_error("Cannot inspect PDF page");
        pages.push_back(layout(dimensions.width,dimensions.height));
    }
    try {
        for (int index=0; index<count; ++index) {
            progress("Rendering page " + std::to_string(index+1) + " of " + std::to_string(count));
            const auto& l = pages[static_cast<std::size_t>(index)];
            const auto& p = l.page;
            std::unique_ptr<fpdf_page_t__,decltype(&FPDF_ClosePage)> pdf(FPDF_LoadPage(doc,index), FPDF_ClosePage);
            if (!pdf) throw std::runtime_error("Cannot load PDF page");
            const int stride = (p.raster_width+3) & ~3;
            Bytes gray(static_cast<std::size_t>(stride)*p.raster_rows,255);
            std::unique_ptr<fpdf_bitmap_t__,decltype(&FPDFBitmap_Destroy)> bitmap(
                FPDFBitmap_CreateEx(p.raster_width,p.raster_rows,FPDFBitmap_Gray,gray.data(),stride),FPDFBitmap_Destroy);
            if (!bitmap) throw std::runtime_error("Cannot allocate grayscale bitmap");
            // The PPD imageable area starts at 13.35 pt, or 111 pixels at
            // 600 dpi. Center the packed raster by whole bytes as CUPS does.
            constexpr int inset = 111;
            FPDF_RenderPageBitmap(bitmap.get(), pdf.get(), -inset, -inset, l.sheet_width, l.sheet_height,
                                  l.landscape ? 1 : 0, FPDF_PRINTING | FPDF_ANNOT | FPDF_GRAYSCALE);
            const GrayImageView image{gray.data(), gray.size(), p.raster_width, p.raster_rows,
                                      static_cast<std::size_t>(stride)};
            const std::size_t packed_width = static_cast<std::size_t>(p.line_size) * 8;
            const std::size_t source_bytes = (image.width + 7) / 8;
            const std::size_t padding = ((p.line_size - source_bytes) / 2) * 8;
            const MonoPageLayout mono{packed_width, p.raster_rows, p.line_size,
                                      {0, 0, padding, 0, image.width, image.height}};
            if (!index) { progress("Starting CAPT job"); session.start_job(time,cancel); }
            progress("Starting CAPT page " + std::to_string(index+1));
            session.begin_page(p,cancel);
            for (std::size_t y = 0; y < p.raster_rows;) {
                const auto band = prepare_monochrome_band(image, mono, y, 160, 128, conversion);
                session.send_band(band, cancel);
                y += band.rows;
            }
            progress("Waiting for page feed and output");
            session.end_page(cancel);
        }
        progress("Waiting for printer completion");
        session.finish_job(cancel);
    } catch (...) {
        try {
            session.cancel_job();
        } catch (const std::exception& error) {
            try { progress(std::string("CAPT cleanup failed: ") + error.what()); } catch (...) {}
        }
        throw;
    }
}
}
