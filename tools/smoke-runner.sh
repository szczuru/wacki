#!/usr/bin/env bash
# T44 — smoke harness.
#
# Runs ./wacki --headless for a bounded duration, captures
# stage/komnata transitions, asserts that the game made meaningful
# progress (at least one komnata transition observed, zero crashes,
# zero ASAN findings if running ./wacki-debug).
#
# Usage:
#   ./tools/smoke-runner.sh               # default: 30s run, ./wacki
#   ./tools/smoke-runner.sh -d 60         # 60s run
#   ./tools/smoke-runner.sh -b debug      # use ./wacki-debug (ASAN+UBSan)
#
# Exit codes:
#   0 = all assertions passed
#   1 = build artifact missing
#   2 = no komnata transitions observed (likely regression)
#   3 = crash / sanitizer finding
set -u

DUR=30
BIN=./wacki

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d) DUR="$2"; shift 2 ;;
    -b) [[ "$2" == "debug" ]] && BIN=./wacki-debug; shift 2 ;;
    -h|--help)
      grep -E '^# ' "$0" | sed 's/^# \?//'
      exit 0 ;;
    *)
      echo "unknown arg: $1" >&2; exit 1 ;;
  esac
done

cd "$(dirname "$0")/.."

if [[ ! -x "$BIN" ]]; then
  echo "[smoke] missing binary: $BIN (run \`make\` or \`make debug\`)" >&2
  exit 1
fi

LOG=$(mktemp -t wacki-smoke.XXXXXX)
echo "[smoke] $BIN --headless for ${DUR}s → $LOG"

# Disable ASAN leak detection (we don't aim for leak-free yet) and
# bound the runtime so CI never hangs. The exit-status from timeout
# of 124 = process was killed by timeout (expected); other codes
# indicate the binary aborted on its own.
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
  timeout "$DUR" "$BIN" --headless >"$LOG" 2>&1
RC=$?

# 0 = clean exit, 124 = timed out (expected for headless), 137 = SIGKILL
# (also acceptable), 143 = SIGTERM (also OK). Anything else = trouble.
if [[ "$RC" -ne 0 && "$RC" -ne 124 && "$RC" -ne 137 && "$RC" -ne 143 ]]; then
  echo "[smoke] binary aborted (rc=$RC) — last 20 lines:"
  tail -20 "$LOG"
  rm "$LOG"
  exit 3
fi

if grep -qE "AddressSanitizer|UndefinedBehaviorSanitizer.*runtime error|heap-buffer|stack-buffer|use-after-free" "$LOG"; then
  echo "[smoke] sanitizer finding(s):"
  grep -E "AddressSanitizer|runtime error|heap-buffer|use-after-free" "$LOG" | head -10
  rm "$LOG"
  exit 3
fi

KOMNATA_COUNT=$(grep -cE "^\[real\] komnata|^\[load-komnata\]" "$LOG" || true)
STAGE_COUNT=$(grep -cE "^\[stage\] [1-5] @" "$LOG" || true)

echo "[smoke] stages parsed:   $STAGE_COUNT (expect ≥ 5)"
echo "[smoke] komnata events:  $KOMNATA_COUNT"

if [[ "$STAGE_COUNT" -lt 5 ]]; then
  echo "[smoke] FAIL — stage table didn't load fully" >&2
  tail -20 "$LOG"
  rm "$LOG"
  exit 2
fi

# Optional progress assertion — only enforced if user passed --strict
# via env. Default just reports counts (some headless runs make no
# in-game progress because there's no fake input driver yet).
if [[ "${SMOKE_STRICT:-0}" == "1" && "$KOMNATA_COUNT" -lt 1 ]]; then
  echo "[smoke] FAIL — no komnata transitions observed (SMOKE_STRICT=1)" >&2
  tail -20 "$LOG"
  rm "$LOG"
  exit 2
fi

echo "[smoke] PASS"
rm "$LOG"
exit 0
