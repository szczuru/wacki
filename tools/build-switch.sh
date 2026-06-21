#!/usr/bin/env bash
# tools/build-switch.sh — cross-compile TARGET=switch via the devkitpro/
# devkita64 Docker image (devkitA64 + libnx + the SDL2 portlib already
# preinstalled — see that image's own Dockerfile, which runs `dkp-pacman -S
# switch-portlibs` at image-build time). Mirrors tools/build-ps2.sh /
# tools/build-miyoo.sh's wrapper pattern so local dev and CI both call this
# one script instead of duplicating the docker invocation — see
# .github/workflows/switch.yml.
set -euo pipefail
cd "$(dirname "$0")/.."

docker run --rm -v "$PWD:/wacki" -w /wacki devkitpro/devkita64:latest sh -c '
    set -e
    . /etc/profile.d/devkit-env.sh 2>/dev/null || true
    export DEVKITPRO=/opt/devkitpro
    export DEVKITA64=$DEVKITPRO/devkitA64
    export PATH=$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH

    if [ ! -f assets/icons/wacki-switch.jpg ]; then
        echo "UWAGA: brak assets/icons/wacki-switch.jpg (elf2nro wymaga 256x256 JPEG) — build prawdopodobnie sie wywali na etapie pakowania .nro"
    fi

    make TARGET=switch
'

echo "Gotowe: dist/wacki.nro"
