#!/usr/bin/env python3
"""Build app-private modern-ABI userlandfs host from matching Haiku/Renku sources.
The kernel addon remains the official userland_fs package. Run under setarch x86
on hybrid Haiku, natively on x86_64; source files retain their original MIT licenses.
"""
import pathlib, subprocess, concurrent.futures, argparse, platform
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=pathlib.Path)
p.add_argument('--output',type=pathlib.Path,default=pathlib.Path('deps/userland-runtime'))
a=p.parse_args(); source=a.source.resolve(); top=source/'src/add-ons/kernel/file_systems/userlandfs'; out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
h=source/'headers'
flags=['-O1','-fPIC','-D_DEFAULT_SOURCE','-DUSER=1','-DBUILDING_USERLAND_FS_SERVER=1','-D_FILE_OFFSET_BITS=64','-DPACKAGE_VERSION="2.9.9"']
for d in [source/'build/user_config_headers',source/'build/config_headers',h/'private',top/'server',top/'server/fuse',top/'shared',top/'private',h/'private/userlandfs',h/'private/userlandfs/private',h/'private/userlandfs/shared',h/'private/userlandfs/fuse',h/'private/kernel',h/'private/kernel/arch/x86',h/'private/shared',h/'private/libroot',h/'private/system',h/'private/fs_shell',h/'private/storage',h/'private/app',h/'private/system/arch'/('x86_64' if platform.machine()=='x86_64' else 'x86')]:
 flags += ['-iquote' if d in [h/'private/userlandfs/private',h/'private/userlandfs/shared'] else '-I', str(d)]
server='AreaSupport Debug LazyInitializable ObjectTracker Port Request RequestAllocator RequestHandler RequestPort Requests SingleReplyRequestHandler String FileSystem kernel_emu main RequestThread ServerDefs UserlandFSServer UserlandRequestHandler Volume'.split()
fuse='RWLockManager.cpp fuse_config.c fuse_fs.cpp fuse_main.cpp fuse_opt.c fuse_signals.c helper.c FUSEFileSystem.cpp FUSELowLevel.cpp FUSEVolume.cpp mime_ext_table.c'.split()
def locate(name):
 for d in [top/'server',top/'server/fuse',top/'shared',top/'private',top.parent/'shared',source/'src/kits/shared']:
  if (d/name).exists():return d/name
 raise FileNotFoundError(name)
def compile(name):
 src=locate(name);obj=out/(name+'.o')
 if name == 'RWLockManager.cpp':
  text = src.read_text()
  old = 'if (lockable->fReaderCount == 0 && lockable->fWaiters.IsEmpty())'
  assert text.count(old) == 3
  text = text.replace(old, 'if (lockable->fOwner < 0 && lockable->fReaderCount == 0 && lockable->fWaiters.IsEmpty())')
  old = 'if (lockable->fWaiters.IsEmpty())'
  assert text.count(old) == 3
  text = text.replace(old, 'if ((lockable->fOwner < 0 && lockable->fWaiters.IsEmpty()) || lockable->fOwner == find_thread(NULL))')
  patched = out / name
  if not patched.exists() or patched.read_text() != text: patched.write_text(text)
  src = patched
 if name == 'FUSEVolume.cpp':
  text = src.read_text()
  old = "status_t error = volume->fLockManager.GenericLock(\n\t\t\t\tnextNode == firstNode && writeLock, nextNode);"
  new = "status_t error = volume->fLockManager.GenericLock(\n\t\t\t\tnextNode == firstNode && writeLock, nextNode) ? B_OK : B_ERROR;"
  if old in text:
   text = text.replace(old, new).replace("*_volumeUnlocked = false;\n\n\t\t\tif (error", "*_volumeUnlocked = true;\n\n\t\t\tif (error")
  elif new not in text:
   raise RuntimeError('Review FUSEVolume lock patch for this source version')
  patched = out / 'FUSEVolume.cpp'
  if not patched.exists() or patched.read_text() != text: patched.write_text(text)
  src = patched
 cmd=['gcc' if src.suffix=='.c' else 'g++',*flags,'-c',str(src),'-o',str(obj)]
 stamp = obj.with_suffix('.command')
 signature = repr(cmd)
 if obj.exists() and obj.stat().st_mtime >= src.stat().st_mtime and stamp.exists() and stamp.read_text() == signature: return str(obj)
 r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 if r.returncode:print(r.stdout,flush=True);raise RuntimeError(name)
 stamp.write_text(signature)
 print('built '+name,flush=True);return str(obj)
with concurrent.futures.ThreadPoolExecutor(2) as pool: objs=list(pool.map(compile,[s+'.cpp' for s in server]))
serverpath=out/'userlandfs_server'
subprocess.run(['g++','-Wl,--export-dynamic','-Wl,-soname,_APP_',*objs,'-lbe','-o',str(serverpath)],check=True)
with concurrent.futures.ThreadPoolExecutor(2) as pool: objs=list(pool.map(compile,fuse))
subprocess.run(['g++','-shared','-Wl,-soname,libuserlandfs_fuse.so',*objs,str(serverpath),'-lshared','-lbe','-o',str(out/'libuserlandfs_fuse.so')],check=True)
subprocess.run(['rc','-o',str(out/'runtime.rsrc'),str(top/'server/userlandfs_server.rdef')],check=True)
subprocess.run(['xres','-o',str(serverpath),str(out/'runtime.rsrc')],check=True)
print('Built app-private userlandfs host and FUSE runtime')
