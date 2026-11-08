#!/usr/bin/env bash
set -euo pipefail
ADB="${ADB:-adb -s localhost:57866}"
PKG=dev.silkraid.app
POGO=com.nianticlabs.pokemongo/com.nianticproject.holoholo.libholoholo.unity.UnityMainActivity

echo "== install =="
$ADB install -r app/build/outputs/apk/debug/app-debug.apk >/dev/null

echo "== start raid service =="
$ADB shell am force-stop "$PKG" || true
$ADB shell am force-stop com.nianticlabs.pokemongo || true
sleep 2
$ADB shell am start -n "$POGO" >/dev/null
sleep 3
$ADB shell am start-foreground-service -a dev.silphmod.nativecheck.START -n "$PKG/dev.silphmod.nativecheck.CheckService"
$ADB shell am start -n "$POGO" >/dev/null

deadline=$((SECONDS + 120))
ok=""
while (( SECONDS < deadline )); do
  if $ADB logcat -d -s SilphRaid 2>/dev/null | grep -q "join lobby ok"; then ok=join; break; fi
  if $ADB logcat -d -s SilphRaid 2>/dev/null | grep -q "raid_move"; then ok=move; fi
  if $ADB logcat -d -s SilphRaid 2>/dev/null | grep -q "raid battle rpc accepted"; then ok=battle; break; fi
  if $ADB shell "run-as $PKG tail -3 files/sessions/\$(run-as $PKG ls -t files/sessions 2>/dev/null | head -1)" 2>/dev/null | grep -q '"finished"'; then ok=finished; break; fi
  sleep 8
done

echo "== SilphRaid tail =="
$ADB logcat -d -s SilphRaid 2>/dev/null | tail -25
echo "== result: ${ok:-timeout} =="
test -n "$ok"
