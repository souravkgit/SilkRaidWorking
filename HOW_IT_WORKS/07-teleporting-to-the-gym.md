# 07 — How we teleport to the gym

**Files:** `app/src/main/java/dev/silphmod/nativecheck/NativeRaidHost.java`,
`app/src/main/java/dev/rootcatch/ugphone/KecleonMovement.java`, `KecleonFeed.java`,
`native/location-tools.c`.

Teleportation = **mock GPS** driven from the Java side, plus a **native hook that hides the mock
flag** from the game (the classic "Fix error 12" bypass).

---

## 1. Who moves the location

`NativeRaidHost` (Java) is the only component that moves the GPS. It receives phase events from the
native engine (`NativeKecleonWire.Event`) and moves the joystick accordingly:

| Native phase | Host action |
|---|---|
| `phase==1` (request anchor) | parse `raidAnchor` pref → `move(direct(anchor))` → ack `send(e,1,0,…)` |
| `phase==3` (scan) | if a gym was chosen → `move(KecleonMovement.points(lat,lon))`; else back to anchor |
| `phase==4` (gym approach) | `move(KecleonMovement.points(e.latitude,e.longitude))` → ack `send(e,3,0,…)` |
| `phase==6` (join / capture) | `move(direct(e.latitude,e.longitude))` (gym join), or back to anchor for capture |

`KecleonMovement.command(lat,lon)` emits the shell command that drives the on-device **GPS
joystick** (the "PGTools" mock-location app), and `KecleonMovement.points(lat,lon)` produces a
**multi-step path** (with `STEP_MS` between steps) so the movement looks like walking rather than a
single jump (reduces soft-ban/cooldown risk).

The host only moves while Pokémon GO is in the foreground:

```java
if(!active.getAsBoolean()) throw new IOException("PoGO must be in foreground for raid GPS moves");
```

and retries each step up to `GPS_RETRIES=3` until `KecleonMovement.teleportSucceeded()`.

---

## 2. Hiding the mock flag (`location-tools.c`)

Even with mock GPS, Pokémon GO calls `android.location.Location.isFromMockProvider()` /
`isMock()` and refuses (error 12) if they return `true`. `location-tools.c` hooks the **JNI
`CallBooleanMethod` / `CallBooleanMethodV` / `CallBooleanMethodA`** function pointers:

```c
current.methods[0] = GetMethodID(android/location/Location, "isFromMockProvider", "()Z");
current.methods[1] = GetMethodID(android/location/Location, "isMock",           "()Z");
// hook CallBooleanMethod{V,A} with proxies call_v / call_a / call_variadic
```

The proxy (`filter`, line 49) checks whether the call is one of those two `methodID`s on an
`android.location.Location` instance and, if so, returns **`JNI_FALSE`** (and bumps the
`changed`/`legacy`/`modern` counters). All other boolean method calls pass through unchanged.

This hook is armed in the **location bootstrap** stage (level `bootstrap`, see `02`) so it is active
before the game's first location read.

---

## 3. Settling after a move

After each move the host sleeps `SETTLE_MS=2000` (scan) or `GYM_SETTLE_MS=2500` (gym join) so the
game's map updates before the engine reads gym/raid state again. The native engine also enforces an
in-range check (`ready_to_open` / `raid_at_gym`) before tapping the gym, and re-teleports via the
mock path if the player is out of range (`gym gps not in range … re-teleport via mock`).

---

## 4. Anchor

The anchor coordinate comes from the `raidAnchor` pref (`NativeSettings` default `34.693738,135.502165`;
the device is currently configured to `19.435494,-99.146011`). It is the centre of the scan radius
(`raidRadius` metres, default 5000).
