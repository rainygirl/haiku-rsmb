#!/usr/bin/env python3
"""Exercise actual mounted paths. Supply a dedicated writable test share directory."""
import concurrent.futures
import errno
import os
import pathlib
import shutil
import sys
import tempfile

share = pathlib.Path(sys.argv[1])
assert share.is_dir(), share
root = pathlib.Path(tempfile.mkdtemp(prefix='rsmb-volume-test-', dir=share))
try:
    folder = root / '폴더 with spaces'
    folder.mkdir()
    p = folder / '한글.txt'
    payload = bytes(range(256)) * 1027
    p.write_bytes(payload)
    assert p.read_bytes() == payload
    with p.open('r+b', buffering=0) as f:
        f.seek(65530)
        f.write(b'R SMB random write')
        f.flush()
        os.fsync(f.fileno())
        f.seek(65530)
        assert f.read(18) == b'R SMB random write'
        f.truncate(131071)
    assert p.stat().st_size == 131071
    before = p.read_bytes()
    try:
        fd = os.open(p, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except OSError as e:
        assert e.errno == errno.EEXIST, e
    else:
        os.close(fd)
        raise AssertionError('O_EXCL overwrote an existing file')
    assert p.read_bytes() == before
    renamed = folder / 'renamed.txt'
    p.rename(renamed)
    assert not p.exists() and renamed.read_bytes() == before
    replacement = folder / 'editor-save.tmp'
    replacement.write_bytes(b'atomic editor save\n')
    os.replace(replacement, renamed)
    assert renamed.read_bytes() == b'atomic editor save\n'
    # Mounted -> local -> mounted copy uses ordinary file APIs.
    with tempfile.TemporaryDirectory() as tmp:
        local = pathlib.Path(tmp) / 'copy.txt'
        shutil.copyfile(renamed, local)
        shutil.copyfile(local, folder / 'roundtrip.txt')
        assert (folder / 'roundtrip.txt').read_bytes() == local.read_bytes()
    try:
        folder.rmdir()
    except OSError as e:
        assert e.errno in (errno.ENOTEMPTY, errno.EEXIST), e
    else:
        raise AssertionError('removed a nonempty directory')
    def simultaneous(i):
        f = root / ('parallel-%d.bin' % i)
        data = bytes([i]) * 140001
        f.write_bytes(data)
        assert f.read_bytes() == data
        f.unlink()
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(simultaneous, range(8)))
    for f in folder.iterdir():
        f.unlink()
    folder.rmdir()
    print('PASS: mounted Unicode paths, read/write/seek/truncate/fsync, O_EXCL, rename, atomic replacement, copy, concurrent files, delete and rmdir')
finally:
    shutil.rmtree(root)
