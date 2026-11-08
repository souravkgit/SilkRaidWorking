# 05 — How we find the active raid gyms

**Files:** `native/game-read.c` (`gym_scan_directory`, `gym_scan_dict_or_list`, `gym_scan_key_collection`,
`gym_scan_indexed`, `gym_scan_enumerator`, `gym_consider`, `gym_consider_active_fallback`,
`gym_raid_active`, `gym_raid_apply_info`, `gym_raid_level`, `gym_pick_better`),
`native/game-read.h` (`SilphGameGym`).

The scan runs inside the injected engine while the game sits on the map. It is called from
`raid-session.c` during **phase 3** (after the anchor teleport, before tapping a gym).

---

## 1. Where the gym list comes from

`gym_scan_directory()` (line ~1739) tries a list of il2cpp methods on the map directory service
(`s->directory`), in order, and uses the first that returns a collection:

```
get_UncompletedRaidGyms   (raids that have hatched / are live but not completed)
get_ActiveMapPlaces
GatherNearbyGyms
GatherNearbyForts
GetNearbyGyms
GatherNearbyMapGyms
get_NearbyGyms
get_ActiveGyms
get_ActiveMapGyms
```

Each returned collection is handed to `gym_scan_dict_or_list()`, which handles every .NET container
shape the game may return:

- a **Dictionary** → `get_Keys`/`get_Values` + `ToArray` / `CopyTo` (`gym_scan_key_collection`),
- an **array/list** → `get_Item(i)` + `silph_read_array_item` fallback (`gym_scan_indexed`),
- an **enumerator** → `GetEnumerator` + `MoveNext` + `get_Current` (`gym_scan_enumerator`),
- `KeyValuePair` entries → `get_Key` / `get_Value` (`gym_scan_dict_kvp`).

Each collection **entry** can be a `MapGym`, a fort id string, or a `KeyValuePair`; the scan
resolves it back to a `MapGym` via `gym_resolve_entry` / `gym_lookup_from_key` / `gym_lookup_by_id` /
`gym_resolve_any` / the fort dictionary (`emkw` / `emdp`).

---

## 2. Reading a gym's raid state (`gym_raid_active`, line ~763)

For each resolved `MapGym`, `gym_raid_active()` reads, in order:

1. `gym_raid_level()` — the raid tier via `get_RaidLevel` / `get_Level` / `get_RaidTier` / `caop`
   (the obfuscated `RaidLevel` getter on `MapGym`).
2. Raid timing/seed: `get_RaidBattleMs` / `get_RaidEndMs` / `get_RaidSeed` on the gym, and if the
   gym exposes a `PokemonFortProto` (field by type `Holoholo.Rpc.PokemonFortProto`, or via
   `get_FortData`/`get_PokemonFort`/`get_Fort`/`get_ClientFortProto`/`get_CachedFortProto`), the seed
   from the fort.
3. The raid **active** flag, via `get_CurrentRaidState` / `get_RaidTier` / `RaidState` and the
   `gym_par_has_raid` / `get_IsRaidActive` / `get_HasActiveRaid` fallbacks.
4. The **raid boss species** (`gym_raid_apply_info`, see `06`).

It returns `active`, `level`, `seed`, `players`, and `boss_species` into a `SilphGameGym`.

---

## 3. Picking the "best" gym (`gym_consider` / `gym_consider_active_fallback`)

Each candidate is scored by `gym_pick_better()`:

- **distance** from the player (the anchor) — nearest wins,
- **tier** (higher tier preferred),
- **players** (prefer more players, per the `raid_min_players` config),
- and the tier must pass `gym_tier_allowed(tier_mask, level)` (the user's raid-tier mask).

`gym_consider_active_fallback()` is used for the "active/uncompleted" collections, and
`gym_consider()` for the general map collections. The winner is stored in `SilphGameGym output`:

```c
typedef struct {
    double latitude,longitude,player_latitude,player_longitude,meters;
    uint64_t raid_seed,expires_ms;
    uint32_t raid_level,players,lobby_players,scanned,scan_complete,matches,raid_live,boss_species;
    char fort[161];
    SilphManagedRef map_place;
} SilphGameGym;
```

---

## 4. Rejections before a gym is accepted (in `raid-session.c` `stop_at`)

The chosen gym is dropped and the scan re-runs (every 500 ms) if it is:

- an **egg** (not hatched) — `gym_raid_unhatched_egg` / `gym_raid_is_egg`,
- already **battled** (`raid_done_has`) or **blacklisted** (`silph_game_read_raid_gym_add_skip`),
- **ending** (`gym_raid_ending`) or **no pass** available,
- not matching the **raid-boss filter** (see `06`).

The engine logs the decision at every stage: `gym scan method=… count=…`,
`gym active sample[0] id=… level=… tier=… rstate=… dist=…m`, `gym hatched pick …`,
`gym scan chosen fort=… tier=… dist=…m`, `gym scan reject boss species=…`.

---

## 5. Map-readiness gate

The scan only runs once the map is populated (`raid_map_ready`: `map_stops>=40` and
`pokemon_count>0`), which is why the flow waits for `map ready; anchor teleport armed stops=…`
before scanning.
