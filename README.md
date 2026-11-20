# Canon CAPT for Windows ARM64

An independent Windows 11 ARM64 printing solution for the USB Canon LBP6000
family. It installs a printer you can select from Windows apps. Version 0.3.1
printed successfully through the installed queue on a Windows 11 ARM64 Surface
with USB printer hardware ID `04A9:271A`.

## Install

1. Download [CAPT-Windows-ARM64-0.3.1.zip](https://github.com/sohaibbabajan/canon-capt-windows/releases/tag/v0.3.1) and extract the entire archive.
2. Connect the printer via USB. Run `CAPT-Setup.exe` and choose **Install printer**. Setup needs administrator access and the Windows **Microsoft Print to PDF** feature. The installer is unsigned.
3. Select **Canon LBP6000 CAPT** in your app's print dialog. Use **View last job report** in Setup to check the result, or **Remove printer** to uninstall.

Setup is available in English and Arabic. The archive includes both quick-start
guides and two test files for Arabic text, page scale, and grayscale.

[English quick start](docs/WINDOWS_QUEUE_QUICK_START.txt) ·
[دليل البدء بالعربية](docs/WINDOWS_QUEUE_QUICK_START_AR.txt) ·
[Print path and validation checklist](docs/WINDOWS_QUEUE.md)

## Development and provenance

- [Development and build instructions](DEVELOPMENT.md)
- [Testing and hardware evidence](TESTING.md)
- [Architecture](ARCHITECTURE.md) and [feasibility notes](FEASIBILITY.md)
- [License](LICENSE) and [third-party attribution](NOTICE.md)

## Workspace

| Path | Purpose |
| --- | --- |
| `captdriver/` | Optional local Linux reference checkout for codec comparison tests |
| `include/capt/`, `src/` | New portable C++17 core; no Windows or CUPS dependencies |
| `tests/` | Deterministic tests and mock transport; no device access |
| `windows/` | Native setup, spooler handoff, USB worker and connection diagnostic |
| `printing/` | LPR receive-job handling and PDFium rendering into CAPT bands |
| `cmake/toolchains/`, `tools/` | Optional pinned ARM64 cross-build and source/binary packaging |
| `build/` | Local generated files, ignored |

The core provides ordinary CAPT framing, explicit binary/BCD reply lengths,
bounded reply assembly over an abstract transport, grayscale-to-monochrome band
preparation and a bounded Hi-SCoA compressor adapted from the reference.
The codec was checked on 1,042 bands against a separate test decoder and the
unchanged C encoder. See the [codec audit and fixtures](docs/HISCOA.md).
A reference-derived LBP6000 session now handles startup, page/band sequencing,
status, deadlines and cancellation over a deadline-aware transport interface.
See the [session contract and limitations](docs/SESSION.md). Its tests use
synthetic byte scripts. The identity suite also includes a physical USBPRINT output capture.

## Build the portable tests on Ubuntu

Requires a C++17 compiler and CMake 3.20 or newer. No CUPS, WDK or printer needed.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

See [DEVELOPMENT.md](DEVELOPMENT.md) for sanitizer builds and Windows ARM64
build instructions.

New core source is GPL-3.0-or-later; see [LICENSE](LICENSE). The reference keeps
its own licenses and attribution. This is an independent project, not an
official Canon driver.
