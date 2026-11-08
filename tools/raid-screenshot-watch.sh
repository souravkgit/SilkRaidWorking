#!/usr/bin/env bash
# Poll device screen every N seconds for PoGO / SilkRaid debugging.
set -euo pipefail
ADB="${ADB:-adb}"
SERIAL="${ADB_SERIAL:-localhost:57866}"
INTERVAL="${INTERVAL_SEC:-10}"
OUT="${OUT_DIR:-/tmp/silkraid-screenshots}"
mkdir -p "$OUT"
echo "screenshot watch: serial=$SERIAL interval=${INTERVAL}s out=$OUT"
while true; do
  ts="$(date +%Y%m%d-%H%M%S)"
  file="$OUT/pogo-${ts}.png"
  if "$ADB" -s "$SERIAL" exec-out screencap -p >"$file" 2>/dev/null; then
    echo "$ts $(wc -c <"$file" | tr -d ' ') bytes -> $file"
  else
    echo "$ts screencap failed"
  fi
  sleep "$INTERVAL"
done
