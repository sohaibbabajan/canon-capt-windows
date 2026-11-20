# Provenance and licensing

The existing `captdriver/` checkout is an independently versioned Linux
reference and remains unchanged. Inspected origin:
`https://github.com/muxoc/captdriver-lbp6000.git`, commit
`ca52686e380be289a5339c1e5ef30e52fc82a225`.
Its README credits agalakhov/captdriver and mounaiban/captdriver. Preserve its
`COPYING`, authorship and per-file notices when reusing it.

New C++ core and test files are offered under **GPL-3.0-or-later**, as indicated
by their SPDX headers. The root `LICENSE` reproduces the GPLv3 text from the
reference's `COPYING`; it does not modify that license document or relicense
third-party files. New code implements a small documented subset using the
reference's `SPECS` and source as protocol information. It is not a clean-room
claim or an extraction of the complete Linux driver.

Initial wire-format provenance:

* `captdriver/SPECS` §1.1: little-endian four-byte header, total size, echoed
  reply command, first six reply bytes and documented BCD quirk.
* `captdriver/src/capt-command.c`: reference send chunk size and command handling.
* `captdriver/src/capt-command.h`, `src/prn_lbp2900.c`: LBP6000 setup command
  `0xE0BA` and `01 00` payload used as a synthetic serialization fixture.
* `captdriver/src/rastertocapt.c`, `tests/test-hiscoa.c`: packed raster and
  zero padding as a representation reference. New row thresholding is a
  limited utility, not a port of CUPS document rasterization.

`src/hiscoa.cpp` is a modified C++17 adaptation of
`captdriver/src/hiscoa-compress.c`, copyright (C) 2013 Alexey Galakhov
<agalakhov@gmail.com>, GPL-3.0-or-later. It retains that attribution and identifies
the 2026-11-16 modification. Parameter defaults and format knowledge also derive
from `src/hiscoa-common.c`, `src/hiscoa-common.h` and `SPECS` §3 under the same
reference provenance. The [codec audit](docs/HISCOA.md) records changes and fixtures.

The separate test decoder uses the documented format and the reference test
decoder's prefix interpretation. No original decoder file is copied or compiled.
`CAPT_ENABLE_REFERENCE_TESTS=ON` additionally compiles the unchanged C compressor
and a small test adapter into a **test-only** target. This target is not linked
into `capt_core`. No Windows sample is currently compiled into the project.

`src/session.cpp` adapts the job/page sequence and payloads from
`captdriver/src/prn_lbp2900.c`, whose existing copyright notices name Alexey
Galakhov (2013) and Alexei Gordeev (2016). Those notices and the modification
date are retained. The LBP6000 additions are traced to the same fork/commit
identified above. Command constants come from `src/capt-command.h`; status
offsets and flag knowledge come from `SPECS` and `src/capt-status.*`.
The new status implementation uses checked payload lengths and fresh values.
See [session audit](docs/SESSION.md) for deliberate behavioral changes.

Some reference files have incomplete individual license headers. Audit file
provenance and notices before importing them. Further modified GPL code must
retain applicable upstream attribution and identify modifications; distributing
binaries must satisfy corresponding-source duties.
See the licensing assessment and official sources in [FEASIBILITY.md](FEASIBILITY.md).

The new identity parser, Windows GUI/worker, USBPRINT diagnostic and developer
scripts are GPL-3.0-or-later. Windows calls were implemented against the linked
Microsoft API documentation; no Microsoft sample source was copied. The model
allow-list's combined `LBP6000/LBP6018` name comes from the unchanged reference.

The optional binary bundle is built with llvm-mingw 20261104 (LLVM 23.1.5), whose
statically linked runtime components retain their own notices. Packaging copies
the distribution's `LICENSE.TXT` and ARM64 MinGW `COPYING*` files into
`third-party-notices/`, alongside the GPL license and complete project/reference
source archive. Toolchain binaries are not redistributed. The exact compiler
release and verified archive hash are in [WINDOWS_PROBE.md](docs/WINDOWS_PROBE.md).

The optional USB print test separately bundles `pdfium.dll` from the ARM64
Windows [pdfium-binaries Chromium 8165 release](https://github.com/bblanchon/pdfium-binaries/releases/tag/chromium%2F8165).
The DLL retains its distribution license and each bundled component notice
in `third-party-notices/`. The test sheet is generated with ReportLab, a
build-time dependency. The Windows print test uses PDFium through its public
C API. It does not bundle Canon binaries.

The Windows queue uses the inbox Microsoft Print to PDF driver and Standard
TCP/IP port monitor installed by Windows. Their binaries are external system
components. The new LPR receive-job code implements the Windows client subset
of [RFC 1179](https://www.rfc-editor.org/rfc/rfc1179.html) and is GPL-3.0-or-later.

The Arabic validation PDF embeds subsets of Noto Sans Arabic and Noto Sans
under the SIL Open Font License 1.1. The distributor's copyright and license
record is included at `docs/fixtures/NOTO-FONT-NOTICE.txt` and in the test package.
The HTML validation page uses fonts supplied by the viewer's operating system.
