#!/bin/sh
# Run under `setarch x86` on 32-bit hybrid Haiku; run directly on arm64/x86_64.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$project_dir"
mkdir -p deps lib
archive=deps/libsmb2-v4.0.0.tar.gz
if [ ! -f "$archive" ]; then
    curl -L --fail -o "$archive.tmp" https://github.com/sahlberg/libsmb2/archive/refs/tags/v4.0.0.tar.gz
    mv "$archive.tmp" "$archive"
fi
printf '%s  %s\n' b4d1b13bc07adc68379a72f723b9032a950afd62fed7f2aa3e57f3421406da11 "$archive" | sha256sum -c -
if [ ! -d deps/libsmb2-4.0.0 ]; then tar xzf "$archive" -C deps; fi
python3 tools/patch-libsmb2.py deps/libsmb2-4.0.0
# libsmb2 uses POSIX-positive errno internally. The mapper must be linked into
# the library itself, not just its caller; Haiku's native errno is negative.
cmake -S deps/libsmb2-4.0.0 -B deps/libsmb2-build \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS='-D_DEFAULT_SOURCE -DB_USE_POSITIVE_POSIX_ERRORS -include errno.h' \
    -DCMAKE_DISABLE_FIND_PACKAGE_GSSAPI=ON -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON \
    '-DCORE_LIBRARIES=network;posix_error_mapper'
cmake --build deps/libsmb2-build -j2
cp deps/libsmb2-build/lib/libsmb2.so.4.0.0 lib/libsmb2.so.1
ln -sf libsmb2.so.1 lib/libsmb2.so
