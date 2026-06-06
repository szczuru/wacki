#!/usr/bin/env bash
# Assemble a PortMaster .zip for Anbernic & other handhelds. Run
# tools/build-portmaster.sh first to produce the per-arch binaries.
#
# Zip layout (PortMaster convention — Capitalised script + port dir at
# the root, plus the installer metadata):
#
#   Wacki.zip
#   ├── Wacki.sh                 launch script (entry point)
#   ├── port.json                installer manifest
#   ├── screenshot.png           shown in the PortMaster UI
#   ├── README.md                instructions
#   └── Wacki/                   the port directory (→ /roms/ports/Wacki)
#       ├── wacki.aarch64        64-bit engine
#       ├── wacki.armhf          32-bit engine
#       ├── wacki.gptk           gptokeyb kill-switch (native controls)
#       └── data/                user drops Dane_*.dta here
#           └── README.txt
#
# SDL2 is NOT bundled: the dynamically-linked binaries pick up
# PortMaster's per-device libSDL2 at run time (right KMSDRM/fbdev driver)
# via the launcher environment. The engine reads the pad natively through
# SDL_GameController (mapping comes from $sdl_controllerconfig), so
# gptokeyb only provides the quit hotkey.
#
# The zip is named Wacki.zip to match port.json's "name" field — that's
# what PortMaster's "install from zip" expects.
#
# Usage: ./tools/pack-portmaster.sh [dist/Wacki.zip]

set -euo pipefail
cd "$(dirname "$0")/.."

bin_dir="dist/portmaster"
a64="$bin_dir/wacki.aarch64"
a32="$bin_dir/wacki.armhf"
for b in "$a64" "$a32"; do
    if [ ! -f "$b" ]; then
        echo "error: $b missing — run ./tools/build-portmaster.sh first." >&2
        exit 1
    fi
done

out="${1:-dist/Wacki.zip}"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

port="$stage/Wacki"
mkdir -p "$port/data"

cp "$a64" "$port/wacki.aarch64"
cp "$a32" "$port/wacki.armhf"
chmod +x "$port/wacki.aarch64" "$port/wacki.armhf"

# ---- gptokeyb kill-switch (comments only = no remapping) ----------------
cat > "$port/wacki.gptk" <<'GPTK'
# Wacki czyta pada natywnie w silniku (SDL_GameController), wiec ten plik
# niczego nie mapuje. Istnieje tylko po to, by gptokeyb dzialal jako
# hotkey wyjscia z gry (Start+Select) bez przechwytywania pada.
GPTK

# ---- launch script ------------------------------------------------------
cat > "$stage/Wacki.sh" <<'LAUNCH'
#!/bin/bash
# Wacki: Kosmiczna rozgrywka — PortMaster launcher.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt

[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR="/$directory/ports/Wacki"
cd "$GAMEDIR"

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# Pad mapping for the engine's native SDL_GameController reader.
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

$ESUDO chmod +x "$GAMEDIR/wacki.${DEVICE_ARCH}" 2>/dev/null

if ! ls "$GAMEDIR"/data/[Dd]ane_02.[Dd]ta >/dev/null 2>&1; then
  echo "BRAK DANYCH: skopiuj pliki Dane_*.dta z oryginalnej plyty do $GAMEDIR/data/ i uruchom ponownie."
fi

# gptokeyb = quit hotkey only; the engine reads the pad itself.
$GPTOKEYB "wacki.${DEVICE_ARCH}" -c "./wacki.gptk" &
pm_platform_helper "$GAMEDIR/wacki.${DEVICE_ARCH}"

./wacki.${DEVICE_ARCH}

$ESUDO kill -9 $(pidof gptokeyb) 2>/dev/null
pm_finish
LAUNCH
chmod +x "$stage/Wacki.sh"

# ---- data drop-off note -------------------------------------------------
cat > "$port/data/README.txt" <<'EOF'
Skopiuj tutaj wszystkie pliki Dane_*.dta z oryginalnej plyty CD
(Dane_01, Dane_02, Dane_10..14, Dane_21/22, 30/31/32, 40/41/42,
50/51/52). Silnik szuka katalogu data/ obok siebie.
EOF

# ---- port.json ----------------------------------------------------------
# rtr=false: the player must supply the copyrighted Dane_*.dta first.
# runtime=null: needs only core libSDL2, which the PortMaster base ships.
cat > "$stage/port.json" <<'JSON'
{
  "version": 2,
  "name": "Wacki.zip",
  "items": [
    "Wacki.sh",
    "Wacki"
  ],
  "items_opt": null,
  "attr": {
    "title": "Wacki: Kosmiczna rozgrywka",
    "desc": "Natywna reimplementacja w C/SDL2 polskiej przygodowki point-and-click z 1998 roku (Seven Stars Multimedia). Kursorem sterujesz galka/d-padem, dolny przycisk to interakcja, prawy zmienia postac. Wymaga oryginalnych plikow danych Dane_*.dta.",
    "inst": "Skopiuj oryginalne pliki Dane_*.dta do ports/Wacki/data/, potem uruchom z menu Ports.",
    "genres": [
      "adventure"
    ],
    "porter": [
      "Mateusz Szula"
    ],
    "image": {},
    "rtr": false,
    "runtime": null,
    "reqs": [],
    "arch": [
      "aarch64",
      "armhf"
    ]
  }
}
JSON

# ---- README.md ----------------------------------------------------------
cat > "$stage/README.md" <<'EOF'
# Wacki: Kosmiczna rozgrywka

Natywna reimplementacja w C/SDL2 polskiej przygodowki point-and-click
z 1998 roku (Seven Stars Multimedia), spakowana dla PortMastera. Dziala
na wiekszosci handheldow aarch64 (Anbernic H700 / RK3566 / RK3399 i
pokrewne) oraz armhf (seria Anbernic RG351, oryginalny RG35XX przez
Koriki/Batocera i inne 32-bitowe cele PortMastera).

## Pliki gry (wymagane)

Oryginalne pliki danych sa objete prawami autorskimi i NIE sa dolaczone.
Skopiuj je z plyty CD do folderu Wacki w katalogu ports, np.:

    /roms/ports/Wacki/data/

Pliki: Dane_01, Dane_02, Dane_10..14, Dane_21/22, 30/31/32, 40/41/42,
50/51/52 (.dta).

## Sterowanie

Przyciski wg pozycji SDL: **A = dolny**, **B = prawy**. Na handheldach
w ukladzie Nintendo (wiekszosc Anbernica) sa one fizycznie oznaczone
jako **B** i **A**.

| Wejscie | Akcja |
| --- | --- |
| Galka / D-pad | Ruch kursora |
| A (dolny przycisk) | Chodzenie / interakcja (lewy klik) |
| B (prawy przycisk) | Zmiana postaci Franc / Edek (prawy klik) |
| L1 | Szybki odczyt |
| R1 | Szybki zapis |
| Start | Menu pauzy |

## Tworcy

Port: Mateusz Szula -- https://github.com/mszula/wacki (GPLv3)
EOF

# ---- screenshot ---------------------------------------------------------
# Real 640x480 gameplay capture; falls back to the cover art only if it's
# somehow missing.
if [ -f assets/portmaster/screenshot.png ]; then
    cp assets/portmaster/screenshot.png "$stage/screenshot.png"
elif command -v magick >/dev/null 2>&1 && [ -f assets/icons/wacki-source.jpg ]; then
    magick assets/icons/wacki-source.jpg -resize 640x480^ \
        -gravity center -extent 640x480 "$stage/screenshot.png"
else
    echo "warn: no screenshot (assets/portmaster/screenshot.png missing)." >&2
fi

# ---- zip ----------------------------------------------------------------
out_abs="$(cd "$(dirname "$out")" && pwd)/$(basename "$out")"
rm -f "$out_abs"
( cd "$stage" && zip -rq "$out_abs" . )

echo "[portmaster] built $out"
unzip -l "$out" | sed -n '4,$p'
echo "size: $(du -h "$out" | cut -f1)"
