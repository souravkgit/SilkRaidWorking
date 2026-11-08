#!/usr/bin/env bash
# Wait for ADB, install latest build, restart raid service, poll until done.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERIAL="${ADB_SERIAL:-localhost:60406}"
ADB="${ADB:-adb -s $SERIAL}"
PKG=dev.silkraid.app
POGO=com.nianticlabs.pokemongo/com.nianticproject.holoholo.libholoholo.unity.UnityMainActivity
SVC="$PKG/dev.silphmod.nativecheck.CheckService"

wait_adb() {
  while true; do
    adb connect "$SERIAL" >/dev/null 2>&1 || true
    if $ADB shell echo ok >/dev/null 2>&1; then return 0; fi
    echo "waiting for adb $SERIAL..."
    sleep 15
  done
}

build_install() {
  cd "$ROOT"
  pwsh -NoProfile -File tools/build_native.ps1 >/dev/null || return 1
  ./gradlew assembleDebug -q || return 1
  $ADB install -r app/build/outputs/apk/debug/app-debug.apk >/dev/null || return 1
}

start_raid() {
  $ADB logcat -c 2>/dev/null || true
  $ADB shell am force-stop "$PKG" 2>/dev/null || true
  $ADB shell am force-stop com.nianticlabs.pokemongo 2>/dev/null || true
  sleep 2
  $ADB shell am start -n "$POGO" >/dev/null 2>&1 || true
  sleep 5
  $ADB shell am start-foreground-service -a dev.silphmod.nativecheck.START -n "$SVC" >/dev/null 2>&1 || true
}

check_done() {
  $ADB logcat -d -s SilphRaid 2>/dev/null | grep -qE "raid battle rpc accepted|capture confirmed|native shadow result completed"
}

wait_adb
attempt=0
while true; do
  attempt=$((attempt + 1))
  echo "== dev-loop attempt $attempt $(date +%H:%M:%S) =="
  build_install || { sleep 30; continue; }
  start_raid
  inner=$((SECONDS + 900))
  while (( SECONDS < inner )); do
    if check_done; then
      echo "SUCCESS"
      $ADB logcat -d -s SilphRaid 2>/dev/null | tail -40
      exit 0
    fi
    sleep 10
    $ADB logcat -d -s SilphRaid 2>/dev/null | tail -4
  done
  echo "attempt $attempt timeout; retrying..."
done
