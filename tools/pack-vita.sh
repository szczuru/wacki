#!/usr/bin/env bash
# tools/pack-vita.sh — paczka release dla PS Vita.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-dist/wacki-vita.zip}"
mkdir -p dist

if [ ! -f dist/wacki.vpk ]; then
    echo "Brak dist/wacki.vpk — najpierw: ./tools/build-vita.sh"
    exit 1
fi

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

cp dist/wacki.vpk "$STAGE/"

cat > "$STAGE/README-Vita.txt" << 'EOF'
Wacki: Kosmiczna rozgrywka — port PlayStation Vita
==================================================

Instalacja
----------
1. Skopiuj wacki.vpk na kartę i zainstaluj przez VitaShell.
2. Skopiuj dane z oryginalnej płyty:
     ux0:/data/wacki/data/Dane_*.dta
     ux0:/data/wacki/data/WACKI.EXE   (opcjonalnie, jeśli build bez embedu)

Sterowanie
----------
  Cross (X)     — lewy klik
  Circle (O)    — prawy klik / zmiana postaci
  Triangle      — proporcje (stretch / 4:3)
  Square        — tryb touch (absolute / relative / off)
  START         — menu pauzy
  L / R         — quick-load / quick-save
  D-pad / analog — kursor

Touch (tryb != relative)
------------------------
  Przedni ekran — lewy klik (tap)
  Tylny panel   — prawy klik (tap)
EOF

(cd "$STAGE" && zip -9 -r "$(cd - >/dev/null && pwd)/$OUT" .)
echo "Gotowe: $OUT"
