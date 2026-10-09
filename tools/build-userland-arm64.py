#!/usr/bin/env python3
"""Cross-build R SMB's private userlandfs runtime against an ARM64 Haiku SDK.
Source must be a matching Haiku source tree (or the runtime source archive).
The output requires guest resources and runtime validation before distribution.
"""
import argparse, concurrent.futures, json, pathlib, subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk', type=pathlib.Path, required=True)
p.add_argument('--source', type=pathlib.Path, required=True)
p.add_argument('--output', type=pathlib.Path, required=True)
p.add_argument('--config-dir', type=pathlib.Path, help='Generated kernel headers from the matching Haiku build')
p.add_argument('--kernel', action='store_true', help='Also build the userlandfs kernel addon; needs matching full kernel headers')
p.add_argument('--lld', type=pathlib.Path, default=pathlib.Path('/opt/homebrew/bin/ld.lld'))
p.add_argument('--llvm', type=pathlib.Path, default=pathlib.Path('/opt/homebrew/opt/llvm/bin'))
a = p.parse_args()
sdk, source, out = a.sdk.resolve(), a.source.resolve(), a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
h = sdk/'sysroot/boot/system/develop/headers'
lib = sdk/'sysroot/boot/system/develop/lib'
syslib = sdk/'sysroot/boot/system/lib'
cxx = sdk/'cross-tools-arm64/aarch64-unknown-haiku/include/c++/13.3.0'
top = source/'src/add-ons/kernel/file_systems/userlandfs'
vfs = out/'headers.json'
vfs.write_text(json.dumps({'version':0,'case-sensitive':True,'roots':[
 {'type':'file','name':'/__haiku/'+str(f.relative_to(h)),'external-contents':str(f)}
 for f in h.rglob('*') if f.is_file()]}))
flags=['--target=aarch64-unknown-haiku','-O1','-fPIC','-D_DEFAULT_SOURCE','-DUSER=1',
 '-DBUILDING_USERLAND_FS_SERVER=1','-D_FILE_OFFSET_BITS=64','-DPACKAGE_VERSION="2.9.9"',
 '-ivfsoverlay',str(vfs),'-I',str(out)]
for d in ['config','bsd','posix','os',*[str(d.relative_to(h)) for d in sorted((h/'os').iterdir()) if d.is_dir()],
 'private','private/userlandfs','private/userlandfs/fuse','private/kernel','private/kernel/arch/arm64',
 'private/shared','private/libroot','private/system','private/fs_shell','private/storage','private/app','private/system/arch/arm64','']:
 flags += ['-isystem','/__haiku/'+d]
for d in [top/'server',top/'server/fuse',top/'shared',top/'private',source/'headers/private/userlandfs/private',source/'headers/private/userlandfs/shared']:
 flags += ['-iquote',str(d)]
cpp=['-std=c++17','-nostdinc++','-isystem',str(cxx),'-isystem',str(cxx/'aarch64-unknown-haiku')]
server='AreaSupport Debug LazyInitializable ObjectTracker Port Request RequestAllocator RequestHandler RequestPort Requests SingleReplyRequestHandler String FileSystem kernel_emu main RequestThread ServerDefs UserlandFSServer UserlandRequestHandler Volume'.split()
fuse='RWLockManager.cpp fuse_config.c fuse_fs.cpp fuse_main.cpp fuse_opt.c fuse_signals.c helper.c FUSEFileSystem.cpp FUSELowLevel.cpp FUSEVolume.cpp mime_ext_table.c'.split()
def locate(name):
 for d in [top/'server',top/'server/fuse',top/'shared',top/'private',top.parent/'shared',source/'src/kits/shared']:
  if (d/name).is_file(): return d/name
 raise FileNotFoundError(name)
def compile(name):
 src=locate(name)
 if name in ('RWLockManager.cpp','FUSEVolume.cpp'):
  text=src.read_text()
  if name=='RWLockManager.cpp':
   old='if (lockable->fReaderCount == 0 && lockable->fWaiters.IsEmpty())'
   assert text.count(old)==3
   text=text.replace(old,'if (lockable->fOwner < 0 && lockable->fReaderCount == 0 && lockable->fWaiters.IsEmpty())')
   old='if (lockable->fWaiters.IsEmpty())'
   assert text.count(old)==3
   text=text.replace(old,'if ((lockable->fOwner < 0 && lockable->fWaiters.IsEmpty()) || lockable->fOwner == find_thread(NULL))')
  else:
   old='status_t error = volume->fLockManager.GenericLock(\n\t\t\t\tnextNode == firstNode && writeLock, nextNode);'
   assert old in text
   text=text.replace(old,old[:-1]+' ? B_OK : B_ERROR;')
   text=text.replace('*_volumeUnlocked = false;\n\n\t\t\tif (error','*_volumeUnlocked = true;\n\n\t\t\tif (error')
  patched=out/name
  if not patched.exists() or patched.read_text()!=text: patched.write_text(text)
  src=patched
 obj=out/(name+'.o')
 cmd=[str(a.llvm/('clang' if src.suffix=='.c' else 'clang++')),*([] if src.suffix=='.c' else cpp),*flags,'-c',str(src),'-o',str(obj)]
 stamp=obj.with_suffix('.command'); signature=repr(cmd)
 if obj.exists() and obj.stat().st_mtime>=src.stat().st_mtime and stamp.exists() and stamp.read_text()==signature:return str(obj)
 r=subprocess.run(cmd,capture_output=True,text=True)
 if r.returncode: raise RuntimeError(name+'\n'+r.stdout+r.stderr)
 stamp.write_text(signature)
 print('built '+name,flush=True)
 return str(obj)
crt = sdk/'cross-tools-arm64/lib/gcc/aarch64-unknown-haiku/13.3.0'
link=[str(a.lld),'-m','aarch64elf','--no-undefined','--no-rosegment','-z','norelro','--eh-frame-hdr','--hash-style=both','-L'+str(lib),'-L'+str(syslib)]
with concurrent.futures.ThreadPoolExecutor(4) as pool: objs=list(pool.map(compile,[s+'.cpp' for s in server]))
host=out/'userlandfs_server'
subprocess.run(link+['-shared','-e','_start','-soname','_APP_','--export-dynamic','-o',str(host),str(lib/'crti.o'),str(crt/'crtbeginS.o'),str(lib/'start_dyn.o'),str(lib/'init_term_dyn.o'),*objs,'-lbe','-lstdc++','-lgcc_s','-lroot',str(crt/'crtendS.o'),str(lib/'crtn.o')],check=True)
with concurrent.futures.ThreadPoolExecutor(4) as pool: objs=list(pool.map(compile,fuse))
subprocess.run(link+['-shared','-soname','libuserlandfs_fuse.so','-o',str(out/'libuserlandfs_fuse.so'),str(lib/'crti.o'),str(crt/'crtbeginS.o'),str(lib/'init_term_dyn.o'),*objs,str(host),'-lshared','-lbe','-lstdc++','-lgcc_s','-lroot',str(crt/'crtendS.o'),str(lib/'crtn.o')],check=True)
print('ARM64 userlandfs runtime linked; guest execution is still required.')

if a.kernel:
 config = a.config_dir.resolve() if a.config_dir else source/'build/config_headers'
 if not (config/'kernel_debug_config.h').is_file():
  p.error('--kernel requires kernel_debug_config.h in --config-dir')
 flags += ['-I',str(config)]
 # Kernel modules must never pull userland libraries or ARM FP/SIMD instructions.
 flags = [f for f in flags if f not in ('-DUSER=1','-DBUILDING_USERLAND_FS_SERVER=1')]
 flags += ['-D_KERNEL_MODE=1','-D_KERNEL=1','-isystem','/__haiku/private/kernel/boot/platform/efi','-mgeneral-regs-only','-ffixed-x18',
           '-fno-stack-protector','-fno-exceptions',
           '-iquote',str(source/'src/system/kernel/device_manager')]
 kernel = 'AreaSupport Debug LazyInitializable ObjectTracker Port Request RequestAllocator RequestHandler RequestPort RequestPortPool Requests SingleReplyRequestHandler String userlandfs_ioctl FileSystem FileSystemInitializer kernel_interface KernelDebug KernelRequestHandler Settings UserlandFS Volume'.split()
 def kernel_source(name):
  for d in [top/'kernel_add_on',top/'private',top/'shared']:
   if (d/name).is_file():return d/name
  raise FileNotFoundError(name)
 locate=kernel_source
 out=out/'kernel';out.mkdir(exist_ok=True)
 with concurrent.futures.ThreadPoolExecutor(4) as pool: objs=list(pool.map(compile,[n+'.cpp' for n in kernel]))
 subprocess.run(link+['-z','max-page-size=4096','-shared','-soname','userlandfs','-o',str(out/'userlandfs'),*objs,
  '-l:libsupc++-kernel.a','-l:libgcc-kernel.a','-l:_KERNEL_'],check=True)
 print('ARM64 userlandfs kernel addon linked; guest kernel compatibility still requires verification.')
