# SilkRaid — How It Works (Build-from-scratch guide)

This folder explains, end-to-end, how the **SilkRaid** Android app automates the Pokémon GO
raid loop. It is written so that **another agent/engineer with no prior context can rebuild the
entire app from scratch** — every mechanism is described at the file + function + method level.

The app's package id is `dev.silkraid.app`. It targets **rooted Android (arm64)** and **Pokémon GO
0.433.1 / 0.435.1**. It uses `su` + `ptrace` to inject a native engine into the running game.

---

## The raid loop (one sentence)

1. Launch Pokémon GO and wait for Unity/il2cpp to load.
2. ptrace-inject a native engine (`libsilph-raid-check.so`) into the game process.
3. The engine skips the welcome/onboarding screens and all popups.
4. It teleports (mock GPS) to a raid anchor, scans the map for active raid gyms,
   picks the gym whose raid boss matches the user's selection, teleports to it, taps it.
5. It claims the daily free raid pass (gym spin) if needed, joins the lobby with the pass.
6. It battles the boss, and when the boss is defeated, catches it.
7. It returns to the anchor and repeats until the raid limit / no pass.

---

## The two halves

| Half | Where | Role |
|------|-------|------|
| **Android app (Java)** | `app/src/main/java/` | UI, settings, root shell, process supervision, GPS joystick, screen taps, session logging, and the JSON control channel to the native engine. |
| **Native engine (C)** | `native/` → `app/src/main/jniLibs/arm64-v8a/*.so` | Injected into the game; hooks Unity/il2cpp; performs the actual raid decisions + reading game state. |

---

## Index of this folder

| File | Topic |
|------|-------|
| `01-game-launch.md` | How we launch Pokémon GO and find a stable foreground/il2cpp state. |
| `02-injection.md` | How we ptrace-inject the native engine into the game process. |
| `03-welcome-screen-skip.md` | How the "Welcome! / referral / first-time" onboarding is skipped. |
| `04-popup-and-screen-skipping.md` | How **all** popups / non-required screens are suppressed (the full catalogue). |
| `05-finding-active-raid-gyms.md` | How we enumerate the active raid gyms near the anchor. |
| `06-finding-selected-boss-gym.md` | How we read the raid boss species and filter to the user's selected boss. |
| `07-teleporting-to-the-gym.md` | How we move the mock GPS location (and hide it from the game). |
| `08-claiming-free-raid-pass.md` | How we claim the daily free raid pass (gym spin). |
| `09-joining-with-raid-pass.md` | How we spend the raid pass and enter the lobby. |
| `10-completing-the-raid.md` | How the battle is fought and the raid is completed. |
| `11-catching-the-raid-boss.md` | How the boss encounter + capture works. |
| `12-full-architecture-and-build.md` | The complete architecture, file inventory, build pipeline, wire protocol, recovery and cleanup. |

---

## Key terms

- **il2cpp** — Unity's C#→C++ runtime. Pokémon GO is a Unity/il2cpp game; its managed classes
  are visible through the `il2cpp_*` API.
- **ShadowHook** — the bytedance `android-inline-hook` library used to patch (inline-hook) functions.
- **ptrace** — the Linux syscall used to attach to + load code into the game process.
- **`RK_*` operations** — the native engine's action vocabulary (tap, dismiss, challenge, …).
- **`SilphGameGym` / `SilphGameStop`** — the native model of a gym / stop.
- **`SilphSessionConfig` / `SilphSessionSnapshot`** — the JSON/binary config + telemetry that flows
  between Java and native over the control channel.
- **GPS joystick / PGTools** — the on-device mock-location app used to teleport (invoked via `su`).
