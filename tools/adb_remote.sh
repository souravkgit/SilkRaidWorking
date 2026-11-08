#!/usr/bin/env bash
# adb via SSH tunnel localhost:55321 with auto-reconnect
set -uo pipefail
ADB_PORT="${ADB_PORT:-56975}"
SSH_HOST="${SSH_HOST:-s@128.14.170.195}"
SSH_PORT="${SSH_PORT:-1824}"
export SSHPASS="${SSHPASS:-wDqeeanN+9ZvFku507Mayfjx8bC5sbdepvPY45KnA/Ym9Kc2iEcnrfC5PvX8ZnIezI3Ii0w+309qNkQlAm0VoRvFWwtoYONInPqHz+TeIpG8gwzbIVz2HeXQ8ZKWIHNTvUROyHIJ7gzHMcMV/8v7t9NSKJVQEEwGfL1AyPqBhimtOWXCGF9Gm8bGj/7fj+G0+fjAHwFEwI8rMVylOuzYTnhjqQDleYg85Xgw1k507gD6}"
ADB_SERIAL="localhost:${ADB_PORT}"

kill_tunnel() {
  local pids
  pids=$(lsof -ti :"$ADB_PORT" 2>/dev/null || true)
  if [[ -n "$pids" ]]; then
    kill $pids 2>/dev/null || true
    sleep 1
  fi
}

adb_alive() {
  python3 -c '
import subprocess, sys
try:
    p = subprocess.run(["adb", "-s", sys.argv[1], "shell", "echo", "ok"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=4)
    sys.exit(0 if b"ok" in p.stdout else 1)
except Exception:
    sys.exit(1)
' "$ADB_SERIAL"
}

start_tunnel() {
  [[ -n "${SSHPASS:-wDqeeanN+9ZvFku507Mayfjx8bC5sbdepvPY45KnA/Ym9Kc2iEcnrfC5PvX8ZnIezI3Ii0w+309qNkQlAm0VoRvFWwtoYONInPqHz+TeIpG8gwzbIVz2HeXQ8ZKWIHNTvUROyHIJ7gzHMcMV/8v7t9NSKJVQEEwGfL1AyPqBhimtOWXCGF9Gm8bGj/7fj+G0+fjAHwFEwI8rMVylOuzYTnhjqQDleYg85Xgw1k507gD6}" ]] || { echo "SSHPASS not set" >&2; return 1; }
  kill_tunnel
  sshpass -e ssh -oStrictHostKeyChecking=accept-new -o ServerAliveInterval=15 -o ServerAliveCountMax=4 \
    -p "$SSH_PORT" -L "${ADB_PORT}:localhost:1" -N "$SSH_HOST" &
  sleep 5
}

ensure_tunnel() {
  if nc -z localhost "$ADB_PORT" 2>/dev/null && adb_alive; then
    return 0
  fi
  adb disconnect "$ADB_SERIAL" >/dev/null 2>&1 || true
  adb kill-server >/dev/null 2>&1 || true
  sleep 1
  start_tunnel || return 1
  adb connect "$ADB_SERIAL" >/dev/null 2>&1
  sleep 1
  adb_alive
}

adb_remote() {
  ensure_tunnel || return 1
  if ! adb_alive; then
    adb kill-server >/dev/null 2>&1 || true
    sleep 1
    ensure_tunnel || return 1
  fi
  adb -s "$ADB_SERIAL" "$@"
}

adb_remote "$@"
