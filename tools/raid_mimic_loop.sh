#!/usr/bin/env bash
# Adaptive mimic: screenshot every 5s, dismiss popups, log SilphRaid
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="${ROOT}/tools/raid-capture/run-$(date -u +%Y%m%d-%H%M%S)"
mkdir -p "$DIR"
ADB="$ROOT/tools/adb_remote.sh"
LAT=41.671948
LON=-0.885460
LOG="$DIR/mimic.log"
log(){ echo "$(date -u +%H:%M:%S) $*" | tee -a "$LOG"; }

teleport(){
  "$ADB" shell su -c "am start-foreground-service -a theappninjas.gpsjoystick.TELEPORT --ef lat $LAT --ef lng $LON" 2>/dev/null || true
}

dismiss_popups(){
  "$ADB" shell input tap 360 1240 2>/dev/null || true
  sleep 1
  "$ADB" shell input tap 360 640 2>/dev/null || true
}

ensure_pogo(){
  local focus
  focus=$("$ADB" shell dumpsys window 2>/dev/null | { grep mCurrentFocus || true; } | head -1)
  if [[ "$focus" != *pokemongo* ]]; then
    "$ADB" shell monkey -p com.nianticlabs.pokemongo -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
    sleep 3
  fi
}

log "run dir=$DIR anchor=$LAT,$LON"
teleport
ensure_pogo
sleep 20
dismiss_popups
sleep 3
dismiss_popups
teleport
sleep 5
"$ADB" logcat -c 2>/dev/null || true
"$ADB" shell am start-foreground-service -n dev.silkraid.app/dev.silphmod.nativecheck.CheckService \
  -a dev.silphmod.nativecheck.START --ei mode 1 2>/dev/null || true
log "raid service started"

STEPS="${1:-72}"
for i in $(seq 1 "$STEPS"); do
  f="$DIR/step_$(printf '%03d' "$i").png"
  if "$ADB" exec-out screencap -p > "$f" 2>/dev/null && [[ -s "$f" ]]; then
    sz=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null || echo 0)
    log "shot $i size=$sz"
  else
    log "shot $i FAILED"
  fi
  ensure_pogo
  # periodic dismiss first 6 shots
  if [[ "$i" -le 6 ]]; then dismiss_popups; fi
  "$ADB" logcat -d -s SilphRaid:I 2>/dev/null | grep -E 'gym scan|egg|chosen|teleport|join|lobby|battle|stage completed|dismiss gui|anchor|empty err|no joinable' | tail -8 >> "$LOG" || true
  sleep 5
done
log "done steps=$STEPS"
