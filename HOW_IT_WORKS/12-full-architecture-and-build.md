# 12 — Full architecture, build, wire protocol, recovery

This is the "everything else" reference: the complete file inventory, the build pipeline, the
Java↔native wire protocol, and the supervision/recovery/cleanup loop. Together with `01`–`11` it is
sufficient to rebuild the app.

---

## 1. Complete file inventory

### App (Java) — `app/src/main/java/`

**UI + settings**
- `dev/silphmod/nativecheck/MainActivity.java` — the single-activity UI (settings tabs, Start/Stop,
  the "Raid boss" tile, pass/team/berry dialogs).
- `dev/silphmod/nativecheck/NativeSettings.java` — reads/writes `native-settings` prefs and builds
  the `NativeSessionWire.Config` (anchor, radius, tier mask, pass mask, boss list, berries, …).
- `dev/silphmod/nativecheck/ModeUi.java`, `SettingsDialogs.java`, `PokemonPickerDialog.java` — UI helpers.
- `dev/rootcatch/automation/PokemonCatalog.java`, `PokemonSelection.java`, `BerryOptions.java`,
  `ThrowQuality.java` — Pokémon dex/form + berry/throw enums (from `assets/pokemon/*.tsv`).

**Service + supervision**
- `dev/silphmod/nativecheck/CheckService.java` — the foreground service; spawns `NativeCheckRunner`.
- `dev/silphmod/nativecheck/NativeCheckRunner.java` — the whole lifecycle: preflight, launch,
  injection, control channel, monitoring, recovery, cleanup (see `01`, `02`).
- `dev/silphmod/nativecheck/NativeSessionController.java` — pumps the JSON control channel, decodes
  snapshots/totals, enqueues commands.
- `dev/silphmod/nativecheck/NativeRaidHost.java` — GPS joystick moves + screen taps + phase acks.
- `dev/silphmod/nativecheck/NativeFocus.java` / `dev/rootcatch/nativeengine/NativeFocus.java`,
  `ForegroundFocus.java`, `FocusReader.java`, `FocusLogPolicy.java` — foreground detection.
- `dev/silphmod/nativecheck/NativeStartupPolicy.java`, `NativeStagePolicy.java`,
  `NativeRecoveryPlan.java`, `NativeProgressWatch.java`, `NativeExitReport.java`,
  `NativeComponentIntegrity.java`, `NativeManualRecord.java` — startup/recovery/cleanup policy.
- `dev/silphmod/nativecheck/CheckLog.java`, `SessionLogFiles.java`, `LogPrivacy.java` — session JSONL logs.
- `dev/silphmod/nativecheck/NativeRocketHost.java` — the Rocket (grunt) mode host (sibling of the raid host).

**Engine glue**
- `dev/rootcatch/nativeengine/NativeSessionWire.java` — the 1024-byte `Config` + snapshot wire codec.
- `dev/rootcatch/nativeengine/NativeSessionState.java` — totals/statistics.
- `dev/rootcatch/nativeengine/NativeKecleonWire.java` — the phase event + command codec.
- `dev/rootcatch/nativeengine/EnginePolicy.java`, `RocketCareOrder.java`, `ItemCatalog.java`,
  `NativeProgressWatch.java` — helpers.
- `dev/rootcatch/root/RootShell.java` — `su` command execution.
- `dev/rootcatch/ugphone/KecleonFeed.java`, `KecleonMovement.java`, `KecleonHost.java`,
  `KecleonQueue.java`, `KecleonHistory.java`, `KecleonRetryPolicy.java` — the GPS joystick client
  (parses/emits mock-location commands).

### Native (C) — `native/` (→ `app/src/main/jniLibs/arm64-v8a/*.so`)

| File(s) | Role |
|---|---|
| `unity-pilot.c` | Engine entry (`silph_native_bootstrap`) + the `Scheduler.Update` pilot loop. |
| `map-observer.c` | Resolves the il2cpp `Scheduler.Update` address + `GuiController` pointer offset (the "map contract"). |
| `managed.c` / `managed.h` | il2cpp managed API wrapper (types/methods/fields, `runtime_invoke`, thread attach). |
| `managed-read.c` / `managed-read.h` | `silph_read_*` — safe typed reads/calls/fields/enums over il2cpp. |
| `game-read.c` / `game-read.h` | Reads game state: gym scan, raid info, boss species, pokemon bag, services. |
| `raid-session.c` / `raid-session.h` | The raid state machine (scan → teleport → tap → pass → join → battle → catch). |
| `raid-bridge.inc` / `raid-bridge.h` | The `RK_*` action operations (`RK_TOP/TAP/GEM/RAID_GUI/GUI_CLOSE/LOBBY/…`) + the `welcome_gui`/`item_gui`/`global_err` handlers. |
| `raid-flow.c` / `raid-flow.h` | Pre-raid presentation shortening (NPC dialogue / combat transitions / summary). |
| `raid-policy.c` / `raid-policy.h` | Raid policy predicates (joinable, ending, tier allowed). |
| `presentation-tools.c` / `presentation-tools.h` | Scene catalogue + onboarding GUI dismissal + gift/manual accelerators + raid gates. |
| `location-tools.c` / `location-tools.h` | Hooks `Location.isFromMockProvider`/`isMock` → false. |
| `pass-actions.c` / `pass-actions.h` | Event-pass reward claims (separate from the raid pass). |
| `rpc-actions.c` / `rpc-actions.h` | Raid/lobby RPC actions (join/confirm/start/throw). |
| `scheduler.c` / `scheduler.h` | The cooperative schedule (leases, permits, jobs) the pilot drives. |
| `control.c` / `control-client.c` | In-engine control + the stdio JSON bridge client. |
| `session-wire.c` / `session-wire.h` | 1024-byte config + snapshot wire codec (CRC). |
| `session-link.c` / `session-link.h` | Shared-memory session link (config lock/retry). |
| `kecleon-wire.c` / `kecleon-wire.h`, `kecleon-actions.c`, `kecleon-plan.c`, `kecleon-session.c` | The "kecleon" event/command protocol + plan. |
| `farmer-actions.c`, `farmer-session.c`, `farmer-view.c` | The generic "farmer" (catch/spin) engine shared with other modes. |
| `promise.c` / `managed-promise.c` | RPC promise tracking (the catch/claim replies). |
| `core.c` / `core.h` | Capture authorization state machine. |
| `visual-tools.c` / `readback.c` | IV/name readback from the caught Pokémon. |
| `maintenance-actions.c`, `gift-actions.c`, `buddy-actions.c`, `buddy-care.c`, `research-actions.c`, `transfer-policy.c`, `recovery-policy.c`, `code-memory.c`, `callback-memory.c`, `startup-thread.c` | Support subsystems. |
| `standalone/probe.c`, `standalone/remote.c`, `standalone/elf-symbol.c` | The ptrace loader (→ `libsilph-native-probe.so`). |
| `pilot-reader.c` (+ `standalone/remote.c`) | The read-only status reader (→ `libsilph-raid-status.so`). |

### Assets & build
- `app/src/main/assets/pokemon/{catalog.tsv,forms.tsv,icons/*}` — the Pokémon database.
- `app/src/main/jniLibs/arm64-v8a/*.so` — the native libraries (output of the build).
- `tools/build_native.ps1` — the native build.
- `.dependencies/shadowhook` — bytedance `android-inline-hook` at revision `854c775c…`.
- `app/build.gradle.kts`, `settings.gradle.kts`, `gradle/` — the Gradle Android build.
- `docs/AGENT_HANDOFF.md`, `README.md` — existing high-level docs.

---

## 2. Build pipeline

### Native (PowerShell, `tools/build_native.ps1`)

```powershell
pwsh tools/build_native.ps1            # NDK via $ANDROID_NDK_HOME or $ANDROID_HOME/ndk/28.2.13676358
```

It compiles with `clang --target=aarch64-linux-android29 -std=c11 -O2 -Werror -fvisibility=hidden
-fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wl,-z,relro,-z,now,-z,noexecstack …`:

1. `libshadowhook.so` + `libshadowhook_nothing.so` (from `.dependencies/shadowhook`).
2. `libsilph-native-probe.so` (PIE): `standalone/probe.c remote.c elf-symbol.c`.
3. `libsilph-control.so` (PIE): `control-client.c control.c scheduler.c session-wire.c kecleon-wire.c` with `-DSILPH_EVENT_WIRE=1`.
4. `libsilph-raid-check.so` (the engine, `-DSILPH_PILOT_RAID=1 -DSILPH_PILOT_COMBINED=1
   -DSILPH_PILOT_SERVICE=1 -DSILPH_PILOT_CONFIGURED=1 -DSILPH_PILOT_ACTIONS=1 -DSILPH_PILOT_GAME_READ=1
   -DSILPH_PILOT_EXTERNAL=1 -DSILPH_ENABLE_ACTIONS=1 -DSILPH_PILOT_FRAME=1 -DSILPH_PRESENT_UNWIND=1 …`):
   `unity-pilot.c managed.c managed-read.c game-read.c scheduler.c control.c promise.c
   managed-promise.c farmer-actions.c transfer-policy.c rpc-actions.c kecleon-actions.c
   maintenance-actions.c session-wire.c session-link.c kecleon-wire.c kecleon-plan.c visual-tools.c
   research-policy.c research-actions.c location-tools.c code-memory.c presentation-tools.c
   buddy-actions.c recovery-policy.c readback.c core.c startup-thread.c callback-memory.c
   gift-actions.c pass-actions.c farmer-view.c raid-policy.c raid-session.c raid-flow.c`.
5. `libsilph-raid-status.so` (PIE): `pilot-reader.c remote.c elf-symbol.c`.

Output → `app/src/main/jniLibs/arm64-v8a/`.

### APK

```bash
./gradlew assembleDebug
# → app/build/outputs/apk/debug/app-debug.apk
```

The `local.properties` must point `sdk.dir` at the Android SDK.

---

## 3. The Java↔native wire protocol

### Config (`NativeSessionWire.Config`, 1024 bytes, little-endian, CRC at offset 1020)

Fixed offsets include: `pid`, `mode`, `ticks`, `sessionHi/Lo`, `flags`, `quality`, `delayMs`, `keep`,
`maxCatches/maxSpins/maxEncounters`, berries (`80..99`), limits (`100..`), `species` (`616..872`),
`filters` (`872`), `durationMs` (`876`), `raidAnchorLat/Lon` (`880/888`), `raidRadiusM` (`896`),
`raidTierMask` (`904`), `raidPassItem` (`908`), `raidMinPlayers` (`912`), `raidGlobalHot` (`916`),
potion/revive order (`920..927`), **`raidBossSpecies` (928), `raidBossSpeciesList[8]` (932..960),
`raidBossCount` (964)**.

### Snapshot (`NativeSessionWire.Snapshot`, 256 bytes, CRC at 252)

`mode,state,error,reason,flags,pid,candidateState,rewardsAvailable,visualAvailable,visualFailed,
researchStatus,locationStatus,ivReads,nameUpdates` + counters (`caught,fled,spun,discarded,
transferred,encounters,shinies,berries,throws,xp,stardust,encounter,captured,candidateRequest`) +
`latitude,longitude`.

### Control channel (JSON lines over `su` stdio)

Java → native (commands) and native → Java (events) as JSON objects. Key events:

- `native_engine_snapshot` — periodic `SilphSessionSnapshot`.
- `native_kecleon_event` — raid phase events (`request`, `phase`, `move`, `reason`, `source`, `fort`,
  `latitude`, `longitude`). The Java `NativeRaidHost` answers with a `native_kecleon_command`.
- `native_control_reply` / `native_control_failure` — command acks / failures.
- `native_finished` — the engine stopped.

---

## 4. The raid phase machine (native `raid-session.c`)

Phases (the `metrics.phase` / `metrics.stage` pair):

| phase | meaning | stage |
|---|---|---|
| 1 | wait for map + dismiss popups | — |
| 3 | scan gyms + teleport | — |
| 4 | approach the gym (incident/grunt) | — |
| 5 | open/join the raid UI | `RS_OPEN` → `RS_UI` → `RS_BATTLE` |
| 6 | return / capture / post-raid | `RS_CAPTURE` / `RS_ENCOUNTER` |
| 7 | done | `RS_DONE` |
| 8 | loss / stop | `RS_LOSS` / `RS_HEAL` |

Stages: `RS_OPEN=1, RS_UI=2, RS_BATTLE=3, RS_ENCOUNTER=4, RS_CAPTURE=5, RS_HEAL=6, RS_DONE=7, RS_LOSS=8`.

---

## 5. Supervision, recovery, cleanup

`NativeCheckRunner.monitor()` supervises the engine with several independent safety nets:

- **Pulse** — `NativeSessionController.pulse()` every 150 ms; a gap >700 ms is logged.
- **Process identity** — `/proc/<pid>/stat` field 20 (`ticks`) re-checked every second; a change
  means the game restarted → `Game process changed`.
- **Progress watch** — `NativeProgressWatch` detects a stalled engine (no snapshot/catch/spin
  progress for `stallMs` = 65 s, or 180 s before any work in raid mode) → recovery.
- **Recovery plan** — `NativeRecoveryPlan` decides between `startup_retry` (≤10), `transport_retry`
  (≤10), `progress_retry` (≤3), `restart` (automatic raid restart), and `raid_screen_recovery` (≤3).
- **Cleanup** — on stop/error the runner closes the control client, force-stops the bound game,
  removes the staged native files (`cleanGameStage`), and verifies hook removal via
  `libsilph-raid-status.so` (`state 3/4`, `hookRemoved=1`, `remaining=0`, `created==released`,
  `presentationHooks==0`, `locationRemaining==0`).
- **Exit report** — `NativeExitReport` parses `logcat -b crash` + `dumpsys activity exit-info` to
  classify a game crash vs. an intentional stop.

---

## 6. Modes

The same engine supports multiple "pilot" modes via compile flags (`mode` int passed to the service):

- **1 = raid** (this document),
- **rocket** (grunt/incident battles — `SILPH_PILOT_ROCKET`),
- **farmer** (catch/spin — `SILPH_PILOT_COMBINED` without raid),
- **event** (research/event rewards).

Only the raid mode is wired into the current app's `MainActivity` "Start service" (`mode=1`).
