#!/usr/bin/env bash
set -euo pipefail
ADB="${ADB:-adb -s localhost:60406}"
PKG=dev.silkraid.app
POGO=com.nianticlabs.pokemongo/com.nianticproject.holoholo.libholoholo.unity.UnityMainActivity
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

cd "$ROOT"
pwsh -NoProfile -File tools/build_native.ps1 >/dev/null
./gradlew assembleDebug -q
$ADB install -r app/build/outputs/apk/debug/app-debug.apk >/dev/null

attempt=0
deadline=$((SECONDS + 3600))
while (( SECONDS < deadline )); do
  attempt=$((attempt + 1))
  echo "== attempt $attempt =="
  $ADB logcat -c 2>/dev/null || true
  $ADB shell am force-stop "$PKG" || true
  $ADB shell am force-stop com.nianticlabs.pokemongo || true
  sleep 2
  $ADB shell am start -n "$POGO" >/dev/null
  sleep 4
  $ADB shell am start-foreground-service -a dev.silphmod.nativecheck.START -n "$PKG/dev.silphmod.nativecheck.CheckService"
  $ADB shell am start -n "$POGO" >/dev/null

  inner=$((SECONDS + 600))
  ok=""
  while (( SECONDS < inner )); do
    log=$($ADB logcat -d -s SilphRaid 2>/dev/null || true)
    if echo "$log" | grep -q "join lobby ok"; then ok=join; fi
    if echo "$log" | grep -q "raid battle rpc accepted"; then ok=battle; fi
    if echo "$log" | grep -q "capture confirmed"; then ok=capture; fi
    if echo "$log" | grep -q "native shadow result completed"; then ok=done; fi
    if [[ -n "$ok" ]]; then break; fi
    sleep 5
  done

  echo "$log" | tail -35
  echo "== attempt $attempt result: ${ok:-still trying} =="
  if [[ -n "$ok" ]]; then
    echo "SUCCESS: $ok"
    exit 0
  fi
done
echo "FAILED after 900s"
exit 1
