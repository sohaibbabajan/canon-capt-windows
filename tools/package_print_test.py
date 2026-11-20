#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package the ARM64 Windows queue or USB test from a local build and PDFium."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory
import zipfile

def audit(readobj: Path, binary: Path, allowed: set[str], required: str | None = None) -> str:
    metadata = subprocess.check_output([readobj, "--file-headers", "--coff-imports", binary], text=True)
    if "Machine: IMAGE_FILE_MACHINE_ARM64 (0xAA64)" not in metadata:
        raise RuntimeError(f"Expected native ARM64 in {binary}")
    imports = re.findall(r"^  Name: (.+\.(?:dll|drv))$", metadata, flags=re.MULTILINE | re.IGNORECASE)
    if not imports or any(name.lower() not in allowed and not name.lower().startswith("api-ms-win-crt-")
                          for name in imports):
        raise RuntimeError(f"Unexpected imports in {binary}: {imports}")
    if required and required not in {name.lower() for name in imports}:
        raise RuntimeError(f"Missing {required} import in {binary}")
    return f"{binary.name}: ARM64 (0xAA64)\n  Imports: {', '.join(imports)}\n"


def source_archive(root: Path, destination: Path) -> None:
    files = [root / "LICENSE", root / "CMakeLists.txt", root / ".gitignore", *root.glob("*.md")]
    for name in ("printing", "include", "src", "windows", "tests", "docs", "cmake", "tools"):
        files.extend(path for path in (root / name).rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts)
    reference = root / "captdriver"
    if (reference / ".git").exists():
        tracked = subprocess.check_output(["git", "-C", reference, "ls-files", "-z"]).decode().split("\0")
        files.extend(reference / name for name in tracked if name)
    else:
        files.extend(path for path in reference.rglob("*") if path.is_file())
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(files):
            archive.write(path, Path("source") / path.relative_to(root))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--pdfium", type=Path, required=True)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--queue", action="store_true", help="Package the installable Windows queue")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    binaries = args.build / "windows"
    pdfium = args.pdfium / "bin" / "pdfium.dll"
    readobj = args.toolchain / "bin" / "llvm-readobj"
    system = {"kernel32.dll", "user32.dll", "gdi32.dll", "setupapi.dll", "cfgmgr32.dll", "advapi32.dll"}
    report = audit(readobj, pdfium, system)
    if args.queue:
        name = "CAPT-Windows-ARM64-0.3.1"
        quick_start = "WINDOWS_QUEUE_QUICK_START.txt"
        apps = {"CAPT-Setup.exe": binaries / "CaptSetup.exe", "CAPT-Queue.exe": binaries / "CaptQueue.exe"}
        report += audit(readobj, apps["CAPT-Setup.exe"], system | {"shell32.dll", "comctl32.dll", "winspool.drv"},
                        required="winspool.drv")
        report += audit(readobj, apps["CAPT-Queue.exe"],
                        system | {"pdfium.dll", "ws2_32.dll", "iphlpapi.dll", "ole32.dll"}, required="pdfium.dll")
        status = "Windows 11 ARM64 print queue with Arabic and English Setup."
    else:
        name = "CAPT-USB-Print-Test-ARM64-0.2"
        quick_start = "PRINT_TEST_QUICK_START.txt"
        apps = {"CAPT-USB-Print-Test.exe": binaries / "CaptPrint.exe"}
        report += audit(readobj, apps["CAPT-USB-Print-Test.exe"], system | {"pdfium.dll"}, required="pdfium.dll")
        status = "Direct USB test. A prior 0.2 build printed the A4 test page on the Surface."
    version = (args.pdfium / "VERSION").read_text().strip().replace("\n", " ")
    bundle = root / "build" / "deliverables" / (name + ".zip")
    bundle.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix="capt-print-", dir=bundle.parent) as temp:
        stage = Path(temp)
        source = stage / "corresponding-source.zip"
        source_archive(root, source)
        files = {**apps, "pdfium.dll": pdfium, source.name: source,
                 "START-HERE.txt": root / "docs" / quick_start,
                 "LICENSE": root / "LICENSE", "NOTICE.md": root / "NOTICE.md",
                 "third-party-notices/PDFium-LICENSE": args.pdfium / "LICENSE",
                 "third-party-notices/llvm-mingw-LICENSE.TXT": args.toolchain / "LICENSE.TXT"}
        if args.queue:
            files["START-HERE-AR.txt"] = root / "docs" / "WINDOWS_QUEUE_QUICK_START_AR.txt"
            for fixture in ("Arabic-App-Test.html", "Arabic-Print-Test.pdf", "NOTO-FONT-NOTICE.txt"):
                files[f"validation/{fixture}"] = root / "docs" / "fixtures" / fixture
        else:
            from make_test_page import make_test_page
            page = stage / "Test-page.pdf"
            make_test_page(page)
            files[page.name] = page
        for license_file in sorted((args.pdfium / "licenses").iterdir()):
            files[f"third-party-notices/pdfium/{license_file.name}"] = license_file
        for license_file in sorted((args.toolchain / "aarch64-w64-mingw32" / "share" / "mingw32").glob("COPYING*")):
            files[f"third-party-notices/{license_file.name}"] = license_file
        info = stage / "BUILD-INFO.txt"
        info.write_text(f"{status}\nCross-compiled on Ubuntu. Unsigned.\n"
                        f"PDFium {version}\n\n" + report, encoding="utf-8")
        files[info.name] = info
        checksums = "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {relative}\n"
                            for relative, path in sorted(files.items()))
        output = stage / bundle.name
        with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
            for relative, path in sorted(files.items()):
                archive.write(path, f"{name}/{relative}")
            archive.writestr(f"{name}/SHA256SUMS.txt", checksums)
        output.replace(bundle)
    print(bundle)
    print("SHA256:", hashlib.sha256(bundle.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
