#!/usr/bin/env bash
# Screenshot every 5s + SilphRaid logs; manual raid mimic helper
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SHOT_DIR="${ROOT}/tools/raid-capture"
LOG="${ROOT}/tools/raid-capture/session.log"
mkdir -p "$SHOT_DIR"
ADB="$ROOT/tools/adb_remote.sh"
ANCHOR_LAT=34.693738
ANCHOR_LON=135.502165

log() { echo "$(date -u +%H:%M:%S) $*" | tee -a "$LOG"; }

log "=== raid manual run anchor=${ANCHOR_LAT},${ANCHOR_LON} ==="

teleport() {
  local cmd="am start-foreground-service -a theappninjas.gpsjoystick.TELEPORT --ef lat ${ANCHOR_LAT} --ef lng ${ANCHOR_LON}"
  "$ADB" shell su -c "$cmd" 2>/dev/null || "$ADB" shell "$cmd" 2>/dev/null || true
  log "gps teleport sent"
}

"$ADB" shell am force-stop com.nianticlabs.pokemongo 2>/dev/null || true
sleep 2
teleport
"$ADB" shell monkey -p com.nianticlabs.pokemongo -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
sleep 25
teleport
"$ADB" shell am start-foreground-service -n dev.silkraid.app/dev.silphmod.nativecheck.CheckService \
  -a dev.silphmod.nativecheck.START --ei mode 1 2>/dev/null || true
"$ADB" logcat -c 2>/dev/null || true

for i in $(seq 1 36); do
  f="$SHOT_DIR/step_$(printf '%03d' "$i").png"
  if "$ADB" exec-out screencap -p > "$f" 2>/dev/null && [[ -s "$f" ]]; then
    log "screenshot $i ok $(stat -f%z "$f" 2>/dev/null || stat -c%s "$f") bytes"
  else
    log "screenshot $i failed"
  fi
  "$ADB" logcat -d -s SilphRaid:I 2>/dev/null | tail -5 >> "$LOG" || true
  sleep 5
done

log "=== done ==="
