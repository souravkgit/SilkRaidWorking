# 01 — How we launch the game

**Files:** `app/src/main/java/dev/silphmod/nativecheck/NativeCheckRunner.java`,
`app/src/main/java/dev/silphmod/nativecheck/NativeStartupPolicy.java`,
`app/src/main/java/dev/rootcatch/root/RootShell.java`.

Pokémon GO is **not** launched and then forgotten; the launcher watches it until it reaches a
**stable foreground state with both `libil2cpp.so` and `libunity.so` mapped**, because that is the
exact moment it is safe to inject.

---

## Step 0 — preflight (in `NativeCheckRunner.prepare()`)

Before launching, the runner validates the environment with `su` root commands:

1. **Root check** — `id` must print `uid=0`.
2. **Device/ABI** — `getprop ro.build.version.sdk`, `ro.product.cpu.abilist`, `getenforce`, `uname -r`, `getconf PAGESIZE`.
3. **Game version** — `PackageManager.getPackageInfo("com.nianticlabs.pokemongo")`. Only
   `0.433.1` (SHA `a98b7107…`) and `0.435.1` (SHA `88cff795…`) are accepted.
4. **ARM64 split present** — `pm path` must find `split_config.arm64_v8a.apk`, and its `sha256sum`
   must match the game SHA.
5. **Native components** — every `lib*.so` under the app's `nativeLibraryDir`
   (`/data/app/…/lib/arm64`) must exist and match its recorded SHA (`NativeArtifacts.hash`).
6. **No conflicting process** — `pidof com.nianticlabs.pokemongo` must be a single pid (if any) and
   its `/proc/<pid>/maps` must not already contain a `rootcatch-inject` / `silph-minimal` mapping.

The package component (launch activity) is resolved once:

```java
Intent launch = pm.getLaunchIntentForPackage(GAME);          // com.nianticlabs.pokemongo/...UnityMainActivity
String component = launch.getComponent().flattenToString();
```

---

## Step 1 — `launchAndLoad()` chooses a route

```java
launchAndLoad(directory, route, attempt==0 && !fixLocationEnabled() && startup!=1 && existing pid);
```

- **`existing`** = attach to the already-running game (no force-stop).
- **`route`** = an int 0..3 encoding two orthogonal choices:
  - `trace`: even→`PTRACE_ATTACH`, odd→`PTRACE_SEIZE`.
  - `library`: even→inject via an open **fd**, odd→inject via a **path** (`dlopen` by path).

If not `existing`, the runner does `am force-stop com.nianticlabs.pokemongo` first, then:

```java
command("am start -W -n " + quote(component));   // -W waits for the activity to draw
```

The chosen route is cached per-device-per-game-SHA in a pref (`route.<fingerprint>.<sha>`).

---

## Step 2 — wait for the libraries (`launchAndLoad` loop)

A 75-second deadline loop polls:

```java
current = command("pidof com.nianticlabs.pokemongo");
maps = command("grep -E 'libil2cpp[.]so|libunity[.]so' /proc/<pid>/maps");
```

It requires **both** `libil2cpp.so` and `libunity.so` to be mapped, then requires a
**stability window** (`mode==1 ? 5_000 : 20_000` ms) during which the process stays foreground
(`NativeFocus.active()`). When stable + foreground, it records:

```java
gamePid = pid;                 // the game process id
ticks   = readTicks(pid);      // /proc/<pid>/stat field 20 (process start ticks) — identity guard
```

`ticks` is used everywhere as a **process-identity nonce**: if `/proc/<pid>/stat` field 20 ever
changes, the runner knows the game restarted (e.g. crashed) and aborts/re-recovers.

---

## Step 3 — identity + instrumentation guard

- `verifyInstrumentation(pid, payload)` reads `/proc/<pid>/status` and asserts `TracerPid: 0`
  (no other debugger attached), and that `/proc/<pid>/maps` has no foreign `silph-*check` mapping.
- `assertStartupTarget()` re-checks `/proc/<pid>/stat` before every load attempt; if the game died
  it throws `TargetExited` (→ recovery/cleanup).

---

## Important note on `fixLocation`

- `settings.prefs.getBoolean("fixLocation", false)` controls two things:
  1. the **config flag** `FIX_LOCATION` (whether the native engine hides the mock-location flag), and
  2. whether the runner is allowed to attach to an **existing** process (vs a forced cold relaunch).

For the "manually log in, then press Start" workflow, `fixLocation` is `false` so the runner
attaches to the already-logged-in game instead of relaunching (which would re-run the fresh
onboarding).

---

## Why this order matters

Injection must happen **after** `libil2cpp.so` is loaded (the managed runtime must exist) but
**before** the user-facing flow needs the engine. The stability window avoids injecting mid-load,
which is the common cause of the `TargetExited` / native-crash failures seen during development.
