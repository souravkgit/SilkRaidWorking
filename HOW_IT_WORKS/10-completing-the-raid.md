# 10 — How we complete the raid (the battle)

**Files:** `native/raid-session.c` (`battle`, `battle_view`, `find_director`, the `RS_BATTLE` /
`RS_LOSS` / `RS_ENCOUNTER` transitions), `native/raid-bridge.inc` (`RK_CHALLENGE`, `RK_SHIELD`,
`RK_RESULT_CLOSE`, …).

The engine **fights** the raid boss, then handles both outcomes: a win (→ encounter) and a loss
(all Pokémon fainted → rejoin, or heal + retry).

---

## 1. Entering the battle

When the lobby countdown ends (or the start RPC completes), the engine moves to `RS_BATTLE`:

```c
s->metrics.stage = RS_BATTLE;
```

It locates the combat director (`find_director()` → the `ICombatDirector` / combat state) and then
runs the per-frame `battle()` function.

---

## 2. The battle loop (`battle()`, line ~1045)

`battle()` reads the battle state each tick via `battle_view()` and acts:

- It watches the combat **director** `BattleStatus` (and the current GUI) to detect the outcome.
- When `BattleStatus == 2` (won) **or** `== 7` (won via the raid-win state), it marks the raid won:

```c
if(state==2 || state==7){
    if(!s->won){ s->won=1; raid_mark_won(s,"battle won"); s->metrics.raids++;
                 s->metrics.stage=RS_ENCOUNTER; …; s->post_battle_at=now; }
}
```

- It drives the battle **inputs** where needed (charge move / shield handling through
  `RK_SHIELD` / `charge_slot`, etc.), though the modern flow relies on the game's own auto-battle
  and mostly just **watches** for the outcome and advances the UI.

---

## 3. Losing — "all your Pokémon have fainted" (RS_LOSS)

If the team is wiped, the game shows the "All your Pokémon have fainted" screen
(`GymBattleUIVNext`) with an **OK** button. The flow enters `RS_LOSS` (`result==5`) and:

```c
// in the RS_LOSS handler
if(!victory && result==5 && now>=loss_ok_at
   && strcmp(name,"MainMenuGuiController") && !strstr(name,"Lobby") && !strstr(name,"Combat")){
    s->loss_ok_at = now + 2s;
    int ok = action(s, permit, RK_RAID_GUI, top.reference, NULL, 0);   // press OK/rejoin
}
```

`RK_RAID_GUI` → the `welcome_gui` handler, which now matches `Faint/Fainted/Defeat/Defeated/
BattleUIV/BattleResult` and invokes the OK/rejoin method (`OnRejoinClicked`, `RejoinClicked`,
`Rejoin`, `OnRejoin`, `OkClicked`, `OnOkClicked`, `OnNextClicked`, `AcceptButtonPressed`, `OnAccept`,
or the generic `OkPressed`/`NextPressed`/`ConfirmPressed`). This re-enters the raid to keep fighting
until the boss is beaten or the timer runs out.

If the raid is truly lost (not rejoinable), the flow closes the loss screens
(`loss screens closed; healing team`), heals the team, and either retries the same gym or finds
another (`RS_HEAL` → rescan).

---

## 4. Winning → the encounter

On a win the engine transitions to `RS_ENCOUNTER`, which:
- reads the boss **encounter** (`IEncounterState` / `RaidEncounterProto` / `MapPokemon`),
- records the encounter id + IVs (`species`, `iv[0..2]`, `shiny`) — see `11`.

The log line is:

```
raid boss encounter ready encounter=<id> species=<n> iv=<a>/<d>/<s> balls=<n> spawn=<gym id>
```

---

## 5. Post-battle screen close

The `raid_post_battle_gui` / `Victory` / `Reward` / `RaidEnd` screens are recognised and closed
(`RK_RESULT_CLOSE` / the victory branch) so the flow reaches the encounter (and then the map).
