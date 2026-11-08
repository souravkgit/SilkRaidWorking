#!/usr/bin/env bash
# Run from a persistent terminal (tmux/screen). Survives SIGHUP via setsid.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export SSHPASS="${SSHPASS:?Set SSHPASS for adb_remote.sh}"
exec >>"$ROOT/tools/raid-capture/overnight.log" 2>&1
echo "=== overnight $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
while true; do
  "$ROOT/tools/raid_autopilot.sh" 240 || true
  echo "cycle sleep 30s $(date -u +%H:%M:%S)"
  sleep 30
done
