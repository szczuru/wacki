#!/usr/bin/env bash
# tools/build-3ds.sh — cross-compile TARGET=3ds via devkitpro/devkitarm.
set -euo pipefail
cd "$(dirname "$0")/.."

docker run --rm -v "$PWD:/wacki" -w /wacki devkitpro/devkitarm:latest sh -c '
    set -e
    . /etc/profile.d/devkit-env.sh 2>/dev/null || true
    export DEVKITPRO=/opt/devkitpro
    export DEVKITARM=$DEVKITPRO/devkitARM
    export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH
    make TARGET=3ds
'
echo "Gotowe: dist/wacki.3dsx"
