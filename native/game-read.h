#ifndef SILPH_GAME_READ_H
#define SILPH_GAME_READ_H
#include "managed-read.h"
#include "transfer-policy.h"

typedef struct {
    uint64_t samples,account_hi,account_lo,first_encounter;
    uint32_t pokemon_count,max_pokemon,item_count,max_items,map_pokemon,map_stops,candidates_read,first_species;
    uint32_t items[8];
} SilphGameSnapshot;
typedef struct { SilphManagedRef object;uint64_t encounter;uint32_t species,spawn_kind;double meters; } SilphGameTarget;
typedef struct {
    double latitude,longitude,player_latitude,player_longitude,meters;
    uint64_t expires_ms;
    uint32_t incident_type,active,complete,scanned,scan_complete,matches;
    char fort[161],incident[161];
} SilphGameStop;
typedef struct {
    double latitude,longitude,player_latitude,player_longitude,meters;
    uint64_t raid_seed,expires_ms;
    uint32_t raid_level,players,lobby_players,scanned,scan_complete,matches,raid_live,boss_species;
    char fort[161];
    SilphManagedRef map_place;
} SilphGameGym;
typedef bool (*SilphStopPredicate)(void *,const char *);
#ifdef SILPH_PRESENT_SCOPED
typedef bool (*SilphPokemonPredicate)(void *,uint64_t,uint32_t,SilphManagedRef);
int silph_game_read_pokemon_form(void *,SilphManagedRef,bool,uint32_t *);
int silph_game_read_buddy_service(void *,uint32_t,SilphManagedRef *);
#else
typedef bool (*SilphPokemonPredicate)(void *,uint64_t,uint32_t);
#endif
size_t silph_game_read_size(void);
int silph_game_read_open(void *,size_t,void *,const SilphManagedApi *,void *,void *);
int silph_game_read_compact(void *);
int silph_game_read_sample(void *,void *,SilphGameSnapshot *);
int silph_game_read_poll(void *,void *,bool,SilphGameSnapshot *);
int silph_game_read_refresh(void *,bool,SilphGameSnapshot *);
int silph_game_read_close(void *);
void silph_game_read_raid_boss_diag(void *);
void silph_game_read_raid_rpc_diag(void *);
void silph_game_read_gui_diag(void *);
int silph_game_read_raid_root_boss(void *,uint32_t *);
unsigned silph_game_read_stage(void *);
uint64_t silph_game_read_samples(void *);
int silph_game_read_nearest(void *,double,SilphGameTarget *);
int silph_game_read_select(void *,double,SilphPokemonPredicate,void *,SilphGameTarget *);
int silph_game_read_distance(void *,SilphManagedRef,double *);
int silph_game_read_position(void *,double *,double *);
int silph_game_read_rocket_position(void *,double *,double *);
int silph_game_read_stop(void *,double,uint32_t,SilphStopPredicate,void *,SilphGameStop *);
int silph_game_read_stop_at(void *,double,double,double,double,SilphGameStop *);
int silph_game_read_raid_gym(void *,double,uint32_t,uint32_t,const SilphGameStop *,SilphGameGym *);
void silph_game_read_raid_gym_clear_skips(void *);
int silph_game_read_raid_gym_add_skip(void *,const char *);
int silph_game_read_raid_gym_joinable(void *,const char *,SilphGameGym *);
int silph_game_read_gym_is_raid_egg(void *,const char *);
#ifdef SILPH_PILOT_RAID
#define silph_game_read_raid_service silph_game_read_rocket_service
#endif
int silph_game_read_item_bag(void *,SilphManagedRef *);
int silph_game_read_item(void *,int32_t,uint32_t *);
int silph_game_read_owned(void *,uint64_t,SilphManagedRef *);
int silph_game_read_rpc_handler(void *,SilphManagedRef *);
int silph_game_read_service(void *,uint32_t,SilphManagedRef *);
int silph_game_read_gift_service(void *,uint32_t,SilphManagedRef *);
int silph_game_read_map_count(void *,uint32_t *);
int silph_game_read_research_service(void *,uint32_t,SilphManagedRef *);
int silph_game_read_rocket_service(void *,uint32_t,SilphManagedRef *);
int silph_game_read_place(void *,const char *,SilphManagedRef *);
int silph_game_read_gym_map(void *,const char *,SilphManagedRef *);
int silph_game_read_raid_gym_target(void *,const char *,SilphManagedRef *);
int silph_game_read_raid_team(void *,uint64_t *,unsigned,unsigned *);
int silph_game_read_heal_list(void *,uint64_t *,int32_t *,int32_t *,unsigned,unsigned *);
int silph_game_read_faction_team(void *,uint32_t *);
int silph_game_read_transfer_data(void *,uint64_t,SilphTransferInput *,SilphManagedRef *,SilphManagedRef *);
#endif
