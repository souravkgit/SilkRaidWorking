#!/usr/bin/env bash
# Keep PoGO foreground + SilkRaid service alive until capture confirmed in logcat.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ADB="$ROOT/tools/adb_remote.sh"
LOG="${RAID_KEEPALIVE_LOG:-/tmp/silkraid-keepalive.log}"
ANCHOR_LAT="${RAID_ANCHOR_LAT:-41.671948}"
ANCHOR_LON="${RAID_ANCHOR_LON:--0.885460}"
INTERVAL="${RAID_KEEPALIVE_SEC:-20}"

log() { echo "$(date -u +%H:%M:%S) $*" | tee -a "$LOG"; }

start_pogo() {
  "$ADB" shell am force-stop com.nianticlabs.pokemongo 2>/dev/null
  sleep 2
  "$ADB" shell su -c "am start-foreground-service -a theappninjas.gpsjoystick.TELEPORT --ef lat $ANCHOR_LAT --ef lng $ANCHOR_LON" 2>/dev/null
  "$ADB" shell am start -n com.nianticlabs.pokemongo/com.nianticproject.holoholo.libholoholo.unity.UnityMainActivity 2>/dev/null \
    || "$ADB" shell monkey -p com.nianticlabs.pokemongo -c android.intent.category.LAUNCHER 1
  sleep 25
  "$ADB" shell input tap 360 1240 2>/dev/null
  "$ADB" shell input tap 360 1240 2>/dev/null
  sleep 35
}

start_service() {
  # Never force-stop SilkRaid here — CheckService teardown force-stops Pokémon GO.
  if "$ADB" shell pidof dev.silkraid.app >/dev/null 2>&1; then
    return 0
  fi
  "$ADB" shell am start-foreground-service -n dev.silkraid.app/dev.silphmod.nativecheck.CheckService \
    -a dev.silphmod.nativecheck.START --ei mode 1 2>/dev/null \
    || "$ADB" shell am startservice -n dev.silkraid.app/dev.silphmod.nativecheck.CheckService \
    -a dev.silphmod.nativecheck.START --ei mode 1
}

pogo_foreground() {
  "$ADB" shell dumpsys activity activities 2>/dev/null | grep -q 'topResumedActivity=.*pokemongo'
}

log "keepalive start interval=${INTERVAL}s log=$LOG"
if ! "$ADB" shell pidof com.nianticlabs.pokemongo >/dev/null 2>&1; then
  log "PoGO not running — cold start"
  start_pogo
  log "PoGO load wait done — starting SilkRaid"
  start_service
elif ! pogo_foreground; then
  log "PoGO running but background — bringing to front"
  "$ADB" shell am start -n com.nianticlabs.pokemongo/com.nianticproject.holoholo.libholoholo.unity.UnityMainActivity 2>/dev/null \
    || "$ADB" shell monkey -p com.nianticlabs.pokemongo -c android.intent.category.LAUNCHER 1
  sleep 5
  "$ADB" shell input tap 360 1240 2>/dev/null
else
  log "PoGO already foreground"
fi
if ! "$ADB" shell pidof dev.silkraid.app >/dev/null 2>&1; then
  log "Starting SilkRaid service"
  start_service
fi

while true; do
  if "$ADB" logcat -d -s SilphRaid:I 2>/dev/null | grep -q 'capture confirmed'; then
    log "DONE: capture confirmed"
    exit 0
  fi
  if ! "$ADB" shell pidof com.nianticlabs.pokemongo >/dev/null 2>&1; then
    log "PoGO not running — restarting"
    start_pogo
    start_service
  elif ! pogo_foreground; then
    log "PoGO background — bringing to front"
    "$ADB" shell am start -n com.nianticlabs.pokemongo/com.nianticproject.holoholo.libholoholo.unity.UnityMainActivity 2>/dev/null \
      || "$ADB" shell monkey -p com.nianticlabs.pokemongo -c android.intent.category.LAUNCHER 1
    sleep 5
    "$ADB" shell input tap 360 1240 2>/dev/null
    "$ADB" shell input tap 360 700 2>/dev/null
    "$ADB" shell input keyevent KEYCODE_BACK 2>/dev/null
    sleep 2
    "$ADB" shell input tap 360 1240 2>/dev/null
  fi
  if ! "$ADB" shell pidof dev.silkraid.app >/dev/null 2>&1; then
    log "SilkRaid dead — restarting service"
    start_service
  fi
  tail -3 "$LOG" >/dev/null 2>&1
  "$ADB" logcat -d -s SilphRaid:I 2>/dev/null | grep -E 'join armed|rpc send|join lobby|start raid|battle|capture confirmed|presentation cleanup' | tail -2 >> "$LOG" || true
  sleep "$INTERVAL"
done
