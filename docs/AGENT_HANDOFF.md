# SilkRaid — Agent Handoff Context

## Project and goal

**Repository:** `/Users/grind/development/PokemonGo/SilkRaid`  
**Product:** Android app (`dev.silkraid.app`) that injects native code into **Pokémon GO** and automates raids end-to-end on a **physical device** controlled over **ADB**.

**Target workflow (CheckService mode 1):**

1. Scan map for raid gyms near anchor  
2. Teleport to gym (mock location via PGTools / GPS Joystick)  
3. Join lobby (UI + RPC)  
4. Battle  
5. Capture boss  
6. Repeat without manual steps  

**Success criterion (not yet achieved on device):** sustained native control connection, `SilphRaid` log progression (`map ready` → `join armed` → RPC join → battle → **`capture confirmed`**), and session JSONL without crashy recovery loops.

**Git:** Do **not** commit unless the user explicitly asks.

---

## Device and environment

| Item | Value |
|------|--------|
| Host OS | macOS (user); device is remote |
| ADB | `tools/adb_remote.sh` → SSH tunnel `localhost:53698` (override `ADB_PORT`) to `s@128.14.170.195:1824`; needs **`SSHPASS`** for tunnel |
| Device | Root OPPO; PoGO + SilkRaid installed |
| PoGO version | **0.435.1** (also supports **0.433.1** in code) |
| arm64 APK SHA (0.435.1) | `88cff795fca76d05e4bd4e97dd147a50a0b52450ec48c3a82f2804e90d88c356` |
| Mock location | PGTools / GPS Joystick — action `theappninjas.gpsjoystick.TELEPORT` |
| **Raid anchor (current default)** | **Osaka:** `34.693738, 135.502165` (was León / other defaults; migrated in `NativeSettings.java`) |

---

## How to build, install, run

```bash
cd /Users/grind/development/PokemonGo/SilkRaid

# Native (.so into app jniLibs)
pwsh tools/build_native.ps1

# APK
./gradlew assembleDebug

# Install
tools/adb_remote.sh install -r app/build/outputs/apk/debug/app-debug.apk
```

**Start raid check (PoGO should be foreground on map, teleported to anchor):**

```bash
tools/adb_remote.sh shell am start-foreground-service \
  -n dev.silkraid.app/dev.silphmod.nativecheck.CheckService \
  -a dev.silphmod.nativecheck.START --ei mode 1
```

**Teleport (example):**

```bash
tools/adb_remote.sh shell su -c \
  "am start-foreground-service -a theappninjas.gpsjoystick.TELEPORT --ef lat 34.693738 --ef lng 135.502165"
```

**Monitoring:**

- Logcat: `SilphRaid`, `SilphRpc`, `SilphRuntime`  
- Sessions: `run-as dev.silkraid.app` → `files/sessions/session-*.jsonl`  
- Optional: `tools/raid_watch.sh` (caps + log tail until `capture confirmed`)  
- Live pilot metrics: `libsilph-raid-status.so <pid> <startTicks> <pilotAddress> <nonce>` via `su` (values from session loader line `Unity pilot: address=... nonce=...`)

**Operational rules that matter on device:**

- Do **not** force-stop PoGO mid-connect (causes `Game process changed`, `client_exit` 3, re-inject loops).  
- Prefer: map visible at Osaka → then start CheckService.  
- Cold launch + immediate inject often yields PID/tick mismatch and Play Store / SIGSEGV during recovery.

---

## Architecture (high level)

```
CheckService (Java)
  └─ NativeCheckRunner.java
       ├─ ptrace inject libsilph-raid-check.so into PoGO
       ├─ Unity pilot hook (map callback + Zeppelin Scheduler Update pump)
       ├─ Process: libsilph-control.so (su) ↔ socket ↔ in-process control server
       └─ NativeSessionController + NativeRaidHost (mode 1)

In PoGO process (libsilph-raid-check.so, built with many -DSILPH_* flags):
  unity-pilot.c   — hook, scheduler, external control loop, farmer_view pump
  raid-session.c  — raid FSM: anchor → scan → approach → join → battle → capture
  raid-bridge.inc / presentation-tools.c — GUI dismiss (welcome, GlobalError, raid dialogs)
  game-read.c     — map/gym reads
  managed-rpc.inc / rpc-actions.c — IL2CPP RPC (join etc.; array shim for missing symbols on some builds)
  control.c       — pulse/stop/engine commands from Java
  control-client.c — separate process; exit codes 3=ticks mismatch, 11=reply timeout
```

**Native build defines (from `tools/build_native.ps1`):** includes `SILPH_PILOT_FRAME`, `SILPH_PILOT_EXTERNAL`, `SILPH_PILOT_RAID`, `SILPH_PILOT_SERVICE`, `SILPH_PILOT_COMBINED`, `SILPH_PILOT_GAME_READ`, `SILPH_PILOT_ACTIONS`, etc.

---

## Key files (where to edit)

| Area | Path |
|------|------|
| Raid FSM, startup GUI pump, join, capture | `native/raid-session.c` (~1910 lines) |
| Scheduler / map hook / control loop / **farmer_pump** | `native/unity-pilot.c` |
| GUI actions (welcome, GlobalError, raid UI) | `native/raid-bridge.inc`, `native/presentation-tools.c` |
| RPC / array shim | `native/managed-rpc.inc`, `native/unity-pilot.c`, `native/rpc-actions.c` |
| Gym scan logging | `native/game-read.c` |
| Injection, recovery, progress stall | `app/src/main/java/dev/silphmod/nativecheck/NativeCheckRunner.java` |
| Stall detection grace | `app/src/main/java/dev/rootcatch/nativeengine/NativeProgressWatch.java` |
| Anchor / prefs | `app/src/main/java/dev/silphmod/nativecheck/NativeSettings.java`, `NativeRaidHost.java` |
| Control client PID/ticks | `native/control-client.c` |
| Recovery policy | `app/src/main/java/dev/silphmod/nativecheck/NativeRecoveryPlan.java` |
| ADB tunnel | `tools/adb_remote.sh` |

---

## Problems diagnosed (historical + current)

### 1. Version gate (fixed)

`NativeCheckRunner` initially rejected **0.435.1**. Now accepts 0.433.1 and 0.435.1 with separate `GAME_SHA_*` and `refreshLiveGameIdentity()` before control client.

### 2. Control disconnect `client_exit` 3

`libsilph-control.so` exits when `/proc/<pid>/stat` start ticks ≠ ticks captured at inject (PoGO restart or PID change during startup).

### 3. `client_exit` 11

Control client **reply timeout** (~1.5s). Triggered when attempting **`silph_schedule_tick` inside the map proxy callback** — likely scheduler lock deadlock with control thread. **That approach was reverted.** Do not re-add heavy work in map callback without verifying control latency.

### 4. “Native engine progress stalled”

Java `NativeProgressWatch`: after ~65s foreground with no snapshot sequence change → `ProgressStalled` → recovery (often force-stop / relaunch PoGO → crashes in `libNianticLabsPlugin.so`).

**Mitigations added:**

- Mode 1 pre-work: **180s** stall threshold (`NativeProgressWatch.check(..., stallMs)`).  
- If stall pending but pilot shows map progress (`stops` / `accountHi`), refresh snapshot timer and continue.  
- Native: startup welcome pump no longer gated on `map_stops >= 8` (chicken-and-egg with welcome screen).

### 5. Scheduler never executes raid jobs (`executed=0`, `queued=1`)

With **`SILPH_PILOT_FRAME`**, map `proxy()` returns early after `prepare_view()`; real ticks happen in **`farmer_pump`** (Zeppelin `Scheduler.Update` hook).

**Root cause:** `farmer_pump` used to **`return` until `retire_map_callback()`** succeeded (unmap hook retirement). On welcome/loading, map keeps firing but pump never ran → jobs queued forever, snapshots stuck at `sequence=0`.

**Mitigations added:**

- **Raid build:** do not block pump on retire failure; best-effort retire only (`#ifdef SILPH_PILOT_RAID` in `unity-pilot.c`).  
- When queue non-empty, treat tick as **visible** even if map reports not visible (`farmer_pump` + non-FRAME path).

### 6. Welcome / location UI

GlobalError (location), welcome/safety dialogs block map. Startup pump walks top GUI + stack + layers; GlobalError triggers re-anchor to config anchor (Osaka).

### 7. RPC / join (earlier work in thread)

` silph_rpc_enable`, array shim for missing `il2cpp_array_addr*` on 0.433.1; approach cutscene blocking join RPC relaxed (8–12s); gym scan logging in `game-read.c`.

### 8. Recent clean-run failure pattern

3-minute scripted run: teleport → cold PoGO → CheckService → **`raid_configuration` OK** → then **`Game process changed`** within seconds; **no** `SilphRaid` progress in logcat. Re-starting CheckService **without** force-stopping PoGO is the recommended retry.

---

## Changes made in latest sessions (may be uncommitted)

Verify with `git diff` / `git status`.

**Java:**

- `NativeCheckRunner.java`: 0.435.1 support, `refreshLiveGameIdentity`, shorter pre-control wait, extended stall logic for mode 1.  
- `NativeProgressWatch.java`: overload `check(now, active, stallMs)`.

**Native:**

- `raid-session.c`: startup GUI pump without map_stops gate; `dirty=1` after pump.  
- `unity-pilot.c`: raid pump without blocking on map-hook retire; queued-job visible tick in `farmer_pump`; **removed** map-callback `schedule_tick` (caused exit 11).

**Defaults:** Osaka anchor in settings / host / tools.

---

## Session JSONL events (how to read)

Useful grep targets:

- `raid_configuration` — anchor, flags  
- `control` + `native_control_connected` — attach OK  
- `native_snapshot` — should increment `sequence` when raid publishes  
- `native_control_reply` — watch `executed`, `queued`, `state` (1=RUNNING)  
- `connection_error`, `client_exit`, `progress_watch`, `native_stall_status`  
- `native_finished` — reason string  

Example failure: only one `native_snapshot` with `sequence=0`, many control replies with `executed=0`, `queued=1`, then `ProgressStalled` or `Game process changed`.

---

## Unity pilot status reader (debug)

From session loader JSON (embedded in `event=loader`):

- `Unity pilot: address=0x... nonce=... resident=1 readOnly=0 controlKeyLo=...`  
- `pid`, `startTicks` from same loader / `startup_memory_maps`

Fields to watch when stuck:

- `callbacks` high but `jobs=0`, `gameActions=0`, `mapVisible=0`, `stops=0`, `accountHi=0` → still bootstrapping or pump not executing  
- `viewPumps`, `viewUpdates` (if present) → farmer_view activity  
- `controlPulses` increasing → Java side OK  
- `error` non-zero → native pilot error code  

---

## What is **not** done (honest status)

- **No verified end-to-end raid** on the current device run (no `capture confirmed` in logcat).  
- Injection + control connect **sometimes** works; stability through map load, scan, join, battle is **not** proven.  
- Recovery loops (PoGO SIGSEGV, Play Store foreground) still observed when stall/transport recovery fires.  
- User wants **working completed app**, not partial fixes.

---

## Suggested priority for next agent

1. **Stable attach:** PoGO already on map at Osaka; inject once; no force-stop during first 3–5 minutes; fix any remaining tick/PID race in `NativeCheckRunner` / control client if `client_exit` 3 persists.  
2. **Prove scheduler execution:** After connect, confirm `executed` > 0, `gameActions` > 0, multiple `native_snapshot` sequences, `SilphRaid` log lines (startup dismiss, `map ready`).  
3. **If still `executed=0`:** Debug `farmer_view` open (`prepare_view`), `visual_allowed` gating in `farmer-view.c` (rocket mode should classify as MAP), scheduler `foreground` from pulses, and whether `SILPH_PILOT_RAID` retire bypass is in the **installed** `.so` (check `NativeArtifacts` hash / rebuild install).  
4. **Join path:** Once phase 3/5 works, validate RPC op=14 / join lobby in logs.  
5. **Soften recovery:** Avoid relaunching PoGO on pre-work stall; require explicit transport failure.  
6. **Do not** commit unless user asks.

---

## User preferences (from rules)

- Run commands yourself; real ADB/network environment.  
- Minimal code diffs; match existing style.  
- PRs/commits only when requested.

---

## Prior conversation transcript (optional)

`/Users/grind/.cursor/projects/Users-grind-development-PokemonGo-SilkRaid/agent-transcripts/380e7f3f-c86d-4ce4-bde7-a14bf7b55207/380e7f3f-c86d-4ce4-bde7-a14bf7b55207.jsonl`  

Search: `ProgressStalled`, `client_exit`, `Osaka`, `0.435.1`, `executed`, `raid_pump_startup_guis`.

---

## Elevator pitch

SilkRaid injects a native raid automator into Pokémon GO 0.435.1 on a rooted phone over ADB, using an external control process and a Unity map/scheduler hook to run `raid-session.c`. The user wants fully automated raids from Osaka anchor without manual steps. The app connects intermittently but fails before completing a raid because of PID/tick mismatches, premature “progress stalled” recovery, welcome/map bootstrap deadlock, and a frame-mode scheduler pump that previously never ran jobs until map-hook retirement. Recent fixes address version gate, Osaka anchor, welcome pump gating, stall grace, and raid-specific pump gating—but **capture confirmed** is still unverified; next work is stable connect + proof of job execution + join/capture on device without killing PoGO mid-session.
