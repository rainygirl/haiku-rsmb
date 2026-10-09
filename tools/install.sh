#!/bin/sh
set -eu
exec sh "$(dirname "$0")/install-volume.sh" "$@"
