#!/usr/bin/env bash
# tools/build-vita.sh — cross-compile TARGET=vita via vitasdk Docker.
set -euo pipefail
cd "$(dirname "$0")/.."

WACKI_VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo "dev")

docker run --rm -v "$PWD:/wacki" -w /wacki \
    -e VITASDK=/usr/local/vitasdk \
    vitasdk/vitasdk:latest \
    sh -c '
        set -e
        export PATH=$VITASDK/bin:$PATH
        # opcjonalne pakiety (ignoruj błąd jeśli już są)
        vdpm install sdl2 sdl2_mixer vitagl vitashark taihen 2>/dev/null || true
        make TARGET=vita WACKI_VERSION="'"$WACKI_VERSION"'" -j"$(nproc)"
    '

echo "Gotowe: dist/wacki.vpk"
