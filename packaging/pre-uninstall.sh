#!/bin/sh
# Unmount before the files go away. A busy volume stays mounted until reboot.
/boot/system/apps/RSMB/RSMB --unmount || true
