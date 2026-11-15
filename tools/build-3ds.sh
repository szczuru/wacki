#!/usr/bin/env bash
# tools/build-3ds.sh — cross-compile TARGET=3ds (dist/wacki.3dsx + dist/wacki ELF).
#
# Toolchain, in this order:
#   1. a local devkitPro (DEVKITPRO set, or /opt/devkitpro) with devkitARM,
#      libctru and citro3d — used when Docker is not available, or always
#      with WACKI_3DS_LOCAL=1;
#   2. the devkitpro/devkitarm Docker image (has libctru + citro3d).
# picaGL is no longer needed: the GPU backend (video_3ds_c3d.c) uses
# citro3d directly.
#
# Extra make arguments are passed through, e.g.:
#   tools/build-3ds.sh VIDEO_3DS=fb     # the old CPU framebuffer backend
set -euo pipefail
cd "$(dirname "$0")/.."

# Version from git on the HOST (before entering Docker)
WACKI_VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo "dev")

DKP="${DEVKITPRO:-/opt/devkitpro}"
have_local() {
    [ -x "$DKP/devkitARM/bin/arm-none-eabi-gcc" ] &&
    [ -f "$DKP/libctru/lib/libctru.a" ] &&
    [ -f "$DKP/libctru/lib/libcitro3d.a" ]
}
have_docker() {
    command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1
}

if [ "${WACKI_3DS_LOCAL:-0}" = 1 ] || ! have_docker; then
    if ! have_local; then
        echo "error: no Docker and no local devkitPro with devkitARM + libctru + citro3d" >&2
        echo "       (looked in $DKP; install the 3ds-dev group or start Docker)" >&2
        exit 1
    fi
    echo "Local devkitPro: $DKP"
    export DEVKITPRO="$DKP"
    export DEVKITARM="$DKP/devkitARM"
    export PATH="$DEVKITARM/bin:$DKP/tools/bin:$PATH"
    make TARGET=3ds WACKI_VERSION="$WACKI_VERSION" "$@"
else
    docker run --rm -v "$PWD:/wacki" -w /wacki devkitpro/devkitarm:latest sh -c '
        set -e
        . /etc/profile.d/devkit-env.sh 2>/dev/null || true
        export DEVKITPRO=/opt/devkitpro
        export DEVKITARM=$DEVKITPRO/devkitARM
        export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH
        make TARGET=3ds WACKI_VERSION="'"$WACKI_VERSION"'" '"$*"'
    '
fi

echo "Gotowe: dist/wacki.3dsx"
