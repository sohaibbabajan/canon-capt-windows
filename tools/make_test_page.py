#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create the one-sheet A4 portrait fixture for CAPT hardware testing."""
import argparse
from pathlib import Path

from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.pdfgen.canvas import Canvas


def make_test_page(output: Path) -> None:
    page = Canvas(str(output), pagesize=A4, invariant=True, pageCompression=1)
    page.setTitle("Canon CAPT USB test sheet")
    page.setAuthor("Canon CAPT for Windows ARM64 project")
    width, height = A4
    inset = 15 * mm

    page.setLineWidth(0.65)
    for x, dx in ((inset, 1), (width - inset, -1)):
        for y, dy in ((inset, 1), (height - inset, -1)):
            page.lines([(x, y, x + dx * 6 * mm, y), (x, y, x, y + dy * 6 * mm)])

    page.setFont("Helvetica-Bold", 25)
    page.drawString(inset, height - 31 * mm, "Canon CAPT USB test")
    page.setFont("Helvetica", 12)
    page.drawString(inset, height - 39 * mm, "LBP6000 / LBP6000B  |  A4 portrait  |  600 dpi")
    page.setFont("Helvetica", 10)
    page.drawString(inset, height - 53 * mm, "Check that all four corner marks and both black bars are visible.")
    page.drawString(inset, height - 59 * mm, "Measure the horizontal line between its end marks. It should be 100 mm.")

    ruler_y = height - 80 * mm
    end = inset + 100 * mm
    page.lines([(inset, ruler_y, end, ruler_y),
                (inset, ruler_y - 2 * mm, inset, ruler_y + 2 * mm),
                (end, ruler_y - 2 * mm, end, ruler_y + 2 * mm)])
    page.drawCentredString((inset + end) / 2, ruler_y - 7 * mm, "100 mm")

    page.rect(inset, height - 107 * mm, 25 * mm, 3 * mm, fill=1, stroke=0)
    page.rect(width - inset - 25 * mm, 29 * mm, 25 * mm, 3 * mm, fill=1, stroke=0)
    page.showPage()
    page.save()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    make_test_page(args.output)
