#!/usr/bin/env bash
# Push a GitHub release's archives to the itch.io page (mszula/wacki) by hand —
# the same thing the CI `release` job does on a v* tag, but for a release that
# was tagged BEFORE the CI butler step existed (e.g. backfilling v1.2.3), or any
# other one-off. Idempotent: re-running just re-pushes the same channels.
#
# Auth: butler reads the itch.io key from $BUTLER_API_KEY. The key never appears
# in this script — pass it in the environment of the single invocation:
#
#   BUTLER_API_KEY='<your itch key>' itch/push-release.sh [tag]
#
# `tag` defaults to the latest GitHub release. Get a key at
# itch.io -> Settings -> API keys (the same value as the BUTLER_API_KEY repo
# secret used by CI).
set -euo pipefail

: "${BUTLER_API_KEY:?set BUTLER_API_KEY (itch.io -> Settings -> API keys)}"
TAG="${1:-$(gh release view --json tagName -q .tagName)}"
echo "Pushing release $TAG -> mszula/wacki"

# ---- ensure butler ----------------------------------------------------------
# Prefer one already on PATH; otherwise fetch the official binary from itch's
# broth into a cache dir (out of the repo). macOS ships only the amd64 build —
# it runs under Rosetta on Apple Silicon.
BUTLER=butler
if ! command -v butler >/dev/null 2>&1; then
  case "$(uname -s)-$(uname -m)" in
    Darwin-*) chan=darwin-amd64 ;;
    Linux-x86_64) chan=linux-amd64 ;;
    *) echo "::error:: no butler on PATH and no known build for $(uname -sm)"; exit 1 ;;
  esac
  dir="${XDG_CACHE_HOME:-$HOME/.cache}/itch-butler"
  mkdir -p "$dir"
  echo "-> fetching butler ($chan) into $dir"
  curl -fsSL -o "$dir/butler.zip" "https://broth.itch.zone/butler/$chan/LATEST/archive/default"
  unzip -oq "$dir/butler.zip" -d "$dir"
  chmod +x "$dir/butler"
  BUTLER="$dir/butler"
fi
"$BUTLER" -V

# ---- download the release archives -----------------------------------------
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
echo "-> downloading $TAG assets"
gh release download "$TAG" -D "$work" \
  -p 'wacki-*.tar.gz' -p 'wacki-*.zip' -p 'wacki-*.apk'

# ---- push each archive to its channel --------------------------------------
# itch reads osx/linux/windows/android as platform tags; the handheld + console
# channels (miyoo/portmaster/ps2) are plain labels. Channels auto-create on the
# first push. --userversion stamps the tag so the page shows the version.
push() {
  if [ -f "$work/$1" ]; then
    echo "-> $1 -> mszula/wacki:$2"
    "$BUTLER" push "$work/$1" "mszula/wacki:$2" --userversion "$TAG"
  else
    echo "::warning:: skip $2 — $1 not in release $TAG"
  fi
}
push wacki-macos-arm64.tar.gz   osx
push wacki-linux-x86_64.tar.gz  linux
push wacki-windows-x86_64.zip   windows
push wacki-miyoo.zip            miyoo
push wacki-portmaster.zip       portmaster
push wacki-ps2.zip              ps2
push wacki-android.apk          android

echo "OK — check the Uploads on https://mszula.itch.io/wacki"
