#!/usr/bin/env bash
#
# gen-changelog.sh — build a grouped Markdown changelog for a release tag.
#
# Usage:
#   tools/gen-changelog.sh <tag> [<prev-tag>]
#
# Lists every non-merge commit in (prev-tag, tag] grouped by
# conventional-commit type (feat/fix/docs/ci/render/menu/...) into
# friendly sections. Commits from the early history that predate the
# type-prefix convention are bucketed by keyword/verb heuristics so the
# initial release still reads sensibly. Output is Markdown on stdout,
# ending with a GitHub "Full Changelog" compare link.
#
# The same script powers the CI release job and the one-off backfill of
# older releases — keeping both paths byte-identical. No dependencies
# beyond git + coreutils (bash 3.2 compatible, so it runs on the stock
# macOS shell as well as the Linux CI runner).
set -euo pipefail

tag="${1:?usage: gen-changelog.sh <tag> [prev-tag]}"
prev="${2:-}"

# ---- repository slug (owner/name) for the compare link --------------
repo="${GITHUB_REPOSITORY:-}"
if [ -z "$repo" ]; then
  url="$(git remote get-url origin 2>/dev/null || true)"
  case "$url" in
    *github.com[:/]*) repo="${url#*github.com}"; repo="${repo#[:/]}"; repo="${repo%.git}" ;;
  esac
fi

# ---- pick the predecessor tag --------------------------------------
# Auto-detect when not given: the version-sorted tag immediately before
# $tag. Only accept it when it is a real ancestor of $tag — early tags
# can sit on rebased/orphaned commits (v0.0.1 vs v0.1.0 here), in which
# case the tag is treated as the project's initial release.
if [ -z "$prev" ]; then
  prev="$(git tag --sort=v:refname | awk -v t="$tag" '$0==t{print last; exit} {last=$0}')"
fi
if [ -n "$prev" ] && ! git merge-base --is-ancestor "$prev" "$tag" 2>/dev/null; then
  prev=""
fi

if [ -n "$prev" ]; then
  range="$prev..$tag"
else
  range="$tag"
fi

# ---- section buckets ------------------------------------------------
feat=();  fix=();  engine=();  plat=();  docs=();  build=();  maint=();  other=()

lc() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }

# Upper-case the first character so "hold actor ..." reads "Hold actor ..."
ucfirst() {
  local s="$1"
  [ -z "$s" ] && { printf '%s' "$s"; return; }
  printf '%s%s' "$(printf '%s' "${s:0:1}" | tr '[:lower:]' '[:upper:]')" "${s:1}"
}

# Route a commit that carries no recognised type prefix, using keyword
# then leading-verb heuristics. Order matters: content keywords win over
# the verb so "Add docs/ ..." lands in Documentation, not Features.
classify_freeform() {
  local subj="$1" low; low="$(lc "$subj")"
  case "$low" in
    *"gh actions"*|*"github actions"*|*"ci matrix"*|*"matrix build"*|*runner*|*pipeline*|*"static-link"*|*"static sdl2"*|*"section-gc"*)
      build+=("$subj") ;;
    *readme*|*"docs/"*|*documentation*|*mermaid*|*spdx*|*license*|*"issue templates"*)
      docs+=("$subj") ;;
    *miyoo*|*portmaster*|*anbernic*|*onionos*|*".app"*|*".icns"*|*".ico"*|*gatekeeper*|*"window icon"*|*squircle*|*".command"*|*"menu-bar"*)
      plat+=("$subj") ;;
    macos*|windows*|linux*|platform:*)
      plat+=("$subj") ;;
    fix*)
      fix+=("$subj") ;;
    add*|implement*|introduce*|support*|bake*|create\ *|display*|show*)
      feat+=("$subj") ;;
    refactor*|extract*|split*|clean\ up*|tidy*|rename*|consolidate*|strip*|purge*|polish*|drop*|remove*|slim*|standardise*|standardize*|route*|baseline*|comment\ hygiene*|bulk-strip*|make\ *|return\ *)
      maint+=("$subj") ;;
    *)
      other+=("$subj") ;;
  esac
}

classify() {
  local subj="$1"
  local type="" low
  local rest="$subj"
  if [[ "$subj" =~ ^([A-Za-z][A-Za-z0-9_-]*)(\([^\)]*\))?(!)?:[[:space:]]+(.*)$ ]]; then
    type="${BASH_REMATCH[1]}"
    rest="$(ucfirst "${BASH_REMATCH[4]}")"
  fi
  if [ -z "$type" ]; then
    classify_freeform "$subj"
    return
  fi
  low="$(lc "$type")"
  case "$low" in
    feat)                                                          feat+=("$rest") ;;
    fix)                                                           fix+=("$rest") ;;
    render|anim|gfx|graphics|audio|sfx|flic|vm|scene|menu|hud|game|engine|platform|save|font|text)
                                                                   engine+=("$rest") ;;
    miyoo|portmaster|handheld|macos|windows|linux|pack|icons)      plat+=("$rest") ;;
    docs)                                                          docs+=("$rest") ;;
    ci|build)                                                      build+=("$rest") ;;
    refactor|style|chore|test|perf|cleanup)                        maint+=("$rest") ;;
    *)                                                             other+=("$type: $rest") ;;
  esac
}

# Read subjects newest-first (default git log order). The `|| [ -n … ]`
# guard keeps the final line: `format:` omits the trailing newline, so a
# plain `while read` would silently drop the oldest commit in the range.
while IFS= read -r subject || [ -n "$subject" ]; do
  [ -n "$subject" ] && classify "$subject"
done < <(git log --no-merges --pretty=format:'%s' "$range")

# ---- emit -----------------------------------------------------------
# Sections beyond this many entries are folded into a <details> block so
# the release page stays scannable (the initial release has ~150 mostly
# refactor commits). Nothing is ever dropped — the fold just collapses.
FOLD_THRESHOLD=12

emit_section() {
  local title="$1"; shift
  local -a items=("$@")
  local n="${#items[@]}"
  [ "$n" -eq 0 ] && return
  printf '\n### %s\n\n' "$title"
  if [ "$n" -gt "$FOLD_THRESHOLD" ]; then
    printf '<details><summary>%s commits</summary>\n\n' "$n"
    printf '* %s\n' "${items[@]}"
    printf '\n</details>\n'
  else
    printf '* %s\n' "${items[@]}"
  fi
}

if [ -z "$prev" ]; then
  printf '🎉 **Initial release** — the first tagged build of the Wacki SDL2 port.\n'
fi

# Bash 3.2: expanding an empty array under `set -u` aborts, so guard each.
[ "${#feat[@]}"   -gt 0 ] && emit_section '🚀 Features'              "${feat[@]}"
[ "${#fix[@]}"    -gt 0 ] && emit_section '🐛 Bug Fixes'             "${fix[@]}"
[ "${#engine[@]}" -gt 0 ] && emit_section '🎮 Engine & Gameplay'     "${engine[@]}"
[ "${#plat[@]}"   -gt 0 ] && emit_section '📦 Platforms & Packaging' "${plat[@]}"
[ "${#docs[@]}"   -gt 0 ] && emit_section '📚 Documentation'         "${docs[@]}"
[ "${#build[@]}"  -gt 0 ] && emit_section '🔧 Build & CI'            "${build[@]}"
[ "${#maint[@]}"  -gt 0 ] && emit_section '🧹 Maintenance'           "${maint[@]}"
[ "${#other[@]}"  -gt 0 ] && emit_section '📝 Other'                 "${other[@]}"

# ---- footer: compare link ------------------------------------------
if [ -n "$repo" ]; then
  printf '\n---\n\n'
  if [ -n "$prev" ]; then
    printf '**Full Changelog**: https://github.com/%s/compare/%s...%s\n' "$repo" "$prev" "$tag"
  else
    printf '**Full Changelog**: https://github.com/%s/commits/%s\n' "$repo" "$tag"
  fi
fi
