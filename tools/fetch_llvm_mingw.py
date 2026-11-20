#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit, pinned developer toolchain download; never called by CMake."""
import hashlib
from pathlib import Path
import tarfile
import urllib.request

NAME = "llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64"
DIGEST = "919380403de87dca3c581c2053230cd298af5e891286628ba0613b7fed370926"
URL = f"https://github.com/mstorsjo/llvm-mingw/releases/download/20261104/{NAME}.tar.xz"


def main():
    root = Path(__file__).resolve().parents[1] / "build" / "toolchains"
    root.mkdir(parents=True, exist_ok=True)
    archive = root / (NAME + ".tar.xz")
    if not archive.exists():
        temporary = archive.with_suffix(".download")
        try:
            urllib.request.urlretrieve(URL, temporary)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != DIGEST:
                raise RuntimeError("Toolchain download digest mismatch")
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != DIGEST:
        raise RuntimeError("Cached toolchain archive digest mismatch")
    with tarfile.open(archive) as source:
        source.extractall(root, filter="data")
    print(root / NAME)


if __name__ == "__main__":
    main()
