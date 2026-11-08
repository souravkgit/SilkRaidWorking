#!/usr/bin/env bash
# Screenshot every 5s over adb tunnel for raid debugging.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ADB="${ADB:-$ROOT/tools/adb_remote.sh}"
CAPDIR="${RAID_CAPS_DIR:-/tmp/silkraid-caps/$(date -u +%Y%m%d-%H%M%S)}"
mkdir -p "$CAPDIR"
echo "caps -> $CAPDIR"
while true; do
  ts="$(date -u +%H%M%S)"
  if "$ADB" exec-out screencap -p > "$CAPDIR/$ts.png" 2>/dev/null; then
    sz=$(wc -c < "$CAPDIR/$ts.png" 2>/dev/null || echo 0)
    if [[ "$sz" -lt 5000 ]]; then rm -f "$CAPDIR/$ts.png"; fi
  fi
  sleep 5
done
