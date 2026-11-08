#!/usr/bin/env bash
# Never-idle raid watch: reconnect ADB tunnel, keep PoGO + CheckService up, caps + log tail until capture confirmed.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ADB="$ROOT/tools/adb_remote.sh"
CAP="${RAID_CAPS_DIR:-/tmp/silkraid-caps/watch-$(date -u +%Y%m%d-%H%M%S)}"
LOG="${RAID_WATCH_LOG:-/tmp/silkraid-watch.log}"
ANCHOR_LAT="${RAID_ANCHOR_LAT:-34.693738}"
ANCHOR_LON="${RAID_ANCHOR_LON:-135.502165}"
INTERVAL="${RAID_WATCH_SEC:-15}"

log(){ echo "$(date -u +%H:%M:%S) $*" | tee -a "$LOG"; }

adb_ok(){
  [[ -n "${SSHPASS:-}" ]] || { log "SSHPASS unset — cannot open SSH tunnel"; return 1; }
  "$ADB" shell echo ok 2>/dev/null | grep -q ok
}

start_pogo(){
  log "start PoGO + teleport"
  "$ADB" shell am force-stop com.nianticlabs.pokemongo 2>/dev/null || true
  sleep 2
  "$ADB" shell am start-foreground-service -a theappninjas.gpsjoystick.TELEPORT --ef lat "$ANCHOR_LAT" --ef lng "$ANCHOR_LON" 2>/dev/null \
    || "$ADB" shell su -c "am start-foreground-service -a theappninjas.gpsjoystick.TELEPORT --ef lat $ANCHOR_LAT --ef lng $ANCHOR_LON" 2>/dev/null || true
  "$ADB" shell am start -n com.nianticlabs.pokemongo/com.nianticproject.holoholo.libholoholo.unity.UnityMainActivity 2>/dev/null \
    || "$ADB" shell monkey -p com.nianticlabs.pokemongo -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
  sleep 45
}

start_service(){
  log "start CheckService mode 1"
  "$ADB" shell am force-stop dev.silkraid.app 2>/dev/null || true
  sleep 1
  "$ADB" shell am start-foreground-service -n dev.silkraid.app/dev.silphmod.nativecheck.CheckService \
    -a dev.silphmod.nativecheck.START --ei mode 1 2>/dev/null || true
}

mkdir -p "$CAP"
chmod +x "$ADB" 2>/dev/null || true
log "watch caps=$CAP log=$LOG"

while true; do
  if ! adb_ok; then
    log "adb/tunnel down — retry in ${INTERVAL}s"
    sleep "$INTERVAL"
    continue
  fi
  if "$ADB" logcat -d -s SilphRaid:I 2>/dev/null | grep -q 'capture confirmed'; then
    log "SUCCESS capture confirmed"
    exit 0
  fi
  pg=$("$ADB" shell pidof com.nianticlabs.pokemongo 2>/dev/null | tr -d '\r\n')
  if [[ -z "$pg" ]]; then start_pogo; start_service; fi
  if ! "$ADB" shell dumpsys activity services dev.silkraid.app 2>/dev/null | grep -q CheckService; then
    start_service
  fi
  line=$("$ADB" logcat -d -s SilphRaid:I 2>/dev/null | grep -E 'scan chosen|CopyTo ok|sample\[|join armed|capture confirmed' | tail -1)
  [[ -n "$line" ]] && log "$line"
  ts=$(date -u +%H%M%S)
  "$ADB" exec-out screencap -p > "$CAP/$ts.png" 2>/dev/null || true
  sleep "$INTERVAL"
done
