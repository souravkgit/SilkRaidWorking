# 06 — How we find the gym whose boss matches the selected Pokémon

**Files (app):** `app/src/main/java/dev/rootcatch/automation/PokemonCatalog.java`,
`PokemonSelection.java`, `BerryOptions.java`,
`app/src/main/java/dev/silphmod/nativecheck/PokemonPickerDialog.java`,
`MainActivity.java`, `NativeSettings.java`,
`app/src/main/java/dev/rootcatch/nativeengine/NativeSessionWire.java`.

**Files (native):** `native/session-wire.h` / `session-wire.c`, `native/game-read.h` / `game-read.c`
(`gym_raid_apply_info`, `silph_game_read_raid_root_boss`), `native/raid-session.c` (post-tap filter).

This is the **raid-boss selector**. The user picks one or more boss species in the UI; the engine
raids only those bosses.

---

## 1. The Pokémon catalogue + selection (Java)

- `PokemonCatalog.java` loads `assets/pokemon/catalog.tsv` and `forms.tsv` — the public Pokémon
  dex: species id, name, forms, and the item icons (`assets/pokemon/icons/*.png`).
- `PokemonSelection.java` parses/encodes a comma-separated list of species ids
  (`"888,889"` ⇄ `List<Integer>`).
- `PokemonPickerDialog.java` is the multi-select dialog shown by the "Raid boss (only these)" tile
  in `MainActivity`. On save it writes `settings.prefs.putString("raidBossSpeciesList", s)`.

---

## 2. Flowing the selection into the config

`NativeSettings.parseBosses()` reads the `raidBossSpeciesList` pref and returns `int[]` (≤8 ids).
`NativeSettings.config()` then sets the wire fields:

```java
int[] bosses = parseBosses();
c.raidBossSpecies = (bosses.length>0) ? bosses[0] : 0;   // primary
c.raidBossCount   = bosses.length;
c.raidBossSpeciesList = new int[8];                       // up to 8
for (int i=0;i<bosses.length&&i<8;i++) c.raidBossSpeciesList[i] = bosses[i];
```

`NativeSessionWire.Config` encodes these into the 1024-byte binary config at fixed offsets:

```
offset 928: raidBossSpecies (int32)
offset 932..960: raidBossSpeciesList[8] (int32 each)
offset 964: raidBossCount (int32)
```

(`session-wire.c` mirrors this: `put32(p+928,…)`, `put32(p+932+4*i,…)`, `put32(p+964,…)`, with a
CRC over the whole 1020 bytes.)

---

## 3. Reading the actual raid boss species (native)

The boss species is **not** on the `MapGym` object — it lives behind the raid info. There are two
read paths:

### (a) At scan time — `gym_raid_apply_info()` (`game-read.c` line ~599)

Given a `RaidInfoProto` (`info`), it resolves the `RaidPokemon` (`PokemonProto`) and reads its
`PokemonId`:

1. `info.get_RaidPokemon()` → `SILPH_READ_REFERENCE` → the `PokemonProto`, then
   `read_enum_any(pokemon, "get_PokemonId", "PokemonId")` (also tries `pokemon_id_`).
2. fallback: `info.raid_pokemon_` / `info.RaidPokemon` field → same.
3. fallback: `PokemonProto.PokemonId` field read as enum32.

The species lands in `SilphGameGym.boss_species`. (`RaidPokemon.PokemonId` = 888 is Zacian, 889 is
Zamazenta, etc.)

### (b) Post-tap — `silph_game_read_raid_root_boss()` (`game-read.c`)

Because at scan time the gym's `PokemonFortProto.RaidInfo` is often **null** (the raid info has not
been fetched yet), the authoritative read happens **after the gym is opened**: it resolves the
`IGymRootController` service and reads `get_RaidInfo()` (the current gym's `RaidInfoProto`), then
reuses `gym_raid_apply_info()` to extract the boss. Fallbacks `caam/caan/caao` are also tried.

---

## 4. The filter (post-tap, in `raid-session.c`)

A `boss_checked` flag is reset when a gym is opened. During phase 5 (`RS_OPEN`), once ~3 s have
passed (giving `IGymRootController.get_RaidInfo()` time to load), the engine reads the boss and:

```c
if(!s->boss_checked && phase==5 && stage==RS_OPEN && stop.fort[0] && now>=ui_started+3s){
    uint32_t boss=0;
    if(!silph_game_read_raid_root_boss(s->game,&boss) && boss){
        s->boss_checked=1;
        bool want = (config.raid_boss_species && boss==config.raid_boss_species);
        for(i=0;!want&&i<config.raid_boss_count&&i<8;i++)
            if(boss==config.raid_boss_species_list[i]) want=true;
        if(!want){
            // skip this gym: blacklist + abandon + rescan
            silph_game_read_raid_gym_add_skip(s->game, s->stop.fort);
            raid_clear_gym_target(s, now, "boss mismatch");
            return 0;
        }
    }
}
```

- **match** → continue to the pass/join/battle.
- **mismatch** → the gym is added to the skip list and the flow teleports to the next candidate.

The engine logs `raid boss match boss=888 fort=…` or `raid boss mismatch boss=889 want=888; skip …`.

---

## Why the boss read had to be post-tap

`get_UncompletedRaidGyms` returns `MapGym` objects whose `PokemonFortProto.RaidInfo` is null at scan
time (the raid info is fetched over `IRaidRpcService` only when the gym is opened). So the species
is read from `IGymRootController.get_RaidInfo()` **after** tapping the gym — hence the post-tap
filter rather than a scan-time filter.
