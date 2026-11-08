# 08 — How we claim the free raid pass

**Files:** `native/raid-session.c` (pass counting + free-pass gym spin),
`native/game-read.c` (`silph_game_read_item`), `app/src/main/java/dev/silphmod/nativecheck/NativeSettings.java`
(`passMask`), `native/raid-bridge.inc` (the pass/`UseRaidTicket` GUI handling).

The daily free raid pass is **not** an inventory item you already have — it is earned by **spinning
a gym** (first gym spin of the day). The flow claims it only when it has no pass in the bag.

---

## 1. Which passes are allowed

`NativeSettings.passMask()` reads the `raidPassMask` pref (1..7, or the legacy `raidPassItem`), and
the config field is `raidPassItem = 0x10000 | passMask()`.

In `raid-session.c`:

```c
uint32_t raid_pass_mask(const Raid *s){ uint32_t v=s->config.raid_pass_item; return v==1400||v==1401?1u:v==1402?2u:3u; }
bool raid_pass_allowed(const Raid *s,int32_t item){
    uint32_t m=raid_pass_mask(s);
    return item==1401?(m&1u):item==1402?(m&2u):item==1408?(m&4u):false;
}
```

- `1401` = free (daily) raid pass, `1402` = premium pass, `1408` = remote pass.

`raid_pass_total()` counts the passes actually in the bag (`silph_game_read_item` for `1401` + `1402`).

---

## 2. The decision

In `raid-session.c` (the `stop_at` / approach flow), before opening the raid:

- If `raid_pass_total(s) > 0` → there is a pass in the bag; proceed straight to join (see `09`).
- If **zero** → log `raid pass: none in bag, using free pass id 1401` and claim the daily free pass.

If the gym has no raid pass available at all, the gym is **skipped** (`gym skip … no raid pass in bag`).

---

## 3. Claiming by gym spin

The free pass is obtained by spinning the gym the player is standing at:

```
no raid pass; waiting for free pass at gym waited=0ms
claiming daily free raid pass (gym spin) fort=<id> err=0
free pass gym spin result=1 fort=<id>
waiting for claimed free pass in bag spin_result=1 waited=…
```

The spin is performed through the raid RPC / stop-spin action. The result is polled until the pass
appears in the bag (or a timeout).

If the daily free pass was **already claimed today**, the spin returns "already used" and the flow
stops the account (`free raid pass not claimable (spin=…): daily free pass already used; stopping`,
finish reason `No raid pass: daily free pass already used`), because it cannot raid without a pass.

---

## 4. The free-pass popup

When the free pass is granted, the game shows a "free raid pass" popup. The flow closes it with a
screen tap (or the pass GUI handler) — see `NativeRaidHost` (`phase==5 move==2` → `screenTap(0.5,0.875)`)
and `raid-session.c` (`free pass popup: requesting close tap`).

---

## 5. Pass / ticket GUI handling (`raid-bridge.inc` + `ui()`)

The `UseRaidTicketGuiController` (the "use this pass?" screen) is handled by `RK_RAID_GUI`:

- it tries `StartAnimation` / `OnUseButtonPressed` / `UseButtonPressed` / `OnConfirmButtonPressed` /
  `PrimaryPressed` …,
- and a `InteractionComplete` delegate `Invoke()` fallback,
- then the pass-selection GUI (`RaidPassSelection` / `UseRaidPass` / `FreeRaidPassGui` / `Shop`) is
  navigated (`RK_RAID_GUI`) to actually spend the pass and enter the lobby.

The engine logs `raid pass used gui=UseRaidTicketGuiController method=StartAnimation` when the pass
is consumed.
