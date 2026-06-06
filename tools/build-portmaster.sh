#!/usr/bin/env bash
# Cross-build the engine for PortMaster handhelds (Anbernic & friends)
# via Docker. Produces two ELFs under dist/portmaster/:
#
#   wacki.aarch64   64-bit ARM — Allwinner H700 (RG35XX Plus/H/SP/2024,
#                   RG34XX, RG40XX, RG28XX, RG CubeXX), Rockchip RK3566
#                   (RG353x, RG503) / RK3399 (RG552), and most other
#                   PortMaster devices.
#   wacki.armhf     32-bit ARM — original RG35XX (Actions, Cortex-A9)
#                   and older armhf handhelds.
#
# Built against Debian bullseye (glibc 2.31, SDL2 2.0.14) for broad
# firmware compatibility. SDL2 is linked DYNAMICALLY on purpose: the
# binary picks up PortMaster's per-device libSDL2 at run time (which is
# wired to the right KMSDRM/fbdev video driver) via the launch script's
# LD_LIBRARY_PATH. SDL2 keeps a stable 2.x ABI, so a 2.0.14 link runs
# fine against PortMaster's newer runtime.
#
# Usage:
#   ./tools/build-portmaster.sh                 # both arches
#   ./tools/build-portmaster.sh aarch64         # one arch
#
# Requires Docker with linux/arm64 + linux/arm/v7 emulation (Docker
# Desktop on Apple Silicon runs arm64 natively, arm/v7 under qemu).

set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -f data/WACKI.EXE ]; then
    echo "error: data/WACKI.EXE missing — needed for the embedded PE blob." >&2
    exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "error: docker not found." >&2
    exit 1
fi

VER="$(git describe --tags --always --dirty 2>/dev/null || echo unknown)"
OUT="dist/portmaster"
mkdir -p "$OUT"

# data/ is usually a symlink to the real (uncommitted) game files. Inside
# the container that absolute target dangles, so the Makefile can't see
# data/WACKI.EXE for the embedded blob. Mount the target at the same
# absolute path so the in-tree symlink resolves — without touching the
# working tree. (If data/ is a real dir it's already under the /src mount.)
DATA_MOUNT=()
DATA_TARGET="$(readlink data 2>/dev/null || true)"
case "$DATA_TARGET" in
    /*) DATA_MOUNT=(-v "$DATA_TARGET":"$DATA_TARGET":ro) ;;
esac

build_one() {
    plat="$1"; arch="$2"; image="$3"
    echo "[portmaster] building $arch ($plat) — this pulls Debian + SDL2, be patient..."
    # Arch-explicit image (arm64v8 / arm32v7) so the toolchain ABI is
    # deterministic even when Docker's --platform selection is flaky.
    docker run --rm --platform "$plat" \
        -e "WACKI_VERSION=$VER" \
        "${DATA_MOUNT[@]}" \
        -v "$PWD":/src -w /src \
        "$image" sh -euc '
            export DEBIAN_FRONTEND=noninteractive
            apt-get update -qq
            apt-get install -y -qq --no-install-recommends \
                build-essential libsdl2-dev xxd ca-certificates >/dev/null
            make engine TARGET=portmaster STATIC_SDL2=0 DIST=dist/pm-build
            strip --strip-all dist/pm-build/wacki
            install -D dist/pm-build/wacki "/src/'"$OUT"'/wacki.'"$arch"'"
            rm -rf dist/pm-build
        '
    file "$OUT/wacki.$arch"
}

case "${1:-all}" in
    aarch64) build_one linux/arm64  aarch64 arm64v8/debian:bullseye-slim ;;
    armhf)   build_one linux/arm/v7 armhf   arm32v7/debian:bullseye-slim ;;
    all)
        build_one linux/arm64  aarch64 arm64v8/debian:bullseye-slim
        build_one linux/arm/v7 armhf   arm32v7/debian:bullseye-slim
        ;;
    *) echo "usage: $0 [aarch64|armhf|all]" >&2; exit 2 ;;
esac

echo "[portmaster] done → $OUT/"
