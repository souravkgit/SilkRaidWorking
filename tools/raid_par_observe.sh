#!/usr/bin/env bash
# Run PoGoHost PAR raid once and capture logs + screenshots (reference for SilkRaid).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ADB="$ROOT/tools/adb_remote.sh"
CAPDIR="${RAID_PAR_CAPS:-/tmp/silkraid-par-observe-$(date -u +%Y%m%d-%H%M%S)}"
LOG="${RAID_PAR_LOG:-/tmp/silkraid-par-observe.log}"
DUR="${RAID_PAR_SEC:-600}"
ANCHOR_LAT="${RAID_ANCHOR_LAT:-41.671948}"
ANCHOR_LON="${RAID_ANCHOR_LON:--0.885460}"

log(){ echo "$(date -u +%H:%M:%S) $*" | tee -a "$LOG"; }
mkdir -p "$CAPDIR"

log "PAR observe caps=$CAPDIR duration=${DUR}s"
"$ADB" shell am force-stop dev.silkraid.app 2>/dev/null || true
"$ADB" shell su -c "am start-foreground-service -a theappninjas.gpsjoystick.TELEPORT --ef lat $ANCHOR_LAT --ef lng $ANCHOR_LON" 2>/dev/null || true
"$ADB" logcat -c 2>/dev/null || true
"$ADB" shell am start-foreground-service -n dev.pluteo.pogohost/.raid.RaidRunService -a dev.pluteo.pogohost.RAID_START 2>/dev/null \
  || "$ADB" shell am startservice -n dev.pluteo.pogohost/.raid.RaidRunService -a dev.pluteo.pogohost.RAID_START 2>/dev/null || true
log "PoGoHost RaidRunService started"

end=$(( $(date +%s) + DUR ))
i=0
while [[ $(date +%s) -lt $end ]]; do
  ts="$(date -u +%H:%M%S)"
  "$ADB" exec-out screencap -p > "$CAPDIR/$ts.png" 2>/dev/null || true
  "$ADB" logcat -d 2>/dev/null | grep -iE 'raid-scan|raid-flow|RaidGymPick|raid-nav|bonus catch|BONUS_CATCH' | tail -5 >> "$LOG" || true
  i=$((i+1))
  if [[ $((i % 12)) -eq 0 ]]; then log "tick screenshots=$i"; fi
  sleep 5
done
log "PAR observe done"
