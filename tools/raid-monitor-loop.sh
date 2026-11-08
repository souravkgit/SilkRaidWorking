#!/usr/bin/env bash
# Watch SilphRaid logs + screenshot age; alert on stall.
ADB="${ADB:-adb}"
SERIAL="${ADB_SERIAL:-localhost:57866}"
OUT="${OUT_DIR:-/tmp/silkraid-screenshots}"
INTERVAL="${INTERVAL_SEC:-10}"
STALL="${STALL_SEC:-120}"
last_log=""
last_change=$(date +%s)
while true; do
  ts="$(date +%H:%M:%S)"
  snap=$(ls -t "$OUT"/pogo-*.png 2>/dev/null | head -1)
  snap_age="none"
  if [[ -n "$snap" ]]; then
    now=$(date +%s)
    mtime=$(stat -f %m "$snap" 2>/dev/null || stat -c %Y "$snap")
    snap_age="$((now-mtime))s"
  fi
  lines=$("$ADB" -s "$SERIAL" logcat -d -s SilphRaid 2>/dev/null | tail -8)
  if [[ "$lines" != "$last_log" ]]; then
    last_log="$lines"
    last_change=$(date +%s)
    echo "=== $ts snap_age=$snap_age ==="
    echo "$lines"
  fi
  idle=$(( $(date +%s) - last_change ))
  if (( idle > STALL )); then
    echo "=== $ts STALL ${idle}s — restarting service ==="
    "$ADB" -s "$SERIAL" shell am start-foreground-service -n dev.silkraid.app/dev.silphmod.nativecheck.CheckService -a dev.silphmod.nativecheck.START --ei mode 1 >/dev/null 2>&1 || true
    last_change=$(date +%s)
  fi
  sleep "$INTERVAL"
done
