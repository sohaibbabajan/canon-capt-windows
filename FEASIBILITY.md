# Canon LBP6000 / LBP6000B: technical feasibility

Research date: **2026-11-16**. Local reference: `captdriver/`, remote
`https://github.com/muxoc/captdriver-lbp6000.git`, commit
`ca52686e380be289a5339c1e5ef30e52fc82a225` (clean tracked working tree at inspection).
This is an engineering assessment, not a hardware compatibility certificate.

Implementation update: the portable core now has a separate native ARM64
Windows GUI diagnostic using SetupAPI/USBPRINT identity and status I/O. It
cross-compiles successfully on Ubuntu. On 2026-11-19 the user reported GUI,
worker, exclusive USB open and identity-retrieval success on the Surface, then
an identity-parser rejection. Version 0.1.1 accepted the identity and completed one
live CAPT status exchange. The WDK printing pipeline still needs validation. This does not change
the conditional feasibility decision below. See [diagnostic evidence and
limitations](docs/WINDOWS_PROBE.md).

## Decision

**Direct USB is a credible development target, with significant unproven integration work.**
The reference contains a host-side CAPT implementation and Hi-SCoA compressor.
Windows exposes user-mode print integration and ships a USB printer class driver.
There is no evidence here that a new kernel-mode driver is necessary.
The remaining question is whether the actual device's bidirectional transactions
can be carried reliably through Windows' USB printing stack, with bounded waits
and correct job cancellation. That requires a printer and Windows ARM64 testing.

Prototype a **native ARM64 v3 XPSDrv rendering pipeline plus a CAPT language
monitor above the inbox USB port monitor**. Keep all device protocol logic in a
portable library. Make this architecture conditional on an early ARM64 pipeline
smoke test and USB transport probe; do not build an installer around assumptions.
An IPP bridge is the practical fallback. No Wine, emulated driver binaries, or
proprietary Canon DLLs are part of the proposal.

## What the reference actually does

The local source was inspected, including its protocol notes, build files,
model operations, raster filter, compressor, test decoder, PPD and license.

| Area | Local evidence | Behavior and implications |
| --- | --- | --- |
| Integration | `src/rastertocapt.c`, `src/Makefile.am` | Builds `rastertocapt`, a CUPS filter, not a Windows driver or a standalone USB backend. It accepts CUPS raster input. |
| USB communication | `src/capt-command.c` | Writes to stdout in chunks of at most 4096 bytes, flushes, requests CUPS side-channel output draining, and reads through `cupsBackChannelRead`. CUPS's separate USB backend owns enumeration, endpoints and transfers. There is no libusb transport in this tree. |
| Identification | `src/printer.c` | Requests the IEEE 1284 ID via CUPS; matches `MDL`/`MODEL` text. `CAPT_MODEL` can override detection. This override is a diagnostic mechanism, not device identity validation. |
| Framing | `SPECS` §1, `src/capt-command.c` | Four-byte header: little-endian command and total length including header. Replies generally echo the command. The reference reads six bytes first, then additional data. Nested `0xD0A9` packets combine page setup commands. |
| Protocol quirks | `SPECS`, `src/word.h` | Reverse-engineered, incomplete documentation. Some reply lengths reportedly use decimal digits encoded as BCD. `0xA1A0` identification returns raw data, outside ordinary reply framing. Neither quirk should be generalized to every command. |
| LBP6000 path | `src/prn_lbp2900.c`, `src/capt-command.h` | `lbp6000_ops` selects extended-status-only polling and LBP3010-style GPIO handling. Its prologue includes `CAPT_LBP6000_SETUP_0` (`0xE0BA`) with payload `01 00`, before job setup. Much of the page logic is shared with other models. |
| Page data | `src/generic-ops.c` | Compressed data is sent in `0xC0A0` chunks of at most `0xFF00` payload bytes. This helper polls readiness every sixteenth chunk using `capt_wait_ready`; it is not entirely model-dispatched. End-of-page uses `0xC0A4`. |
| Raster | `src/rastertocapt.c`, `src/paper.c`, LBP6000 PPD | CUPS supplies prepared monochrome raster. The filter crops/pads/centers bytes, groups rows into bands, and compresses them; it does not render arbitrary application documents or provide a complete Windows halftoning pipeline. PPD default is 600×600 dpi, 1 bit/channel, 160 rows/band; 600×400 is also advertised. |
| Compression | `src/hiscoa-compress.c`, `src/hiscoa-common.c`, `SPECS` §3 | Hi-SCoA: bit-coded literals, byte stash, LZ-style repeats, XOR `0x43`, band termination and 32-bit padding. Default origins are 1, 4, 0, -7, 0. Compression is largely independent of CUPS. |
| Jobs | `src/rastertocapt.c`, `src/prn_lbp2900.c`, `src/capt-status.c` | Job prologue obtains a printer job number; page setup, status-driven waits, upload, fire, page completion and job end follow. One compressed page is cached for retries. SIGTERM triggers protocol cleanup. Several waits have no overall deadline. |
| Tests | `tests/`, `tests/Makefile.am` | PBM compression/decompression tool and CAPT defilter help inspect output. `check_PROGRAMS` builds tools, but no Automake `TESTS` list runs an automated regression suite. These are useful references, not hardware proof. |

The fork's README reports a Raspberry Pi 3B / Ubuntu 22.04 armv7l / LBP6000B
test. The code still registers `LBP6000/LBP6018` as `EXPERIMENTAL`. That is an
upstream author's claim, not a test performed in this project. The LBP6000B
label does not establish its USB identifiers, firmware behavior or equivalence
to every LBP6000 revision. Capture those from the actual unit.

### Dependencies and portability

`configure.ac` uses Autoconf/Automake and `cups-config --image --libs`; source
uses libcups raster, back-channel and side-channel APIs. CUPS's scheduler,
USB backend and preceding raster conversion filters are runtime dependencies.
The suggested Linux installation also uses the CUPS PPD compiler. POSIX signals,
file descriptors, `sleep`, global mutable state, GCC constructor attributes,
variable-length arrays and process termination do not fit Windows spooler DLLs.
The compressor is ordinary C and can be evaluated separately from the CUPS front end.

### Findings that rule out a blind port

These are code inspection findings, not claims that the reference always fails:

* Job prologues declare an uninitialized `size_t size` and pass it to
  `capt_sendrecv` as a reply capacity. The latter reads it before writing it.
* `capt_sendrecv` reports a length including the four-byte header while copying
  from the payload, and its null-capacity path can copy the total frame length
  from the payload pointer. A replacement needs one explicit payload-length contract.
* `decode_status` has coarse length checks but reads fields through payload byte
  39; not every truncated status length is safe. Transport reads assume particular
  fragmentation and do not provide a general short-read assembler.
* The compressor's bit writer stops at output capacity without returning an
  explicit overflow error. Arithmetic, alignment, termination and worst-case
  sizing need independent tests before reuse in a print service.
* `paper.c` assigns `line_size` from `PageSize[0]` (ordinarily points), rather than
  a conventional byte-stride calculation, and reads a media-size string from
  `MediaType`. The PPD uses `MediaType` for media type. Margins and media/fuser
  values also need unit and model-specific reconciliation. Do not copy these
  values into Windows capabilities as though they were verified dimensions.
* Signal-driven cleanup performs substantial I/O; global job/status/buffer state
  and indefinitely sleeping loops need per-device ownership, cancellation and
  finite deadlines in a DLL or service.

## Licensing and reuse boundaries

`captdriver/COPYING` contains GPL version 3. Main C source headers explicitly
permit GPLv3 **or later**, while README summarizes the project as GPLv3. Some
test/PPD files lack equivalent per-file license wording; preserve their notices
and audit provenance before redistributing them separately.

Reusing or adapting GPL-covered implementation requires a GPL-compatible
distribution with applicable notices, license text and corresponding source,
including necessary build material. A DLL or service boundary alone is not a
license exemption. New core files here use `GPL-3.0-or-later` to keep a future
source port possible; this does not relicense the reference. No claim is made
that this is a clean-room implementation. See the [GPLv3 text](https://www.gnu.org/licenses/gpl-3.0.html)
and [GNU licensing FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#SystemLibraryException).

Windows system libraries can fall under the GPL system-library exception;
that does not automatically cover redistributing SDK code or every Microsoft
sample. Check each sample license, runtime redistribution terms, and package
terms before distribution. Do not bundle Canon's proprietary binaries or
branding assets. A source-compatible GPL port is the current technical plan;
a proprietary distribution would need a separate rights assessment.

| Component | Reuse approach | Windows-specific replacement |
| --- | --- | --- |
| CAPT commands/status knowledge | Preserve provenance and build trace fixtures from observed behavior | Safe framing, length validation, explicit quirks, per-device session and job state machine |
| Hi-SCoA codec | Best candidate for a small audited C source port, after boundary/differential tests | Defined capacity errors and fixed-width arithmetic; no CUPS dependency |
| Raster geometry and media tables | Reference only until units and actual device limits are reconciled | Windows document rasterization, grayscale/halftoning, packing, margins and capabilities |
| USB backend | No USB backend implementation to reuse here | Inbox USB printing adapter first; WinUSB-bound adapter only if necessary |
| Filter/job integration | Use sequencing as a reference, not CUPS control flow | Spooler callbacks, PrintTicket, cancellation, job completion, durable recovery |
| PPD/build/install | Capability hints, not a Windows package | ARM64 DLLs, INF/catalog, pipeline configuration and installer |

## Windows architecture assessment

Native ARM64 driver development is supported by the WDK. The supported build
path requires Windows tooling: Visual Studio 2022 with ARM64 C++ tools and a
matching Windows SDK/WDK, or the EWDK. Ubuntu is suitable for the portable core,
tests and source preparation; it does not replace Windows package validation
or target testing. Build Windows extensions as ARM64, not ARM64EC, x86 or x64.
See [Microsoft's ARM64 WDK guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/building-arm64-drivers).

| Option | Assessment |
| --- | --- |
| v3 user-mode XPSDrv + CAPT language monitor + inbox USB monitor | Preferred direct-USB prototype. XPSDrv provides rendering/configuration integration; language monitors support duplex printer job control. Needs an ARM64 sample build/install proof, then real CAPT USB validation. |
| v3 Unidrv + OEM rendering plug-in + monitor | Viable candidate if the XPSDrv proof fails. Reduces some raster plumbing but requires a different plug-in/graphics contract and an ARM64 availability/build proof. Do not implement both first. |
| v4 XPSDrv + ordinary USB output | Not selected: non-Microsoft language/port monitors are unsupported. Bidi XML/JavaScript support exists, but evidence is missing that it can own CAPT's complete stateful printing conversation. Producing CAPT bytes alone does not solve that requirement. |
| IPP class driver + conventional PSA | Preferred Microsoft direction for IPP devices. The CAPT reference provides no IPP endpoint; a PSA alone does not make this legacy printer speak IPP. |
| PSA v4 virtual printer + native desktop USB broker | Real newer API worth a focused feasibility spike. It can register a software queue without a legacy driver. Device access, application lifecycle, broker IPC, capabilities and job-status mapping remain work. UWP USB APIs exclude printer class `0x07`, so direct UWP printer access must not be assumed. |
| Local Windows IPP-to-CAPT service | Could preserve USB locality and use the inbox IPP queue. Requires an actual IPP implementation, accepted PDL rendering, native USB access, service/queue lifecycle and loopback discovery/install validation. More than a small protocol shim. |
| Linux CUPS IPP bridge | Fastest reuse path: Windows sends a supported document format to Linux; Linux converts and runs `rastertocapt` and its USB backend. Requires an always-on host attached to the printer. No CAPT Windows driver is needed. |

Sources: [XPSDrv model](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/xpsdrv-printer-driver),
[language monitors](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/language-monitors),
[v4 connectivity restrictions](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/v4-driver-connectivity-architecture),
[PSA v4 virtual printers](https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/print-support-app-v4-design-guide),
[virtual-printer API requirements](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.printing.workflow.printworkflowvirtualprintersession?view=winrt-26100),
[UWP USB restrictions](https://learn.microsoft.com/en-us/uwp/api/windows.devices.usb?view=winrt-26100).
PSA API versions are separate from legacy v3/v4 driver model versions. Check the
exact Surface OS build and SDK API contracts before choosing the PSA option.

For the network fallback, [CUPS sharing](https://openprinting.github.io/cups/doc/sharing.html)
documents IPP sharing and discovery. OpenPrinting's
[Printer Applications guidance](https://openprinting.github.io/cups/drivers.html)
also explains the move away from legacy PPD/filter drivers. Use an explicitly
supported CUPS 2.x environment for the existing filter; assess a Printer
Application wrapper for a longer-lived bridge. Neither configuration removes
the need to test Windows format negotiation and real printer behavior.

### USB APIs and ownership

For the preferred path, keep `usbprint.sys` bound to a matching printer-class
interface. The CAPT language monitor receives the underlying monitor's
`MONITOR2` callbacks through `OpenPortEx`; it uses `WritePort`/`ReadPort` for
transactions. Its entry point is `InitializePrintMonitor2`; job boundaries use
`StartDocPort`/`EndDocPort`. Validate timeout and cancellation behavior in the
actual inbox monitor rather than assuming all callbacks support overlapped I/O.
See [monitor functions](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/language-monitor-functions)
and [monitor interaction](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/language-and-port-monitor-interaction).

A desktop diagnostic/custom-transport spike can enumerate device interfaces
with SetupAPI, open an authorized device path with `CreateFileW`, and evaluate
`ReadFile`, `WriteFile` and `DeviceIoControl` on that handle. The documented
`IOCTL_USBPRINT_GET_1284_ID` retrieves identity through USBPRINT. Direct handle
access, sharing rights and read cancellation are validation items, not a
guaranteed bypass of the spooler. Never race two owners on one device.
See [USB printer binding](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/printer-connected-to-a-usb-port)
and [IEEE 1284 IOCTL](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbprint/ni-usbprint-ioctl_usbprint_get_1284_id).

If the USB printing stack cannot expose required behavior, a desktop service
could instead own a WinUSB-bound interface. APIs include `WinUsb_Initialize`,
`WinUsb_QueryInterfaceSettings`, `WinUsb_QueryPipe`, `WinUsb_ReadPipe`,
`WinUsb_WritePipe`, `WinUsb_ControlTransfer`, pipe policies and abort/reset.
WinUSB requires the appropriate device binding and interface GUID; it is not
a library that can freely access an interface still owned by USBPRINT.
Changing binding changes normal printer enumeration and needs a separate
queue/installer design and rollback. Do not assume this old device advertises
Microsoft descriptors for automatic WinUSB installation, or guess endpoints.
See [WinUSB installation](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb-installation)
and [WinUSB communication](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/using-winusb-api-to-communicate-with-a-usb-device).

### Rendering, installation and policy

An XPSDrv filter uses `IPrintPipelineFilter` and PrintTicket/PrintCapabilities;
the XPS rasterization service offers `IXpsRasterizationFactory` and
`IXpsRasterizer::RasterizeRect`. WIC bitmaps still need alpha composition,
grayscale conversion, halftoning and CAPT-compatible stride/band preparation.
The factory is provided by the filter pipeline property bag, not an arbitrary
standalone COM factory. See [rasterization factory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/xpsrassvc/nn-xpsrassvc-ixpsrasterizationfactory)
and [rasterizer output](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/xpsrassvc/nf-xpsrassvc-ixpsrasterizer-rasterizerect).

Future packages need verified hardware IDs, `NTarm64` INF model sections,
proper DLL/pipeline/monitor registration, a catalog, signature validation and
install/uninstall tests. Use WDK InfVerif, Inf2Cat and SignTool, then appropriate
HLK validation and release signing assessment. No guessed hardware IDs or
placeholder installable INF are supplied. Microsoft warns that USBPRINT IDs
should be retrieved from the actual system, not calculated from model text:
[USBPRINT identifiers](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/identifiers-generated-by-usbprint-sys).

As of this research date, Windows still permits separately supplied legacy
printer packages. Microsoft has restricted new Windows Update submissions
since January 15, 2026 and now prefers the inbox IPP driver in ranking. Native
ARM64 packages have a case-reviewed exception path, not automatic approval.
The published next servicing restriction is July 1, 2027. Distribution and
signing are project risks distinct from whether the protocol can print.
See [servicing plan](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/end-of-servicing-plan-for-third-party-printer-drivers-on-windows)
and [submission exceptions](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/legacy-printer-driver-submission-process).

The proposed legacy driver path requires **Windows protected print mode off**;
that mode excludes third-party drivers. The installer must explain a policy
block rather than silently changing security settings. A generic CUPS queue
or home-built IPP bridge must not be claimed Mopria-certified or guaranteed
compatible with protected mode. See [protected print constraints](https://learn.microsoft.com/en-us/windows/modern-print/windows-protected-print-mode/more-information-on-windows-protected-print-mode-for-enterprises-and-developers).

## Difficulties and stop/go evidence

The largest risks are stateful CAPT sequencing, incomplete status semantics,
USB short transfers and reply lengths, page geometry/media correctness, Windows
ARM64 pipeline behavior, cancellation and recovery, and deployable signing.
Compression has substantial reference material; queue integration is a separate
body of work. Expect staged engineering and hardware iteration, not a recompile.

| Gate | Required evidence |
| --- | --- |
| Portable foundation | Byte-exact command fixtures, malformed-input rejection, mocked fragmented I/O, raster packing tests; no hardware needed |
| Codec/model readiness | Audited compressor and decoder checks, captured/curated fixtures, status and job-state tests with deadlines and cancellation |
| Windows pipeline | ARM64 build and no-device queue test from a normal application, verified raster/PrintTicket output; no successful printer claim |
| Transport | Actual descriptors, USBPRINT/WinUSB binding, ID, bounded status exchanges and disconnect behavior on Surface |
| First physical page | Trace-backed job startup, page data and confirmed paper output; inspect size, orientation and clipping |
| Ordinary installed printer | Repeated/multipage jobs from multiple applications, queue errors/cancel/reconnect/reboot, GUI installation and clean uninstall, release package verification |

The initial implementation slice covered framing with mock transport and a
grayscale-to-monochrome row primitive. The subsequent portable codec/band
milestone now adapts the audited Hi-SCoA encoder, with 1,042 bands matching the
reference byte-for-byte and a separate test decoder. This does not close any
Windows or hardware gate. See the [codec audit](docs/HISCOA.md) and the roadmap
in [DEVELOPMENT.md](DEVELOPMENT.md).

The portable session milestone subsequently adds checked extended status,
reference-derived startup/page control, in-order band transfer, deadline-aware
transport contracts, cancellation and bounded cleanup. Five Linux CTest suites
pass in Debug, Release and ASan/UBSan builds. Session scripts are synthetic;
actual printer behavior and all Windows gates remain unverified. The
[session audit](docs/SESSION.md) records deliberate differences from the reference.
