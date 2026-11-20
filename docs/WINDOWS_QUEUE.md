# Installed Windows ARM64 queue

Version 0.3.1 installs a local Windows printer named **Canon LBP6000 CAPT**. The
user confirmed that 0.2 printed the direct-USB A4 test sheet and that its
measured scale was correct. Version 0.3 uses that raster and CAPT session path
through a Windows print queue and applies ordered grayscale dithering. The
Setup menu uses Arabic or English and remembers a manually chosen language.
Technical job logs use English. On November 20, 2026, the project owner
confirmed successful physical printing through the installed 0.3.1 queue on
a Windows 11 ARM64 Surface.

## Print path

Windows applications print into a spooler queue using the inbox Microsoft
Print to PDF driver. Setup uses the Standard TCP/IP port monitor in LPR mode,
with byte counting enabled and SNMP disabled. Its destination is the CAPT
service on loopback port 515. The use of an alternate port for the inbox PDF
renderer was exercised by the successful Surface print. At intake the service
requires ordinary PDF framing and the LPR byte count. See Microsoft's
[TCPMON Xcv commands](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/tcpmon-xcv-commands)
and [RFC 1179](https://www.rfc-editor.org/rfc/rfc1179.html).

The service is installed under the Windows
[LocalService account](https://learn.microsoft.com/en-us/windows/win32/services/localservice-account).
It binds only to 127.0.0.1. It accepts an LPR connection only if Windows' TCP
table identifies the matching local client endpoint as owned by the running
Print Spooler service process. It stages jobs in private, delete-on-close
files, accepts one control and one data file per connection, and discards
control metadata. The print worker renders ordinary PDF into monochrome
bands using PDFium, then uses the tested CAPT session to send them over the
printer's existing USBPRINT binding.

There are three pending service slots and one active USB job. Intake is
acknowledged before the physical result. The Windows queue can therefore show
submission as complete while the job remains pending or still prints. Reported
success in **View last job report** requires CAPT printer completion. The
service retains the latest report and does not retain document files after
the job. Setup's elevated cancellation sends a request for an active USB job,
with bounded cleanup and supervised termination if Windows USB I/O stalls.
Windows controls jobs still held by its spooler. Pending service jobs are
discarded if the service stops. The service never replays an interrupted job.

Setup requires an administrator and uses a dedicated Program Files folder. It
installs and removes its own queue, port and service. It uses the existing
Microsoft PDF renderer and USBPRINT driver.

## Surface validation checklist

The [quick start](WINDOWS_QUEUE_QUICK_START.txt) gives the ordered test steps.
The included HTML exercises the app's text rendering. The two-page PDF has
embedded Arabic fonts, mixed-direction text, a 100 mm line and grayscale chips.

- Install on Windows 11 ARM64. Record OS build and any install error.
- Print one Notepad page through the named queue. Confirm there is no save
  prompt and the CAPT worker reports completion.
- Print two numbered pages from Edge or Word, then send two distinct jobs.
  Inspect quantity, ordering, orientation and margins. Check an image and
  landscape A4. Check Letter with matching paper if available.
- Disconnect USB or run out of paper during a job. Verify a useful report,
  bounded failure and a successful later job after recovery. Exercise the
  Windows queue cancel before dispatch and CAPT cancel after dispatch.
- Reboot and print again. Remove using CAPT Setup, check that the queue, port,
  service and app folder are removed, and confirm the system PDF printer still
  works. Reinstall and run one final job.
