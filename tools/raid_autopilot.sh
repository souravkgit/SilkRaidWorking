#!/usr/bin/env bash
# App-first raid loop: screenshot every 5s, intervene if stuck >60s, rotate NYC anchors
set -u
SSHPASS="${SSHPASS:-}"
export SSHPASS
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ADB="$ROOT/tools/adb_remote.sh"
RUN="$ROOT/tools/raid-capture/autopilot-$(date -u +%Y%m%d-%H%M%S)"
mkdir -p "$RUN"
LOG="$RUN/autopilot.log"
STUCK_FILE="$RUN/.stuck_since"
ANCHORS=(
  "41.671948,-0.885460"
  "41.673000,-0.884000"
  "41.670500,-0.887000"
)
ANCHOR_I=0

log(){ echo "$(date -u +%H:%M:%S) $*" | tee -a "$LOG"; }

teleport(){
  local pair="${ANCHORS[$ANCHOR_I]}"
  local lat="${pair%,*}" lon="${pair#*,}"
  "$ADB" shell su -c "am start-foreground-service -a theappninjas.gpsjoystick.TELEPORT --ef lat $lat --ef lng $lon" 2>/dev/null || true
  log "teleport anchor=$pair"
}

dismiss(){
  "$ADB" shell input tap 360 1240 2>/dev/null || true
  sleep 1
  "$ADB" shell input tap 360 640 2>/dev/null || true
  "$ADB" shell input tap 360 1050 2>/dev/null || true
}

focus_pogo(){
  local f
  f=$("$ADB" shell dumpsys window 2>/dev/null | grep mCurrentFocus | head -1 || true)
  if [[ "$f" != *pokemongo* ]]; then
    "$ADB" shell monkey -p com.nianticlabs.pokemongo -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
    sleep 4
  fi
}

start_raid_service(){
  focus_pogo
  "$ADB" shell am start-foreground-service -n dev.silkraid.app/dev.silphmod.nativecheck.CheckService \
    -a dev.silphmod.nativecheck.START --ei mode 1 2>/dev/null || true
  log "raid service start requested"
}

rotate_anchor(){
  ANCHOR_I=$(( (ANCHOR_I + 1) % ${#ANCHORS[@]} ))
  teleport
}

intervene(){
  local reason="$1"
  log "INTERVENTE: $reason"
  dismiss
  focus_pogo
  "$ADB" shell input tap 360 520
  sleep 3
  "$ADB" shell input tap 360 720
  sleep 2
  "$ADB" shell input tap 360 720
}

check_logs(){
  "$ADB" logcat -d -s SilphRaid:I 2>/dev/null | grep -E 'gym scan chosen|post-battle|bonus encounter|capture confirmed|stage completed|abandon|error=' | tail -8 >> "$LOG" || true
}

LAST_HASH=""
trap 'log "autopilot exit code=$?"' EXIT
log "autopilot run=$RUN"
teleport
"$ADB" shell am force-stop com.nianticlabs.pokemongo 2>/dev/null || true
sleep 2
teleport
"$ADB" shell monkey -p com.nianticlabs.pokemongo -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
sleep 28
dismiss
sleep 3
dismiss
teleport
"$ADB" logcat -c 2>/dev/null || true
start_raid_service

STEPS="${1:-720}"
i=0
set +e
while [[ "$i" -lt "$STEPS" ]]; do
  i=$((i + 1))
  f="$RUN/step_$(printf '%04d' "$i").png"
  if "$ADB" exec-out screencap -p > "$f" 2>/dev/null && [[ -s "$f" ]]; then
    sz=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null || echo 0)
    log "shot $i size=$sz"
  else
    log "shot $i FAILED"
  fi
  focus_pogo
  if [[ "$i" -le 8 ]]; then dismiss; fi
  check_logs
  if "$ADB" logcat -d -s SilphRaid:I 2>/dev/null | grep -q 'capture confirmed'; then
    log "SUCCESS capture confirmed by app"
    break
  fi
  hash=""
  if [[ -s "$f" ]]; then hash=$(md5 -q "$f" 2>/dev/null || md5sum "$f" 2>/dev/null | awk '{print $1}'); fi
  if [[ -n "$hash" && -n "${LAST_HASH:-}" && "$hash" == "$LAST_HASH" ]]; then
    if [[ ! -f "$STUCK_FILE" ]]; then echo "$(date +%s)" > "$STUCK_FILE"; fi
    since=$(cat "$STUCK_FILE")
    now=$(date +%s)
    if (( now - since > 60 )); then
      intervene "stuck>60s hash=$hash"
      rm -f "$STUCK_FILE"
      LAST_HASH=""
      if (( i % 120 == 0 )); then rotate_anchor; start_raid_service; fi
    fi
  else
    rm -f "$STUCK_FILE"
    LAST_HASH="$hash"
  fi
  if (( i % 24 == 0 )); then
    if ! "$ADB" logcat -d -s SilphRaid:I 2>/dev/null | tail -30 | grep -qE 'gym scan chosen|teleporting to gym|raid lobby'; then
      rotate_anchor
      sleep 5
      start_raid_service
    fi
  fi
  sleep 5
done
log "autopilot finished steps=$i"
