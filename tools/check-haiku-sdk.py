#!/usr/bin/env python3
"""Syntax/type check against a Haiku SDK on a case-insensitive macOS host.

Usage: python3 tools/check-haiku-sdk.py --sdk SDK --smb-include LIBSMB2/include
SDK must contain sysroot/ and cross-tools-arm64/ (GCC 13.3 headers).
This does not link an executable or substitute for running the app on Haiku.
"""
import argparse
import json
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sdk", type=pathlib.Path, required=True)
parser.add_argument("--smb-include", type=pathlib.Path, required=True)
parser.add_argument("--clang", default="/opt/homebrew/opt/llvm/bin/clang++")
args = parser.parse_args()
headers = args.sdk.resolve() / "sysroot/boot/system/develop/headers"
cxx = args.sdk.resolve() / "cross-tools-arm64/aarch64-unknown-haiku/include/c++/13.3.0"
if not headers.is_dir() or not cxx.is_dir():
    parser.error("SDK is missing Haiku or C++ headers")
with tempfile.TemporaryDirectory(prefix="haiku-smb-sdk-") as temp:
    overlay = pathlib.Path(temp) / "headers.json"
    overlay.write_text(json.dumps({"version": 0, "case-sensitive": True, "roots": [
        {"type": "file", "name": "/__haiku/" + str(p.relative_to(headers)),
         "external-contents": str(p)} for p in headers.rglob("*") if p.is_file()
    ]}))
    command = [args.clang, "--target=aarch64-unknown-haiku", "-std=c++17", "-nostdinc++",
               "-ivfsoverlay", str(overlay), "-D_DEFAULT_SOURCE", "-fsyntax-only",
               "-Wall", "-Wextra", "-Werror", "-Wno-multichar", "-Isrc",
               "-I" + str(args.smb_include.resolve())]
    paths = [cxx, cxx / "aarch64-unknown-haiku", headers / "config", headers / "bsd",
             headers / "posix", headers / "os", *sorted((headers / "os").iterdir()), headers]
    for path in paths:
        if path.is_dir():
            command += ["-isystem", str(path).replace(str(headers), "/__haiku")]
    sources = sorted(pathlib.Path("src").glob("*.cpp")) + sorted(pathlib.Path("tests").glob("*.cpp"))
    sources += [pathlib.Path('src/volume/Manager.cpp'), pathlib.Path('src/volume/NetworkAddOn.cpp')]
    command += ['-isystem','/__haiku/os/add-ons/network_settings']
    subprocess.run(command + [str(p) for p in sources], check=True)
    subprocess.run(command + ['-DRSMB_FUSE','-DB_USE_POSITIVE_POSIX_ERRORS','-D_FILE_OFFSET_BITS=64','-isystem','/__haiku/private/userlandfs/fuse','src/volume/FileSystem.cpp'], check=True)
    print("PASS: Haiku SDK syntax/type check for all source and test files")
