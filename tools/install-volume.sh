#!/bin/sh
set -eu
cd "$(dirname "$0")"
[ -f RSMB ] || cd ..
root=/boot/home/config/non-packaged
install_file() {
    cp "$1" "$2.new.$$"
    mv -f "$2.new.$$" "$2"
}
if [ ! -f /boot/system/lib/libuserlandfs_fuse.so ] && [ ! -f deps/userland-runtime/kernel/userlandfs ]; then
    echo 'Install the userland_fs package from your Haiku system repository first.' >&2
    exit 1
fi
# Never replace a filesystem while it is mounted.
installed=/boot/home/config/non-packaged/apps/RSMB/RSMB
if [ -x "$installed" ]; then "$installed" --unmount || true; fi
if ./RSMB --is-mounted; then
    echo 'R SMB is in use. Close its files and Tracker windows, then install again.' >&2
    exit 1
fi
./RSMB --unmount
addon="$root/add-ons/userlandfs"
if [ -d /boot/system/lib/x86 ]; then addon="$root/add-ons/x86/userlandfs"; fi
mkdir -p "$root/apps/RSMB/lib" "$addon" "$root/add-ons/Network Settings"
if [ ! -f deps/userland-runtime/userlandfs_server ]; then
    echo 'R SMB needs the app-private patched userlandfs runtime; see README.' >&2
    exit 1
fi
if [ -f deps/userland-runtime/kernel/userlandfs ]; then
    kernel=/boot/system/non-packaged/add-ons/kernel/file_systems
    mkdir -p "$kernel"
    install_file deps/userland-runtime/kernel/userlandfs "$kernel/userlandfs"
fi
install_file RSMB "$root/apps/RSMB/RSMB"
install_file lib/libsmb2.so.1 "$root/apps/RSMB/lib/libsmb2.so.1"
volume_addon=rsmb
if [ -f rsmb_volume ]; then volume_addon=rsmb_volume; fi
install_file "$volume_addon" "$addon/rsmb_volume"
install_file deps/userland-runtime/userlandfs_server "$root/apps/RSMB/userlandfs_server"
install_file deps/userland-runtime/libuserlandfs_fuse.so "$root/apps/RSMB/lib/libuserlandfs_fuse.so"
install_file RSMBNetwork "$root/add-ons/Network Settings/RSMB"
mimeset -f "$root/apps/RSMB/RSMB"
# Tracker already displays the mounted volume. Remove only our legacy shortcut.
mkdir -p '/R SMB Network'
desktop=/boot/home/Desktop/R\ SMB
if [ -L "$desktop" ] && [ "$(readlink "$desktop")" = '/R SMB Network' ]; then
    rm "$desktop"
fi
for previous in 'R FileShare' 'Haiku SMB'; do
    old="/boot/home/Desktop/$previous"
    case "$(readlink "$old" 2>/dev/null || true)" in
        /boot/home/config/non-packaged/apps/RFileShare/RFileShare|/boot/home/config/non-packaged/apps/HaikuSMB/HaikuSMB) rm "$old";;
    esac
done
boot=/boot/home/config/settings/boot/UserBootscript
mkdir -p "$(dirname "$boot")"
if ! grep -F '# R SMB volume startup' "$boot" >/dev/null 2>&1; then
    printf '\n# R SMB volume startup\n/boot/home/config/non-packaged/apps/RSMB/RSMB --restore &\n' >> "$boot"
fi
chmod +x "$boot"
# Connect right away; Preferences > Network > R SMB can turn it off.
if "$root/apps/RSMB/RSMB" --enable; then
    printf '%s\n' 'Installed. Shared folders appear in the R SMB volume on the Desktop.'
else
    printf '%s\n' 'Installed, but the volume could not be mounted. See ~/config/settings/RSMB-mount.log.' >&2
fi
