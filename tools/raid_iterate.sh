#!/usr/bin/env bash
# Build → install → run SilkRaid until capture confirmed (PAR-aligned iteration).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ADB="$ROOT/tools/adb_remote.sh"
CAPDIR="${RAID_CAPS_DIR:-/tmp/silkraid-caps/iterate-$(date -u +%Y%m%d-%H%M%S)}"
LOG="${RAID_ITER_LOG:-/tmp/silkraid-iterate.log}"
ANCHOR_LAT="${RAID_ANCHOR_LAT:-41.671948}"
ANCHOR_LON="${RAID_ANCHOR_LON:--0.885460}"
export RAID_ANCHOR_LAT="$ANCHOR_LAT" RAID_ANCHOR_LON="$ANCHOR_LON"

log(){ echo "$(date -u +%H:%M:%S) $*" | tee -a "$LOG"; }

ensure_adb(){
  for _ in 1 2 3; do
    if "$ADB" shell echo ok 2>/dev/null | grep -q ok; then return 0; fi
    log "adb retry $_"
    sleep 5
  done
  return 1
}

deploy(){
  log "build native+apk"
  (cd "$ROOT" && pwsh tools/build_native.ps1 >/dev/null && ./gradlew assembleDebug -q) || return 1
  APK="$ROOT/app/build/outputs/apk/debug/app-debug.apk"
  log "push+install"
  "$ADB" push "$APK" /data/local/tmp/silk-debug.apk || return 1
  "$ADB" shell pm install -r /data/local/tmp/silk-debug.apk || return 1
}

teleport(){
  "$ADB" shell su -c "am start-foreground-service -a theappninjas.gpsjoystick.TELEPORT --ef lat $ANCHOR_LAT --ef lng $ANCHOR_LON" 2>/dev/null \
    || "$ADB" shell "am start-foreground-service -a theappninjas.gpsjoystick.TELEPORT --ef lat $ANCHOR_LAT --ef lng $ANCHOR_LON" 2>/dev/null || true
}

start_stack(){
  teleport
  if ! "$ADB" shell pidof com.nianticlabs.pokemongo >/dev/null 2>&1; then
    "$ADB" shell monkey -p com.nianticlabs.pokemongo -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
    sleep 35
  fi
  "$ADB" shell am force-stop dev.silkraid.app 2>/dev/null || true
  sleep 1
  "$ADB" shell am start-foreground-service -n dev.silkraid.app/dev.silphmod.nativecheck.CheckService \
    -a dev.silphmod.nativecheck.START --ei mode 1 2>/dev/null || true
  log "stack started anchor=$ANCHOR_LAT,$ANCHOR_LON"
}

mkdir -p "$CAPDIR"
chmod +x "$ROOT/tools/raid_caps_loop.sh" 2>/dev/null || true
RAID_CAPS_DIR="$CAPDIR" "$ROOT/tools/raid_caps_loop.sh" &
CAP_PID=$!

log "iterate log=$LOG caps=$CAPDIR"
ensure_adb || { log "adb unavailable"; exit 1; }
deploy || log "deploy failed (continuing with existing apk)"
start_stack

while true; do
  if "$ADB" logcat -d -s SilphRaid:I 2>/dev/null | grep -q 'capture confirmed'; then
    log "SUCCESS capture confirmed"
    kill "$CAP_PID" 2>/dev/null || true
    exit 0
  fi
  "$ADB" logcat -d -s SilphRaid:I 2>/dev/null | grep -E 'scan chosen|join armed|join lobby|start raid|capture confirmed|reject egg|active value' | tail -3 >> "$LOG" || true
  if ! "$ADB" shell pidof dev.silkraid.app >/dev/null 2>&1; then start_stack; fi
  if ! "$ADB" shell pidof com.nianticlabs.pokemongo >/dev/null 2>&1; then start_stack; fi
  sleep 20
done
