# 03 — How we skip the welcome / first-time onboarding

**Files:** `native/presentation-tools.c` (`onboarding_gui_name`, `dismiss_onboarding_gui`, `on_open`,
`silph_presentation_map`, the `PlayerService` raid gates), `native/raid-session.c`
(`raid_startup_dialog_name`, `raid_dismiss_startup_ref`, `raid_pump_startup_guis`),
`native/raid-bridge.inc` (`welcome_gui` handler).

There are **two independent layers** that together skip the "Welcome! / Your Pokémon GO journey is
about to begin! / Have a referral code?" sequence and the "Stay Aware of Your Surroundings" series:

---

## Layer 1 — the presentation hook (suppresses *before* the user sees it)

`presentation-tools.c` inline-hooks `GuiController.<Open>` (the il2cpp method every full-screen GUI
controller calls when it opens) with `on_open` (`presentation-tools.c` line 188).

In `on_open`, when the opened controller's class name matches `onboarding_gui_name(name)`
(and is **not** a `Referral` screen — the referral prompt is handled separately so the account can
still bind a referral code), it immediately calls `dismiss_onboarding_gui(s, object)`.

`onboarding_gui_name()` matches (substring):

```
Welcome  FirstTimeUser  Referral  Onboarding  Willow  Safety  Surroundings  Courteous
Aware  ArPlus  Tutorial  Ftue/FTUE  FirstTime  Intro  Tip  Walk  Modal  Dialog
```

`dismiss_onboarding_gui()` (`presentation-tools.c` line 139) tries a fixed list of zero-arg methods
(`ContinuePressed`, `OnContinue`, `GetStartedPressed`, `OnGetStarted`, `LetsGoPressed`,
`StartPlaying`, `Advance`, `Complete`, `PrimaryButtonPressed`, `Dismiss`, `OnDismiss`, `ClosePressed`,
`OnClose`, `OkPressed`, `ConfirmPressed`, `AcceptPressed`, `GotItPressed`, `NextPressed`, `OnNext`,
`SkipPressed`, `OnSkip`, `DonePressed`, `OnDone`, `TapToContinue`, `ScreenTapped`, `ReadyPressed`…)
and, if none match, falls back to a **dynamic scan** of every zero-arg method whose name contains
`Advance/Next/Continue/Tap/Proceed/Skip/Dismiss/Close/LetsGo/Start` (skipping lifecycle methods like
`Start/Awake/OnEnable/Update/…`). The first method that invokes without an exception wins.

There is also a **map-layer path** (`silph_presentation_map`, line 166): it reads
`MapExploreState.ellz` → `GuiLayerService.TopGui` every 100 ms and, if the top GUI is an onboarding
screen (not referral), dismisses it. This catches screens that open **without** going through the
hooked `GuiController.<Open>` path.

### The "Stay Aware of Your Surroundings" screens specifically

Those are the *Be courteous / Do not drive / Do not enter dangerous areas / Do not trespass* pages.
They are dismissed by the same `on_open` + `dismiss_onboarding_gui` path (`Safety`, `Surroundings`,
`Courteous`, `Aware` all match).

### The raid gates (so the game thinks the account is ready)

For raid mode, `presentation-tools.c` also hooks `PlayerService` (`setup_player_raid_gates`,
line 307) to force the "can I raid?" checks true:

- `IsTutorialComplete` / `get_IsTutorialComplete` → **true** (tutorial considered done).
- `get_PlayerIsMinLevelForRaids` / `ForRemoteRaids` / `ForIncidents` / `ForInviteToRaid` / `ForDeepLinkedUrl` → **true**.
- `get_Level` / `GetLevel` / `get_PlayerLevel` / `get_CurrentLevel` → **5** (a raid-eligible level).
- `GetPlayerTeam` / `get_Team` / `GetTeam` / `get_TeamId` / `get_TeamColor` → **1** (Mystic).

---

## Layer 2 — the raid-session startup pump (the fallback + boot sequence)

`raid-session.c` also dismisses GUI screens from inside the raid flow, in case the presentation
hook missed them (it runs during the **map phase too**, not only at boot).

- `raid_startup_dialog_name(name)` (line ~2225) is a substring matcher listing
  `Welcome/FirstTimeUser/Referral/Onboarding/Willow/Safety/Surroundings/Courteous/Aware/ArPlus/
  News/Announcement/Details/Friend/GlobalError/ItemInventory/ItemBag/StampRally/Rally/DailyReward/
  ReturningPlayer/Tutorial/Reward/Popup/Modal/Dialog/Dialogue/Conversation/Story/Npc/Narrative/Speech/
  Interaction/Trainer/Power/BootSequence/Spot/Buddy/Activity/Adventure/Toast/Notification/Dismiss/…`.
- `raid_dismiss_startup_ref()` matches the current GUI against that list and calls
  `action(RK_RAID_GUI)`.
- `raid_pump_startup_guis()` (line ~2275) runs in **phase 1 and phase 3**, iterating:
  1. the **top** GUI (`RK_TOP` → `GuiLayerService.TopGui`),
  2. the `dddt` GUI **stack**,
  3. every **layer** (the `RK_LAYER` operation for layer indices `0,100,200,…,1100`),
  …and dismisses each matched screen.

`RK_RAID_GUI` lands in the `welcome_gui` handler in `raid-bridge.inc` (line ~640), which invokes
the same "welcome methods" + dynamic-scan dismissal used by the presentation layer.

---

## The key insight

The welcome/onboarding screens are **ordinary il2cpp `GuiController` subclasses**. The engine never
"taps" them; it **invokes their Continue/Skip/Next/Dismiss method directly** the moment the
controller opens (or is detected on the GUI layer), so they never appear — this works for any
screens whose class name contains one of the matcher substrings.
