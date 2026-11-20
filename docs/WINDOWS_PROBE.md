# Native ARM64 connection diagnostic

Implemented 2026-11-16. This is a real Win32 program using Windows USBPRINT
interfaces, not a simulated USB demonstration. **The user verified 0.1 GUI,
worker and USBPRINT identity I/O on the Surface on 2026-11-19. Version 0.1.1
accepted the printer identity and completed a live CAPT status exchange with
the tray empty.** Physical printing still needs verification. Read the
[capture and narrowly scoped exception](CAPTURE_2026-11-19.md).
It is a diagnostic milestone, not a printer driver, queue, installer, rendering
filter or production transport. See [quick start](WINDOWS_QUICK_START.txt).

## What the app does

The GUI starts the same executable in a supervised worker process. No device
access happens on startup. **Check this app** only tests worker launch and its
report pipe. **Inspect USB printers** enumerates present
`GUID_DEVINTERFACE_USBPRINT` interfaces using SetupAPI, records device instance,
parent, hardware IDs, service and interface path, then requests the IEEE 1284 ID.
Paths are used as returned by Windows; hardware IDs are not invented or parsed
out of symbolic paths. These properties are not full USB descriptors or an
exhaustive list of child `USBPRINT` PnP nodes. Capture those separately before
writing a matching printer INF.

Each device is opened with `CreateFileW`, `GENERIC_READ | GENERIC_WRITE`,
share mode zero and `FILE_FLAG_OVERLAPPED`. Exclusive access deliberately fails
if a current owner or driver permissions prevent it. There is no shared-access
fallback, driver rebinding, spooler stop, elevation or reset.

`IOCTL_USBPRINT_GET_1284_ID` has no input buffer and uses a 4,094-byte output
buffer, following Microsoft's documented device-size limitation. Only the
actual completed byte count is parsed. Raw bytes are included in the report
before parsing so unsupported identities remain diagnosable.

**Check Canon status** repeats identification, retains the verified target's
exclusive handle, and sends only `A8 A0 04 00` (CAPT `0xA0A8`). It requires
exactly one accepted device and no unresolved enumeration/identity errors.
This gate is conservative; an unrelated busy printer can block the check.
After a bounded, ordinary binary-length reply, the portable checked status
decoder displays flags and page counters. Completed USB read/write chunks are
logged, including chunks received before a later framing error. These are
application I/O boundaries, not USB bus packet boundaries. No BCD inference,
initialization sequence, job start, page data, fire, soft reset or retry exists.

An awake or uninitialized printer may not answer this standalone query.
Failure is evidence for investigation; it does not by itself rule out CAPT
printing. Success proves one exchange, not a working printer driver.

## Identity policy and portable tests

By default, `decode_device_id` accepts bounded printable ASCII fields with semicolon
termination and unambiguous manufacturer/model. Aliases and ASCII case are
normalized. Duplicate identity fields, malformed lengths, missing fields,
truncation, embedded control bytes and trailing nonzero garbage are rejected.
The two-byte length must equal the text extent including its two-byte prefix;
an optional trailing NUL or NUL padding is outside that extent. Both byte orders
are accepted only on this exact consistency check and the interpretation is
reported. Microsoft documents the two-byte prefix but does not specify its
byte order on the linked API page. This is an explicit diagnostic policy,
not a claim that every USBPRINT implementation returns this convention.
An unsupported convention must be captured, reviewed and tested before adding
it. The explicit `canon_usb_04a9_271a` policy now handles the captured duplicated
length bytes and complete unterminated final DES field under the constraints
in [the capture audit](CAPTURE_2026-11-19.md). No fallback simply searches
arbitrary bytes for a model substring.

The narrow allow-list is manufacturer `Canon`, `Canon Inc` or `Canon Inc.` and
model `LBP6000`, `LBP6000B` or the reference's registered `LBP6000/LBP6018`.
Standalone LBP6018 and prefix/suffix matches are not accepted. The combined
name comes from `captdriver/src/prn_lbp2900.c`, not a new compatibility claim.
The ID is self-reported, not cryptographic device authentication. Tests cover
aliases, both lengths, NUL conventions, every truncation of accepted fixtures,
unknown/similar models, duplicate fields and all 65,536 possible length prefixes
for a fixed response. Version 0.1.1 additionally replays the user's real identity
capture and rejects its malformed variants. The status suite now includes the captured A0A8 response.

## Timeouts, cancellation and process lifetime

Each identity exchange and the entire CAPT write/reply exchange share a
five-second deadline based on `GetTickCount64`. Pending I/O waits use event
objects, `GetOverlappedResult` and short waits against the remaining deadline.
On failure while pending, `CancelIoEx` is requested and completion is drained
before the stack's `OVERLAPPED`, event or transfer buffers can be destroyed.
Cancellation is not treated as completion.

**That drainage can block in a malfunctioning kernel/device stack.** Even
submission or `CreateFileW` can stall. This is why the helper implements the
primitive diagnostic `Transport`, not the stronger `SessionTransport` contract.
Do not load it in the spooler or use it as a production session adapter.

The GUI starts the child suspended, assigns it to a
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` job, then resumes it. Its inherited handles
are explicitly restricted to the report pipe and NUL input. The GUI reads only
available report bytes, at most 8 KiB per 100 ms tick, with a 256 KiB report cap.
The parent requests worker termination on Cancel, close, report failure or a
30-second overall timeout. It observes termination asynchronously and prevents
another check until the worker actually exits. Windows may still delay process
teardown while pending kernel I/O completes; the app does not promise otherwise.
Closing the job requests cleanup without a blocking UI join.

Reports are local, manually saved text; the app has no upload/network code.
Device paths and serials are intentionally available for diagnosis, so review
reports before sharing. Non-ASCII UTF-16 device properties are escaped.

## Build and package from Ubuntu

The optional toolchain script downloads upstream **llvm-mingw 20261104 UCRT**
(LLVM 23.1.5) and verifies the pinned SHA256 supplied by the upstream release:

`919380403de87dca3c581c2053230cd298af5e891286628ba0613b7fed370926`

Requires Python 3.12+ for tar extraction filters, CMake and a working Linux build
tool. The default CMake build never downloads anything. From the project root:

```sh
python3 tools/fetch_llvm_mingw.py
cmake -S . -B build/arm64-cross \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/llvm-mingw-arm64.cmake \
  -DLLVM_MINGW_ROOT="$PWD/build/toolchains/llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64"
cmake --build build/arm64-cross --parallel
python3 tools/package_windows.py --build build/arm64-cross \
  --toolchain build/toolchains/llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64
```

The package is `build/deliverables/CAPT-Connection-Check-ARM64-0.1.1.zip`. It contains
the GUI app, compiled developer tests, corresponding project/reference source,
runtime license notices, a PE/import audit and file hashes. The C++ runtime is
statically linked; import inspection permits only reviewed Windows system DLLs
and inbox UCRT API sets. All eight executables are checked for PE machine
`IMAGE_FILE_MACHINE_ARM64` (`0xAA64`). This is native ARM64, not ARM64EC.
These compiler/header libraries are an independent toolchain, not the WDK.
No WDK package validation or signing is implied. The binary is unsigned.

The ordinary Visual Studio ARM64 CMake commands in [DEVELOPMENT.md](../DEVELOPMENT.md)
also include the GUI and worker test on Windows, but MSVC/SDK compilation has
not been verified here. `usbprint.h` must be available in the selected kit.
Do not link llvm-mingw C++ objects into MSVC C++ components; rebuild all objects
with the target toolchain. A future WDK spooler component needs its own proof.

## Windows acceptance checks still required

On the Surface, run the six core tests and `test_windows_worker` natively.
The worker test launches the GUI executable's hardware-free child modes,
verifies exact report bytes, repeats launch/cleanup, rejects overlapping starts,
terminates a deliberately sleeping child, and checks a missing executable.
It makes no USB calls. For developers using the extracted binary package:

```bat
developer-tests\test_protocol.exe
developer-tests\test_raster.exe
developer-tests\test_hiscoa.exe
developer-tests\test_status.exe
developer-tests\test_session.exe
developer-tests\test_device_id.exe
developer-tests\test_windows_worker.exe "%CD%\CAPT-Connection-Check.exe"
```

For a native Windows build, `ctest --test-dir build/arm64 -C Debug --output-on-failure`
runs all seven. Do not run CTest against the Windows cross-build on Linux;
those executables have not run here and Wine is not used.

Manually verify launch without administrator privileges, no-printer result,
keyboard navigation/scaling, cancellation/close while checking, report saving,
power off/unplug during reads, occupied handles and a successful identity/status
exchange. Keep actual results distinct from compile results. Test ARM64 DLL
loading, job-object behavior and device access permissions on the real target.

Even after these pass, rendering, an installed queue, the language/port monitor
path, a bounded production USB adapter, package signing and physical printing
remain separate acceptance gates.

## API and toolchain sources

* [Microsoft USB printer driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/usb-printing): USBPRINT/USBMON relationship.
* [Programming considerations for USBPRINT](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/programming-considerations-for-usbprint): monitor access and descriptor limitations.
* [SetupDiGetDeviceInterfaceDetailW](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdigetdeviceinterfacedetailw): interface path retrieval and buffer sizing.
* [IOCTL_USBPRINT_GET_1284_ID](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbprint/ni-usbprint-ioctl_usbprint_get_1284_id): output framing and 4,094-byte limit recommendation.
* [CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex) and [GetOverlappedResult](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult): cancellation and completion lifetime.
* [Job objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects) and [handle inheritance attributes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute): worker lifetime and inherited-handle restriction.
* [llvm-mingw release 20261104](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261104): pinned cross-compiler distribution.

Consulted 2026-11-16. API availability is not evidence of printer compatibility.
