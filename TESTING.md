# Testing

## Arabic and grayscale validation (0.3.1)

The Arabic Setup menu, language preference and visible right-to-left dialog
flags were cross-compiled for ARM64. Native visual and installation checks
await the Surface. The two-page PDF uses embedded Noto Sans Arabic and Noto
Sans font subsets. Both fixture pages were rendered and reviewed, including
Arabic connections, diacritics, digits and the isolated English job code.
The companion live-text HTML was reviewed in Firefox on Linux.

A separate Linux PDFium 153.0.7999.0 smoke run rendered both pages, packed
the ordered halftone and sent them through the CAPT session into a simulated
printer. The independent decoder recovered 86 bands. The six chip samples
contained 64, 52, 40, 24, 12 and 0 black pixels per 64, respectively. Both
reconstructed pages were visually checked. The raster suite also verifies
coverage, clear margins and unchanged dithering phase across split bands.
On November 20, 2026, the project owner reported a successful physical print
through the installed 0.3.1 queue on a Windows 11 ARM64 Surface. This result
covers the reported print. See the [Surface acceptance checklist](docs/WINDOWS_QUEUE.md#surface-validation-checklist)
for additional cases such as paper-out, cancellation, and reinstall.

## Windows queue candidate (0.3)

On 2026-11-19 the user confirmed that the direct USB test printed on the
Surface, including correct measured scale. This validates the 0.2 A4 print
path on the tested device. The 0.3 install path uses the same CAPT session.

All seven Linux CTest suites passed in Debug and ASan/UBSan with the separate
reference encoder. The new LPR suite covers both transfer orders, every
truncation of both sample transfers, invalid document rejection, cancel,
duplicate files, malformed sizes, overflow, and bounds. The optional Windows
setup, service, diagnostic and developer test binaries cross-built with
warnings treated as errors. Package inspection verifies the native ARM64
machine type and allowed imports, including the bundled PDFium DLL.

The first confirmed on-device run of the installed queue used 0.3.1.

## First physical print prototype (0.2)

Linux reruns passed all six portable CTest suites in Debug and ASan/UBSan with
reference codec comparison. The session suite checks the exact A4 page
parameter payload derived from the LBP6000 PPD and a local CUPS raster header.

The reference CUPS/Ghostscript raster for the test sheet at 600 dpi has 4,738
imageable pixels per row, 6,793 rows, 593 source bytes per row, 160 rows per
band and integer margins of 13 points. The reference CAPT filter selects
595 packed bytes per row and centers the source by one byte. A Letter sample
produced 4,877 × 6,377 pixels and 610 source bytes centered in 612 CAPT bytes.
These values are offline reference evidence. Printed alignment and scale
still need measurement.

A Linux smoke run through the real PDF renderer and CAPT session used PDFium
153.0.7999.0 and a synthetic transport. The sink decoded all 43 compressed
bands, checked both black bars, a blank region and scanline padding, and
verified that the session returned to idle. The one-sheet validator rejected
Letter. Poppler independently rendered the test page for visual inspection.
The ARM64 executable and Windows PDFium 160.0.8165.0 DLL were checked for native
PE machine type and imports. Execution of the print app on Windows, printer
job exchanges and physical output await the Surface. [Quick start](docs/PRINT_TEST_QUICK_START.txt)

## Surface status exchange on 2026-11-19 at 17:41 UTC

Version 0.1.1 completed one real A0A8 exchange with the tray confirmed empty.
The status suite now replays its 92-byte reply through the protocol and status
decoders with the observed read sizes. [Hardware report](docs/CAPTURE_2026-11-19.md)

## 2026-11-19 capture regression: version 0.1.1

User-reported Surface evidence for 0.1: worker self-test exit 0; USBPRINT
enumeration, exclusive open and identity retrieval succeeded. Identity parsing
then failed in both inspection and status modes; no CAPT request was sent.
See [capture provenance and policy](docs/CAPTURE_2026-11-19.md).

The new regression preserves the 86 captured bytes without a serial number.
It verifies an explicit hardware-ID-gated exception, strict-mode rejection,
all 65,536 prefix mutations, all truncations with original/rewritten lengths,
field mutations, NUL injection, duplicate fields and hardware-ID lookalikes.
The complete original synthetic suite remains in place. Version 0.1.1 requires
new Surface execution; the Windows worker suite and live CAPT status/printing
are still not verified.

Local verification on 2026-11-19: all 6 suites passed in Debug (1.58 s), Release
(0.63 s), and ASan/UBSan with reference codec comparison (10.30 s), with no
sanitizer findings. All eight ARM64 executables cross-compiled successfully;
the package step audits their machine types and imports. All 86 reference
files match the original package byte-for-byte and its nested git tree is clean.

## Previous verification: Windows diagnostic and identity milestone

Recorded 2026-11-16 on Ubuntu x86_64, GCC 15.2.0 / CMake 4.2.3.
Older milestone results below are retained as history.

| Check | Observed result |
| --- | --- |
| Linux Debug | 6/6 CTest suites passed |
| Linux Release | 6/6 CTest suites passed |
| ASan + UBSan with unchanged reference codec comparison | 6/6 passed; no sanitizer findings; 1,042 codec bands still match |
| Production core without tests/reference | Release static library built |
| Windows ARM64 cross-build | GUI diagnostic and seven test executables built with llvm-mingw 20261104 / Clang 23.1.5, warnings as errors |
| PE architecture / static DLL import audit | Every executable is ARM64 `0xAA64`; only reviewed Windows system/UCRT imports |
| Windows execution / Windows worker test | **Not run**; binaries compiled only |
| USB identity/status, installed queue, physical print | **Not tested** |

The new identity suite checks bounded IEEE 1284 decoding, field aliases,
ambiguity, exact diagnostic model matching, every fixture truncation and
65,536 possible length prefixes. No actual printer identities are in the tests.
The Windows-only worker test covers child startup/reporting, repeated lifecycle,
concurrent-start rejection, intentional-hang termination and launch failure.
It does not access a device and still needs execution on Windows ARM64.

The connection app's GUI, worker supervisor, overlapped USBPRINT I/O and report
saving have compile evidence only. See [WINDOWS_PROBE.md](docs/WINDOWS_PROBE.md)
for contracts, remaining acceptance checks and commands. The compiled diagnostic
cannot print or install a printer. Direct USB device-handle access does not prove
the separate spooler monitor integration.

All 86 reference files remain byte-identical to the original SHA256 baseline;
the nested `captdriver` repository remains clean. No Wine or Windows emulation
was used to produce test results.

## Evidence levels

Portable unit tests establish only the documented byte/raster contracts.
They do not validate USB transactions, Canon compatibility, Windows integration,
driver signing or physical print quality. No hardware or Windows tests were
performed during the initial research.

## Automated foundation suite

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The tests use explicit checks, not `assert`, so they remain active in Release.
There are no network calls, device opens, CUPS services or privileged operations.
Synthetic protocol vectors are taken from documented formats and local
reference constants; none is represented as a captured printer reply.

Protocol checks cover fixed wire bytes, payload and maximum-length boundaries,
short I/O, six-byte initial reply plus remainder, explicit binary/BCD lengths,
invalid BCD, wrong command, undersized/oversized reply, truncated input,
zero progress, adapter failure propagation and adjacent replies without
overreading. A write-only test verifies that sending a packet never reads.

Raster checks cover black/white polarity, threshold equality, MSB-first order,
non-byte-aligned row padding, empty rows and invalid pointers. Band checks add
padded source stride, explicit crop/placement, white margins/output padding,
short final bands, blank sources, truncated views, extent overflow and memory
caps. These functions do not render documents or implement the final halftoner.

Sanitizer invocation is in [DEVELOPMENT.md](DEVELOPMENT.md). Record actual
results below after execution; never mark planned tests as passed.

## Initial local verification

Tested on 2026-11-16 on the Ubuntu x86_64 development environment with
GNU C++ 15.2.0 and CMake 4.2.3. Warnings are treated as errors.

| Configuration | Actual result |
| --- | --- |
| Debug | 2/2 CTest suites passed |
| Release | 2/2 CTest suites passed; checks remain active |
| Debug with AddressSanitizer + UndefinedBehaviorSanitizer | 2/2 CTest suites passed; no sanitizer findings |

Protocol coverage includes 257 deterministic payload/fragmentation combinations
in addition to fixed vectors and error cases. Raster coverage includes widths
0–129 in addition to polarity/threshold fixtures. These are synthetic tests.
All 86 reference files outside `.git` were verified against a SHA-256 manifest
created before edits, and the reference's tracked diff remained empty.
Windows ARM64 compilation/execution, USB and physical printing are **untested**.

## Codec and band milestone verification

Also tested on 2026-11-16 with GNU C/C++ 15.2.0 and CMake 4.2.3:

| Configuration | Actual result |
| --- | --- |
| Debug, default build | 3/3 suites passed |
| Release, default build | 3/3 suites passed |
| Debug with reference comparison | 3/3 suites passed; 1,042 bands byte-identical to the C encoder |
| ASan + UBSan with reference comparison | 3/3 suites passed; no sanitizer findings |

The codec suite decodes each band with a separate bounded test decoder and
checks exact raw data, marker, alignment and full consumption. Seven manually
assembled fixed byte fixtures provide checks beyond round trips. All five
repeat commands, both origin swaps, prefix ranges, stash eviction, narrow
rows, 595/640-byte lines, 160-row bands, three parameter tuples and deterministic
random data are covered. Seed: `0x6000B123`. Output-limit failures, invalid
parameters/geometry, resource limits and corrupt/truncated decoder input are
also tested. A grayscale page is prepared, compressed in bands and decoded to
fixed expected packed bytes, including margins and a short final band.

See [docs/HISCOA.md](docs/HISCOA.md) for derivations, source discrepancies and
memory policy. Optional differential commands are in [DEVELOPMENT.md](DEVELOPMENT.md).
All expected vectors are synthetic, not device captures. Physical geometry,
printer acceptance and Windows ARM64 execution remain **untested**.

## Session/status milestone verification

Tested on 2026-11-16 with GNU C/C++ 15.2.0 and CMake 4.2.3:

| Configuration | Actual result |
| --- | --- |
| Debug | 5/5 CTest suites passed |
| Release | 5/5 CTest suites passed |
| ASan + UBSan with reference codec comparison | 5/5 CTest suites passed; no sanitizer findings |
| Release core-only build (`BUILD_TESTING=OFF`) | Static library built without the reference/CUPS/Windows dependencies |

The new status suite rejects every payload length 0–39 and checks optional
field boundaries through 84 bytes, raw word/counter decoding and flag masks.
The session suite uses strict synthetic request/reply scripts: a new command
cannot be written until the preceding reply is drained, and unexpected reads
after a one-way command fail the script. No real clock waits occur.

Session tests cover partial byte I/O, repeated/multipage jobs, initialization
upload, explicit BCD override, buffer-full/busy flow control, a multi-chunk
high-entropy band, all-rows/in-order checks, stale counters and counter wrap,
page-out versus completed status, paper-out before data and after firing,
operation/transaction/cleanup deadlines, cancellation before I/O and during
partial transfers/waits, cancellation after firing, malformed replies,
disconnect, a late adapter return, reentrancy, invalid options and clock overflow.
The shared transaction deadline is checked across partial writes and reads.
Ambiguous page delivery never causes automatic replay. Lost framing refuses
cleanup commands; synchronized cleanup uses a fresh token and a separate budget.

All 86 reference files still matched the original SHA-256 manifest and the
reference's tracked diff remained empty. The new session is a reference-derived
implementation tested against synthetic scripts, not captured printer traces.
The real transport must enforce deadlines/cancellation internally; the tests
cannot establish that Windows USB callbacks satisfy that requirement.
See [docs/SESSION.md](docs/SESSION.md). Windows ARM64 and physical printing
remain **untested**.

## Subsequent hardware-free tests

* Codec: add trace-derived fixtures and measured page geometry when available.
  Round trips and reference agreement can preserve a shared reverse-engineering
  mistake; neither proves printer acceptance.
* Capacity and input safety: truncated status at every field boundary, fuzzed
  internal records and their bounded-memory behavior; extend existing arithmetic,
  dimensions/stride/parameter and compression-capacity tests as APIs grow.
* Model: extend current synthetic scripts with independently captured printer
  traces and any newly observed status/error semantics. The implemented tests
  establish internal sequencing and failure behavior only.
* Windows without printer: real ARM64 binaries loaded in a diagnostic queue,
  normal application printing, PrintTicket settings, band output, job cleanup,
  spooler restart and callback fault handling. A fake device cannot pass the
  hardware transport gate.

## Physical device gate

Record the actual Windows build, protected-print setting, kit/compiler versions,
printer label/firmware if available, USB descriptors, interface/endpoints,
IEEE 1284 string, hardware IDs and binding. Do not invent VID/PID or use a
calculated USBPRINT ID as a substitute for observed identity.

First validate identity/status with finite timeouts and one device owner.
Then record first-page output, startup after power cycle, repeated and multipage
jobs, A4/Letter and printable margins, blank/text/grayscale patterns, orientation,
copies, empty tray, cancel, cable removal, reconnect, sleep/resume and reboot.
Use supported ordinary paper initially; media/fuser modes require specific
validation before exposing them as supported settings.

Cancellation and reconnect must leave the queue usable without restarting
scripts. Keep trace evidence for page-received/printed/completed transitions;
host write completion is insufficient evidence of physical output. Test bounded
timeouts with the printer unavailable and the spooler remaining responsive.

## Installer and fallback gates

Test installation and uninstall on a clean Windows ARM64 system under normal
security settings, signed catalog verification, USB re-enumeration on another
port, policy-block messages, queue persistence, upgrades and rollback. End users
must print through normal dialogs without terminals. Developer test-signing
is not a production deployment strategy.

For IPP fallback, verify accepted formats/capabilities from Windows and
server-side raster conversion, discovery/manual queue addition, status,
cancellation, host loss and real paper output. A CUPS shared queue, an IPP
class queue and Mopria certification are separate claims.
