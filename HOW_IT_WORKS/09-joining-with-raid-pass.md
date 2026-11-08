# 09 — How we join the gym using the raid pass

**Files:** `native/raid-session.c` (`raid_open_join`, `raid_bind_gym_from_gui`, `raid_tap_place`,
the `ui()` phase-5 handler, `raid_rpc_kickoff`, `raid_open_rpc`), `native/raid-bridge.inc`
(`RK_TAP`, `RK_LOBBY`, `RK_RAID_GUI`), `native/rpc-actions.c`.

Joining = **tap the gym → open the raid approach → press Join → spend the pass → enter the lobby**.
It is driven partly through the GUI (taps) and partly through the raid RPC (the same calls the game
makes), whichever the engine's "fast/RPC" path decides.

---

## 1. Opening the gym

The engine already holds the winning `SilphGameGym` (with `fort`, `latitude`, `longitude`,
`raid_seed`). When the player is in range (`ready_to_open` / `raid_at_gym`):

- `raid_open_join(s, now)` (line ~1234) resets the UI state, sets `teleport_ready=1`,
  `join_ui_fallback=1`, and transitions to `phase 5, stage RS_OPEN`.
- The gym is **tapped** via `raid_tap_place()` → `action(RK_TAP, s->place)` (the `OnTapped` /
  map-tap action on the `MapGym` place). Log: `gym map tap fort=<id> attempt=… gui=MainMenuGuiController`.

`raid_bind_gym_from_gui()` (line ~1248) confirms the tap by reading the now-open
`GymGuiController`'s `get_FortId` and cross-checking it against the chosen fort id + seed, then
re-arms `raid_open_join`.

---

## 2. The raid approach screen

When `RaidApproachUIController` opens, the engine drives it:

- `presentation shortened gui=RaidApproachUIController` (the approach/transition timing is shrunk).
- It reads the approach buttons (`post-battle child buttons … InPersonJoinButton …`) and presses
  **Join** via `OnJoinButtonPressed` (`raid gui method ok … method=OnJoinButtonPressed`).

---

## 3. Spending the pass (`UseRaidTicketGuiController`)

Pressing Join opens the "use a raid pass" screen. The engine handles `UseRaidTicketGuiController`
via `RK_RAID_GUI` (see `08`) — invoking `StartAnimation` / the use-button / `InteractionComplete` —
which consumes the pass (`raid pass used gui=UseRaidTicketGuiController method=StartAnimation`).

If the pass-selection/Shop GUI appears instead (`RaidPassSelection` / `Shop` / `GetMorePasses`), the
`ui()` handler navigates it with `RK_RAID_GUI` (select the pass / buy if configured).

---

## 4. The RPC join path (fast path)

In parallel, the engine can join through the **raid RPC** directly (`raid_rpc_kickoff`, `rpc-actions.c`):

```
raid rpc done op=5 result=1        (join lobby RPC accepted)
```

`raid_rpc_step` advances through the lobby RPC sequence (join → confirm → start). When the RPC is
accepted, `raid-session.c` marks `metrics.stage=RS_BATTLE` once the battle starts
(`raid rpc success … stage=RS_BATTLE`).

---

## 5. Lobby handling

Once in the lobby (`RaidLobbyGuiController`), the engine:

- waits for the lobby countdown / other trainers,
- enforces the minimum-player config (`Minimum lobby players · tier`) — if the lobby is below the
  minimum it leaves and finds another gym (`lobby below minimum players`),
- presses "ready" / sends the start RPC when the countdown ends,
- on entering the battle transitions to `RS_BATTLE`.

---

## 6. Team selection

Before battle, the engine selects the party:

- `RK_PARTY_RECOMMEND` / the recommended-team path, or the configured team,
- it also drives `CombatPokemonPartyWidget` / `CombatPreparationRootGuiController` (via
  `raid-flow.c` shortening + `ui()` handling) and `prepare_team()` to confirm the party.

---

## 7. Where the boss filter sits

The **boss-species filter** (see `06`) runs right at the top of this phase (`RS_OPEN`): the engine
reads `IGymRootController.get_RaidInfo()` and, if the boss does not match the user's selection, it
**does not join** — it blacklists the gym, closes it, and re-scans for the next candidate.
