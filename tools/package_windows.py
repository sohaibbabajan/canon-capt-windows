#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package the cross-built diagnostic, source, test binaries and runtime notices.

Does not sign, upload, install, or claim to execute Windows binaries.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--toolchain", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build, toolchain = args.build.resolve(), args.toolchain.resolve()
    destination = root / "build" / "deliverables" / "CAPT-Connection-Check-ARM64-0.1.1"
    destination.mkdir(parents=True, exist_ok=True)
    readobj = toolchain / "bin" / "llvm-readobj"
    executables = [(build / "windows" / "capt_connection_check.exe", "CAPT-Connection-Check.exe")]
    for name in ("protocol", "raster", "hiscoa", "status", "session", "device_id", "lpr"):
        executables.append((build / f"test_{name}.exe", f"developer-tests/test_{name}.exe"))
    executables.append((build / "windows" / "test_windows_worker.exe", "developer-tests/test_windows_worker.exe"))
    audit = []
    for source, relative in executables:
        metadata = subprocess.check_output([str(readobj), "--file-headers", "--coff-imports", str(source)], text=True)
        if "Machine: IMAGE_FILE_MACHINE_ARM64 (0xAA64)" not in metadata:
            raise RuntimeError(f"Not native ARM64: {source}")
        imports = re.findall(r"^  Name: (.+\.dll)$", metadata, flags=re.MULTILINE | re.IGNORECASE)
        allowed = {"kernel32.dll", "user32.dll", "gdi32.dll", "setupapi.dll", "cfgmgr32.dll", "comdlg32.dll", "advapi32.dll"}
        for dependency in imports:
            if dependency.lower() not in allowed and not dependency.lower().startswith("api-ms-win-crt-"):
                raise RuntimeError(f"Unreviewed DLL dependency: {dependency} in {source}")
        if not imports:
            raise RuntimeError(f"Could not audit imports: {source}")
        output = destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, output)
        audit.append(relative + "\n  IMAGE_FILE_MACHINE_ARM64 (0xAA64)\n  Imports: " + ", ".join(imports))
    for name in ("LICENSE", "NOTICE.md"):
        shutil.copy2(root / name, destination / name)
    shutil.copy2(root / "docs" / "WINDOWS_QUICK_START.txt", destination / "START-HERE.txt")
    notices = destination / "third-party-notices"
    notices.mkdir(exist_ok=True)
    shutil.copy2(toolchain / "LICENSE.TXT", notices / "llvm-mingw-LICENSE.TXT")
    for notice in (toolchain / "aarch64-w64-mingw32" / "share" / "mingw32").glob("COPYING*"):
        shutil.copy2(notice, notices / notice.name)
    source_archive = destination / "corresponding-source.zip"
    # Explicit project roots; no caches, .git metadata, user files or toolchain.
    sources = [*root.glob("*.md"), root / "LICENSE", root / "CMakeLists.txt", root / ".gitignore"]
    for directory in ("include", "src", "printing", "tests", "windows", "cmake", "docs", "tools", "captdriver"):
        sources.extend(p for p in (root / directory).rglob("*")
                       if p.is_file() and not any(part in {".git", "__pycache__"} for part in p.relative_to(root).parts))
    with zipfile.ZipFile(source_archive, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(set(sources)):
            archive.write(path, "source/" + path.relative_to(root).as_posix())
    (destination / "BUILD-INFO.txt").write_text(
        "Development diagnostic 0.1.1 (2026-11-19); not a printer driver or installer.\n"
        "Cross-compiled on Ubuntu with llvm-mingw 20261104 / LLVM 23.1.5 UCRT.\n"
        "Toolchain archive SHA256: 919380403de87dca3c581c2053230cd298af5e891286628ba0613b7fed370926\n"
        "All binaries use native ARM64 PE machine type. This 0.1.1 build has NOT run on Windows here.\n"
        "User reports 0.1 GUI/worker/USBPRINT enumeration/open/identity I/O on Surface; no CAPT request sent.\n"
        "0.1.1 adds a tested parser policy for that captured identity. Live CAPT exchange remains unverified.\n"
        "Unsigned. No non-system DLLs required according to static import inspection.\n"
        "Core CTest: 6/6 on Linux Debug, Release and ASan/UBSan with reference comparison.\n"
        "Windows worker test: compiled; NOT executed. See source/docs/WINDOWS_PROBE.md.\n\n" +
        "\n\n".join(audit) + "\n", encoding="utf-8")
    files = sorted(p for p in destination.rglob("*") if p.is_file() and p.name != "SHA256SUMS.txt")
    (destination / "SHA256SUMS.txt").write_text("".join(
        hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.relative_to(destination).as_posix() + "\n" for p in files), encoding="ascii")
    bundle = destination.parent / (destination.name + ".zip")
    with zipfile.ZipFile(bundle, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(destination.rglob("*")):
            if path.is_file(): archive.write(path, destination.name + "/" + path.relative_to(destination).as_posix())
    print(bundle)
    print("SHA256:", hashlib.sha256(bundle.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
