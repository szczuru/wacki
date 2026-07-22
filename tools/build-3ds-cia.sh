#!/usr/bin/env bash
# tools/build-3ds-cia.sh — package an installable .cia from the already-built
# 3DS ELF (tools/build-3ds.sh must run first — this script does NOT rebuild
# the engine, it only packages what's in dist/).
#
# Produces dist/wacki.cia: a single self-contained installer a user sideloads
# via FBI/any CIA installer, no manual .3dsx + Homebrew-Launcher juggling and
# no separate icon/banner steps — everything (icon, top-screen HOME Menu
# banner, New3DS speedup exheader flags, SD-card FS permissions) is baked in.
#
# Game DATA FILES still need to be supplied separately by the user — see
# src/platform/3ds/data_root_3ds.c's candidate paths (sdmc:/3ds/wacki/data
# etc.) — a .cia can bundle romfs data, but Wacki's original DANE_*.DTA/
# WACKI.EXE assets are copyrighted game data this repo has no right to
# redistribute, so they are NOT embedded here; the user drops them onto the
# SD card exactly as they already do for the .3dsx build.
#
# Tools used (NOT part of devkitpro/devkitarm's image, fetched here):
#   makerom    - github.com/3DSGuy/Project_CTR (linked from makerom's own
#                README.md as the canonical distribution point)
#   bannertool - github.com/carstene1ns/3ds-bannertool (actively-maintained
#                fork of the original Steveice10/bannertool; upstream repo
#                has no prebuilt release binaries, this fork does)
set -euo pipefail
cd "$(dirname "$0")/.."

MAKEROM_VERSION=v0.19.0
BANNERTOOL_VERSION=1.2.3

# $(DIST)/$(BIN_NAME) in the Makefile -> "dist/wacki" (no .elf extension,
# but it IS an ELF binary -- confirmed via `file dist/wacki`; makerom's
# -elf flag just wants a valid ELF path, the extension is cosmetic).
ELF="dist/wacki"
ICON_PNG="assets/icons/wacki-3ds-icon.png"
BANNER_PNG="assets/icons/wacki-3ds-banner.png"
SILENT_WAV="assets/3ds/wacki-silent.wav"
RSF="assets/3ds/wacki-cia.rsf"

WORK="dist/cia-work"
CIA_OUT="dist/wacki.cia"

if [ ! -f "$ELF" ]; then
    echo "error: $ELF not found — run tools/build-3ds.sh first (it produces" >&2
    echo "       dist/wacki alongside dist/wacki.3dsx; makerom packages the" >&2
    echo "       ELF directly, the .3dsx step is unrelated/unaffected)." >&2
    exit 1
fi

mkdir -p "$WORK"

# ---- Fetch makerom + bannertool (Linux x86_64 release binaries) -----------
# Cached in dist/cia-work/bin/ so a re-run (e.g. after only changing the RSF)
# doesn't re-download; CI runners start clean every time so this only saves
# time in local iterative use.
BIN="$WORK/bin"
mkdir -p "$BIN"

if [ ! -x "$BIN/makerom" ]; then
    echo "Fetching makerom $MAKEROM_VERSION..."
    curl -fsSL -o "$WORK/makerom.zip" \
        "https://github.com/3DSGuy/Project_CTR/releases/download/makerom-${MAKEROM_VERSION}/makerom-${MAKEROM_VERSION}-ubuntu_x86_64.zip"
    unzip -oq "$WORK/makerom.zip" -d "$BIN"
    chmod +x "$BIN/makerom"
fi

if [ ! -x "$BIN/bannertool" ]; then
    echo "Fetching bannertool $BANNERTOOL_VERSION..."
    curl -fsSL -o "$WORK/bannertool.tar.gz" \
        "https://github.com/carstene1ns/3ds-bannertool/releases/download/${BANNERTOOL_VERSION}/bannertool-${BANNERTOOL_VERSION}-linux.tar.gz"
    # Archive contains a versioned subdirectory (bannertool-X.Y.Z-linux/),
    # unlike makerom's flat zip -- extract then flatten.
    tar -xzf "$WORK/bannertool.tar.gz" -C "$WORK"
    cp "$WORK/bannertool-${BANNERTOOL_VERSION}-linux/bannertool" "$BIN/bannertool"
    chmod +x "$BIN/bannertool"
fi

export PATH="$BIN:$PATH"

# ---- Build SMDH (title/publisher text + icon shown on HOME Menu) ----------
echo "Building SMDH..."
bannertool makesmdh \
    -s "Wacki" \
    -l "Wacki: Kosmiczna rozgrywka" \
    -p "mszula/szczuru/AI" \
    -i "$ICON_PNG" \
    -o "$WORK/icon.icn" \
    -r regionfree \
    -f visible,new3ds

# ---- Build banner (top-screen HOME Menu banner) ----------------------------
# bannertool's makebanner REQUIRES an audio track argument even for a
# static (non-CGFX-animated) banner — see this script's top comment on
# where wacki-silent.wav comes from; there is no "-a none" escape hatch
# in the tool itself (confirmed against its actual source).
echo "Building banner..."
bannertool makebanner \
    -i "$BANNER_PNG" \
    -a "$SILENT_WAV" \
    -o "$WORK/banner.bnr"

# ---- Build the .cia itself --------------------------------------------------
# -target t = "test" keychain, the correct target for unsigned homebrew (see
# makerom's own README: "t=test, suitable for homebrew"). -rsf carries the
# exheader / access-control settings (New3DS speedup, SD card FS access —
# see wacki-cia.rsf's own comments for why those specific flags are there).
echo "Building CIA..."
makerom \
    -f cia \
    -o "$CIA_OUT" \
    -target t \
    -elf "$ELF" \
    -rsf "$RSF" \
    -icon "$WORK/icon.icn" \
    -banner "$WORK/banner.bnr" \
    -major 1 -minor 0 -micro 0

echo "Gotowe: $CIA_OUT"
