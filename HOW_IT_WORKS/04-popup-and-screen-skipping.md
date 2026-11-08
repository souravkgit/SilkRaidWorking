# 04 — How we skip all popups / non-required screens

**Files:** `native/presentation-tools.c` (scene catalogue + generic dismissal + gift/manual-batch
accelerators), `native/raid-session.c` (`ui()` phase-5 handler), `native/raid-bridge.inc`
(`welcome_gui`/`item_gui`/`global_err` + `RK_*` actions).

Popup suppression has **four complementary mechanisms**. They cover **every** kind of popup the
game shows, each handled differently.

---

## 1. Generic onboarding/UI dismissal (name substring + method scan)

Any full-screen `GuiController` whose class name contains an onboarding/UI keyword is dismissed by
invoking its Continue/Skip/Next/Dismiss/Close method (see `03`). This is the broad net.

---

## 2. The explicit **scene catalogue** (`presentation-tools.c` `scene_specs`, line 17)

A fixed table of *specific* controller classes and the *exact* method to call on each:

| Controller (namespace.name) | Method invoked |
|---|---|
| `BadgeIncrementGuiController` | `Dismiss` |
| `DailyIncenseRecapGuiController` | `Dismiss` |
| `LevelUpV3GuiController` | `Dismiss` |
| `XpAwardGuiControllerV2` | `LuckyBonusSkipButtonClicked` |
| `EggHatchGuiController` | `Dismiss` |
| `EggHatchCutsceneGuiController` | `Dismiss` |
| `SpeedModal` (you're going too fast) | `Dismiss` |
| `NewStampCollectionGuiController` | `Dismiss` |
| `FriendshipLevelUpGuiController` | `EndLevelUpFlow` |
| `PokemonInfoGuiController` | `DismissPokemonInfoPanel` |
| `BadgeAwardGuiController` | `Dismiss` |
| `GymBadgeAwardGuiController` | `Dismiss` |
| `VivillonSubBadgeAwardGuiController` | `DismissButtonHit` |
| `AvatarCustomizationAwardGuiController` | `Dismiss` |
| `QuestDialogGuiController` | `ClickSkip` |
| `ArPlusSafetyPromptGuiController` | `PressOk` |
| `WeeklyFitnessDialog` | `ButtonPressed` |

`setup_scenes()` (line 329) resolves each class + action method, hooks `GuiController.<Open>`
(`on_open`), and `silph_presentation_drain()` (line 219) polls the queued scenes (only when they are
`currentState==2`, i.e. top) and invokes the action (and the scene's `finish`/`Dismiss` where
defined, e.g. `FriendshipLevelUpGuiController` and `ArPlusSafetyPromptGuiController`). Each scene is
retried up to 3 times with a 30 s timeout.

This handles the **award / level-up / egg / speed / quest** popups that a plain substring match
would mis-handle, and the AR safety prompt (`PressOk`).

---

## 3. Raid flow UI popups (`raid-session.c` `ui()`, phase 5)

The raid flow handles popups that only appear during the raid UI, by **class type** (`istype`):

- `NewsStartupEntryGuiController` → `RK_RAID_GUI` (the news popup — its dismiss button is at the
  bottom of the screen, but the controller's `Dismiss` method still closes it).
- `WelcomeGuiController` / `FirstTimeUserWelcomeState` / `ReferralEntryGuiController` → `RK_RAID_GUI`.
- `ReceivedRaidTicketGuiController` / `TooltipToastGuiController` → `RK_RAID_GUI` (raid ticket toast).
- `UseRaidTicket…` → the raid-pass selection/use flow (see `09`).
- `RaidPassSelection` / `FreeRaidPassGui` / `Shop` / `GetMorePasses` → pass/selection handling.
- `NpcTrainerInterstitialGui` / `IncidentInvasionDialogGui` / `CombatPreparationRootGuiController` /
  `CombatPokemonPartyWidget` → `RK_CHALLENGE` / `RK_DIALOG` / `RK_FAST_GUI` (shorten pre-raid combat).
- `PokestopSpinnerInteractionGuiController` → `RK_STOP_EXIT`.

The `ui()` handler also shortens the **raid approach** (`RaidApproachUIController`) and the
**battle transition** timings (see `10`), and dismisses the **"all your Pokémon have fainted"** dialog
(`RS_LOSS` stage, `GymBattleUIVNext`) by pressing the OK/rejoin button — the `!strstr(name,"Battle")`
exclusion was removed and `Faint/Fainted/Defeat/Defeated/BattleUIV/BattleResult` + the rejoin/OK
method names (`OnRejoinClicked`, `RejoinClicked`, `Rejoin`, `OnRejoin`, `OkClicked`, `OnOkClicked`,
`OnNextClicked`, `AcceptButtonPressed`, `OnAccept`) were added.

---

## 4. Native dialogs shown **before** il2cpp loads

The "Stay Aware of Your Surroundings" screens that appear during early boot (native UnityPlayer
safety dialogs) are **not** il2cpp GUIs — `GuiLayerService.TopGui` reports `BootSequenceGuiController`
instead. These are handled in `03` by the `BootSequence`/`Safety`/`Surroundings`/`Courteous`/`Aware`
substrings in `raid_startup_dialog_name` + the `welcome_gui` handler (method `Dismiss`). The
`BootSequenceGuiController` is dismissed directly.

---

## 5. Animation / wait accelerators (non-screen, but "non-required" time)

- `presentation-tools.c` **gifts**: hooks `OpenGiftGuiController.OnOpenClick`,
  `HandleDismissComplete`, `GiftBoxStage.AnimateGiftBoxOpen`, `MajorRewardSequenceView.Initialize`
  and the two gift iterator `MoveNext`s; it temporarily sets `guiAnimator.speed=12` and the gift
  open length to `0.05`, then restores them on close (`remember`/`restore`, lines 240–251).
- `presentation-tools.c` **manual multi-select**: hooks the trade multi-select gate and batch send
  (`PokemonInventoryMultiSelect.cdjt`, `PokemonInventoryListLine.cdio`) to auto-filter the trade list.
- `raid-flow.c` **presentation shortening**: hooks `IncidentInvasionState.StartConversation`,
  `NpcTrainerInterstitialGui.HandleOpenComplete` (`PressChallenge`), `CombatPreparationRootGuiController.Initialize`
  (zeroes `backgroundTransitionWaitDurationSec`/`versusScreenDurationSec`/`combatTransitionDurationSec`),
  and the `IncidentCombatResolveGuiController` iterator `MoveNext` (shrinks the scheduler `Wait`
  durations). This makes the pre-raid NPC dialogue + combat transitions near-instant.

---

## Summary table

| Popup kind | Mechanism |
|---|---|
| Welcome / first-time / referral / Willow / onboarding / tutorial | `on_open` + `onboarding_gui_name` + `dismiss_onboarding_gui` (method scan). |
| Safety / surroundings / courteous / aware (boot) | same, via `BootSequence`/`Safety`/… substrings + `welcome_gui` `Dismiss`. |
| News (`NewsStartupEntryGuiController`) | `ui()` `RK_RAID_GUI` (Dismiss). |
| Level-up / XP / egg hatch / badge / stamp / friendship / quest / speed | `scene_specs` catalogue + `silph_presentation_drain`. |
| AR safety prompt | `scene_specs` (`PressOk`) / `finish` (`Dismiss`). |
| Raid ticket / pass selection / free pass / shop | `ui()` phase-5 (see `09`). |
| NPC trainer interstitial / incident dialog / combat prep | `raid-flow.c` hooks (auto-advance / shorten). |
| "All Pokémon fainted" OK | `ui()` `RS_LOSS` stage (OK/rejoin). |
| Gift open / major reward animations | `presentation-tools.c` gifts (speed-up + restore). |
| Buddy/activity "… Days Ago / DISMISS" toast | `Buddy/Activity/Adventure/Toast/Notification/Dismiss` substrings + method scan (added for the map phase). |
