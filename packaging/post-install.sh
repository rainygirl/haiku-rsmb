#!/bin/sh
# Mount right after "pkgman install"; an upgrade keeps an earlier Disable.
/boot/system/apps/RSMB/RSMB --installed || true
