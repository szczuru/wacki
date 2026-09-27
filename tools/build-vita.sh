#!/usr/bin/env bash
# tools/build-vita.sh — TARGET=vita, SDL2 bez vitaGL
set -euo pipefail
cd "$(dirname "$0")/.."

WACKI_VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo "dev")

docker run --rm -v "$PWD:/wacki" -w /wacki \
    -e VITASDK=/usr/local/vitasdk \
    vitasdk/vitasdk:latest \
    sh -c '
        set -e
        export PATH=$VITASDK/bin:$PATH

        # Jeśli libSDL2 ciągnie vitaGL (vglInitExtended) — przebuduj oficjalne SDL2
        if nm -A "$VITASDK/arm-vita-eabi/lib/libSDL2.a" 2>/dev/null | grep -q vglInitExtended; then
            echo "==> libSDL2 ma vitaGL — buduję oficjalne SDL2 (GXM only)"
            rm -rf /tmp/SDL2-build /tmp/SDL2-src
            git clone --depth 1 --branch SDL2 https://github.com/libsdl-org/SDL.git /tmp/SDL2-src
            cmake -S /tmp/SDL2-src -B /tmp/SDL2-build \
                -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
                -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_INSTALL_PREFIX=$VITASDK/arm-vita-eabi \
                -DVIDEO_VITA_PIB=OFF \
                -DVIDEO_VITA_PVR=OFF \
                -DSDL_SHARED=OFF \
                -DSDL_STATIC=ON
            cmake --build /tmp/SDL2-build -j"$(nproc)"
            cmake --install /tmp/SDL2-build
            echo "==> zainstalowano czyste SDL2"
        else
            echo "==> libSDL2 bez vitaGL — OK"
        fi

        # SDL2_mixer (jeśli brak)
        if [ ! -f "$VITASDK/arm-vita-eabi/lib/libSDL2_mixer.a" ]; then
            vdpm install sdl2_mixer 2>/dev/null || true
        fi

        make TARGET=vita WACKI_VERSION="'"$WACKI_VERSION"'" -j"$(nproc)"
    '

echo "Gotowe: dist/wacki.vpk"
