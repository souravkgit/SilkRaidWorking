# 11 — How we catch the raid boss after the win

**Files:** `native/raid-session.c` (`raid_fill_raid_encounter`, the `RS_CAPTURE` catch loop around
line 1745–1790, `raid_catch_accept`, `RK_CAPTURE_COMPLETED`), `native/raid-bridge.inc`
(`RK_CAPTURE_COMPLETED`), `native/core.c` (capture authorization state machine),
`native/visual-tools.c` (IV/name readback).

---

## 1. Obtaining the encounter

After the win (`RS_ENCOUNTER`), the engine resolves the boss encounter object from one of three
sources (`raid_fill_raid_encounter`):

1. `IEncounterState.get_RaidEncounter` → `RaidEncounterProto` (has `ThrowsRemaining`),
2. `IEncounterState.get_MapPokemon` (the bonus/event encounter path),
3. `Holoholo.Rpc.RaidEncounterProto` typed field.

It records:

```c
s->metrics.encounter = id;          // encounter id
s->species            = species;    // boss species
s->iv[0..2], s->shiny               // IVs + shininess (visual readback)
s->balls              = balls;      // premier balls remaining (ThrowsRemaining / item 5)
```

and transitions to `RS_CAPTURE` (`headless=1`).

---

## 2. The catch decision

The engine may use a **berry** first (the `berries` / `BerryOptions` config → `RK_*` berry actions),
then throws premier balls one at a time using the **encounter RPC** (`silph_managed_promise` +
`rpc-actions.c`), and watches the reply:

```c
catch result=<code> (1=caught 2=escaped 3=fled) balls_left=<n>
```

- `1` (caught) → the boss is captured.
- `2` (escaped) → throw again while balls remain.
- `3` (fled) → the boss ran.

If the RPC reply is lost, it re-throws after a timeout (`catch reply lost; throwing again`).

---

## 3. Capture completion + authorization (`RK_CAPTURE_COMPLETED` + `core.c`)

When the ball lands, the engine calls `RK_CAPTURE_COMPLETED` (the `CaptureCompleted` /
`EncounterStateComplete` method) to finalise the encounter, and the **capture authorization state
machine** (`core.c`) records a *proof* that a capture actually occurred:

- `silph_core_issue()` reserves a sequence for the encounter,
- `silph_core_settle()` validates a 6-word proof (sequence, encounter id, sent-at, received-at,
  the caught Pokémon id, and a constant `15`), and only then counts a **captured** entry,
- `silph_core_authorize()/begin()/finish()` gate the capture-confirm so a capture can only be
  reported once, with a `limit`.

This is the anti-spoof/anti-double-count guard for the "caught" statistic.

---

## 4. Post-capture

On capture, the flow:

- increments `metrics.caught`,
- dismisses the caught-Pokémon screen (`PokemonInfoGuiController` → `RK_GUI_CLOSE` /
  `DismissPokemonInfoPanel`),
- returns to the **anchor** (`raid_capture_anchor`; the host moves the GPS back),
- and loops: `metrics.raids++`; if under `raidsMax`, it re-scans for the next gym; otherwise it
  finishes the account (`Session raid limit reached`).

---

## 5. IV / name readback (optional)

`visual-tools.c` / `readback.c` read the caught Pokémon's **IVs** and (if configured) apply a
trainer-name / stats readback for the session snapshot. The caught `id`, `iv_reads` and
`name_updates` are part of the `SilphSessionSnapshot` sent to Java (and, in the extended build, to
the configured webhook/Mongo endpoint).
