#!/usr/bin/env bash
# Companion to app "Record manual raid": screenshots + SilphRaid log every N seconds.
set -uo pipefail
ADB="${ADB:-adb}"
SERIAL="${ADB_SERIAL:-localhost:60406}"
INTERVAL="${INTERVAL_SEC:-5}"
OUT="${OUT_DIR:-/tmp/silkraid-manual-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
echo "manual-raid-watch serial=$SERIAL interval=${INTERVAL}s out=$OUT" | tee "$OUT/session.txt"
last_log=""
while true; do
  ts="$(date +%Y%m%d-%H%M%S)"
  png="$OUT/screen-${ts}.png"
  logf="$OUT/silphraid-${ts}.txt"
  if "$ADB" -s "$SERIAL" exec-out screencap -p >"$png" 2>/dev/null; then
    bytes=$(wc -c <"$png" | tr -d ' ')
    echo "$ts screenshot ${bytes}b"
  else
    echo "$ts screenshot FAILED"
  fi
  lines=$("$ADB" -s "$SERIAL" logcat -d -s SilphRaid 2>/dev/null || true)
  if [[ "$lines" != "$last_log" ]]; then
    echo "$lines" >"$logf"
    echo "$lines" | tail -6 | sed 's/^/  /'
    last_log="$lines"
  fi
  sleep "$INTERVAL"
done
