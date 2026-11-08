#!/usr/bin/env bash
# Record device screen while raid automation runs (ADB via tools/adb_remote.sh).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ADB="$ROOT/tools/adb_remote.sh"
SEC="${RAID_RECORD_SEC:-90}"
OUT="${RAID_RECORD_OUT:-$ROOT/.raid-verify/raid-repro-$(date -u +%Y%m%d-%H%M%S).mp4}"
mkdir -p "$(dirname "$OUT")"
"$ADB" shell rm -f /sdcard/silkraid-repro.mp4
"$ADB" shell screenrecord --time-limit "$SEC" /sdcard/silkraid-repro.mp4 &
REC=$!
echo "Recording ${SEC}s -> $OUT (pid $REC)"
sleep $((SEC + 3))
wait "$REC" 2>/dev/null || true
"$ADB" pull /sdcard/silkraid-repro.mp4 "$OUT"
ls -la "$OUT"
