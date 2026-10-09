#!/usr/bin/env python3
"""Apply the minimal Haiku platform adaptations to upstream libsmb2 v4.0.0."""
import pathlib
import sys
root=pathlib.Path(sys.argv[1])
p=root/'include/portable-endian.h'
s=p.read_text()
needle='#elif defined(__linux__) || defined(__CYGWIN__)'
haiku='''#elif defined(__HAIKU__)
# include <endian.h>
# include <ByteOrder.h>
# ifndef htobe16
# define htobe16(x) B_HOST_TO_BENDIAN_INT16(x)
# define htole16(x) B_HOST_TO_LENDIAN_INT16(x)
# define be16toh(x) B_BENDIAN_TO_HOST_INT16(x)
# define le16toh(x) B_LENDIAN_TO_HOST_INT16(x)
# define htobe32(x) B_HOST_TO_BENDIAN_INT32(x)
# define htole32(x) B_HOST_TO_LENDIAN_INT32(x)
# define be32toh(x) B_BENDIAN_TO_HOST_INT32(x)
# define le32toh(x) B_LENDIAN_TO_HOST_INT32(x)
# define htobe64(x) B_HOST_TO_BENDIAN_INT64(x)
# define htole64(x) B_HOST_TO_LENDIAN_INT64(x)
# define be64toh(x) B_BENDIAN_TO_HOST_INT64(x)
# define le64toh(x) B_LENDIAN_TO_HOST_INT64(x)
# endif

'''
if '#elif defined(__HAIKU__)' not in s:
 assert needle in s
 p.write_text(s.replace(needle,haiku+needle))
# A failed authentication can leave no pending PDU yet keep the socket open.
# Bound the whole synchronous request, not just individual outstanding PDUs.
p=root/'lib/sync.c'
s=p.read_text()
if 'haiku_request_started' not in s:
 s='#ifdef __HAIKU__\n#include <OS.h>\n#endif\n'+s
 s=s.replace('\ttime_t t = time(NULL);','\ttime_t t = time(NULL);\n#ifdef __HAIKU__\n        bigtime_t haiku_request_started = system_time();\n#endif',1)
 s=s.replace('while (!cb_data->is_finished) {','''while (!cb_data->is_finished) {
#ifdef __HAIKU__
                if (smb2->timeout > 0 && system_time() - haiku_request_started
                    >= (bigtime_t)smb2->timeout * 1000000) {
                        smb2_set_error(smb2, "SMB request timed out");
                        return -1;
                }
#endif''',1)
 p.write_text(s)
# POSIX mounted filesystems need atomic replacement (editors save via temp+rename).
p=root/'lib/libsmb2.c'
s=p.read_text().replace('rn_info.replace_if_exist = 0;', 'rn_info.replace_if_exist = 1; /* RSMB POSIX rename */')
p.write_text(s)
# Preserve errno for synchronous pointer-returning open calls.
p=root/'lib/sync.c'
s=p.read_text()
if 'RSMB preserve open status' not in s:
 start=s.index('static void open_cb(');end=s.index('/*\n * close()',start)
 block=s[start:end].replace('cb_data->ptr = command_data;', 'cb_data->status = status; /* RSMB preserve open status */\n        cb_data->ptr = command_data;')
 block=block.replace('ptr = cb_data->ptr;', 'ptr = cb_data->ptr;\n        if (ptr == NULL) errno = cb_data->status < 0 ? -cb_data->status : EIO;')
 s=s[:start]+block+s[end:]
 p.write_text(s)
# MS-SMB2 3.2.5.3: guest and anonymous sessions have no session key, so the client
# must not sign. macOS requires signing and rejects a guest tree connect signed
# with a key derived from the empty password.
p=root/'lib/libsmb2.c'
s=p.read_text()
if 'RSMB guest session' not in s:
 needle='        if (rep->session_flags & SMB2_SESSION_FLAG_IS_ENCRYPT_DATA) {'
 assert needle in s
 s=s.replace(needle,'''        if (rep->session_flags & (SMB2_SESSION_FLAG_IS_GUEST | SMB2_SESSION_FLAG_IS_NULL)) {
                smb2->sign = 0; /* RSMB guest session */
                smb2->seal = 0;
        }
'''+needle,1)
 needle='        if (smb2->sign || smb2->seal || smb2->dialect == SMB2_VERSION_0311) {'
 assert needle in s
 s=s.replace(needle,'        if (!(rep->session_flags & (SMB2_SESSION_FLAG_IS_GUEST | SMB2_SESSION_FLAG_IS_NULL)) &&\n            (smb2->sign || smb2->seal || smb2->dialect == SMB2_VERSION_0311)) {',1)
 p.write_text(s)
# Rename converted '/' to '\' in the request header instead of the new name, so
# subfolder targets went out with '/' (macOS answers EINVAL) and names longer than
# 16 UTF-16 units wrote past the 32-byte header buffer.
p=root/'lib/smb2-cmd-set-info.c'
s=p.read_text()
if 'RSMB rename separators' not in s:
 old='''                        /* Convert '/' to '\\' */
                        for (i = 0; i < name->len; i++) {
                                smb2_get_uint16(iov, i * 2, &ch);
                                if (ch == 0x002f) {
                                        smb2_set_uint16(iov, i * 2, 0x005c);
                                }
                        }

                        len = 20 + name->len * 2;'''
 new='''                        /* Convert '/' to '\\' (RSMB rename separators) */
                        for (i = 0; i < name->len; i++) {
                                uint8_t *c = (uint8_t *)&name->val[i]; /* UTF-16LE */
                                if (c[0] == 0x2f && c[1] == 0) {
                                        c[0] = 0x5c;
                                }
                        }

                        len = 20 + name->len * 2;'''
 assert old in s
 p.write_text(s.replace(old,new,1))
# Kerberos login to a Mac's Local KDC (tools/libsmb2/rsmb-krb5.c), used with
# smb2_set_authentication(SMB2_SEC_KRB5). NTLM stays the default.
import shutil
here=pathlib.Path(__file__).resolve().parent/'libsmb2'
for name in ('rsmb-krb5.c','rsmb-krb5.h'):
 shutil.copyfile(here/name, root/'lib'/name)
p=root/'lib/CMakeLists.txt'
s=p.read_text()
if 'rsmb-krb5.c' not in s:
 assert '            krb5-wrapper.c\n' in s
 p.write_text(s.replace('            krb5-wrapper.c\n','            krb5-wrapper.c\n            rsmb-krb5.c\n',1))
p=root/'lib/libsmb2.c'
s=p.read_text()
if 'rsmb-krb5.h' not in s:
 old='#ifdef HAVE_LIBKRB5\n#include "krb5-wrapper.h"\n#endif'
 assert old in s
 s=s.replace(old,'#include "rsmb-krb5.h"',1)
 keep='#ifdef HAVE_LIBKRB5\n                smb2->sec = SMB2_SEC_KRB5;'
 assert keep in s
 s=s.replace(keep,'@@RSMB_KEEP@@')
 assert s.count('#ifdef HAVE_LIBKRB5')==5
 s=s.replace('#ifdef HAVE_LIBKRB5','#if 1 /* RSMB krb5 */').replace('@@RSMB_KEEP@@',keep)
 p.write_text(s)
