#!/usr/bin/env python3
"""Cross-build ARM64 with a Haiku SDK and upstream libsmb2 v4.0.0.
Run tools/patch-libsmb2.py on the SMB source first. Run from the project root.
The result still needs Haiku xres -o RSMB App.rsrc and guest execution QA.
"""
import pathlib,subprocess,json,concurrent.futures,re,argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk',type=pathlib.Path,required=True)
parser.add_argument('--smb-source',type=pathlib.Path,required=True)
parser.add_argument('--output',type=pathlib.Path,required=True)
parser.add_argument('--clang',required=True)
parser.add_argument('--lld',required=True)
parser.add_argument('--userland-lib',type=pathlib.Path,required=True,help='ARM64 libuserlandfs_fuse.so built from matching Haiku sources')
args=parser.parse_args()
if not args.userland_lib.is_file(): parser.error('ARM64 userlandfs runtime is required for the virtual-volume build')
subprocess.run(['python3','tools/patch-libsmb2.py',str(args.smb_source)],check=True)
base=args.output.resolve().parent; src=args.smb_source.resolve();sdk=args.sdk.resolve();h=sdk/'sysroot/boot/system/develop/headers';lib=sdk/'sysroot/boot/system/develop/lib';syslib=sdk/'sysroot/boot/system/lib';cxx=sdk/'cross-tools-arm64/aarch64-unknown-haiku/include/c++/13.3.0'
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);(out/'lib').mkdir(exist_ok=True)
clang=args.clang;lld=args.lld
vfs=base/'vfs.json';vfs.write_text(json.dumps({'version':0,'case-sensitive':True,'roots':[{'type':'file','name':'/__haiku/'+str(p.relative_to(h)),'external-contents':str(p)} for p in h.rglob('*') if p.is_file()]}))
flags=['--target=aarch64-unknown-haiku','-O2','-fPIC','-D_DEFAULT_SOURCE','-ivfsoverlay',str(vfs)]
for p in [h/'config',h/'bsd',h/'posix',h/'os',*sorted((h/'os').iterdir()),h]:
 if p.is_dir():flags+=['-isystem',str(p).replace(str(h),'/__haiku')]
config=(src/'cmake/config.h.cmake').read_text()
missing={'HAVE_GSSAPI_GSSAPI_H','HAVE_LIBNSL','HAVE_LIBSOCKET','HAVE_SYS_FILIO_H','HAVE_SYS_SYSMACROS_H','HAVE_SYS_VFS_H','MAJOR_IN_MKDEV','MAJOR_IN_SYSMACROS'}
config=re.sub(r'#cmakedefine (\w+)',lambda m:'/* unavailable '+m[1]+' */' if m[1] in missing else '#define '+m[1],config)
(out/'config.h').write_text(config)
sources=re.search(r'set\(SOURCES (.*?)\)',(src/'lib/CMakeLists.txt').read_text(),re.S)[1].split()
def cc(name):
 obj=out/(name+'.o');cmd=[clang,*flags,'-DHAVE_CONFIG_H','-DB_USE_POSITIVE_POSIX_ERRORS','-D_FILE_OFFSET_BITS=64','-D_U_=__attribute__((unused))','-include','errno.h','-I'+str(out),'-I'+str(src/'include'),'-I'+str(src/'include/smb2'),'-c',str(src/'lib'/name),'-o',str(obj)];subprocess.run(cmd,check=True,capture_output=True);return str(obj)
with concurrent.futures.ThreadPoolExecutor(4) as pool: objects=list(pool.map(cc,sources))
crt = sdk/'cross-tools-arm64/lib/gcc/aarch64-unknown-haiku/13.3.0'
link=[lld,'-m','aarch64elf','--no-undefined','--no-rosegment','-z','norelro','--eh-frame-hdr','--hash-style=both','-L'+str(lib),'-L'+str(syslib)]
subprocess.run(link+['-shared','-soname','libsmb2.so.1','-o',str(out/'lib/libsmb2.so.1'),str(lib/'crti.o'),str(crt/'crtbeginS.o'),str(lib/'init_term_dyn.o'),*objects,'-lnetwork','-lposix_error_mapper','-lroot','-lgcc_s',str(crt/'crtendS.o'),str(lib/'crtn.o')],check=True)
cppflags=['-std=c++17','-nostdinc++','-isystem',str(cxx),'-isystem',str(cxx/'aarch64-unknown-haiku'),'-I'+str(src/'include'),'-Isrc','-isystem','/__haiku/os/add-ons/graphics']+flags
app=[]
for name in ['Manager','Location','Discovery','Client']:
 obj=out/(name+'.o');subprocess.run([clang+'++',*cppflags,'-c',('src/volume/Manager.cpp' if name=='Manager' else 'src/'+name+'.cpp'),'-o',str(obj)],check=True);app.append(str(obj))
subprocess.run(link+['-shared','-e','_start','-soname','_APP_','-o',str(out/'RSMB'),str(lib/'crti.o'),str(crt/'crtbeginS.o'),str(lib/'start_dyn.o'),str(lib/'init_term_dyn.o'),*app,'-L'+str(out/'lib'),'-l:libsmb2.so.1','-lbe','-ltracker','-lnetwork','-lbsd','-lstdc++','-lgcc_s','-lroot',str(crt/'crtendS.o'),str(lib/'crtn.o')],check=True)
print('Built ARM64 RSMB and private libsmb2')
# Diagnostic binaries run against the same ARM64 objects and private runtime.
for executable, test, core in [
 ('smb-tool', 'Tool', ['Location', 'Discovery', 'Client']),
 ('core-test', 'CoreTest', ['Location', 'Discovery']),
 ('transfer-test', 'TransferTest', ['Location', 'Discovery', 'Client']),
 ('volume-probe', 'IntegrationProbe', []),
 ('mounted-path-test', 'MountedPathTest', []),
 ('screen-capture', 'ScreenCapture', [])]:
 obj=out/(test+'.o')
 subprocess.run([clang+'++',*cppflags,'-c','tests/'+test+'.cpp','-o',str(obj)],check=True)
 deps=['-l:libsmb2.so.1'] if executable=='smb-tool' else (['-ltranslation'] if executable=='screen-capture' else [])
 subprocess.run(link+['-shared','-e','_start','-soname','_APP_','-o',str(out/executable),str(lib/'crti.o'),str(crt/'crtbeginS.o'),str(lib/'start_dyn.o'),str(lib/'init_term_dyn.o'),str(obj),*[str(out/(n+'.o')) for n in core],'-L'+str(out/'lib'),*deps,'-lbe','-lnetwork','-lbsd','-lstdc++','-lgcc_s','-lroot',str(crt/'crtendS.o'),str(lib/'crtn.o')],check=True)

# FUSE C ABI addon, plus Network preferences addon (symbols supplied by Network).
fuseflags=cppflags+['-DRSMB_FUSE','-DB_USE_POSITIVE_POSIX_ERRORS','-D_FILE_OFFSET_BITS=64','-isystem','/__haiku/private/userlandfs/fuse']
volume=[]
for name in ['FileSystem','Location','Discovery','Client']:
 obj=out/('volume-'+name+'.o')
 source='src/volume/FileSystem.cpp' if name=='FileSystem' else 'src/'+name+'.cpp'
 subprocess.run([clang+'++',*fuseflags,'-c',source,'-o',str(obj)],check=True);volume.append(str(obj))
subprocess.run(link+['-shared','-soname','_APP_','-o',str(out/'rsmb_volume'),str(lib/'crti.o'),str(crt/'crtbeginS.o'),str(lib/'init_term_dyn.o'),*volume,str(args.userland_lib.resolve()),'-L'+str(out/'lib'),'-l:libsmb2.so.1','-lnetwork','-lposix_error_mapper','-lbe','-lstdc++','-lgcc_s','-lroot',str(crt/'crtendS.o'),str(lib/'crtn.o')],check=True)
obj=out/'NetworkAddOn.o'
subprocess.run([clang+'++',*cppflags,'-isystem','/__haiku/os/add-ons/network_settings','-c','src/volume/NetworkAddOn.cpp','-o',str(obj)],check=True)
pluginlink=[x for x in link if x!='--no-undefined']
subprocess.run(pluginlink+['-shared','-o',str(out/'RSMBNetwork'),str(lib/'crti.o'),str(crt/'crtbeginS.o'),str(lib/'init_term_dyn.o'),str(obj),'-lbe','-lstdc++','-lgcc_s','-lroot',str(crt/'crtendS.o'),str(lib/'crtn.o')],check=True)
print('Built ARM64 virtual-volume and Network preferences addons; guest QA still required')
