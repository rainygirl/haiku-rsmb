#!/usr/bin/env python3
"""Temporary SMB2 server (loopback by default) for integration tests (requires impacket)."""
import pathlib
import sys
import argparse
import ipaddress
from impacket.smbserver import SimpleSMBServer
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('root')
parser.add_argument('--auth', action='store_true')
parser.add_argument('--listen', default='127.0.0.1', type=lambda s: str(ipaddress.ip_address(s)))
args = parser.parse_args()
root = pathlib.Path(args.root).resolve()
root.mkdir(parents=True, exist_ok=True)
(root / 'nested').mkdir(exist_ok=True)
(root / 'hello.txt').write_text('Haiku SMB integration test\n한글 파일 전송 확인\n')
(root / 'nested' / '한글.txt').write_text('Nested Unicode file\n')
authenticated = args.auth
server = SimpleSMBServer(listenAddress=args.listen, listenPort=1445 if authenticated else 445)
if authenticated:
    from impacket.ntlm import compute_lmhash, compute_nthash
    server.addCredential('smbtest', 0, compute_lmhash('test-only-password'), compute_nthash('test-only-password'))
server.addShare('TEST', str(root), 'Temporary Haiku SMB integration fixture')
server.setSMB2Support(True)
server.setLogFile(str(root.parent / (root.name + '-server.log')))
# Impacket 0.12 sends command-specific bodies for some failing compound
# QUERY_INFO/CLOSE operations. SMB2 requires an ERROR body instead. Normalize
# these test-server responses; the client parser remains strict.
from impacket import smb3structs
from impacket.smbserver import SMB2Commands
# Upstream 0.12 extends files by writing a final NUL but never shrinks them.
# Use the host's real truncate for this isolated test server.
import inspect
import textwrap
import impacket.smbserver as server_module
source = textwrap.dedent(inspect.getsource(SMB2Commands.smb2SetInfo))
old = "if infoRecord['EndOfFile'] > 0:\n                        os.lseek(fileHandle, infoRecord['EndOfFile'] - 1, 0)\n                        os.write(fileHandle, b'\\x00')"
assert old in source, 'Review fixture patch for this Impacket version'
source = source.replace(old, "os.ftruncate(fileHandle, infoRecord['EndOfFile'])")
namespace = dict(vars(server_module))
exec(compile(source, '<fixture-set-info>', 'exec'), namespace)
SMB2Commands.smb2SetInfo = staticmethod(namespace['smb2SetInfo'])
for command, old in ((smb3structs.SMB2_CLOSE, SMB2Commands.smb2Close), (smb3structs.SMB2_QUERY_INFO, SMB2Commands.smb2QueryInfo), (smb3structs.SMB2_SET_INFO, SMB2Commands.smb2SetInfo)):
    def normalized(conn_id, srv, packet, handler=old):
        commands, packets, status = handler(conn_id, srv, packet)
        if status & 0xC0000000 == 0xC0000000:
            return [smb3structs.SMB2Error()], None, status
        return commands, packets, status
    server._SimpleSMBServer__server.hookSmb2Command(command, normalized)
server.start()
