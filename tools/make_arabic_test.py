#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a two-page A4 Arabic print fixture with embedded Noto Sans Arabic."""
import argparse
from pathlib import Path

import cairo
import gi

gi.require_version("Pango", "1.0")
gi.require_version("PangoCairo", "1.0")
from gi.repository import Pango, PangoCairo

MM = 72 / 25.4
WIDTH, HEIGHT = 210, 297


def make_test(path: Path) -> None:
    surface = cairo.PDFSurface(str(path), WIDTH * MM, HEIGHT * MM)
    surface.set_metadata(cairo.PDF_METADATA_TITLE, "اختبار الطباعة العربية | Canon CAPT")
    surface.set_metadata(cairo.PDF_METADATA_CREATE_DATE, "2026-11-19T00:00:00Z")
    context = cairo.Context(surface)
    layout = PangoCairo.create_layout(context)
    layout.set_auto_dir(False)
    layout.get_context().set_language(Pango.Language.from_string("ar-IQ"))

    def text(value, x, y, width, size, *, rtl=True, bold=False, centered=False):
        layout.get_context().set_base_dir(Pango.Direction.RTL if rtl else Pango.Direction.LTR)
        layout.context_changed()
        layout.set_alignment(Pango.Alignment.CENTER if centered else Pango.Alignment.RIGHT if rtl else Pango.Alignment.LEFT)
        layout.set_width(int(width * MM * Pango.SCALE))
        font = Pango.FontDescription("Noto Sans Arabic")
        font.set_absolute_size(size * Pango.SCALE)
        if bold:
            font.set_weight(Pango.Weight.BOLD)
        layout.set_font_description(font)
        layout.set_text(value, -1)
        if layout.get_line_count() != 1:
            raise ValueError(f"Unintended wrap in print fixture: {value}")
        context.set_source_rgb(0, 0, 0)
        context.move_to(x * MM, y * MM)
        PangoCairo.show_layout(context, layout)

    def line(x1, y1, x2, y2):
        context.set_source_rgb(0, 0, 0)
        context.set_line_width(0.65)
        context.move_to(x1 * MM, y1 * MM)
        context.line_to(x2 * MM, y2 * MM)
        context.stroke()

    def box(x, y, width, height, gray):
        context.rectangle(x * MM, y * MM, width * MM, height * MM)
        context.set_source_rgb(gray, gray, gray)
        context.fill_preserve()
        context.set_source_rgb(0, 0, 0)
        context.set_line_width(0.55)
        context.stroke()

    def marks():
        for x, dx in ((15, 1), (WIDTH - 15, -1)):
            for y, dy in ((15, 1), (HEIGHT - 15, -1)):
                line(x, y, x + dx * 5, y)
                line(x, y, x, y + dy * 5)

    def caption(value):
        text(value, 26, 267, 158, 11, centered=True)

    marks()
    text("اختبار الطباعة العربية", 23, 20, 164, 28, bold=True)
    text("Canon LBP6000 CAPT   |   A4   |   600 dpi", 23, 38, 164, 11, rtl=False)
    line(23, 49, 187, 49)
    text("اتصال الحروف والتشكيل", 23, 54, 164, 15, bold=True)
    text("السَّلَامُ عَلَيْكُمْ، هٰذَا اخْتِبَارٌ لِلطِّبَاعَةِ بِاللُّغَةِ العَرَبِيَّةِ.", 23, 65, 164, 16)
    text("لا، لأ، لإ، لآ — ٠١٢٣٤٥٦٧٨٩ — 0123456789", 23, 79, 164, 14)
    text("قِس المسافة بين العلامتين باستخدام مسطرة.", 23, 99, 164, 12)
    line(23, 121, 123, 121)
    line(23, 118.5, 23, 123.5)
    line(123, 118.5, 123, 123.5)
    text("100 mm", 23, 126, 100, 11, rtl=False, centered=True)
    text("درجات الرمادي", 23, 146, 164, 15, bold=True)
    for index, percentage in enumerate((100, 80, 60, 40, 20, 0)):
        x = 23 + index * 28
        box(x, 164, 24, 15, 1 - percentage / 100)
        text(f"{percentage}%", x, 182, 24, 10, rtl=False, centered=True)
    caption("الصفحة ١ من ٢")
    context.show_page()

    marks()
    text("اتجاه النص وترتيب الصفحات", 23, 20, 164, 25, bold=True)
    text("رقم المهمة: \u2066CAPT-2026 / USB-15\u2069، الحالة: جاهز.", 23, 46, 164, 14)
    text("الموعد: ٢٠ نوفمبر ٢٠٢٦، الساعة ١٠:٣٥.", 23, 59, 164, 14)
    line(23, 77, 187, 77)
    text("تأكّد من ظهور السطور التالية بوضوح.", 23, 87, 164, 13, bold=True)
    for index, size in enumerate((14, 11, 9)):
        text(f"بغداد، العراق — طباعة عربية واضحة (\u2066{size} pt\u2069)", 23, 100 + index * 11, 164, size)
    for x, y in ((23, 152), (162, 235)):
        box(x, y, 25, 3, 0)
    caption("الصفحة ٢ من ٢")
    context.show_page()
    surface.finish()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    make_test(args.output)
