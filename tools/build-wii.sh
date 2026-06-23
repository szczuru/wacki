#!/usr/bin/env bash
# tools/build-wii.sh — cross-compile TARGET=wii via the devkitpro/devkitppc
# Docker image (devkitPPC + libogc + SDL2 portlib for Wii).
# Mirrors tools/build-switch.sh / tools/build-ps2.sh.
set -euo pipefail
cd "$(dirname "$0")/.."

docker run --rm -v "$PWD:/wacki" -w /wacki devkitpro/devkitppc:latest sh -c '
    set -e
    . /etc/profile.d/devkit-env.sh 2>/dev/null || true
    export DEVKITPRO=/opt/devkitpro
    export DEVKITPPC=$DEVKITPRO/devkitPPC
    export PATH=$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH

    # Zainstaluj wii-dev i portlibs jezeli nie ma (obraz devkitppc
    # moze nie miec ich preinstalowanych w przeciwienstwie do devkita64)
    dkp-pacman -S --needed --noconfirm wii-dev wii-sdl2 wii-sdl2_mixer 2>/dev/null || true

    make TARGET=wii
'

echo "Gotowe: dist/wacki.dol"
