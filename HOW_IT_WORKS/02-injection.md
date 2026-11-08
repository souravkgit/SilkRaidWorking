# 02 — How we inject the native engine into the game

**Files:** `app/src/main/java/dev/silphmod/nativecheck/NativeCheckRunner.java` (the loader),
`native/standalone/probe.c`, `native/standalone/remote.c`, `native/standalone/elf-symbol.c`
(build into `libsilph-native-probe.so`), `native/unity-pilot.c`, `native/map-observer.c`.

---

## What is injected

Five shared libraries are installed with the app (`app/src/main/jniLibs/arm64-v8a/`):

| Library | Role |
|---------|------|
| `libsilph-native-probe.so` | Standalone **ptrace loader** (a PIE executable, not a .so despite the name). |
| `libsilph-raid-check.so` | The **managed engine** (the raid logic). Staged as `libsilph-native-observer.so`. |
| `libsilph-control.so` | The **control client** — the JSON-over-stdio bridge Java talks to. |
| `libsilph-raid-status.so` | The **status reader** — reads the engine's progress non-invasively. |
| `libshadowhook.so` / `libshadowhook_nothing.so` | bytedance inline-hook backend + a no-op stub. |

The actual payload that ends up inside the game is `libsilph-raid-check.so`, **renamed** to
`libsilph-native-observer.so` and placed in a per-build staging directory (see below).

---

## Two-stage injection (bootstrap → managed)

`NativeCheckRunner.launchAndLoad()` performs the injection in up to two stages, driven by
`--level <bootstrap|managed|appearance>` passed to the probe.

### Stage A — location bootstrap (`bootstrapLocation`, level `bootstrap`)

Used when `fixLocation` is on. It loads the engine **early** (before the managed runtime is fully
usable) so it can arm the mock-location hiding hook (`location-tools.c`) as soon as possible:

```java
probe --pid <pid> --start <ticks> --library <payload> --trace attach --route fd --level bootstrap
```

The probe reports back (as JSON lines on stdout) `native_loader_result` with
`detail="Native location bootstrap: pending deadlineMs=120000 gameActions=0"`.

### Stage B — managed engine (level `managed`)

This is the main injection:

```java
probe --pid <pid> --start <ticks> --library <payload> --trace attach|seize --route fd|path --level managed
```

The probe runs up to 6 attempts, alternating `trace`/`route`, retrying at a "safe boundary"
(`native_boundary_wait` event → the Java side re-foregrounds the game with `am start --activity-single-top`).

On success the report contains:

```
Unity pilot: address=0x… nonce=<n> resident=1 readOnly=0 controlKeyLo=<k>
```

The Java side extracts `statusAddress`, `nonceText`, and `controlKey` — these are the shared-memory
address + nonce + key used by `libsilph-raid-status.so` and `libsilph-control.so` to talk to the
resident engine. It also asserts `managed.created==7 && released==7 && closed==1` (the engine
opened/closed 7 il2cpp objects cleanly) before accepting the report.

---

## What the probe does (`standalone/probe.c` + `remote.c`)

1. `PTRACE_ATTACH` or `PTRACE_SEIZE` the target pid.
2. Allocate scratch memory in the target via `mmap`.
3. Open the payload `.so` (`route=fd`) or pass its path (`route=path`).
4. Remote-call `android_dlopen_ext` in the target to load the payload.
5. Resolve + run the payload entry point (`silph_native_bootstrap` / the map contract probe).
6. Restore the target registers, detach, and release scratch/fd.

All steps emit `native_loader_stage` / `native_loader_result` JSON lines that the Java side parses.

---

## What the payload entry does (`unity-pilot.c`)

`silph_native_bootstrap(report, capacity)`:

1. Calls `silph_native_baseline_bootstrap` (from `map-observer.c`) — resolves the il2cpp
   `Scheduler.Update` address and the `GuiController` pointer offset (`map.pointer_offset`) via the
   `SILPH_MAP_ENTRY = silph_native_map_contract_probe` symbol.
2. `dlopen`s itself + `libshadowhook.so` with `RTLD_NODELETE`, then `shadowhook_init(SHADOWHOOK_MODE_UNIQUE, true)`.
3. Starts `location_bootstrap_worker` (arms the `isFromMockProvider`/`isMock` JNI hook, see `07`).
4. Reports `pending deadlineMs=120000`.

Later, the pilot's frame/service section opens `farmer_view` and hooks **`Scheduler.Update`** —
Unity's per-frame entry — with a proxy (`farmer_pump`) that drives the whole engine on every frame.

---

## Staging the payload (why the rename)

`NativeCheckRunner.stage()` copies `libsilph-raid-check.so` → `libsilph-native-observer.so`,
`libshadowhook.so`, `libshadowhook_nothing.so` into a private directory
`<nativeLibDir>/silph-native-check-<mode>-<sha16>/` (SHA-verified, `chmod 755`).

There is also a **game-namespace** variant (`stageForGame()`) that copies the observer directly into
the **game's** `nativeLibraryDir` when a clean namespace rejection is detected (some Android/ART
setups refuse a dlopen of a foreign library by path). The runner remembers this per-device and
cleans the staged files up on exit (`cleanGameStage()`).

---

## The control channel (Java ↔ resident engine)

After the managed engine is resident, the Java side starts `libsilph-control.so` as a `su` process:

```java
client = new ProcessBuilder("su","-c", libsilph-control.so + " " + gamePid + " " + ticks);
writer.write(nonceText + " " + controlKey + "\n");
```

`libsilph-control.so` (`control-client.c` + `control.c` + `scheduler.c`) opens a **JSON-lines
stdio** channel to the resident engine and relays commands/snapshots. `NativeSessionController`
pumps it on a 150 ms `pulseThread`, and a `readerThread` consumes the JSON events
(`native_engine_snapshot`, `native_kecleon_event`, `native_control_reply`, …).

`libsilph-raid-status.so` (`pilot-reader.c` + `standalone/remote.c`) is a separate **read-only**
side channel: Java invokes it as `libsilph-raid-status.so <pid> <ticks> <statusAddress> <nonce>`
to read the engine's progress/cleanup state without disturbing it.
