# Development

## Resume from Windows ARM64 Queue 0.3.1

The Surface printed the A4 USB test sheet at the expected scale in 0.2.
Version 0.3.1 builds a native bilingual installer and uses the Windows PDF
renderer and loopback LPR to feed the existing USB session. The project owner
confirmed successful printing through the installed queue on a Windows 11
ARM64 Surface. See [test instructions](docs/WINDOWS_QUEUE_QUICK_START.txt) and
[the full validation checklist](docs/WINDOWS_QUEUE.md).

Use the existing ARM64 cross-build and the local Windows ARM64 PDFium bundle
from [pdfium-binaries, Chromium 8165](https://github.com/bblanchon/pdfium-binaries/releases/tag/chromium%2F8165).
The cached `build/pdfium/archive.tgz` has locally observed SHA-256
`d65e405c59cffe6fe36c0935b90d40ddf3ce75ef284f3168a28dcf00d0388131`.
Queue packaging uses Python 3.10 or newer. The separate direct-USB test
packaging also needs ReportLab.
The checked-in Arabic PDF fixture is regenerated with
`/usr/bin/python3 tools/make_arabic_test.py docs/fixtures/Arabic-Print-Test.pdf`
using PyCairo, PyGObject/Pango and Noto Sans Arabic. See the fixture's notice.

```sh
cmake -S . -B build/arm64-cross \
  -DCAPT_PDFIUM="$PWD/build/pdfium"
cmake --build build/arm64-cross --parallel
python3 tools/package_print_test.py --queue --build build/arm64-cross --pdfium build/pdfium \
  --toolchain build/toolchains/llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64
```

If there is no initialized `build/arm64-cross`, supply the toolchain options
from [the connection diagnostic build instructions](docs/WINDOWS_PROBE.md#build-and-package-from-ubuntu).
The 0.3 candidate replaces the unfinished raw TCP prototype with an LPR
receiver that validates the spooler as the client and frames each job.
Continue with this path before investing in a custom Windows render filter or
monitor. Physical completion is visible in Setup's latest-job report. Queue
status after handoff and crash recovery are described in the validation guide.

The release bundle contains `corresponding-source.zip`, which includes the
Linux reference and its license. A standalone Git checkout needs the pinned
reference in `captdriver/` to include it when packaging a new release. Follow
the reference checkout instructions below.

## Earlier connection diagnostic milestone

The current milestone adds a cross-compiled ARM64 Windows connection diagnostic
to the portable CAPT session/raster/codec foundation.
Version 0.1.1 fixes the captured Surface identity-format rejection using an
explicit hardware-ID policy; see [capture and regression](docs/CAPTURE_2026-11-19.md).
It does not deliver a finished Windows driver.
Keep new work outside `captdriver/`.
Reference identity, dependencies and source findings are recorded in
[FEASIBILITY.md](FEASIBILITY.md); do not run its installation recipes as part of
this project's build.

## Tools and builds

On Ubuntu, use CMake 3.20+, a C++17 compiler and CTest. CMake has no downloads
and the default build does not configure/build the reference checkout. Run from this workspace:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

GCC/Clang AddressSanitizer and UndefinedBehaviorSanitizer build:

```sh
cmake -S . -B build/sanitize -DCMAKE_BUILD_TYPE=Debug -DCAPT_ENABLE_SANITIZERS=ON
cmake --build build/sanitize --parallel
ctest --test-dir build/sanitize --output-on-failure
```

Optional byte-for-byte comparison with the local reference C encoder. From a
fresh Git checkout, fetch the pinned reference first:

```sh
git clone https://github.com/muxoc/captdriver-lbp6000.git captdriver
git -C captdriver checkout --detach ca52686e380be289a5339c1e5ef30e52fc82a225
```

Then build the reference comparison:

```sh
cmake -S . -B build/reference -DCMAKE_BUILD_TYPE=Debug -DCAPT_ENABLE_REFERENCE_TESTS=ON
cmake --build build/reference --parallel
ctest --test-dir build/reference --output-on-failure -V
```

This option needs a C99 compiler in addition to C++17. It reads reference
sources into a separate test target; generated files stay in the build directory.
It does not run Autotools, depend on CUPS or modify `captdriver/`. The production
core remains independent of that checkout. Combine this option with sanitizers
to instrument both the port and the reference encoder on validated test inputs.

For Windows, use a Windows x64 build host/VM or the Surface with native ARM64
tools. Install compatible Visual Studio 2022 C++ tools, Windows SDK and WDK;
the researched Microsoft baseline is SDK/WDK 26100 or newer. Record exact
versions for the eventual driver project. EWDK is another Windows build route.
Ubuntu cross-compilation is useful later, but is not a substitute for WDK tools,
signing verification or execution on Windows ARM64.

Portable library/test build, from a Windows developer command prompt with
the Visual Studio CMake generator installed:

```bat
cmake -S . -B build/arm64 -G "Visual Studio 17 2022" -A ARM64
cmake --build build/arm64 --config Debug --parallel
ctest --test-dir build/arm64 -C Debug --output-on-failure
```

Execute that CTest command on Windows ARM64; an x64 build host cannot natively
run the ARM64 test executable. These instructions have not been executed here.
The CMake targets include the core, tests and connection diagnostic, not a WDK printer package. Inspect future
PE headers with `dumpbin /headers` and verify every non-system DLL dependency
is ARM64. Do not substitute ARM64EC or an emulated Canon component.

An Ubuntu-to-Windows ARM64 build now succeeds using pinned llvm-mingw. The
diagnostic and seven test executables have native ARM64 PE headers; Windows
execution is still required. See [WINDOWS_PROBE.md](docs/WINDOWS_PROBE.md) for
reproducible cross-build/package commands, API contracts and GUI instructions.
This does not replace the WDK printer-pipeline proof.

## Roadmap and acceptance criteria

Milestones 0–1 are complete. Milestone 2's portable codec and band APIs are
implemented: 1,042 bands match the reference and decode correctly, with fixed
byte fixtures and boundary tests. Physical printable geometry remains open
until trustworthy captures or hardware measurements are available. Read
[docs/HISCOA.md](docs/HISCOA.md) for exact scope and evidence.

Milestone 3's portable session is implemented and covered by strict synthetic
transport scripts and a fake monotonic clock. It handles model startup,
one-way/reply policy, in-order bands, page/counter progression, completion,
timeouts, paper-out and explicit cleanup. The retry policy is to fault without
replaying. Read [docs/SESSION.md](docs/SESSION.md) for reference deviations and
the requirement that real transports enforce cancellation/deadlines internally.
The actual printer sequence, geometry and OS transport behavior remain open.

| Milestone | Work | Acceptance evidence |
| --- | --- | --- |
| 0: feasibility | Inspect local reference and primary Microsoft sources; record licensing, uncertainties and architecture | This report and documentation exist; reference preserved |
| 1: portable foundation | C++17 framing with bounded lengths, abstract transport and fragmented/error mock tests; minimal grayscale row packing | Linux CTest and sanitizer results recorded in TESTING.md; no hardware dependency |
| 2: codec and raster bands | Audit/port Hi-SCoA C in isolation with attribution; explicit capacity/overflow handling; independent bitstream fixtures and decoder comparison; stride, margins and band preparation | Known bytes, randomized round trips, band/end markers, incompressible and insufficient-buffer cases; bounded memory; compare geometries with trustworthy traces |
| 3: model/session | Explicit per-command reply/quirk table; status decoding; LBP6000 job/page state machine; injected clock/cancel; page cache policy | Scripted success/failure traces, malformed status, buffer-full, no-paper, deadlines, cancellation and no duplicate-send tests |
| 4: Windows pipeline proof | Build/install an ARM64 v3 XPSDrv test queue on Surface with a diagnostic sink; prove required inbox rendering/config dependencies | Print from a Win32 app and a modern app; inspect raster, paper settings, cancellation and DLL architecture without requiring a printer |
| 5: USB proof | Inspect actual USB descriptors, IEEE 1284 and USBPRINT IDs; test inbox monitor read/write path; evaluate WinUSB only if necessary | Bounded ID/status exchanges and disconnect recovery on Surface, with saved provenance and traces; no print claim from a mock |
| 6: integrated direct USB | Connect rendering records, CAPT monitor/session and transport; implement accurate completion/status | First page, repeated jobs, multipage, blank/text/image/orientation/margins; capture physical results and protocol traces |
| 7: installation/release | Verified ARM64 INF/catalog, monitor/pipeline configuration, GUI setup, permissions and policy errors, uninstall/rollback, appropriate signing/HLK review | Clean-machine install/reboot/reconnect and ordinary application printing; verified signatures and published corresponding source |
| F: IPP bridge fallback | Linux CUPS 2.x queue using reference, accepted format conversion and network sharing; later Printer Application migration assessment | Surface uses inbox IPP queue, no CAPT client install; actual print/status/cancel tests. Do not label certified without certification |

The native diagnostic build subtask now has compile/import evidence. The next
integration gates are execution on the Surface and the Windows pipeline proof.
Run the pipeline proof before committing to a full renderer. Windows/WDK tools
and target execution are required for that evidence; Linux script tests cannot
pass it. The Windows transport adapter must prove bounded/cancellable I/O before
the session can be connected to hardware.
Milestone 5 gates direct-USB commitment; absence of hardware leaves it open,
not passed. Prototype PSA virtual-printer/broker integration if the v3 pipeline
or deployment constraints make it preferable. Avoid implementing multiple
production front ends before one transport and pipeline pair is proven.

## Future Windows API/tool inventory

* Rendering: `IPrintPipelineFilter`, pipeline properties, PrintTicket and
  PrintCapabilities, XPS rasterization service and WIC. Confirm sample licenses
  and adapt architecture/build settings explicitly.
* Monitor: `InitializePrintMonitor2`, `MONITOR2`, `OpenPortEx`, `StartDocPort`,
  `WritePort`, `ReadPort`, `EndDocPort`, status/cancellation/completion mapping.
* Diagnostics: SetupAPI enumeration and device properties, `CreateFileW`,
  `DeviceIoControl`, documented USBPRINT identity request, Windows print event
  logs, WinDbg and captured traces. Do not hardcode discovered interface paths.
* Alternate transport: WinUSB APIs, endpoint discovery, finite pipe timeouts,
  cancellation, interface binding and rollback; not an extra owner alongside USBPRINT.
* Packaging: WDK printer templates, verified `NTarm64` INF matching, InfVerif,
  Inf2Cat, SignTool and relevant HLK tests. Release signing eligibility needs
  confirmation; no production certificate or acceptance is assumed.

## Trace and contribution discipline

Every hardware fixture needs printer model/revision, OS/driver version,
capture method, direction, byte count and transfer boundaries. Remove document
content, serials and personal metadata before sharing captures. Mark fabricated
test vectors as synthetic. Distinguish Linux upstream claims, local automated
results and actual Surface/printer evidence.

Keep OS handles, timing and spooler policy out of wire/raster primitives. Do
not change `captdriver/` to fix reference defects; first reproduce a defect in
an isolated test and implement the audited replacement outside that tree.
See [NOTICE.md](NOTICE.md) before importing code or Microsoft sample material.
