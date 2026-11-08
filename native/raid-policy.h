#ifndef SILPH_ROCKET_POLICY_H
#define SILPH_ROCKET_POLICY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { ROCKET_WAIT=0,ROCKET_FAST=1,ROCKET_CHARGED=2,ROCKET_SHIELD=3,ROCKET_SWAP=4,
    ROCKET_HEAL=5,ROCKET_REVIVE=6,ROCKET_HEALTHY=7,ROCKET_NO_ITEMS=8,ROCKET_INVALID=9 };
typedef struct {uint64_t id;int32_t hp,max_hp;bool known;} RaidHealth;
typedef struct {int32_t slot,move,energy_delta,duration;uint64_t ready_turn;bool disabled;} RaidMove;
typedef struct {
    bool bound,alive,paused,merged,started,finished,input_blocked;
    bool progress_ready;
    uint32_t ui_mode,shields,charge_slot,species;
    uint64_t generation,turn,serial,active,next_fast_turn;
    int32_t energy;
    float progress;
    uint64_t team[3];
    RaidHealth health[3];
    RaidMove moves[3];
} RaidBattleView;
typedef struct {
    uint32_t kind,event_type,oneof,attack_type;
    uint64_t outgoing,incoming;
    int32_t slot,item;
    float score;
} RaidDecision;
typedef struct {uint64_t generation,serial,turn,next_fast_turn;bool issued,pending,shield_window;} RaidDecisionGate;
typedef struct {uint64_t generation,serial,turn,active;uint32_t ui_mode;bool input_blocked;} RaidBattleClock;
typedef struct {uint64_t generation,turn,active,incoming,until;uint32_t type,ui_mode;int32_t energy;} RaidActionWatch;
typedef struct {int32_t potion,super_potion,hyper_potion,max_potion,revive,max_revive;} RaidCareItems;
typedef struct {uint32_t potions[4],revives[2];} RaidCareOrder;
bool raid_battle_observe(RaidBattleClock *,RaidBattleView *);
bool raid_action_observed(const RaidActionWatch *,const RaidBattleView *,uint64_t);
uint32_t raid_battle_decide(const RaidBattleView *,RaidDecisionGate *,RaidDecision *);
bool raid_battle_settled(RaidDecisionGate *,const RaidBattleView *);
uint32_t raid_care_decide(const uint64_t[3],const RaidHealth[3],const RaidCareItems *,bool,RaidDecision *);
uint32_t raid_care_ordered(const uint64_t[3],const RaidHealth[3],const RaidCareItems *,const RaidCareOrder *,bool,RaidDecision *);
bool raid_catch_accept(uint32_t,uint32_t,uint32_t,uint32_t,bool);
enum {ROCKET_RESULT_WAIT=0,ROCKET_RESULT_DISMISS=1,ROCKET_RESULT_EXPIRED=2};
typedef struct {uint64_t captured,until,next;uint32_t attempts;} RaidCaptureGate;
bool raid_capture_watch(RaidCaptureGate *,uint64_t,uint64_t);
uint32_t raid_capture_result(RaidCaptureGate *,uint64_t,bool,uint64_t);
#endif
