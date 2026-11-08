#!/usr/bin/env bash
# One-shot: SSH tunnel + adb + start SilkRaid raid mode (PoGO must be on the map).
set -euo pipefail
ADB_PORT="${ADB_PORT:-55321}"
SSH_HOST="${SSH_HOST:-s@128.14.170.195}"
SSH_PORT="${SSH_PORT:-1824}"
APK="${APK:-app/build/outputs/apk/debug/app-debug.apk}"

if [[ -z "${SSHPASS:-}" ]]; then
  echo "Set SSHPASS to your SSH password, then re-run." >&2
  exit 1
fi

if ! nc -z localhost "$ADB_PORT" 2>/dev/null; then
  sshpass -e ssh -oStrictHostKeyChecking=accept-new -p "$SSH_PORT" -L "${ADB_PORT}:localhost:1" -N "$SSH_HOST" &
  sleep 4
fi

adb connect "localhost:${ADB_PORT}"
adb -s "localhost:${ADB_PORT}" wait-for-device

if [[ -f "$APK" ]]; then
  adb -s "localhost:${ADB_PORT}" push "$APK" /data/local/tmp/silkraid.apk
  adb -s "localhost:${ADB_PORT}" shell pm install -r /data/local/tmp/silkraid.apk
fi

adb -s "localhost:${ADB_PORT}" shell am start-foreground-service \
  -n dev.silkraid.app/dev.silphmod.nativecheck.CheckService \
  -a dev.silphmod.nativecheck.START --ei mode 1

echo "Raid service started. Keep Pokémon GO on the map and GPS Joystick enabled."
