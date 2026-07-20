#!/usr/bin/env bash
# tools/build-3ds.sh — cross-compile TARGET=3ds via devkitpro/devkitarm.
set -euo pipefail
cd "$(dirname "$0")/.."

# Get version from git on HOST (before entering Docker)
WACKI_VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo "dev")

docker run --rm -v "$PWD:/wacki" -w /wacki devkitpro/devkitarm:latest sh -c '
    set -e
    . /etc/profile.d/devkit-env.sh 2>/dev/null || true
    export DEVKITPRO=/opt/devkitpro
    export DEVKITARM=$DEVKITPRO/devkitARM
    export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH
    
    # Install picaGL from source
    echo "Installing picaGL..."
    cd /tmp
    git clone https://github.com/masterfeizz/picaGL.git
    cd picaGL
    
    # Fix deprecated ARM11 flag warning
    sed -i "s/-DARM11/-D__3DS__/g" Makefile
    sed -i "s/-D_3DS//g" Makefile
    
    make install
    cd /wacki
    
    # Build wacki
    make TARGET=3ds WACKI_VERSION="'"$WACKI_VERSION"'"
'

echo "Gotowe: dist/wacki.3dsx"
