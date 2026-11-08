#!/usr/bin/env bash
# Record a full raid run: chained on-device screenrecord segments + SilphRaid/SilphDump logcat.
# Usage: ANDROID_SERIAL=localhost:56975 tools/raid_flow_record.sh [max_seconds] [--start]
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAX="${1:-900}"
OUT="$ROOT/.raid-verify/flow-$(date +%Y%m%d-%H%M%S)"
DEV=/sdcard/silkraid-rec
mkdir -p "$OUT"

adb shell "rm -rf $DEV; mkdir -p $DEV; touch $DEV/run"
adb shell "nohup sh -c 'i=0; while [ -f $DEV/run ]; do screenrecord --size 360x640 --bit-rate 500000 --time-limit 180 $DEV/seg\$(printf %02d \$i).mp4; i=\$((i+1)); done' >/dev/null 2>&1 &"
adb logcat -c
adb logcat -v time -s SilphRaid:V SilphDump:V SilphControl:V > "$OUT/raid.log" 2>&1 &
LOG=$!

if [[ "${2:-}" == "--start" ]]; then
  adb shell am start-foreground-service -n dev.silkraid.app/dev.silphmod.nativecheck.CheckService \
    -a dev.silphmod.nativecheck.START --ei mode 1 >/dev/null
fi

echo "recording -> $OUT (max ${MAX}s)"
end=$((SECONDS + MAX))
while (( SECONDS < end )); do
  sleep 5
  if grep -qE "capture confirmed|captured result|post-capture transition|post-battle flow timed out|battle ended without victory" "$OUT/raid.log"; then
    echo "raid flow finished; recording 15s more"; sleep 15; break
  fi
done

adb shell "rm -f $DEV/run; pkill -INT screenrecord"
sleep 3
kill "$LOG" 2>/dev/null
adb pull "$DEV/." "$OUT/" >/dev/null
ls -la "$OUT"
