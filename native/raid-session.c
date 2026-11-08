#include "raid-session.h"
#include <sys/stat.h>
#include "raid-bridge.h"
#include "raid-policy.h"
#include "raid-flow.h"
#include "rpc-actions.h"
#include "kecleon-actions.h"
#include "maintenance-actions.h"
#include "response-rewards.h"
#include "managed-promise.h"
#include "managed-action.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <android/log.h>
#include <math.h>

enum {RS_OPEN=1,RS_UI=2,RS_BATTLE=3,RS_ENCOUNTER=4,RS_CAPTURE=5,RS_HEAL=6,RS_DONE=7,RS_LOSS=8};
typedef struct {
    uint32_t grunt_character;
    void *read,*managed,*game,*link,*rpc,*maintenance,*medicine;SilphScheduleIdentity identity;SilphSessionConfig config;
    SilphRaidMetrics metrics;SilphKecleonEvent event;SilphGameStop stop;SilphManagedRef incident,gui,encounter_state,director,place,encounter,pokemon,attack,map_handler,map_settings,reward_gui,approach_gui;
    RaidDecisionGate gate;RaidBattleClock battle_clock;RaidActionWatch pending_action;uint64_t team[3],sequence,event_sequence,deadline,scan_at,nudge_at,next,attack_until,inventory_at,gui_at,clean_due;
    uint64_t charge_at,director_at,healing_target,healing_until,anchor_until;uintptr_t last_gui;uint32_t opened,rpc_active,rpc_kind,clean_opened,clean_index,clean_batches,dirty,nudged,returning,won,reward_read,berry_attempted,active_item,balls,medicine_active,healing_hp,healing_expected,anchor_expected,charge_slot;
    uint32_t species,iv[3],shiny,preparing,team_ready,team_refreshed,map_disabled,lost,loss_recovery,loss_seen,available_shields,shield_clicked,shield_wait,shield_before;uint64_t map_at,map_requests,capture_finished_at,loss_until,loss_started,loss_clear_at,shield_at;float map_saved[3];char source[97],spawn[17],top_name[128];double latitude,longitude,anchor_latitude,anchor_longitude;
    uint32_t attack_type,last_action_result;uint64_t charge_retry_at,action_log_at,battle_started,accepted_actions,rejected_actions,last_active;
    uint32_t raid_screen_seen,team_setup,team_setup_tries;uint64_t gym_press_at,gym_tap_at,team_setup_at,team_log_at,contract_log_at,phase1_at,phase1_log_at,loss_ok_at,claim_tap_log_at,enc_log_at,toast_seen_at,toast_tap_at;uint32_t claim_mode,raid_encounter,tap_request,toast_taps,catch_active,catch_seq,berry_fed,berry_fails;SilphManagedRef map_pokemon;void *catch_promise;uint64_t catch_at,catch_shown_at,catch_code;int32_t berry_item;uint32_t berry_before,free_raid_done,free_claim_fails,claim_ok,gym_no_raid_logged,hot_checked,cold_skips,stale_pending;uint64_t stale_at,stale_since,hot_seen_at;uint64_t gym_first_tap_at;uint32_t visual_off,visual_wait,visual_counted;uint64_t visual_at,visual_before;uint32_t rr_ready,rr_kind;int rr_err;uint64_t rr_code;SilphManagedRef rr_reply;char raid_spawn[96];void *heal_promise;uint32_t heal_active,heal_fails,heal_logged;uint64_t heal_target,heal_check_at,heal_at;
    uint32_t ui_taps,ui_seen,team_choice,team_choice_attempts,result_blocked,headless,raid_rpc_step,teleport_ready,join_failures,join_ui_fallback,raid_details_pending,raid_pass_override,raid_pass_tried,faction_team_attempted,lobby_pokemon_set,join_kickoff_pending,raid_details_ok,lobby_layer_hint,join_send_soft_fails,lobby_players;uintptr_t ticket_gui;uint32_t approach_fails;uint64_t ticket_seen_at;uint32_t clean_round_items,clean_dumped,lobby_seen,lobby_secs_known;int32_t lobby_secs;uint64_t lobby_left_at,lobby_rejoin_at;uint32_t free_pass_claimed,shop_seen,free_pass_spin_result,pass_seen,ui_joined,lobby_ready_pressed;uint64_t lobby_info_at;uint64_t free_pass_wait_at,join_press_at,pass_log_at;uint64_t ui_started,ui_retry_at,result_scan_at,reward_before,transition_since,raid_seed,lobby_joined_at,abandon_until,post_battle_at,approach_at,approach_pump_at,gps_settled_at,rpc_sent_at;char abandon_fort[161];RaidCaptureGate capture_gate,record_gate;uintptr_t fast_gui;uint32_t boss_checked;
    SilphManagedRef visual_properties;float visual_saved[4];unsigned visual_attempted,visual_tuned,timing_stage;uint64_t timing_at,timing_request;
} Raid;
size_t silph_raid_session_size(void){return sizeof(Raid);}
static bool raid_at_gym(Raid *s);
static bool raid_gym_ui_top(const Raid *s);
static void raid_retain_approach_gui(Raid *s,SilphManagedRef gui);
static int raid_pump_approach_gui(Raid *s,const SilphSchedulePermit *permit,uint64_t now);
static int raid_pump_approach_layers(Raid *s,const SilphSchedulePermit *permit,uint64_t now);
static void raid_skip_details_for_ui(Raid *s,uint64_t now);
static bool raid_rpc_soft_wait(int code);
static int ready_to_open(Raid *s,bool require_range,bool *ready);
static int raid_ensure_place(Raid *s);
static int raid_rpc_kickoff(Raid *s,const SilphSchedulePermit *permit,uint64_t now);
static int dismiss_raid_ticket(Raid *s,const SilphSchedulePermit *permit);
static int raid_pass_total(Raid *s);
static bool raid_join_press_allowed(Raid *s,const SilphSchedulePermit *permit,const char *name,uint64_t now);
static bool raid_near_gym(Raid *s,double lat,double lon);
static bool raid_gym_still_joinable(Raid *s);
static bool raid_gym_fast(const Raid *s);
static bool raid_rpc_only(const Raid *s);
static bool raid_pass_ui_assist(const Raid *s);
static bool raid_onboarding_gui(const char *name){
    if(!name||!name[0])return false;
    return strstr(name,"Conversation")||strstr(name,"Dialogue")||strstr(name,"PickTeam")||strstr(name,"TeamSelect")
        ||strstr(name,"JoinTeam")||strstr(name,"GymTeam")||strstr(name,"Willow")||strstr(name,"LevelUp");
}
static void raid_open_join(Raid *s,uint64_t now);
static bool raid_post_battle_gui(const char *name);
static int raid_load_gym_encounter_proto(Raid *s);
static int open_gym_encounter(Raid *s,const SilphSchedulePermit *permit,uint64_t now);
static int raid_post_battle_advance(Raid *s,const SilphSchedulePermit *permit,uint64_t now);
static int raid_pick_pass(Raid *s,int32_t *item);
static int drop(Raid *s,SilphManagedRef *ref){return silph_reward_drop(s->managed,ref);}
static int callref(Raid *s,SilphManagedRef ref,const char *name,SilphManagedRef *out){SilphReadValue v={0};int r=silph_read_call(s->read,ref,name,SILPH_READ_REFERENCE,NULL,0,&v);if(!r)*out=v.reference;return r;}
static int fieldref(Raid *s,SilphManagedRef ref,const char *name,SilphManagedRef *out){SilphReadValue v={0};int r=silph_read_field(s->read,ref,name,SILPH_READ_REFERENCE,&v);if(!r)*out=v.reference;return r;}
static int scalar(Raid *s,SilphManagedRef ref,const char *name,uint32_t kind,uint64_t *out,bool method){SilphReadValue v={0};int r=method?silph_read_call(s->read,ref,name,kind,NULL,0,&v):silph_read_field(s->read,ref,name,kind,&v);if(!r)*out=v.bits;return r;}
static int typed(Raid *s,SilphManagedRef ref,const char *space,const char *name,SilphManagedRef *out){return silph_read_field_type(s->read,ref,space,name,out);}
static int text(Raid *s,SilphManagedRef ref,const char *name,char *out,uint32_t capacity,bool method){SilphManagedRef value={0};int r=method?callref(s,ref,name,&value):fieldref(s,ref,name,&value);uint32_t length=0;if(!r)r=silph_read_utf8(s->read,value,out,capacity,&length);int freed=drop(s,&value);return r?r:freed;}
static bool istype(Raid *s,SilphManagedRef ref,const char *space,const char *name){return ref.serial&&!silph_read_is_type(s->read,ref,space,name);}
static int action(Raid *s,const SilphSchedulePermit *p,uint32_t op,SilphManagedRef receiver,const SilphReadArgument *args,uint32_t count){SilphReadValue out={0};int r=silph_raid_invoke(s->read,p,op,receiver,args,count,&out);int freed=drop(s,&out.reference);return r?r:freed;}
static void phase(Raid *s,uint32_t value,uint32_t stage,uint64_t deadline){s->metrics.phase=value;s->metrics.stage=stage;s->deadline=deadline;s->dirty=1;}
static void raid_mark_ui_joined(Raid *s,uint64_t now,const char *why){
    if(s->ui_joined)return;
    s->ui_joined=1;s->pass_seen=1;s->join_kickoff_pending=0;s->raid_details_pending=0;
    if(!s->lobby_joined_at){s->lobby_joined_at=now;s->cold_skips=0;}
    s->deadline=now+UINT64_C(360000000000);
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid lobby joined via game ui (%s); join rpc disabled",why?why:"?");
}
static inline uint32_t raid_required_min_players(uint32_t tier){
    if(tier==5)return 8;
    if(tier==1)return 3;
    if(tier>=4)return 6;
    if(tier==3)return 4;
    return 1;
}
static uint32_t raid_min_for_tier(const Raid *s,uint32_t tier){
    uint32_t v=s->config.raid_min_players;
    if(v&UINT32_C(0x40000000)){unsigned slot=tier>=7?3:tier==5?2:(tier==3||tier==4||tier==6)?1:0;return (v>>(slot*8))&0x3fu;}
    uint32_t d=raid_required_min_players(tier);return v>d?v:d;
}
static uint32_t raid_read_lobby_players(Raid *s,SilphManagedRef reply){
    if(!reply.serial)return 0;
    SilphManagedRef lobby={0};
    if(callref(s,reply,"get_Lobby",&lobby)&&!lobby.serial)
        if(fieldref(s,reply,"lobby_",&lobby)&&!lobby.serial)
            fieldref(s,reply,"Lobby",&lobby);
    if(!lobby.serial)return 0;
    uint64_t count=0;
    if(!scalar(s,lobby,"get_PlayerCount",SILPH_READ_I32,&count,true)&&count){}
    else if(!scalar(s,lobby,"player_count_",SILPH_READ_I32,&count,false)&&count){}
    else{
        SilphManagedRef rf={0};
        if(!callref(s,lobby,"get_Player",&rf)&&rf.serial){
            uint64_t c=0;if(!scalar(s,rf,"get_Count",SILPH_READ_I32,&c,true))count=c;
            drop(s,&rf);
        }
        if(!count&&!fieldref(s,lobby,"player_",&rf)&&rf.serial){
            uint64_t c=0;if(!scalar(s,rf,"get_Count",SILPH_READ_I32,&c,true))count=c;
            drop(s,&rf);
        }
        if(!count&&!callref(s,lobby,"get_PlayerId",&rf)&&rf.serial){
            uint64_t c=0;if(!scalar(s,rf,"get_Count",SILPH_READ_I32,&c,true))count=c;
            drop(s,&rf);
        }
    }
    drop(s,&lobby);
    return (uint32_t)count;
}
static bool raid_gym_still_joinable(Raid *s){
    if(!s||!s->stop.fort[0])return false;
    if(silph_game_read_gym_is_raid_egg(s->game,s->stop.fort))return false;
    SilphGameGym row={0};int r=silph_game_read_raid_gym_joinable(s->game,s->stop.fort,&row);
    if(!r){
        if(row.raid_seed)s->raid_seed=row.raid_seed;
        if(row.raid_level)s->grunt_character=row.raid_level;
        return true;
    }
    if(r==SILPH_MANAGED_CONTRACT||r==SILPH_MANAGED_ARGUMENT){
        if(silph_game_read_gym_is_raid_egg(s->game,s->stop.fort))return false;
        if(s->grunt_character>=1&&s->grunt_character<=7)return true;
        return false;
    }
    if(r==SILPH_MANAGED_NOT_READY)return true;
    return false;
}
#define RAID_DONE_FILE "/data/data/com.nianticlabs.pokemongo/files/silkraid-done.txt"
#define RAID_DONE_MAX 32
#define RAID_DONE_TTL 5400
static struct{char fort[161];int64_t at;}raid_done[RAID_DONE_MAX];
static unsigned raid_done_count;static bool raid_done_loaded,raid_done_reset;static int64_t raid_done_mtime;
static void raid_done_load(void){
    struct stat st;const int64_t mtime=stat(RAID_DONE_FILE,&st)?-1:(int64_t)st.st_mtime;
    if(raid_done_loaded&&mtime==raid_done_mtime)return;
    if(raid_done_loaded&&mtime<0){raid_done_count=0;raid_done_reset=true;raid_done_mtime=mtime;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","done list file removed; cleared");return;}
    if(raid_done_loaded&&mtime>=0)raid_done_mtime=mtime;
    if(raid_done_loaded)return;
    raid_done_loaded=true;raid_done_mtime=mtime;
    FILE *f=fopen(RAID_DONE_FILE,"r");if(!f)return;
    char fort[161];long long at;int64_t wall=(int64_t)time(NULL);
    while(raid_done_count<RAID_DONE_MAX&&fscanf(f,"%160s %lld",fort,&at)==2){
        if(wall-at>RAID_DONE_TTL)continue;
        memcpy(raid_done[raid_done_count].fort,fort,sizeof(fort));raid_done[raid_done_count++].at=at;
    }
    fclose(f);
}
static bool raid_done_has(const char *fort){
    raid_done_load();int64_t wall=(int64_t)time(NULL);
    for(unsigned i=0;i<raid_done_count;i++)if(wall-raid_done[i].at<=RAID_DONE_TTL&&!strcmp(raid_done[i].fort,fort))return true;
    return false;
}
static void raid_done_add(const char *fort,const char *why){
    if(!fort||!fort[0]||raid_done_has(fort))return;
    int64_t wall=(int64_t)time(NULL);unsigned slot=raid_done_count;
    if(slot>=RAID_DONE_MAX){slot=0;for(unsigned i=1;i<RAID_DONE_MAX;i++)if(raid_done[i].at<raid_done[slot].at)slot=i;}else raid_done_count++;
    snprintf(raid_done[slot].fort,sizeof(raid_done[slot].fort),"%s",fort);raid_done[slot].at=wall;
    FILE *f=fopen(RAID_DONE_FILE,"w");
    if(f){for(unsigned i=0;i<raid_done_count;i++)if(wall-raid_done[i].at<=RAID_DONE_TTL)fprintf(f,"%s %lld\n",raid_done[i].fort,(long long)raid_done[i].at);fclose(f);}
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid gym marked done (%s) fort=%s saved=%d",why?why:"?",fort,f?1:0);
}
#define RAID_CLAIM_FILE "/data/data/com.nianticlabs.pokemongo/files/silkraid-claim.txt"
#define RAID_CLAIM_TTL 2700
static bool raid_claim_get(char *fort,size_t size,double *lat,double *lon){
    FILE *f=fopen(RAID_CLAIM_FILE,"r");if(!f)return false;
    char id[161];double a=0,b=0;long long at=0;bool ok=false;int64_t wall=(int64_t)time(NULL);
    while(fscanf(f,"%160s %lf %lf %lld",id,&a,&b,&at)==4){if(wall-at<=RAID_CLAIM_TTL){snprintf(fort,size,"%s",id);*lat=a;*lon=b;ok=true;}}
    fclose(f);return ok;
}
static void raid_claim_set(const char *fort,double lat,double lon){
    FILE *f=fopen(RAID_CLAIM_FILE,"w");if(!f)return;
    if(fort&&fort[0])fprintf(f,"%s %.7f %.7f %lld\n",fort,lat,lon,(long long)time(NULL));
    fclose(f);
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid catch pending %s fort=%s",fort&&fort[0]?"saved":"cleared",fort&&fort[0]?fort:"-");
}
static int raid_pass_total(Raid *s);
static void raid_mark_won(Raid *s,const char *why){
    if(s->raid_pass_override==1401||s->raid_pass_override==0)s->free_raid_done=1;
    s->heal_check_at=0;
    raid_done_add(s->stop.fort,why);
    if(s->stop.fort[0])raid_claim_set(s->stop.fort,s->stop.latitude,s->stop.longitude);
}
static void raid_clear_gym_target(Raid *s,uint64_t now,const char *why){
    if(!s||!s->stop.fort[0])return;
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","abandon gym (%s) fort=%s seed=%llu",why?why:"?",s->stop.fort,(unsigned long long)s->raid_seed);
    memcpy(s->abandon_fort,s->stop.fort,sizeof(s->abandon_fort));s->abandon_until=now+UINT64_C(120000000000);
    memset(s->stop.fort,0,sizeof(s->stop.fort));s->stop.latitude=s->stop.longitude=0;s->raid_seed=0;s->teleport_ready=0;
    s->join_failures=s->join_ui_fallback=s->raid_details_pending=s->raid_rpc_step=s->lobby_pokemon_set=0;s->ui_taps=s->ui_seen=0;s->approach_at=0;s->approach_pump_at=0;s->gps_settled_at=0;s->raid_details_ok=0;s->lobby_layer_hint=0;s->join_send_soft_fails=0;s->free_pass_claimed=0;s->shop_seen=0;s->free_pass_wait_at=0;s->free_pass_spin_result=0;s->join_press_at=0;s->pass_seen=0;s->raid_pass_override=0;s->raid_pass_tried=0;s->ui_joined=0;s->lobby_ready_pressed=0;s->lobby_info_at=0;s->lobby_seen=0;s->lobby_left_at=0;s->ticket_gui=0;s->ticket_seen_at=0;s->approach_fails=0;s->raid_screen_seen=0;s->gym_press_at=0;s->gym_first_tap_at=0;s->hot_checked=0;s->hot_seen_at=0;s->lobby_secs_known=0;s->lobby_secs=0;s->lobby_rejoin_at=0;
    drop(s,&s->place);drop(s,&s->approach_gui);s->scan_at=now;s->map_at=0;phase(s,3,0,now+UINT64_C(120000000000));
    s->stale_pending=1;s->stale_since=now;s->stale_at=0;memset(s->top_name,0,sizeof(s->top_name));
}
static int release_candidate(Raid *s){SilphManagedRef *refs[]={&s->attack,&s->director,&s->place,&s->encounter,&s->pokemon,&s->reward_gui};int result=0;
    silph_raid_flow_unbind();
    for(unsigned i=0;i<6;i++){int r=drop(s,refs[i]);if(!result)result=r;}memset(&s->gate,0,sizeof(s->gate));memset(&s->battle_clock,0,sizeof(s->battle_clock));memset(&s->pending_action,0,sizeof(s->pending_action));memset(s->team,0,sizeof(s->team));s->preparing=s->team_ready=s->team_refreshed=s->team_choice=s->team_choice_attempts=s->headless=0;return result;}
static void result(Raid *s,uint32_t reason,bool verified){s->metrics.reason=reason;s->metrics.verified=verified;phase(s,7,RS_DONE,0);}
static int publish_event(Raid *s){
    SilphKecleonEvent next={.identity=s->identity,.sequence=s->event_sequence+1,.request=s->metrics.request,.phase=s->metrics.phase,
        .move=s->metrics.phase==6?(s->returning?2:1):(s->metrics.phase==5&&s->tap_request==1?2:s->metrics.phase==1&&s->tap_request==2?2:0),.reason=s->metrics.reason,.verified=s->metrics.verified,.latitude=s->latitude,.longitude=s->longitude};
    if(next.phase!=1&&next.phase!=8){
        if(!s->source[0])memcpy(s->source,"raid-anchor",12);
        memcpy(next.source,s->source,97);memcpy(next.fort,s->stop.fort,161);memcpy(next.incident,s->stop.incident,161);
    }
    if(!s->dirty)return 0;int r=silph_session_link_event(s->link,&next);if(r==SILPH_SCHEDULE_BUSY)return 0;
    if(!r){s->event=next;s->event_sequence++;s->dirty=0;}return r;
}
int silph_raid_session_publish(void *memory){Raid *s=memory;return s&&s->opened?publish_event(s):SILPH_MANAGED_STATE;}
static int raid_open_services(Raid *s,void *game){
    if(!s->incident.serial){int r=silph_game_read_raid_service(game,0,&s->incident);(void)r;}
    if(!s->gui.serial){
        int r=silph_game_read_raid_service(game,1,&s->gui);
        static uint64_t last_gui_log;
        struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
        uint64_t now_ms=(uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
        if(now_ms-last_gui_log>3000){
            last_gui_log=now_ms;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid_open_services gui: res=%d serial=%llu",r,(unsigned long long)s->gui.serial);
        }
    }
    return 0;
}
static int raid_open_rpc(Raid *s,void *read,void *managed,void *game,const SilphScheduleIdentity *id){
    if(!s->rpc)return SILPH_MANAGED_ARGUMENT;
    if(silph_rpc_action_ready(s->rpc))return 0;
    int r=silph_rpc_action_open(s->rpc,silph_rpc_action_size(),read,managed,game,id);
    if(r==SILPH_MANAGED_NOT_READY||r==SILPH_MANAGED_CONTRACT)r=0;
    return r;
}
int silph_raid_session_open(void *memory,size_t bytes,void *read,void *managed,void *game,void *link,const SilphScheduleIdentity *id,const SilphSessionConfig *config){
    if(!memory||bytes!=sizeof(Raid)||!read||!managed||!game||!link||!id||!config||config->mode!=SILPH_MODE_RAID)return SILPH_MANAGED_ARGUMENT;
    Raid *s=memory;
    if(s->opened){
        s->identity=*id;
        int r=raid_open_rpc(s,read,managed,game,id);
        if(!r)r=raid_open_services(s,game);
        return (r==SILPH_MANAGED_NOT_READY||r==SILPH_MANAGED_CONTRACT)?0:r;
    }
    memset(s,0,bytes);s->read=read;s->managed=managed;s->game=game;s->link=link;s->identity=*id;s->config=*config;
    s->rpc=calloc(1,silph_rpc_action_size());s->maintenance=calloc(1,silph_maintenance_size());s->medicine=calloc(1,silph_managed_promise_size());
    if(!s->rpc||!s->maintenance||!s->medicine){free(s->rpc);free(s->maintenance);free(s->medicine);memset(s,0,bytes);return SILPH_MANAGED_CAPACITY;}
    int r=raid_open_rpc(s,read,managed,game,id);
    if(!r)r=raid_open_services(s,game);
    s->metrics.request=1;phase(s,1,0,0);s->dirty=1;s->opened=1;
    return (r==SILPH_MANAGED_NOT_READY||r==SILPH_MANAGED_CONTRACT)?0:r;
}
bool silph_raid_inventory_due(void *memory){Raid *s=memory;if(!s||!s->opened)return true;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);uint64_t n=(uint64_t)t.tv_sec*1000000000+t.tv_nsec;
    if(n<s->inventory_at)return false;s->inventory_at=n+UINT64_C(2000000000);return true;}
static int command(Raid *s,const SilphKecleonCommand *c,uint64_t now){
    if(memcmp(&c->identity,&s->identity,sizeof(s->identity)))return SILPH_MANAGED_STALE;if(c->request!=s->metrics.request)return 0;
    if(c->kind==1&&s->metrics.phase==6&&s->returning&&!strcmp(c->source,"anchor")){
        s->anchor_latitude=c->latitude;s->anchor_longitude=c->longitude;s->anchor_expected=1;
    }else if(c->kind==1&&s->metrics.phase==1){
        int r=release_candidate(s);if(r)return r;SilphManagedRef cached={0};s->reward_before=0;r=typed(s,s->incident,"Holoholo.Rpc","InvasionEncounterOutProto",&cached);
        if(!r&&cached.serial)r=scalar(s,cached,"EncounterId",SILPH_READ_U64,&s->reward_before,false);int freed=drop(s,&cached);if(r||freed)return r?r:freed;
        s->grunt_character=0;s->latitude=c->latitude;s->longitude=c->longitude;memcpy(s->source,c->source,97);memset(&s->stop,0,sizeof(s->stop));
        s->metrics.reason=s->metrics.verified=s->metrics.result=0;s->won=s->reward_read=s->nudged=s->returning=s->berry_attempted=s->active_item=s->balls=s->anchor_expected=s->healing_expected=s->lost=s->loss_recovery=s->loss_seen=s->shield_clicked=s->shield_wait=s->available_shields=0;s->last_gui=s->capture_finished_at=s->loss_clear_at=0;s->charge_slot=2;s->raid_seed=0;s->raid_rpc_step=0;s->teleport_ready=0;s->join_failures=0;s->join_ui_fallback=0;s->raid_details_pending=0;s->raid_pass_override=0;s->raid_pass_tried=0;s->ui_joined=0;s->lobby_ready_pressed=0;s->lobby_info_at=0;s->lobby_seen=0;s->lobby_left_at=0;s->ticket_gui=0;s->ticket_seen_at=0;s->approach_fails=0;s->raid_screen_seen=0;s->gym_press_at=0;s->gym_first_tap_at=0;s->hot_checked=0;s->hot_seen_at=0;s->lobby_secs_known=0;s->lobby_secs=0;s->lobby_rejoin_at=0;s->faction_team_attempted=0;
        s->charge_retry_at=s->action_log_at=s->battle_started=s->accepted_actions=s->rejected_actions=s->last_active=0;s->last_action_result=0;
        s->map_at=0;s->scan_at=now;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","anchor teleport ack; waiting for map before gym scan");
    }else if(c->kind==2&&(s->metrics.phase==2||s->metrics.phase==6)&&c->move==(s->metrics.phase==2?0:s->returning?2:1)){
        if(!c->accepted){result(s,2,false);return 0;}
        if(s->returning){s->returning=0;s->next=now+UINT64_C(100000000);s->anchor_until=now+UINT64_C(10000000000);phase(s,5,RS_CAPTURE,now+UINT64_C(120000000000));}
        else if(s->metrics.phase==6&&s->stop.fort[0]){
            if(!s->stop.incident[0]){
                bool in_range=false;int range_r=ready_to_open(s,true,&in_range);
                bool at_gym=raid_at_gym(s);
                if(!range_r&&(in_range||at_gym)){
                    s->teleport_ready=1;
                    if(s->gps_settled_at&&now-s->gps_settled_at<UINT64_C(45000000000)&&s->metrics.phase==5){
                        s->ui_retry_at=now+UINT64_C(300000000);
                        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym gps hold (skip re-arm) fort=%s",s->stop.fort);
                    }else{
                        s->gps_settled_at=now;s->ui_retry_at=now+UINT64_C(3500000000);
                        if(s->metrics.phase!=5||s->metrics.stage!=RS_OPEN)raid_open_join(s,now);
                        else{s->join_kickoff_pending=(s->raid_seed>1)?1:0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym gps settled; resume join fort=%s",s->stop.fort);}
                        if(s->metrics.phase==6)phase(s,5,RS_OPEN,now+UINT64_C(360000000000));
                        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym gps settled; join armed fort=%s",s->stop.fort);
                    }
                }else{
                    s->teleport_ready=0;
                    s->latitude=s->stop.latitude;s->longitude=s->stop.longitude;s->dirty=1;s->ui_retry_at=now+UINT64_C(800000000);
                    __android_log_print(ANDROID_LOG_WARN,"SilphRaid","gym gps not in range fort=%s range_r=%d in_range=%u at_gym=%u; re-teleport via mock",s->stop.fort,range_r,(unsigned)in_range,(unsigned)at_gym);
                }
            }else{s->teleport_ready=1;phase(s,4,0,now+UINT64_C(30000000000));}
        }
        else{s->scan_at=now;s->map_at=0;s->nudge_at=now+UINT64_C(1500000000);phase(s,3,0,now+UINT64_C(120000000000));}
    }else if(c->kind==3&&s->metrics.phase==4){if(c->accepted){if(s->stop.incident[0]){int r=silph_raid_flow_bind(s->managed,s->incident,s->stop.incident,s->metrics.request,s->grunt_character);if(r)return r;}s->ui_taps=s->ui_seen=0;s->fast_gui=0;s->ui_started=now;s->ui_retry_at=now;phase(s,5,RS_OPEN,now+UINT64_C(90000000000));}else result(s,4,true);
    }else if(c->kind==4&&s->metrics.phase==7){
        int r=release_candidate(s);if(r)return r;
        if(s->loss_recovery){s->metrics.reason=7;phase(s,8,0,0);}
        else if(s->metrics.raids>=s->config.max_encounters){s->metrics.reason=5;phase(s,8,0,0);}
        else{s->metrics.request++;s->metrics.reason=0;s->metrics.encounter=0;s->metrics.result=0;
            memset(s->source,0,sizeof(s->source));memset(s->stop.fort,0,sizeof(s->stop.fort));s->stop.latitude=s->stop.longitude=0;s->raid_seed=0;
            s->teleport_ready=s->faction_team_attempted=0;s->raid_rpc_step=0;s->lobby_pokemon_set=0;drop(s,&s->place);
            phase(s,1,0,0);}
    }else if(c->kind==5&&s->metrics.phase==1){s->metrics.reason=6;phase(s,8,0,0);}return 0;
}
static bool raid_map_ready(const SilphGameSnapshot *snapshot){
    return snapshot&&snapshot->map_stops>=40&&snapshot->pokemon_count>0;
}
static int stop_at(Raid *s,const SilphGameSnapshot *snapshot,uint64_t now,uint64_t wall){
    (void)wall;
    if(s->metrics.phase==5&&s->stop.fort[0]&&(s->metrics.stage==RS_OPEN||s->metrics.stage==RS_UI))return 0;
    if(now<s->scan_at)return 0;
    if(!raid_map_ready(snapshot)){s->scan_at=now+UINT64_C(500000000);return 0;}
    s->scan_at=now+UINT64_C(200000000);
    double radius=s->config.raid_radius_m;
    if(!isfinite(radius)||radius<=0)radius=500;
    if(radius>5000)radius=5000;
        SilphGameStop anchor={0};SilphGameGym gym={0};
    int r=0;
    if(!s->stop.fort[0]&&!s->claim_mode){
        char cf[161]={0};double clat=0,clon=0;
        if(raid_claim_get(cf,sizeof(cf),&clat,&clon)){
            s->claim_mode=1;s->claim_ok=0;memcpy(s->stop.fort,cf,sizeof(s->stop.fort));s->stop.latitude=clat;s->stop.longitude=clon;
            s->latitude=clat;s->longitude=clon;s->dirty=1;s->grunt_character=1;s->raid_seed=0;s->teleport_ready=0;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","revisiting won raid to claim rewards and catch fort=%s lat=%.6f lon=%.6f",cf,clat,clon);
        }
    }
    if(!s->stop.fort[0]){
        double plat=0,plon=0;int pos=silph_game_read_rocket_position(s->game,&plat,&plon);
        bool player_ok=!pos&&isfinite(plat)&&isfinite(plon);
        if(s->config.raid_global_hot&&player_ok){anchor.latitude=plat;anchor.longitude=plon;anchor.meters=0;}
        else if(player_ok){
            double dy=(plat-s->config.raid_anchor_lat)*111195,dx=remainder(plon-s->config.raid_anchor_lon,360.0)*111195*cos(s->config.raid_anchor_lat*0.017453292519943295);
            if(hypot(dx,dy)>radius){anchor.latitude=plat;anchor.longitude=plon;anchor.meters=0;
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan at player (away from anchor) lat=%.6f lon=%.6f",plat,plon);}
            else{anchor.latitude=s->config.raid_anchor_lat;anchor.longitude=s->config.raid_anchor_lon;anchor.meters=0;}
        }else{anchor.latitude=s->config.raid_anchor_lat;anchor.longitude=s->config.raid_anchor_lon;anchor.meters=0;}
        uint32_t tier=s->config.raid_tier_mask;
        if(tier==127u)tier=0;
        uint32_t prefer_players=s->config.raid_min_players>0?1:0;
        SilphGameSnapshot snap={0};(void)silph_game_read_refresh(s->game,false,&snap);
        if(now>=s->abandon_until&&s->abandon_until>0){s->abandon_until=0;s->abandon_fort[0]=0;silph_game_read_raid_gym_clear_skips(s->game);}
        raid_done_load();
        if(raid_done_reset){raid_done_reset=false;silph_game_read_raid_gym_clear_skips(s->game);}
        {int64_t wall=(int64_t)time(NULL);for(unsigned i=0;i<raid_done_count;i++)if(wall-raid_done[i].at<=RAID_DONE_TTL)(void)silph_game_read_raid_gym_add_skip(s->game,raid_done[i].fort);}
        for(unsigned scan_try=0;scan_try<6&&!gym.fort[0];scan_try++){
            memset(&gym,0,sizeof(gym));gym.meters=INFINITY;
            r=silph_game_read_raid_gym(s->game,radius,tier,prefer_players,&anchor,&gym);
            if(r||!gym.fort[0])break;
            if(raid_done_has(gym.fort)){
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan reject already battled fort=%s dist=%.0fm",gym.fort,gym.meters);
                (void)silph_game_read_raid_gym_add_skip(s->game,gym.fort);
                memset(&gym,0,sizeof(gym));gym.meters=INFINITY;
                continue;
            }
            if(silph_game_read_gym_is_raid_egg(s->game,gym.fort)||gym.meters<=1.0){
                if(silph_game_read_gym_is_raid_egg(s->game,gym.fort))
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan reject egg fort=%s dist=%.0fm",gym.fort,gym.meters);
                else
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan reject underfoot fort=%s (prefer nearby active raid)",gym.fort);
                (void)silph_game_read_raid_gym_add_skip(s->game,gym.fort);
                memcpy(s->abandon_fort,gym.fort,sizeof(s->abandon_fort));s->abandon_until=now+UINT64_C(90000000000);
                memset(&gym,0,sizeof(gym));gym.meters=INFINITY;
                continue;
            }
        }
        if(!r&&gym.fort[0])__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan chosen fort=%s tier=%u seed=%llu dist=%.0fm",gym.fort,(unsigned)gym.raid_level,(unsigned long long)gym.raid_seed,gym.meters);
        else if(!r)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan none anchor=%.6f,%.6f radius=%.0f",(double)anchor.latitude,(double)anchor.longitude,radius);
        if(r==SILPH_MANAGED_ARGUMENT){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan waiting; map scan not ready");r=SILPH_MANAGED_NOT_READY;}
        if(r==SILPH_MANAGED_CONTRACT){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan contract; waiting for map data");r=SILPH_MANAGED_NOT_READY;}
        if(r==SILPH_MANAGED_NOT_READY){
            if(now>=s->deadline){result(s,3,false);return 0;}
            if(!s->nudged&&now>=s->nudge_at){s->nudged=1;phase(s,6,0,now+UINT64_C(15000000000));}return 0;
        }
        if(r)return r;
        s->lobby_players=gym.lobby_players;
        if(0&&(s->config.raid_boss_count||s->config.raid_boss_species)){
            bool want=s->config.raid_boss_species&&gym.boss_species==s->config.raid_boss_species;
            for(unsigned i=0;!want&&i<s->config.raid_boss_count&&i<8;i++)if(gym.boss_species&&gym.boss_species==s->config.raid_boss_species_list[i])want=true;
            if(!want){
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan reject boss species=%u (want %u +%u list)",(unsigned)gym.boss_species,(unsigned)s->config.raid_boss_species,(unsigned)s->config.raid_boss_count);
                if(now>=s->deadline){result(s,3,false);return 0;}
                s->scan_at=now+UINT64_C(500000000);return 0;
            }
        }
        if(!gym.raid_level||gym.raid_level>7){
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan rejected fort=%s (need hatched tier)",gym.fort);
            if(now>=s->deadline){result(s,3,false);return 0;}
            s->scan_at=now+UINT64_C(500000000);return 0;
        }
        if(raid_done_has(gym.fort)){
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan skip already battled fort=%s",gym.fort);
            if(now>=s->deadline){result(s,3,false);return 0;}
            s->scan_at=now+UINT64_C(800000000);return 0;
        }
        if(s->abandon_fort[0]&&now<s->abandon_until&&!strcmp(gym.fort,s->abandon_fort)){
            if(gym.raid_seed&&gym.raid_level>=1&&gym.raid_level<=5){
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan un-blacklist fort=%s (active raid seed)",gym.fort);
                s->abandon_fort[0]=0;s->abandon_until=0;
            }else{
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan skip blacklisted fort=%s",gym.fort);
                s->scan_at=now+UINT64_C(800000000);return 0;
            }
        }
        if(gym.raid_level>=4){
            int32_t pass_item=0;uint32_t pass_count=0;
            if(!raid_pick_pass(s,&pass_item))pass_count=1;
            if(!pass_count){
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym skip fort=%s tier=%u (no raid pass in bag)",gym.fort,(unsigned)gym.raid_level);
                s->scan_at=now+UINT64_C(400000000);return 0;
            }
        }
        if(silph_game_read_gym_is_raid_egg(s->game,gym.fort)){
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan reject egg at assign fort=%s",gym.fort);
            (void)silph_game_read_raid_gym_add_skip(s->game,gym.fort);
            s->scan_at=now+UINT64_C(400000000);return 0;
        }
        {
            SilphGameGym live={0};int live_r=silph_game_read_raid_gym_joinable(s->game,gym.fort,&live);
            if(!live_r){
                if(live.raid_seed)gym.raid_seed=live.raid_seed;
                if(live.raid_level)gym.raid_level=live.raid_level;
            }else if(live_r==SILPH_MANAGED_CONTRACT&&!gym.raid_live){
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan reject unjoinable fort=%s live_r=%d",gym.fort,live_r);
                (void)silph_game_read_raid_gym_add_skip(s->game,gym.fort);
                memcpy(s->abandon_fort,gym.fort,sizeof(s->abandon_fort));s->abandon_until=now+UINT64_C(90000000000);
                s->scan_at=now+UINT64_C(400000000);return 0;
            }
        }
        s->latitude=gym.latitude;s->longitude=gym.longitude;s->dirty=1;
        memcpy(s->stop.fort,gym.fort,sizeof(s->stop.fort));s->stop.latitude=gym.latitude;s->stop.longitude=gym.longitude;
        s->grunt_character=gym.raid_level?gym.raid_level:1;s->raid_seed=gym.raid_seed;s->teleport_ready=0;
        if(s->raid_seed)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym raid_seed=%llu fort=%s tier=%u",(unsigned long long)s->raid_seed,s->stop.fort,(unsigned)gym.raid_level);
        s->scan_at=now;
        if(gym.map_place.serial){
            if(s->place.serial)drop(s,&s->place);
            s->place=gym.map_place;gym.map_place=(SilphManagedRef){0};
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym tap target retained from scan fort=%s",s->stop.fort);
        }else{
            if(s->place.serial)drop(s,&s->place);
            (void)silph_game_read_gym_map(s->game,s->stop.fort,&s->place);
            if(s->place.serial)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym map place cached at scan fort=%s",s->stop.fort);
        }
    }
    if(s->place.serial)r=0;
    else{
        r=silph_game_read_gym_map(s->game,s->stop.fort,&s->place);
        if((r==SILPH_MANAGED_NOT_READY||r==SILPH_MANAGED_CONTRACT||r==SILPH_MANAGED_ARGUMENT||r==SILPH_MANAGED_EXCEPTION)&&!s->place.serial)r=silph_game_read_place(s->game,s->stop.fort,&s->place);
    }
    if((r==SILPH_MANAGED_NOT_READY||r==SILPH_MANAGED_CONTRACT||r==SILPH_MANAGED_ARGUMENT||r==SILPH_MANAGED_EXCEPTION)&&!s->place.serial){
        if(!s->stop.incident[0]){
            if(!s->teleport_ready&&!raid_at_gym(s)){
                s->latitude=s->stop.latitude;s->longitude=s->stop.longitude;s->dirty=1;
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym place pending; teleport anyway fort=%s err=%d",s->stop.fort,r);
                phase(s,6,0,now+UINT64_C(15000000000));return 0;
            }
            if(s->raid_seed){
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym place pending; join rpc fort=%s err=%d",s->stop.fort,r);
                raid_open_join(s,now);return 0;
            }
        }
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym place pending fort=%s err=%d",s->stop.fort,r);
        s->scan_at=now+UINT64_C(50000000);return 0;
    }
    if(r&&r!=SILPH_MANAGED_EXCEPTION)return r;
    if(r==SILPH_MANAGED_EXCEPTION)r=0;
    if(!s->stop.incident[0]){
        if(!raid_at_gym(s)){
            s->latitude=s->stop.latitude;s->longitude=s->stop.longitude;s->dirty=1;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","teleporting to gym before join");
            phase(s,6,0,now+UINT64_C(15000000000));return 0;
        }
        if(!raid_at_gym(s)){
            s->teleport_ready=0;s->latitude=s->stop.latitude;s->longitude=s->stop.longitude;s->dirty=1;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym place ready; teleport before join fort=%s",s->stop.fort);
            phase(s,6,0,now+UINT64_C(15000000000));return 0;
        }
        if(silph_game_read_gym_is_raid_egg(s->game,s->stop.fort)){
            raid_clear_gym_target(s,now,"scan target is raid egg");
            return 0;
        }
        if(!raid_gym_still_joinable(s)){
            raid_clear_gym_target(s,now,"map gym is not a joinable raid");
            return 0;
        }
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym place ready fort=%s level=%u join=rpc",s->stop.fort,(unsigned)s->grunt_character);
        s->teleport_ready=1;raid_open_join(s,now);return 0;
    }
    phase(s,4,0,now+UINT64_C(30000000000));return 0;
}
static void refresh_map(Raid *s,const SilphSchedulePermit *permit,const SilphGameSnapshot *snapshot,uint64_t now){
    if(!raid_map_ready(snapshot))return;
    if(s->map_disabled||now<s->map_at)return;
    s->map_at=now+(s->map_requests?UINT64_C(2000000000):UINT64_C(800000000));int r=0;
    if(!s->map_handler.serial)r=silph_game_read_raid_service(s->game,5,&s->map_handler);
    if(!r&&!s->map_settings.serial){
        SilphManagedRef service={0},global={0};r=typed(s,s->map_handler,"Niantic.Holoholo","IGlobalSettings",&service);
        if(!r)r=callref(s,service,"get_Settings",&global);if(!r)r=fieldref(s,global,"MapSettings",&s->map_settings);
        if(!r)r=silph_raid_map_settings(s->read,permit,s->map_settings,s->map_saved,false);drop(s,&service);drop(s,&global);
        if(r)drop(s,&s->map_settings);
    }
    if(!r)r=action(s,permit,RK_MAP_REFRESH,s->map_handler,NULL,0);
    if(r){s->map_disabled=1;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","map refresh unavailable error=%d",r);}
    else __android_log_print(ANDROID_LOG_INFO,"SilphRaid","map refresh requested sequence=%llu",(unsigned long long)++s->map_requests);
}
static int care(Raid *,const SilphSchedulePermit *,uint64_t);
static int open_encounter(Raid *,uint64_t);
static int prepare_team(Raid *s,const SilphSchedulePermit *permit,SilphManagedRef party,uint64_t now){
    if(s->lost){s->gui_at=now+UINT64_C(500000000);int r=action(s,permit,RK_PARTY_CANCEL,party,NULL,0);if(!r)s->loss_seen=1;return r;}
    if((s->config.flags&SILPH_CFG_RECOMMENDED_TEAM)&&!s->team_choice){
        s->gui_at=now+UINT64_C(150000000);
        if(s->team_choice_attempts++>=20){s->metrics.error=121;phase(s,8,0,0);return 0;}
        int r=action(s,permit,RK_PARTY_RECOMMEND,party,NULL,0);if(r==SILPH_MANAGED_NOT_READY)return 0;
        if(!r){s->team_choice++;s->team_choice_attempts=0;}return r;
    }
    uint64_t team[3]={0};bool usable=false;int r=silph_raid_prepared_team(s->read,party,team,&usable);
    if(r==SILPH_MANAGED_NOT_READY)return 0;
    if(!r){
        if(!s->team[0])memcpy(s->team,team,sizeof(team));else if(memcmp(s->team,team,sizeof(team)))r=SILPH_MANAGED_STALE;
        if(!r&&!s->team_ready){s->preparing=1;r=care(s,permit,now);}
        if(!r&&s->team_ready){
            s->gui_at=now+UINT64_C(100000000);
            if(usable&&s->team_refreshed){s->gui_at=now+UINT64_C(2000000000);r=action(s,permit,RK_SUBMIT,party,NULL,0);}
            else if(s->team_refreshed++<3){SilphManagedRef bag={0};r=silph_game_read_raid_service(s->game,7,&bag);SilphReadArgument argument={.kind=SILPH_READ_REFERENCE,.value.reference=bag};if(!r)r=action(s,permit,RK_PARTY_REFRESH,party,&argument,1);drop(s,&bag);}
            else{s->metrics.error=117;phase(s,8,0,0);}
        }
    }
    return r;
}
static bool raid_approach_result(Raid *s,const SilphSchedulePermit *permit,SilphManagedRef gui,uint64_t now,const char *name,int *r){
    if(!name||!strstr(name,"RaidApproach")||!s->stop.fort[0]||s->ui_joined||s->won)return false;
    if(!*r){s->approach_fails=0;s->raid_screen_seen=0;s->gym_press_at=0;return false;}
    bool lockout=*r==SILPH_MANAGED_STATE;
    if(*r==SILPH_MANAGED_NOT_READY&&s->pass_seen)s->approach_fails++;
    if(!lockout&&s->approach_fails<4)return false;
    int closed=gui.serial?action(s,permit,RK_GUI_CLOSE,gui,NULL,0):-1;
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid approach unusable (%s, fails=%u, close=%d); moving to next gym",lockout?"gym closed":"join button failing",s->approach_fails,closed);
    if(!lockout)raid_done_add(s->stop.fort,"join failing");
    raid_clear_gym_target(s,now,lockout?"gym closed (lockout)":"approach join failing");
    s->approach_fails=0;s->raid_screen_seen=0;s->gym_press_at=0;*r=0;return true;
}
static bool raid_lobby_tick(Raid *s,const SilphSchedulePermit *permit,SilphManagedRef gui,uint64_t now){
    if(s->won||!gui.serial||now<s->lobby_info_at||!s->stop.fort[0])return false;
    s->lobby_info_at=now+UINT64_C(1000000000);
    uint32_t req=raid_min_for_tier(s,s->grunt_character);
    bool known=s->lobby_secs_known,battle=s->metrics.stage>=RS_BATTLE;
    uint32_t want=0;
    if(!battle&&known&&s->lobby_secs<-3&&now>=s->lobby_rejoin_at){want=2;s->lobby_rejoin_at=now+UINT64_C(3000000000);}
    else if(!battle&&!s->lobby_ready_pressed&&s->lobby_players>=req&&s->lobby_players)want=1;
    else if(!battle&&!s->lobby_ready_pressed&&known&&s->lobby_secs>=0&&s->lobby_secs<=10&&s->lobby_players<req&&s->lobby_players)want=3;
    else if(!battle&&!s->lobby_ready_pressed&&s->lobby_joined_at&&now-s->lobby_joined_at>UINT64_C(30000000000)&&s->lobby_players<=1&&s->lobby_players<req)want=3;
    SilphReadArgument arg={.kind=SILPH_READ_ENUM32,.value.bits=want};SilphReadValue out={0};
    int r=silph_raid_invoke(s->read,permit,RK_LOBBY,gui,&arg,1,&out);drop(s,&out.reference);
    if(r){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid lobby read err=%d",r);return false;}
    uint32_t total=(uint32_t)(out.bits&0xffff),listed=(uint32_t)((out.bits>>16)&0xffff),found=(uint32_t)((out.bits>>32)&0xff),pressed=(uint32_t)((out.bits>>40)&1);
    int32_t secs=(int16_t)(uint16_t)(out.bits>>48);
    uint32_t players=total?total:listed;
    if(found&8){s->lobby_secs=secs;s->lobby_secs_known=1;}
    if(players!=s->lobby_players||want||(found&8&&secs%10==0))
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid lobby players=%u total=%u listed=%u min=%u secs_left=%d action=%u ok=%u found=%u",players,total,listed,req,(found&8)?secs:-1,want,pressed,found);
    if(players)s->lobby_players=players;
    if(want==1&&pressed)s->lobby_ready_pressed=1;
    if(want==3){
        int closed=pressed?0:action(s,permit,RK_GUI_CLOSE,gui,NULL,0);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","left raid lobby: players=%u below tier minimum %u secs_left=%d (leave press=%u close=%d)",s->lobby_players,req,s->lobby_secs,pressed,closed);
        raid_done_add(s->stop.fort,"lobby below minimum players");
        raid_clear_gym_target(s,now,"lobby below minimum players");
        return true;
    }
    return false;
}
static int raid_exit_non_raid_gym(Raid *s,const SilphSchedulePermit *permit,SilphManagedRef gui,const char *name){
    if(s->stop.incident[0]||!s->stop.fort[0]||!gui.serial)return 0;
    if(raid_gym_still_joinable(s))return 0;
    int closed=action(s,permit,RK_RAID_GUI,gui,NULL,0);
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","non-raid gym exit gui=%s result=%d fort=%s",name?name:"?",closed,s->stop.fort);
    return closed;
}
static int raid_tap_place(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(s->gym_tap_at&&now<s->gym_tap_at)return SILPH_MANAGED_NOT_READY;
    int r=action(s,permit,RK_TAP,s->place,NULL,0);
    if(!r){s->gym_tap_at=now+UINT64_C(4000000000);if(!s->gym_first_tap_at)s->gym_first_tap_at=now;}
    return r;
}
static int ui(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(raid_pass_ui_assist(s))(void)dismiss_raid_ticket(s,permit);
    SilphReadValue top={0};int r=silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&top);if(r)return r;
    if(!top.reference.serial)return 0;void *object=NULL;r=silph_managed_target(s->managed,top.reference,&object);
    char name[128]={0};if(!silph_raid_class(s->read,top.reference,name,sizeof(name))&&strcmp(name,s->top_name)){
        memcpy(s->top_name,name,strlen(name)+1);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gui=%s request=%llu",name,(unsigned long long)s->metrics.request);
        silph_dump_methods(s->read,top.reference,name);
    }
    if(strstr(name,"RaidLobby"))s->lobby_seen=1;
    if(!s->boss_checked&&s->metrics.phase==5&&s->metrics.stage==RS_OPEN&&s->stop.fort[0]&&s->ui_started&&now>=s->ui_started+UINT64_C(3000000000)){
        uint32_t boss=0;
        if(!silph_game_read_raid_root_boss(s->game,&boss)&&boss){
            s->boss_checked=1;
            bool want=(s->config.raid_boss_species!=0&&boss==s->config.raid_boss_species);
            for(unsigned i=0;!want&&i<s->config.raid_boss_count&&i<8;i++)if(boss==s->config.raid_boss_species_list[i])want=true;
            if(!want){
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid boss mismatch boss=%u want=%u; skip fort=%s",(unsigned)boss,(unsigned)s->config.raid_boss_species,s->stop.fort);
                (void)silph_game_read_raid_gym_add_skip(s->game,s->stop.fort);
                drop(s,&top.reference);
                raid_clear_gym_target(s,now,"boss mismatch");
                return 0;
            }
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid boss match boss=%u fort=%s",(unsigned)boss,s->stop.fort);
        }
    }
    if(strstr(name,"RaidApproach")||strstr(name,"UseRaidTicket")||strstr(name,"RaidLobby"))s->raid_screen_seen=1;
    if(s->lobby_seen&&!s->won&&s->metrics.stage<RS_BATTLE&&s->stop.fort[0]
        &&(!strcmp(name,"MainMenuGuiController")||strstr(name,"Approach")||strstr(name,"GymGui")||strstr(name,"GymDetails")||strstr(name,"GymRoot"))){
        if(!s->lobby_left_at)s->lobby_left_at=now;
        else if(now-s->lobby_left_at>(!strcmp(name,"MainMenuGuiController")?UINT64_C(12000000000):UINT64_C(4000000000))){
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid lobby exited (quit) gui=%s; finding another gym",name);
            raid_done_add(s->stop.fort,"lobby quit");
            raid_clear_gym_target(s,now,"lobby quit");
            drop(s,&top.reference);return 0;
        }
    }else if(strstr(name,"RaidLobby"))s->lobby_left_at=0;
    if(!s->stop.fort[0]&&!s->won&&s->metrics.stage<RS_BATTLE&&(strstr(name,"RaidApproach")||strstr(name,"Gym")||strstr(name,"UseRaidTicket")||strstr(name,"Combat")||strstr(name,"BattlePrep"))){
        if(now>=s->gui_at){
            s->gui_at=now+UINT64_C(1000000000);
            int closed=action(s,permit,RK_GUI_CLOSE,top.reference,NULL,0);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","stale gym screen close gui=%s result=%d",name,closed);
        }
        drop(s,&top.reference);return 0;
    }
    bool fast=istype(s,top.reference,"Niantic.Holoholo.Combat.Preparation","CombatPreparationRootGuiController")||istype(s,top.reference,"Niantic.Holoholo.Combat.NpcTrainers","NpcTrainerInterstitialGui")||istype(s,top.reference,"Niantic.Holoholo.Incident","IncidentInvasionDialogGui")||istype(s,top.reference,"Niantic.Holoholo.Incident","IncidentCombatResolveGuiController")
        ||(s->approach_at&&(strstr(name,"GymApproach")||strstr(name,"RaidApproach")));
    if(!r&&fast&&s->fast_gui!=(uintptr_t)object){int tuned=action(s,permit,RK_FAST_GUI,top.reference,NULL,0);s->fast_gui=(uintptr_t)object;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","presentation shortened gui=%s result=%d",name,tuned);}
    uint32_t op=0;
    if(s->metrics.stage==RS_UI&&(istype(s,top.reference,"Niantic.Holoholo.Combat.NpcTrainers","NpcTrainerInterstitialGui")||istype(s,top.reference,"Niantic.Holoholo.Incident","IncidentInvasionDialogGui")||istype(s,top.reference,"Niantic.Holoholo.Combat.Preparation","CombatPreparationRootGuiController")||istype(s,top.reference,"Niantic.Holoholo.Combat.PokemonPartyState","CombatPokemonPartyWidget")))s->ui_seen=1;
    if(istype(s,top.reference,"Niantic.Holoholo.Combat.NpcTrainers","NpcTrainerInterstitialGui"))op=s->lost?RK_CANCEL:silph_raid_flow_challenged(s->managed,top.reference)?0:RK_CHALLENGE;
    else if(istype(s,top.reference,"Niantic.Holoholo.Incident","IncidentInvasionDialogGui"))op=RK_DIALOG;
    else if(s->metrics.stage==RS_UI&&istype(s,top.reference,"Niantic.Holoholo.Pokestop","PokestopSpinnerInteractionGuiController"))op=RK_STOP_EXIT;
    else if(istype(s,top.reference,"Niantic.Holoholo.UI.News","NewsStartupEntryGuiController")||istype(s,top.reference,"Niantic.Holoholo.News","NewsStartupEntryGuiController"))op=RK_RAID_GUI;
    else if(istype(s,top.reference,"Niantic.Holoholo.UI","WelcomeGuiController")||istype(s,top.reference,"Niantic.Holoholo.Onboarding","WelcomeGuiController")||istype(s,top.reference,"Niantic.Holoholo.UI","FirstTimeUserWelcomeState")||istype(s,top.reference,"Niantic.Holoholo.Onboarding","FirstTimeUserWelcomeState")||istype(s,top.reference,"Niantic.Holoholo.UI","ReferralEntryGuiController"))op=RK_RAID_GUI;
    else if(istype(s,top.reference,"Niantic.Holoholo.Raid","ReceivedRaidTicketGuiController")||istype(s,top.reference,"Niantic.Holoholo.Raid","TooltipToastGuiController")){s->ui_seen=1;op=RK_RAID_GUI;}
    else if(s->stop.fort[0]&&s->metrics.phase==5&&strstr(name,"UseRaidTicket")){
        s->ui_seen=1;
        if(s->ticket_gui!=(uintptr_t)object){s->ticket_gui=(uintptr_t)object;s->ticket_seen_at=now;s->gui_at=now+UINT64_C(800000000);}
        if(!s->lobby_seen&&!s->won&&s->ticket_seen_at&&now-s->ticket_seen_at>UINT64_C(15000000000)){
            int closed=action(s,permit,RK_GUI_CLOSE,top.reference,NULL,0);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid pass screen stuck %llus without lobby close=%d",(unsigned long long)((now-s->ticket_seen_at)/1000000000ULL),closed);
            raid_done_add(s->stop.fort,"pass screen never reached lobby");
            raid_clear_gym_target(s,now,"raid pass screen stuck (gym closed); moving to next gym");
            drop(s,&top.reference);return 0;
        }
        if(now>=s->gui_at){
            s->gui_at=now+UINT64_C(2500000000);
            int used=action(s,permit,RK_RAID_GUI,top.reference,NULL,0);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid pass screen gui=%s result=%d",name,used);
            if(!used)raid_mark_ui_joined(s,now,"pass used");
            else if(used!=SILPH_MANAGED_NOT_READY)r=used;
        }
    }
    else if(s->stop.fort[0]&&s->metrics.phase==5&&(strstr(name,"RaidPassSelection")||strstr(name,"UseRaidPass")||strstr(name,"RaidPassGui")||strstr(name,"PassSelection")||strstr(name,"SelectPass")||strstr(name,"FreeRaidPassGui")||strstr(name,"RaidFreePass"))){
        s->ui_seen=1;op=RK_RAID_GUI;s->ui_retry_at=now+UINT64_C(200000000);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","free pass claim gui=%s",name);}
    else if(s->stop.fort[0]&&s->metrics.phase==5&&(strstr(name,"Shop")||strstr(name,"Purchase")||strstr(name,"GetMorePasses")||strstr(name,"AcquirePass")||strstr(name,"IAPGui")||strstr(name,"PaymentGui")||strstr(name,"PassStore")||(strstr(name,"Store")&&strstr(name,"Raid"))
        ||(s->approach_at&&(strstr(name,"Modal")||strstr(name,"Dialog"))&&!strstr(name,"ItemFragment")&&!strstr(name,"GlobalError")&&!s->pass_seen&&raid_pass_total(s)==0))){
        if(now>=s->gui_at){
            s->gui_at=now+UINT64_C(800000000);
            int closed=action(s,permit,RK_GUI_CLOSE,top.reference,NULL,0);
            if(s->pass_seen&&!s->ui_joined)s->shop_seen++;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","pass shop/modal close gui=%s result=%d count=%u",name,closed,s->shop_seen);
            if(closed&&closed!=SILPH_MANAGED_NOT_READY)r=closed;
            else if(!s->ui_joined&&!s->won&&raid_pass_total(s)==0){
                __android_log_print(ANDROID_LOG_WARN,"SilphRaid","pass purchase screen and no raid pass in bag; stopping");
                s->metrics.reason=14;phase(s,8,0,0);
                drop(s,&top.reference);return 0;
            }
            else if(s->shop_seen>=4&&!s->ui_joined&&!s->won){
                raid_done_add(s->stop.fort,"pass shop shown with pass in bag");
                raid_clear_gym_target(s,now,"raid already battled (pass shop loop)");
                drop(s,&top.reference);return 0;
            }
        }
    }
    else if(strstr(name,"GlobalError")){
        s->ui_seen=1;op=RK_RAID_GUI;s->ui_retry_at=now+UINT64_C(300000000);
        if((s->metrics.phase==1||s->metrics.phase==3)&&isfinite(s->config.raid_anchor_lat)&&isfinite(s->config.raid_anchor_lon)){
            s->latitude=s->config.raid_anchor_lat;s->longitude=s->config.raid_anchor_lon;
            if(!s->source[0])memcpy(s->source,"raid-anchor",12);
            s->dirty=1;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","global error ui; re-anchor lat=%.6f lon=%.6f",(double)s->latitude,(double)s->longitude);
        }else if(s->stop.fort[0]&&!s->stop.incident[0]&&s->metrics.phase==5){
            bool in_range=false;int range_r=ready_to_open(s,true,&in_range);
            if(!range_r&&!in_range&&(!s->gps_settled_at||now-s->gps_settled_at>UINT64_C(20000000000))){
                s->teleport_ready=0;s->gps_settled_at=0;s->latitude=s->stop.latitude;s->longitude=s->stop.longitude;s->dirty=1;
                phase(s,6,0,now+UINT64_C(15000000000));
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","global error ui; mock re-teleport fort=%s",s->stop.fort);
            }else{
                s->join_failures++;
                if(s->join_failures>=3){
                    __android_log_print(ANDROID_LOG_WARN,"SilphRaid","repeated global error on gym %s; abandoning gym",s->stop.fort);
                    raid_clear_gym_target(s,now,"repeated global error on gym");
                }else{
                    s->metrics.stage=RS_OPEN;
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","global error ui dismissed (fails=%u); stay on join fort=%s",s->join_failures,s->stop.fort);
                }
            }
        }
    }
    else if((!raid_rpc_only(s)||s->join_ui_fallback)&&(istype(s,top.reference,"Niantic.Holoholo.Raid","RaidApproachUIController")||(!s->stop.incident[0]&&(strstr(name,"GymApproach")||strstr(name,"RaidApproach")||strstr(name,"GymDetails")||strstr(name,"RaidGymDetails"))))){
        s->join_ui_fallback=1;s->metrics.stage=RS_UI;s->approach_at=now;s->ui_seen=0;raid_retain_approach_gui(s,top.reference);op=RK_RAID_GUI;s->ui_retry_at=now+UINT64_C(50000000);s->gui_at=now;}
    else if(s->join_ui_fallback&&(istype(s,top.reference,"Niantic.Holoholo.Raid","RaidLobbyGuiController")||istype(s,top.reference,"Niantic.Holoholo.Raid","RaidBattleLobbyGuiController")||istype(s,top.reference,"Niantic.Holoholo.Raid","IncomingRaidGuiController"))){
        s->ui_seen=1;s->join_failures=0;op=RK_RAID_GUI;s->ui_retry_at=now+UINT64_C(150000000);s->gui_at=now;
        raid_mark_ui_joined(s,now,"lobby gui");if(raid_lobby_tick(s,permit,top.reference,now)){drop(s,&top.reference);return 0;}if(!s->lobby_info_at)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid lobby ui open (rpc fallback)");
    }
    else if(s->join_ui_fallback&&(istype(s,top.reference,"Niantic.Holoholo.Raid","RaidApproachUIController")||istype(s,top.reference,"Niantic.Holoholo.Gym","GymGuiController")||(!s->stop.incident[0]&&(strstr(name,"GymApproach")||strstr(name,"RaidApproach"))))){
        if(!raid_rpc_only(s)&&s->raid_rpc_step>=2&&!s->approach_at&&!istype(s,top.reference,"Niantic.Holoholo.Raid","RaidLobbyGuiController")&&!istype(s,top.reference,"Niantic.Holoholo.Raid","RaidBattleLobbyGuiController")&&istype(s,top.reference,"Niantic.Holoholo.UI","MainMenuGuiController")){
            s->raid_rpc_step=0;s->lobby_joined_at=0;s->lobby_pokemon_set=0;s->ui_seen=0;s->raid_details_pending=1;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","headless lobby reset; ui join required gui=%s",name);}
        s->metrics.stage=RS_UI;s->approach_at=now;op=op?op:RK_RAID_GUI;s->ui_retry_at=now+UINT64_C(150000000);s->gui_at=now;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym raid ui open gui=%s",name);}
    else if(s->join_ui_fallback&&istype(s,top.reference,"Niantic.Holoholo.UI","MainMenuGuiController")&&(s->raid_rpc_step>=2||s->approach_at||s->stop.fort[0])){
        raid_skip_details_for_ui(s,now);
        bool approach_cutscene=s->approach_at&&now-s->approach_at<UINT64_C(4000000000)&&!s->lobby_layer_hint&&!s->lobby_joined_at;
        if(!approach_cutscene&&!s->rpc_active&&!silph_rpc_action_pending(s->rpc)){
            int sent=raid_rpc_kickoff(s,permit,now);
            if(sent&&!raid_rpc_soft_wait(sent))r=sent;
        }
        if(!r&&now>=s->gui_at){
            int pr=raid_ensure_place(s);
            if(!pr&&s->place.serial&&raid_gym_still_joinable(s)){
                s->gui_at=now+UINT64_C(500000000);
                r=raid_tap_place(s,permit,now);
                if(!r){s->ui_taps++;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","mainmenu gym tap fort=%s attempt=%u",s->stop.fort,(unsigned)s->ui_taps);}
            }else if(pr!=SILPH_MANAGED_NOT_READY)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","mainmenu gym tap blocked err=%d fort=%s",pr,s->stop.fort);
        }
        s->ui_retry_at=now+UINT64_C(400000000);
    }
    else if(s->join_ui_fallback&&(strstr(name,"RaidApproach")||strstr(name,"GymApproach")||strstr(name,"RaidLobby")||strstr(name,"GymGui"))){
        s->ui_retry_at=now+UINT64_C(100000000);}
    else if(strstr(name,"Welcome")||strstr(name,"FirstTimeUser")||strstr(name,"Referral")||strstr(name,"Onboarding")||strstr(name,"WillowIntro")||strstr(name,"Safety")||strstr(name,"Surroundings")||strstr(name,"Courteous")||strstr(name,"Aware")||strstr(name,"ArPlus")){
        s->ui_seen=1;op=RK_RAID_GUI;s->ui_retry_at=now+UINT64_C(80000000);s->gui_at=now;}
    else if(raid_onboarding_gui(name)){s->ui_seen=1;op=RK_RAID_GUI;s->ui_retry_at=now+UINT64_C(150000000);}
    else if(!raid_rpc_only(s)&&(istype(s,top.reference,"Niantic.Holoholo.Raid","RaidLobbyGuiController")||istype(s,top.reference,"Niantic.Holoholo.Raid","RaidBattleLobbyGuiController")||istype(s,top.reference,"Niantic.Holoholo.Raid","IncomingRaidGuiController"))){
        s->ui_seen=1;s->join_failures=0;if(strstr(name,"IncomingRaid")){op=RK_RAID_GUI;s->gui_at=now;}
        if(s->join_ui_fallback&&s->raid_rpc_step>=2){s->metrics.stage=RS_UI;}
        else s->join_ui_fallback=0;
        raid_mark_ui_joined(s,now,"lobby gui");if(raid_lobby_tick(s,permit,top.reference,now)){drop(s,&top.reference);return 0;}
    }
    else if(s->join_ui_fallback&&s->raid_rpc_step>=2&&(strstr(name,"RaidLobby")||strstr(name,"BattleLobby")||strstr(name,"IncomingRaid"))){
        s->ui_seen=0;op=RK_RAID_GUI;s->gui_at=now;s->metrics.stage=RS_UI;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid lobby ui tap (name=%s)",name);
    }
    else if(!raid_rpc_only(s)&&istype(s,top.reference,"Niantic.Holoholo.Gym","GymGuiController")){
        if(!raid_gym_still_joinable(s)){
            r=raid_exit_non_raid_gym(s,permit,top.reference,name);
            raid_clear_gym_target(s,now,"opened gym without active raid");
        }else{s->join_ui_fallback=1;s->metrics.stage=RS_UI;s->ui_seen=0;op=RK_RAID_GUI;s->ui_retry_at=now+UINT64_C(150000000);s->gui_at=now;}
    }
    else if(s->lost&&istype(s,top.reference,"Niantic.Holoholo.Incident","IncidentCombatResolveGuiController"))op=RK_INCIDENT_CLOSE;
    else if(s->lost&&istype(s,top.reference,"Niantic.Holoholo.Combat.Resolution","CombatResolveGuiController"))op=RK_RESULT_CLOSE;
    else if(s->won&&istype(s,top.reference,"Niantic.Holoholo.Incident","IncidentCombatResolveGuiController")){
        if(!s->reward_gui.serial)r=silph_managed_retain(s->managed,object,false,&s->reward_gui);
        if(!s->reward_read){SilphManagedRef info={0};int p=typed(s,top.reference,"Niantic.Holoholo.Incident","IIncidentCombatResolveInformation",&info);
            uint64_t xp=0,dust=0;if(!p)p=silph_reward_loot(s->read,s->managed,info,"<ItemRewards>k__BackingField",&xp,&dust);if(p)s->metrics.reward_error=(uint32_t)p;else{s->metrics.xp+=xp;s->metrics.stardust+=dust;}
            drop(s,&info);s->reward_read=1;}
        if(!r)r=open_encounter(s,now);
    }else if(istype(s,top.reference,"Niantic.Holoholo.UI","ItemFragmentAssemblyModalGuiController")){
        SilphManagedRef service={0};r=silph_game_read_raid_service(s->game,3,&service);if(!r)r=action(s,permit,RK_GEM,service,NULL,0);drop(s,&service);
    }
    bool approach_gui=(!raid_rpc_only(s)||s->join_ui_fallback)&&(strstr(name,"RaidApproach")||strstr(name,"GymApproach")||strstr(name,"GymGui")||strstr(name,"GymDetails")||strstr(name,"RaidLobby")||strstr(name,"BattleLobby"));
    if(!r&&op&&approach_gui&&now>=s->gui_at&&raid_join_press_allowed(s,permit,name,now)){
        s->last_gui=(uintptr_t)object;
        s->gui_at=now+(strstr(name,"Approach")?UINT64_C(35000000):UINT64_C(120000000));
        r=action(s,permit,op,top.reference,NULL,0);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym raid ui action result=%d gui=%s",r,name);
        if(raid_approach_result(s,permit,top.reference,now,name,&r)){drop(s,&top.reference);return 0;}
        if(!r&&s->join_ui_fallback&&raid_gym_ui_top(s)){
            s->ui_started=now;
            if(!s->raid_rpc_step)s->ui_retry_at=now+UINT64_C(50000000);
        }
        if(s->join_ui_fallback&&(strstr(name,"Approach")||strstr(name,"GymDetails")||strstr(name,"RaidLobby")||strstr(name,"BattleLobby"))){
            raid_skip_details_for_ui(s,now);
            bool lobby_name=strstr(name,"RaidLobby")||strstr(name,"BattleLobby");
            if(lobby_name&&!s->rpc_active&&!silph_rpc_action_pending(s->rpc)&&now>=s->ui_retry_at){
                int sent=raid_rpc_kickoff(s,permit,now);
                if(sent&&!raid_rpc_soft_wait(sent))r=sent;
                else if(!sent&&!raid_rpc_soft_wait(sent))__android_log_print(ANDROID_LOG_INFO,"SilphRaid","approach ui; join rpc armed gui=%s",name);
                else s->ui_retry_at=now+UINT64_C(2000000000);
            }
        }
    }
    if(!r&&!(!strcmp(name,"MainMenuGuiController")||!name[0])){int pumped=raid_pump_approach_gui(s,permit,now);if(pumped)r=pumped;}
    if(!r&&s->join_ui_fallback&&s->approach_at&&s->metrics.stage==RS_UI&&now>=s->gui_at){
        s->gui_at=now+UINT64_C(350000000);
        int layer=raid_pump_approach_layers(s,permit,now);
        if(layer==SILPH_MANAGED_NOT_READY)layer=0;
        if(!r)r=layer;
    }
    if(!r&&op&&now>=s->gui_at&&((uintptr_t)object!=s->last_gui||op==RK_DIALOG||approach_gui||now-s->gui_at>UINT64_C(2000000000))){
        if(!approach_gui){s->last_gui=(uintptr_t)object;s->gui_at=now+UINT64_C(50000000);r=action(s,permit,op,top.reference,NULL,0);}
        if(!r&&s->lost)s->loss_seen=1;
        if(!r&&op==RK_RAID_GUI&&(strstr(name,"PickTeam")||strstr(name,"TeamSelect")||strstr(name,"JoinTeam")||strstr(name,"GymTeam"))){
            s->join_failures=s->join_ui_fallback=0;s->faction_team_attempted=0;s->raid_details_pending=1;s->ui_seen=0;
            s->latitude=s->stop.latitude;s->longitude=s->stop.longitude;s->dirty=1;s->teleport_ready=1;
            s->metrics.stage=RS_OPEN;s->ui_retry_at=now+UINT64_C(50000000);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","faction picked; teleport and retry raid join");
        }
    }
    if(!s->director.serial&&!typed(s,top.reference,"Niantic.Holoholo.VNext","IBattleDirector",&s->director)){
        if((s->config.flags&SILPH_CFG_TRAINER_NAME)&&istype(s,top.reference,"Niantic.Holoholo.Combat.UI","CombatVNextGuiController"))action(s,permit,RK_PLAYER_ALIAS,top.reference,NULL,0);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","battle director resolved request=%llu",(unsigned long long)s->metrics.request);
    }
    if(!r&&(s->metrics.stage==RS_UI||s->lost)&&istype(s,top.reference,"Niantic.Holoholo.Combat.Preparation","CombatPreparationRootGuiController")&&now>=s->gui_at){
        SilphReadValue party={0};r=silph_raid_invoke(s->read,permit,RK_PARTY,top.reference,NULL,0,&party);
        if(!r&&party.reference.serial)r=prepare_team(s,permit,party.reference,now);
        drop(s,&party.reference);
    }
    else if(!r&&(s->metrics.stage==RS_UI||s->lost)&&istype(s,top.reference,"Niantic.Holoholo.Combat.PokemonPartyState","CombatPokemonPartyWidget")&&now>=s->gui_at)r=prepare_team(s,permit,top.reference,now);
    if(!r&&!s->lost&&s->available_shields&&(!s->shield_clicked||(s->shield_wait&&now>=s->shield_at))&&istype(s,top.reference,"Niantic.Holoholo.Combat.InputChallenge","ShieldModalGuiController")){
        r=action(s,permit,RK_SHIELD,top.reference,NULL,0);
        if(r==SILPH_MANAGED_NOT_READY)r=0;
        else if(!r){s->shield_clicked=s->shield_wait=1;s->shield_before=s->available_shields;s->shield_at=now+UINT64_C(1000000000);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","shield requested remaining=%u",s->shield_before);}
    }
    int freed=drop(s,&top.reference);return r?r:freed;
}
static bool raid_layer_join_gui(Raid *s,const SilphManagedRef gui,char *name,size_t name_cap){
    if(!gui.serial||!name||!name_cap)return false;
    if(silph_raid_class(s->read,gui,name,name_cap))return false;
    if(strstr(name,"Layers")&&!(strstr(name,"GymApproach")||strstr(name,"RaidApproach")))return false;
    return strstr(name,"GymApproach")||strstr(name,"RaidApproach")||strstr(name,"GymGui")||strstr(name,"GymDetails")
        ||strstr(name,"RaidLobby")||strstr(name,"BattleLobby")||strstr(name,"IncomingRaid")||strstr(name,"GymInterior")||strstr(name,"GymHub");
}
static int scan_approach_guis(Raid *s,const SilphSchedulePermit *permit,SilphManagedRef set,uint64_t now,unsigned *matched_out){
    if(!set.serial)return 0;
    unsigned matched=0;
    SilphManagedRef iterator={0},current={0};SilphReadValue value={0};
    int r=silph_read_call(s->read,set,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&value);
    if(r==SILPH_MANAGED_CONTRACT)r=silph_read_call(s->read,set,"GetEnumerator",SILPH_READ_REFERENCE,NULL,0,&value);
    iterator=value.reference;
    for(unsigned i=0;!r&&i<128;i++){
        uint64_t more=0;r=scalar(s,iterator,"MoveNext",SILPH_READ_BOOL,&more,true);if(r||!more)break;
        r=callref(s,iterator,"get_Current",&current);
        char name[128]={0};
        if(!r&&raid_layer_join_gui(s,current,name,sizeof(name))){
            matched++;
            bool lobby=strstr(name,"RaidLobby")||strstr(name,"BattleLobby")||strstr(name,"IncomingRaid");
            bool approach=strstr(name,"Approach")||strstr(name,"GymInterior")||strstr(name,"GymHub");
            if(lobby||strstr(name,"RaidApproach")||strstr(name,"UseRaidTicket"))s->raid_screen_seen=1;
            if(lobby){
                s->lobby_layer_hint=1;s->ui_seen=1;if(!s->won)s->metrics.stage=RS_UI;raid_mark_ui_joined(s,now,"lobby layer");s->lobby_seen=1;s->lobby_left_at=0;if(raid_lobby_tick(s,permit,current,now)){drop(s,&current);break;}
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","layer lobby open gui=%s",name);
            }else if(approach){
                s->lobby_layer_hint=1;
                raid_retain_approach_gui(s,current);
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","layer approach open gui=%s",name);
            }else raid_retain_approach_gui(s,current);
            if(strstr(name,"Approach"))(void)action(s,permit,RK_FAST_GUI,current,NULL,0);
            if(!lobby&&!raid_join_press_allowed(s,permit,name,now)){drop(s,&current);break;}
            int pressed=action(s,permit,RK_RAID_GUI,current,NULL,0);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","layer approach gui=%s result=%d",name,pressed);
            if(raid_approach_result(s,permit,current,now,name,&pressed)){drop(s,&current);break;}
            if(!pressed){
                s->ui_retry_at=now+UINT64_C(200000000);
                if(lobby&&!s->rpc_active&&!silph_rpc_action_pending(s->rpc)){
                    int sent=raid_rpc_kickoff(s,permit,now);
                    if(sent&&!raid_rpc_soft_wait(sent))r=sent;
                }
                drop(s,&current);break;
            }
        }
        drop(s,&current);
    }
    SilphManagedRef *refs[]={&current,&iterator};for(unsigned j=0;j<2;j++){int d=drop(s,refs[j]);if(!r)r=d;}
    if(matched_out)*matched_out+=matched;
    return r;
}
static void raid_log_gui_stack_sample(Raid *s,SilphManagedRef set,uint64_t now){
    static uint64_t last_log;
    if(!s||!set.serial||!s->approach_at||now<last_log+UINT64_C(10000000000))return;
    SilphManagedRef iterator={0},current={0};SilphReadValue value={0};unsigned logged=0;
    int r=silph_read_call(s->read,set,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&value);
    if(r==SILPH_MANAGED_CONTRACT)r=silph_read_call(s->read,set,"GetEnumerator",SILPH_READ_REFERENCE,NULL,0,&value);
    iterator=value.reference;
    for(unsigned i=0;!r&&logged<6&&i<64;i++){
        uint64_t more=0;r=scalar(s,iterator,"MoveNext",SILPH_READ_BOOL,&more,true);if(r||!more)break;
        r=callref(s,iterator,"get_Current",&current);
        char name[128]={0};
        if(!r&&silph_raid_class(s->read,current,name,sizeof(name))==0&&name[0]){
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gui stack sample gui=%s",name);
            logged++;
        }
        drop(s,&current);
    }
    SilphManagedRef *refs[]={&current,&iterator};for(unsigned j=0;j<2;j++)drop(s,refs[j]);
    if(logged){last_log=now;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","approach pump no gym gui (stack+layers)");}
}
static int raid_pump_approach_layers(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    unsigned matched=0;int r=0;
    SilphManagedRef stack={0};
    if(!fieldref(s,s->gui,"dddt",&stack)&&stack.serial){
        r=scan_approach_guis(s,permit,stack,now,&matched);
        if(matched){drop(s,&stack);return r==SILPH_MANAGED_NOT_READY?0:r;}
    }
    if(!matched)raid_log_gui_stack_sample(s,stack,now);
    drop(s,&stack);
    const uint32_t layers[]={300,200,320,100,0,350,400,500,600,700,800,900,1000,1100};
    for(unsigned i=0;!r&&i<14;i++){
        SilphReadArgument argument={.kind=SILPH_READ_ENUM32,.value.bits=layers[i]};SilphReadValue value={0};
        r=silph_raid_invoke(s->read,permit,RK_LAYER,s->gui,&argument,1,&value);
        if(!r)r=scan_approach_guis(s,permit,value.reference,now,&matched);
        int d=drop(s,&value.reference);if(!r)r=d;
        if(matched)break;
    }
    return r==SILPH_MANAGED_NOT_READY?0:r;
}
static int scan_raid_ticket(Raid *s,const SilphSchedulePermit *permit,SilphManagedRef set){
    if(!set.serial)return 0;
    SilphManagedRef iterator={0},current={0};
    SilphReadValue value={0};
    int r=silph_read_call(s->read,set,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&value);
    if(r==SILPH_MANAGED_CONTRACT)r=silph_read_call(s->read,set,"GetEnumerator",SILPH_READ_REFERENCE,NULL,0,&value);
    iterator=value.reference;
    for(unsigned i=0;!r&&i<64;i++){
        uint64_t more=0;
        r=scalar(s,iterator,"MoveNext",SILPH_READ_BOOL,&more,true);
        if(r||!more)break;
        r=callref(s,iterator,"get_Current",&current);
        if(!r&&(istype(s,current,"Niantic.Holoholo.Raid","ReceivedRaidTicketGuiController")||istype(s,current,"Niantic.Holoholo.Raid","TooltipToastGuiController"))){
            int dismissed=action(s,permit,RK_RAID_GUI,current,NULL,0);
            char name[128]={0};
            if(!silph_raid_class(s->read,current,name,sizeof(name)))__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid ticket overlay dismissed gui=%s result=%d",name,dismissed);
            else __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid ticket overlay dismissed result=%d",dismissed);
            if(!dismissed)r=0;
        }
        drop(s,&current);
    }
    SilphManagedRef *refs[]={&current,&iterator};
    for(unsigned i=0;i<2;i++){int d=drop(s,refs[i]);if(!r)r=d;}
    return r;
}
static int dismiss_raid_ticket(Raid *s,const SilphSchedulePermit *permit){
    const uint32_t layers[]={1100,1000,900,800,700,600,500,400,350,320,300,200,100,0};
    int r=0;
    for(unsigned i=0;!r&&i<14;i++){
        SilphReadArgument argument={.kind=SILPH_READ_ENUM32,.value.bits=layers[i]};
        SilphReadValue value={0};
        r=silph_raid_invoke(s->read,permit,RK_LAYER,s->gui,&argument,1,&value);
        if(!r)r=scan_raid_ticket(s,permit,value.reference);
        int d=drop(s,&value.reference);
        if(!r)r=d;
    }
    if(!r){
        SilphManagedRef service={0};
        r=silph_game_read_raid_service(s->game,3,&service);
        if(!r)r=action(s,permit,RK_GEM,service,NULL,0);
        drop(s,&service);
    }
    return r;
}
static int scan_director(Raid *s,const SilphSchedulePermit *permit,SilphManagedRef set){
    if(!set.serial)return 0;SilphManagedRef iterator={0},current={0};SilphReadValue value={0};
    int r=silph_read_call(s->read,set,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&value);
    if(r==SILPH_MANAGED_CONTRACT)r=silph_read_call(s->read,set,"GetEnumerator",SILPH_READ_REFERENCE,NULL,0,&value);iterator=value.reference;
    for(unsigned i=0;!r&&i<128;i++){
        uint64_t more=0;r=scalar(s,iterator,"MoveNext",SILPH_READ_BOOL,&more,true);if(r||!more)break;
        r=callref(s,iterator,"get_Current",&current);
        if(!r&&istype(s,current,"Niantic.Holoholo.Combat.UI","CombatVNextGuiController")){if(s->config.flags&SILPH_CFG_TRAINER_NAME){int alias=action(s,permit,RK_PLAYER_ALIAS,current,NULL,0);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","battle player label result=%d",alias);}r=typed(s,current,"Niantic.Holoholo.VNext","IBattleDirector",&s->director);}
        drop(s,&current);if(s->director.serial)break;
    }
    SilphManagedRef *refs[]={&current,&iterator};for(unsigned i=0;i<2;i++){int d=drop(s,refs[i]);if(!r)r=d;}return r;
}
static int find_director(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(s->director.serial||now<s->director_at)return 0;s->director_at=now+UINT64_C(1000000000);
    SilphManagedRef set={0};int r=fieldref(s,s->gui,"dddt",&set);if(!r)r=scan_director(s,permit,set);drop(s,&set);
    const uint32_t layers[]={300,200,320,100,0,350,400,500,600,700,800,900,1000,1100};
    for(unsigned i=0;!r&&!s->director.serial&&i<14;i++){
        SilphReadArgument argument={.kind=SILPH_READ_ENUM32,.value.bits=layers[i]};SilphReadValue value={0};
        r=silph_raid_invoke(s->read,permit,RK_LAYER,s->gui,&argument,1,&value);if(!r)r=scan_director(s,permit,value.reference);drop(s,&value.reference);
    }
    if(!r&&s->director.serial)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","active battle director resolved request=%llu",(unsigned long long)s->metrics.request);return r;
}
static int health_from_state(Raid *s,SilphManagedRef state,RaidHealth *health){SilphManagedRef pokemon={0};uint64_t id=0,hp=0,max=0;int r=callref(s,state,"get_Pokemon",&pokemon);
    if(!r)r=scalar(s,pokemon,"Id",SILPH_READ_U64,&id,false);if(!r)r=scalar(s,state,"CurrentHealth",SILPH_READ_I32,&hp,false);if(!r)r=scalar(s,state,"MaxHealth",SILPH_READ_I32,&max,false);
    if(!r&&(hp>max||!max||max>100000||!id))r=SILPH_MANAGED_CONTRACT;
    if(!r)*health=(RaidHealth){.id=id,.hp=(int32_t)hp,.max_hp=(int32_t)max,.known=true};drop(s,&pokemon);return r;
}
static int battle_view(Raid *s,RaidBattleView *out,uint32_t *status){
    *out=(RaidBattleView){.bound=true,.alive=true,.merged=true,.started=true,.generation=s->metrics.request,.charge_slot=2};
    SilphManagedRef state={0},actor={0},sync={0},active={0},roster={0},values={0},iterator={0},current={0},pokemon={0},moves={0};uint64_t n=0;
    int r=callref(s,s->director,"get_CurrentState",&state);if(!r&&!state.serial)r=SILPH_MANAGED_NOT_READY;
    if(!r)r=scalar(s,state,"BattleStatus",SILPH_READ_ENUM32,&n,false);*status=(uint32_t)n;
    if(!r&&*status!=1){drop(s,&state);return 0;}
    if(!r)r=callref(s,state,"get_LocalActor",&actor);if(!r)r=callref(s,state,"get_CurrentTurnSyncInfo",&sync);
    if(!r)r=scalar(s,sync,"get_SyncedTurnValid",SILPH_READ_BOOL,&n,true);if(!r&&!n)r=SILPH_MANAGED_NOT_READY;
    if(!r)r=scalar(s,sync,"get_SyncedTurn",SILPH_READ_I64,&out->turn,true);if(!r&&out->turn>INT64_MAX)r=SILPH_MANAGED_NOT_READY;out->serial=out->turn;
    if(!r)r=scalar(s,state,"get_UIMode",SILPH_READ_ENUM32,&n,true);out->ui_mode=(uint32_t)n;
    if(!r)r=scalar(s,s->director,"get_InputBlocked",SILPH_READ_BOOL,&n,true);out->input_blocked=n&&out->ui_mode!=5&&out->ui_mode!=6;
    if(!r)r=callref(s,actor,"get_ActivePokemonState",&active);
    if(!r&&!active.serial)r=SILPH_MANAGED_NOT_READY;
    if(!r){RaidHealth h={0};r=health_from_state(s,active,&h);out->active=h.id;}
    if(!r)r=scalar(s,active,"CurrentEnergy",SILPH_READ_I32,&n,false);out->energy=(int32_t)n;
    if(!r)r=fieldref(s,actor,"Roster",&roster);if(!r)r=callref(s,roster,"get_Values",&values);
    SilphReadValue v={0};if(!r)r=silph_read_call(s->read,values,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&v);iterator=v.reference;
    unsigned count=0;
    for(;!r&&count<3;count++){
        r=scalar(s,iterator,"MoveNext",SILPH_READ_BOOL,&n,true);if(r||!n)break;r=callref(s,iterator,"get_Current",&current);
        if(!r)r=health_from_state(s,current,&out->health[count]);out->team[count]=out->health[count].id;drop(s,&current);
    }
    if(!r&&count!=3)r=SILPH_MANAGED_NOT_READY;
    if(!r&&s->team[0]){for(unsigned i=0;i<3;i++){bool found=false;for(unsigned j=0;j<3;j++)if(s->team[i]==out->team[j])found=true;if(!found)r=SILPH_MANAGED_STALE;}}
    if(!r&&!s->team[0])memcpy(s->team,out->team,sizeof(s->team));
    if(!r)r=scalar(s,actor,"get_RemainingCombatShields",SILPH_READ_I32,&n,true);out->shields=(uint32_t)n;
    SilphReadArgument index={.kind=SILPH_READ_I32,.value.bits=3};v=(SilphReadValue){0};
    if(!r)r=callref(s,active,"get_Pokemon",&pokemon);if(!r)r=scalar(s,pokemon,"PokedexId",SILPH_READ_ENUM32,&n,false);out->species=(uint32_t)n;if(!r)r=fieldref(s,pokemon,"Moves",&moves);
    for(unsigned i=0;!r&&i<3;i++){
        index.value.bits=i+1;v=(SilphReadValue){0};int move=silph_read_call(s->read,moves,"get_Item",SILPH_READ_REFERENCE,&index,1,&v);
        if(move){if(i==2)continue;r=move;break;}
        out->moves[i].slot=(int32_t)i+1;
        r=scalar(s,v.reference,"Move",SILPH_READ_ENUM32,&n,false);out->moves[i].move=(int32_t)n;
        if(!r)r=scalar(s,v.reference,"EnergyDelta",SILPH_READ_I32,&n,false);out->moves[i].energy_delta=(int32_t)n;
        if(!r)r=scalar(s,v.reference,"DurationTurns",SILPH_READ_I32,&n,false);out->moves[i].duration=(int32_t)n;drop(s,&v.reference);
    }
    SilphManagedRef *refs[]={&current,&moves,&pokemon,&iterator,&values,&roster,&active,&sync,&actor,&state};
    for(unsigned i=0;i<10;i++){int d=drop(s,refs[i]);if(!r)r=d;}return r;
}
static int poll_attack(Raid *s,const RaidBattleView *view,uint64_t now,bool *ready){
    *ready=false;
    if(s->attack.serial){uint64_t complete=0,error=0;int r=scalar(s,s->attack,"completeCalled",SILPH_READ_BOOL,&complete,false);if(!r)r=scalar(s,s->attack,"errorCalled",SILPH_READ_BOOL,&error,false);
        if(r)return r;
        if(!complete&&!error){
            if(!raid_action_observed(&s->pending_action,view,now)){
                if(now>s->attack_until){
                    __android_log_print(ANDROID_LOG_ERROR,"SilphRaid","battle action timed out type=%u ui=%u/%u turn=%llu/%llu activeChanged=%u blocked=%u request=%llu",s->attack_type,s->pending_action.ui_mode,view->ui_mode,(unsigned long long)s->pending_action.turn,(unsigned long long)view->turn,s->pending_action.active!=view->active,view->input_blocked,(unsigned long long)s->metrics.request);
                    s->metrics.error=113;phase(s,8,0,0);
                }return 0;
            }
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","pending action retired after battle progress type=%u ui=%u/%u turn=%llu/%llu",s->attack_type,s->pending_action.ui_mode,view->ui_mode,(unsigned long long)s->pending_action.turn,(unsigned long long)view->turn);
        }else{
            uint64_t response=0;if(!error)r=scalar(s,s->attack,"completedValue",SILPH_READ_ENUM32,&response,false);if(r)return r;
            if(!error&&response==1)s->accepted_actions++;else if(response!=6){s->rejected_actions++;
                if((s->attack_type==2||s->attack_type==3)&&view->ui_mode==0)s->charge_retry_at=now+UINT64_C(1000000000);
                if(now>=s->action_log_at||response!=s->last_action_result){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","action settled type=%u ui=%u result=%llu error=%llu",s->attack_type,view->ui_mode,(unsigned long long)response,(unsigned long long)error);s->action_log_at=now+UINT64_C(5000000000);s->last_action_result=(uint32_t)response;}
            }
        }
        r=drop(s,&s->attack);if(r)return r;memset(&s->pending_action,0,sizeof(s->pending_action));
        if(s->gate.pending&&!raid_battle_settled(&s->gate,view))return 0;
    }else if(s->gate.pending&&!raid_battle_settled(&s->gate,view))return 0;
    *ready=true;return 0;
}
static int battle(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    RaidBattleView view;uint32_t state=0;int r=battle_view(s,&view,&state);if(r==SILPH_MANAGED_NOT_READY)return 0;if(r)return r;
    if((state==2||state==7)&&!s->won&&!s->battle_started){
        if(now>=s->action_log_at){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","battle state=%u before battle start; ignoring stale result",state);s->action_log_at=now+UINT64_C(5000000000);}
        return 0;}
    if(state==2||state==7){if(!s->won){s->won=1;raid_mark_won(s,"battle won");s->metrics.raids++;s->metrics.stage=RS_ENCOUNTER;drop(s,&s->attack);s->gate.pending=false;s->next=now+UINT64_C(300000000);s->post_battle_at=now;}
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","battle completed status=%u ms=%llu accepted=%llu rejected=%llu",state,(unsigned long long)((now-s->battle_started)/1000000),(unsigned long long)s->accepted_actions,(unsigned long long)s->rejected_actions);
        return 0;}
    if(state>=3&&state!=7){s->metrics.result=state;s->lost=1;s->metrics.stage=RS_LOSS;s->loss_started=now;s->loss_until=now+(state==5?UINT64_C(240000000000):UINT64_C(45000000000));s->loss_ok_at=0;if(s->deadline&&s->deadline<now+UINT64_C(300000000000))s->deadline=now+UINT64_C(300000000000);drop(s,&s->attack);s->gate.pending=false;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","battle ended status=%u; checking screens for win or loss",state);return 0;}
    if(state!=1)return 0;
    if(!raid_battle_observe(&s->battle_clock,&view))return 0;
    if(!s->battle_started)s->battle_started=now;
    if(s->last_active!=view.active){s->last_active=view.active;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","active species=%u fast=%d rawTurns=%d energyDelta=%d charge1=%d charge2=%d energy=%d",view.species,view.moves[0].move,view.moves[0].duration,view.moves[0].energy_delta,view.moves[1].move,view.moves[2].move,view.energy);}
    s->available_shields=view.shields;
    if(s->shield_wait&&view.shields<s->shield_before){s->shield_wait=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","shield confirmed remaining=%u",view.shields);}
    if(view.ui_mode!=6){s->shield_clicked=0;s->shield_wait=0;}
    view.shields=0;
    bool ready=false;r=poll_attack(s,&view,now,&ready);if(r||!ready)return r;
    view.charge_slot=s->charge_slot;
    if(now<s->charge_retry_at&&view.ui_mode==0)for(unsigned i=0;i<3;i++)if(view.moves[i].slot==2||view.moves[i].slot==3)view.moves[i].disabled=true;
    if(view.ui_mode==5){view.progress_ready=now>=s->charge_at;view.progress=1.0f;}else s->charge_at=0;
    RaidDecision decision={0};uint32_t kind=raid_battle_decide(&view,&s->gate,&decision);
    if(kind==ROCKET_INVALID){
        if(now>=s->action_log_at){s->action_log_at=now+UINT64_C(3000000000);__android_log_print(ANDROID_LOG_WARN,"SilphRaid","battle decision invalid species=%u ui=%u fast=%d turns=%d energy=%d; waiting",view.species,(unsigned)view.ui_mode,view.moves[0].move,view.moves[0].duration,view.energy);}
        return 0;
    }
    if(kind!=ROCKET_WAIT){r=silph_raid_attack(s->read,permit,s->director,decision.attack_type?decision.attack_type:7,decision.score,decision.outgoing,decision.incoming,&s->attack);
        if(!r){s->attack_type=decision.attack_type?decision.attack_type:7;if(kind==ROCKET_CHARGED&&view.ui_mode==0)s->charge_slot=decision.attack_type;s->attack_until=now+UINT64_C(18000000000);s->charge_at=now+UINT64_C(1000000000);
            s->pending_action=(RaidActionWatch){.generation=view.generation,.turn=view.turn,.active=view.active,.incoming=decision.incoming,.until=s->attack_until,.type=s->attack_type,.ui_mode=view.ui_mode,.energy=view.energy};}}
    return r;
}
static bool raid_post_battle_gui(const char *name){
    if(!name||!name[0])return false;
    return strstr(name,"RaidResolve")||strstr(name,"Achievement")||strstr(name,"Summary")||strstr(name,"Bonus")||strstr(name,"RaidReward")
        ||strstr(name,"TrainerAchievement")||strstr(name,"RaidResults")||strstr(name,"PostRaid")||strstr(name,"RewardScreen");
}
static int raid_load_gym_encounter_proto(Raid *s){
    int r=0;
    if(!s->encounter_state.serial)r=silph_game_read_raid_service(s->game,2,&s->encounter_state);
    if(r&&r!=SILPH_MANAGED_NOT_READY)return r;
    if(s->encounter.serial)drop(s,&s->encounter);
    r=typed(s,s->encounter_state,"Holoholo.Rpc","EncounterOutProto",&s->encounter);
    if(r||!s->encounter.serial){drop(s,&s->encounter);r=typed(s,s->encounter_state,"Holoholo.Rpc","PokemonFieldEncounterOutProto",&s->encounter);}
    s->raid_encounter=0;
    if(s->encounter_state.serial){
        uint64_t raid_flag=0;(void)scalar(s,s->encounter_state,"get_IsRaidOrIncidentEncounter",SILPH_READ_BOOL,&raid_flag,true);
        SilphManagedRef mp={0};
        if(raid_flag&&!callref(s,s->encounter_state,"get_MapPokemon",&mp)&&mp.serial){drop(s,&s->map_pokemon);s->map_pokemon=mp;s->raid_encounter=2;return 0;}
        drop(s,&mp);
    }
    if(r||!s->encounter.serial){drop(s,&s->encounter);r=typed(s,s->encounter_state,"Holoholo.Rpc","RaidEncounterProto",&s->encounter);if(!r&&s->encounter.serial)s->raid_encounter=1;}
    if(r||!s->encounter.serial){
        SilphManagedRef tmp={0};
        if(!callref(s,s->encounter_state,"get_Encounter",&tmp)&&tmp.serial)s->encounter=tmp;
        else if(!fieldref(s,s->encounter_state,"encounter",&tmp)&&tmp.serial)s->encounter=tmp;
        else if(!fieldref(s,s->encounter_state,"activeEncounter",&tmp)&&tmp.serial)s->encounter=tmp;
        else drop(s,&tmp);
        r=0;
    }
    return s->encounter.serial?0:SILPH_MANAGED_NOT_READY;
}
static int raid_fill_raid_encounter(Raid *s,uint64_t now){
    uint64_t id=0,n=0;
    int r=scalar(s,s->encounter,"EncounterId",SILPH_READ_U64,&id,false);
    if(!r&&(!id||id==s->metrics.encounter)){drop(s,&s->encounter);return 0;}
    if(!r)r=fieldref(s,s->encounter,"Pokemon",&s->pokemon);
    const char *names[]={"individualAttack_","individualDefense_","individualStamina_"};
    for(unsigned i=0;!r&&i<3;i++){r=scalar(s,s->pokemon,names[i],SILPH_READ_I32,&n,false);s->iv[i]=(uint32_t)n;if(!r&&n>15)r=SILPH_MANAGED_CONTRACT;}
    if(!r)r=scalar(s,s->pokemon,"pokemonId_",SILPH_READ_I32,&n,false);s->species=(uint32_t)n;
    char spawn[161]={0};
    if(!r&&text(s,s->encounter,"SpawnpointId",spawn,sizeof(spawn),false))spawn[0]=0;
    memset(s->spawn,0,sizeof(s->spawn));
    if(spawn[0]&&strlen(spawn)<=16)memcpy(s->spawn,spawn,strlen(spawn));
    if(!r){uint64_t balls=0;if(!scalar(s,s->encounter,"ThrowsRemaining",SILPH_READ_I32,&balls,false)&&balls)s->balls=(uint32_t)balls;else{uint32_t c=0;if(!silph_game_read_item(s->game,5,&c))s->balls=c;}}
    if(!r){s->headless=1;s->metrics.encounter=id;s->metrics.encounters++;s->metrics.stage=RS_CAPTURE;s->next=now+UINT64_C(500000000);s->claim_mode=0;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid boss encounter ready encounter=%llu species=%u iv=%u/%u/%u balls=%u spawn=%s",(unsigned long long)id,(unsigned)s->species,s->iv[0],s->iv[1],s->iv[2],s->balls,spawn[0]?spawn:"(gym id)");
    }else if(now>=s->enc_log_at){s->enc_log_at=now+UINT64_C(3000000000);__android_log_print(ANDROID_LOG_WARN,"SilphRaid","raid encounter read failed r=%d",r);}
    return r==SILPH_MANAGED_CONTRACT?0:r;
}
static int raid_fill_shadow_encounter(Raid *s,uint64_t now){
    uint64_t id=0,n=0;
    int r=scalar(s,s->encounter,"Status",SILPH_READ_ENUM32,&n,false);
    if(!r&&n&&n!=1){drop(s,&s->encounter);return 0;}
    if(!r)r=scalar(s,s->encounter,"EncounterId",SILPH_READ_U64,&id,false);
    if(!r&&(!id||id==s->metrics.encounter||id==s->reward_before)){drop(s,&s->encounter);return 0;}
    if(!r)r=fieldref(s,s->encounter,"EncounterPokemon",&s->pokemon);
    const char *names[]={"individualAttack_","individualDefense_","individualStamina_"};
    for(unsigned i=0;!r&&i<3;i++){r=scalar(s,s->pokemon,names[i],SILPH_READ_I32,&n,false);s->iv[i]=(uint32_t)n;if(!r&&n>15)r=SILPH_MANAGED_CONTRACT;}
    if(!r)r=scalar(s,s->pokemon,"pokemonId_",SILPH_READ_I32,&n,false);s->species=(uint32_t)n;
    SilphManagedRef display={0};if(!r)r=fieldref(s,s->pokemon,"pokemonDisplay_",&display);if(!r)r=scalar(s,display,"Shiny",SILPH_READ_BOOL,&n,false);s->shiny=(uint32_t)n;drop(s,&display);
    if(!r)r=text(s,s->encounter,"SpawnPointGuid",s->spawn,sizeof(s->spawn),false);
    if(!r)r=scalar(s,s->encounter,"ThrowsRemaining",SILPH_READ_I32,&n,false);s->balls=(uint32_t)n;
    if(!r){s->headless=1;s->metrics.encounter=id;s->metrics.encounters++;s->metrics.stage=RS_CAPTURE;s->claim_mode=0;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym bonus encounter ready encounter=%llu species=%u iv=%u/%u/%u shiny=%u balls=%u",(unsigned long long)id,(unsigned)s->species,s->iv[0],s->iv[1],s->iv[2],s->shiny,s->balls);
        if(raid_catch_accept(s->config.filters,s->iv[0],s->iv[1],s->iv[2],s->shiny)){s->returning=1;phase(s,6,RS_CAPTURE,now+UINT64_C(15000000000));}
    }
    return r;
}
static int open_gym_encounter(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    (void)permit;
    if(s->headless||!s->won||s->stop.incident[0]||!raid_gym_fast(s))return 0;
    int r=raid_load_gym_encounter_proto(s);
    if(now>=s->enc_log_at){s->enc_log_at=now+UINT64_C(3000000000);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","encounter lookup r=%d state=%u proto=%u raid=%u",r,s->encounter_state.serial?1:0,s->encounter.serial?1:0,s->raid_encounter);
        if(s->encounter_state.serial){static int dumped;if(!dumped){dumped=1;silph_dump_methods(s->read,s->encounter_state,"EncounterState");}}}
    if(r==SILPH_MANAGED_NOT_READY)return 0;
    if(r)return r;
    if(s->raid_encounter==2){
        uint64_t id=0,balls=0;int er=scalar(s,s->map_pokemon,"get_EncounterId",SILPH_READ_U64,&id,true);
        if(er||!id){if(now>=s->enc_log_at){s->enc_log_at=now+UINT64_C(3000000000);__android_log_print(ANDROID_LOG_WARN,"SilphRaid","raid map pokemon encounter id unreadable r=%d",er);}return 0;}
        if(scalar(s,s->encounter_state,"get_RaidBallCount",SILPH_READ_I32,&balls,true)||!balls){uint32_t c=0;if(!silph_game_read_item(s->game,5,&c))balls=c;}
        {static int md;if(!md){md=1;silph_dump_methods(s->read,s->map_pokemon,"MapPokemon");}
         char sp[96]={0};int spr=text(s,s->map_pokemon,"get_SpawnPointId",sp,sizeof(sp),true);
         memset(s->raid_spawn,0,sizeof(s->raid_spawn));if(!spr&&sp[0])memcpy(s->raid_spawn,sp,strnlen(sp,sizeof(s->raid_spawn)-1));
         __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid map pokemon spawn_point=\"%s\"(r%d) fort=%s",sp,spr,s->stop.fort);}
        s->balls=(uint32_t)balls;s->headless=1;s->metrics.encounter=id;s->metrics.encounters++;s->metrics.stage=RS_CAPTURE;s->next=now+UINT64_C(1500000000);
        s->claim_mode=0;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid boss encounter ready (map pokemon) encounter=%llu balls=%u",(unsigned long long)id,s->balls);
        return 0;
    }
    if(s->raid_encounter)return raid_fill_raid_encounter(s,now);
    return raid_fill_shadow_encounter(s,now);
}
static int raid_post_battle_advance(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(s->stop.incident[0])return 0;
    SilphReadValue top={0};char name[128]={0};
    int r=silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&top);
    if(r||!top.reference.serial){drop(s,&top.reference);return r;}
    if(!silph_raid_class(s->read,top.reference,name,sizeof(name)))memcpy(s->top_name,name,strlen(name)+1);
    if(s->metrics.stage==RS_CAPTURE&&(s->raid_encounter==2||s->catch_shown_at)){drop(s,&top.reference);return 0;}
    bool post=raid_post_battle_gui(name)||istype(s,top.reference,"Niantic.Holoholo.UI.Transitions","ScreenFadeGuiController");
    bool encounter=istype(s,top.reference,"Niantic.Holoholo.Encounter","EncounterGuiController")||!strcmp(name,"EncounterGuiController");
    bool unknown=s->won&&!post&&!encounter&&name[0]&&strcmp(name,"MainMenuGuiController")&&!strstr(name,"Combat")&&!strstr(name,"Encounter")&&!strstr(name,"Inventory")&&!strstr(name,"PokemonInfo")&&!strstr(name,"Bag")&&!strstr(name,"Profile")&&!strstr(name,"Shop");
    if(unknown){
        static char seen[128];if(strcmp(seen,name)){snprintf(seen,sizeof(seen),"%s",name);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","post-battle unknown gui=%s; trying generic advance",name);}
        post=true;
    }
    if(!post&&!encounter){drop(s,&top.reference);return 0;}
    if(!s->won&&post&&s->metrics.stage<RS_BATTLE&&!s->claim_mode){drop(s,&top.reference);return 0;}
    if(!s->won&&post){
        if(!s->won)raid_mark_won(s,"battle won");
        s->won=1;s->metrics.stage=RS_ENCOUNTER;if(!s->post_battle_at)s->post_battle_at=now;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","post-battle recovery armed gui=%s",name);
    }
    if(s->won&&!raid_gym_fast(s)&&!post&&!encounter){drop(s,&top.reference);return 0;}
    if(s->won&&s->metrics.stage!=RS_ENCOUNTER&&s->metrics.stage!=RS_BATTLE&&!post){drop(s,&top.reference);return 0;}
    if(s->post_battle_at&&now>s->post_battle_at+UINT64_C(180000000000)&&!s->headless){
        __android_log_print(ANDROID_LOG_ERROR,"SilphRaid","post-battle flow timed out gui=%s; requesting recovery",s->top_name);
        SilphManagedRef service={0};int r=silph_game_read_raid_service(s->game,1,&service);
        if(!r)r=action(s,permit,RK_GEM,service,NULL,0);
        drop(s,&service);
        s->metrics.error=122;phase(s,8,0,0);return r;
    }
    if(encounter){
        s->metrics.stage=RS_ENCOUNTER;
        r=open_gym_encounter(s,permit,now);
        int freed=drop(s,&top.reference);return r?r:freed;
    }
    if(istype(s,top.reference,"Niantic.Holoholo.UI.Transitions","ScreenFadeGuiController")&&now>=s->gui_at){
        s->gui_at=now+UINT64_C(100000000);
        r=action(s,permit,RK_RAID_GUI,top.reference,NULL,0);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","post-battle skip transition gui=%s result=%d",name,r);
    }else if(s->won&&istype(s,top.reference,"Niantic.Holoholo.Combat.Resolution","CombatResolveGuiController")&&now>=s->gui_at){
        s->gui_at=now+UINT64_C(150000000);
        r=action(s,permit,RK_RESULT_CLOSE,top.reference,NULL,0);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","post-battle combat resolve skip result=%d",r);
    }else if((raid_post_battle_gui(name)||unknown)&&now>=s->gui_at){
        if(s->fast_gui!=(uintptr_t)top.reference.serial){int tuned=action(s,permit,RK_FAST_GUI,top.reference,NULL,0);s->fast_gui=(uintptr_t)top.reference.serial;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","post-battle shorten gui=%s result=%d",name,tuned);}
        s->gui_at=now+UINT64_C(1000000000);
        r=action(s,permit,RK_RAID_GUI,top.reference,NULL,0);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","post-battle advance gui=%s result=%d",name,r);
    }
    int freed=drop(s,&top.reference);return r?r:freed;
}
static int open_encounter(Raid *s,uint64_t now){
    if(s->headless||!s->won||!s->reward_gui.serial)return 0;
    char incident[161]={0};int current=text(s,s->incident,"get_CurrentIncidentId",incident,sizeof(incident),true);
    if(current||strcmp(incident,s->stop.incident))return 0;
    int r=typed(s,s->incident,"Holoholo.Rpc","InvasionEncounterOutProto",&s->encounter);if(r||!s->encounter.serial)return r;
    return raid_fill_shadow_encounter(s,now);
}
static int rpc_send(Raid *s,const SilphSchedulePermit *permit,uint32_t kind,const SilphRequestInput *input,uint64_t now){
    if(s->rpc_active||silph_rpc_action_pending(s->rpc)){
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc send blocked op=%u busy=%u pending=%u",(unsigned)kind,(unsigned)s->rpc_active,(unsigned)silph_rpc_action_pending(s->rpc));
        return SILPH_MANAGED_NOT_READY;
    }
    int r=silph_rpc_action_send(s->rpc,permit,kind,input,++s->sequence,now);
    if(r==SILPH_MANAGED_STATE){silph_rpc_action_reset(s->rpc);s->rpc_active=0;s->rpc_kind=0;return SILPH_MANAGED_NOT_READY;}
    if(!r){s->rpc_active=1;s->rpc_kind=kind;s->rpc_sent_at=now;s->join_kickoff_pending=0;s->join_send_soft_fails=0;}
    else __android_log_print(ANDROID_LOG_WARN,"SilphRaid","raid rpc send failed op=%u err=%d",(unsigned)kind,r);
    return r;
}
static int raid_load_team(Raid *s){
    if(s->team[0])return 0;unsigned n=0;uint64_t ids[3]={0};
    int r=silph_game_read_raid_team(s->game,ids,3,&n);if(r==SILPH_MANAGED_NOT_READY)return 0;if(r)return r;
    memcpy(s->team,ids,sizeof(ids));return 0;
}
static bool raid_near_gym(Raid *s,double plat,double plon){
    if(!s||!isfinite(plat)||!isfinite(plon)||!isfinite(s->stop.latitude)||!isfinite(s->stop.longitude))return false;
    double dy=(plat-s->stop.latitude)*111195,dx=remainder(plon-s->stop.longitude,360.0)*111195*cos(s->stop.latitude*0.017453292519943295);
    double limit=s->stop.incident[0]?120.0:75.0;
    return hypot(dx,dy)<=limit;
}
static bool raid_rpc_soft_wait(int code){
    return code==SILPH_MANAGED_NOT_READY||code==SILPH_MANAGED_STATE;
}
static void raid_open_join(Raid *s,uint64_t now){
    s->ui_seen=0;s->fast_gui=0;s->ui_started=now;s->raid_rpc_step=0;
    s->raid_details_ok=0;
    s->raid_details_pending=0;
    s->ui_retry_at=now;
    s->teleport_ready=1;
    s->join_ui_fallback=1;
    s->join_kickoff_pending=0;
    s->boss_checked=0;
    s->gps_settled_at=now;
    s->approach_pump_at=0;
    s->free_pass_claimed=0;s->shop_seen=0;s->free_pass_wait_at=0;s->free_pass_spin_result=0;s->join_press_at=0;s->pass_seen=0;s->raid_pass_override=0;s->raid_pass_tried=0;s->ui_joined=0;s->lobby_ready_pressed=0;s->lobby_info_at=0;s->lobby_seen=0;s->lobby_left_at=0;s->ticket_gui=0;s->ticket_seen_at=0;s->approach_fails=0;s->raid_screen_seen=0;s->gym_press_at=0;s->gym_first_tap_at=0;s->hot_checked=0;s->hot_seen_at=0;s->lobby_secs_known=0;s->lobby_secs=0;s->lobby_rejoin_at=0;
    phase(s,5,RS_OPEN,now+UINT64_C(360000000000));
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid join armed fort=%s seed=%llu rpc_only=%u",s->stop.fort,(unsigned long long)s->raid_seed,(unsigned)raid_gym_fast(s));
}
static int raid_bind_gym_from_gui(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(s->stop.fort[0]||!s->gui.serial)return 0;
    SilphReadValue top={0};int r=silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&top);
    if(r||!top.reference.serial){drop(s,&top.reference);return r;}
    char cls[128]={0};(void)silph_raid_class(s->read,top.reference,cls,sizeof(cls));
    bool gym_gui=istype(s,top.reference,"Niantic.Holoholo.Gym","GymGuiController")||(cls[0]&&(strstr(cls,"GymGui")||strstr(cls,"GymDetails")||strstr(cls,"MapPlaceGui")));
    if(!gym_gui){drop(s,&top.reference);return 0;}
    char id[161]={0};static const char *names[]={"get_FortId","get_GymId","get_Id",NULL};
    for(unsigned i=0;names[i];i++){char try_id[161]={0};if(!text(s,top.reference,names[i],try_id,sizeof(try_id),true)&&try_id[0]){memcpy(id,try_id,sizeof(id));break;}}
    if(!id[0])for(unsigned i=0;names[i];i++){char try_id[161]={0};if(!text(s,top.reference,names[i],try_id,sizeof(try_id),false)&&try_id[0]){memcpy(id,try_id,sizeof(id));break;}}
    if(!id[0]){drop(s,&top.reference);return 0;}
    SilphGameGym live={0};int jr=silph_game_read_raid_gym_joinable(s->game,id,&live);
    if(jr||!live.raid_seed){drop(s,&top.reference);return jr?jr:0;}
    memcpy(s->stop.fort,id,sizeof(s->stop.fort));
    if(isfinite(live.latitude))s->stop.latitude=live.latitude;
    if(isfinite(live.longitude))s->stop.longitude=live.longitude;
    s->raid_seed=live.raid_seed;
    s->grunt_character=live.raid_level?live.raid_level:1;
    s->latitude=s->stop.latitude;s->longitude=s->stop.longitude;s->dirty=1;
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym gui bind fort=%s tier=%u seed=%llu",s->stop.fort,(unsigned)s->grunt_character,(unsigned long long)s->raid_seed);
    raid_open_join(s,now);
    int freed=drop(s,&top.reference);return freed;
}
static uint32_t raid_pass_mask(const Raid *s){
    uint32_t v=s->config.raid_pass_item;
    if((v>>16)==1u)return v&7u;
    return v==1400||v==1401?1u:v==1402?2u:3u;
}
static bool raid_pass_allowed(const Raid *s,int32_t item){uint32_t m=raid_pass_mask(s);return item==1401?(m&1u):item==1402?(m&2u):item==1408?(m&4u):false;}
static int raid_pick_pass(Raid *s,int32_t *item){
    if(!item)return SILPH_MANAGED_ARGUMENT;
    if(s->raid_pass_override){*item=(int32_t)s->raid_pass_override;return 0;}
    static const int32_t passes[]={1401,1402};
    for(unsigned i=0;i<2;i++){
        if(!raid_pass_allowed(s,passes[i]))continue;
        uint32_t count=0;int r=silph_game_read_item(s->game,passes[i],&count);
        if(r)return r;
        if(count){
            *item=passes[i];
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid pass found item=%d count=%u",passes[i],count);
            return 0;
        }
    }
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid pass: none in bag, using free pass id 1401");
    *item=1401;return 0;
}
static void raid_try_next_pass(Raid *s){
    if(s->raid_pass_tried>=2)return;
    static const uint32_t passes[]={1401,1402};
    for(unsigned i=s->raid_pass_tried;i<2;i++){
        if(!raid_pass_allowed(s,(int32_t)passes[i]))continue;
        uint32_t count=0;if(silph_game_read_item(s->game,(int32_t)passes[i],&count)||!count)continue;
        s->raid_pass_override=passes[i];s->raid_pass_tried=(uint32_t)(i+1);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","join retry with pass item=%u",(unsigned)s->raid_pass_override);
        return;
    }
    s->raid_pass_tried=2;
}
static int raid_pass_total(Raid *s){
    uint32_t free_n=0,paid_n=0,remote_n=0;
    int a=silph_game_read_item(s->game,1401,&free_n),b=silph_game_read_item(s->game,1402,&paid_n);
    (void)silph_game_read_item(s->game,1408,&remote_n);
    if(a&&b)return -1;
    return (int)(free_n+paid_n);
}
static bool raid_pass_ready(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(s->claim_mode)return s->claim_ok;
    if(!s->stop.fort[0]||s->stop.incident[0]||s->pass_seen||s->lobby_joined_at||s->metrics.stage>=RS_BATTLE)return true;
    uint32_t free_n=0,paid_n=0;
    int fr=silph_game_read_item(s->game,1401,&free_n),pr=silph_game_read_item(s->game,1402,&paid_n);
    if(fr&&pr)return true;
    bool log=now>=s->pass_log_at;if(log)s->pass_log_at=now+UINT64_C(2000000000);
    uint32_t mask=raid_pass_mask(s);bool free_ok=mask&1u,paid_ok=(mask&2u)!=0;
    if(mask==1u&&s->free_raid_done){
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","free pass raid already done and only free pass selected; stopping");
        s->metrics.reason=14;phase(s,8,0,0);return false;
    }
    if((free_ok&&free_n)||(paid_ok&&paid_n)){
        bool use_free=free_ok&&free_n;
        s->pass_seen=1;s->raid_pass_override=use_free?1401:1402;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid pass ready using=%s free=%u premium=%u mask=%u",use_free?"free":"premium",free_n,paid_n,mask);
        return true;
    }
    if(!free_ok){
        __android_log_print(ANDROID_LOG_WARN,"SilphRaid","no selected raid pass in bag (mask=%u free=%u premium=%u); stopping",mask,free_n,paid_n);
        s->metrics.reason=14;phase(s,8,0,0);return false;
    }
    if(!s->free_pass_wait_at)s->free_pass_wait_at=now;
    uint64_t waited=now-s->free_pass_wait_at;
    if(!s->free_pass_claimed){
        if(waited<UINT64_C(2000000000)){
            if(log)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","no raid pass; waiting for free pass at gym waited=%llums",(unsigned long long)(waited/1000000));
            return false;
        }
        if(s->rpc_active||silph_rpc_action_pending(s->rpc))return false;
        SilphRequestInput input={.abi=1,.latitude=s->stop.latitude,.longitude=s->stop.longitude,.player_latitude=s->stop.latitude,.player_longitude=s->stop.longitude};
        memcpy(input.fort,s->stop.fort,sizeof(input.fort));
        double plat=0,plon=0;
        if(!silph_game_read_rocket_position(s->game,&plat,&plon)&&isfinite(plat)&&isfinite(plon)){input.player_latitude=plat;input.player_longitude=plon;}
        int r=rpc_send(s,permit,SILPH_RPC_SPIN,&input,now);
        if(raid_rpc_soft_wait(r))return false;
        s->free_pass_claimed=1;s->free_pass_wait_at=now;s->free_pass_spin_result=r?UINT32_MAX:0;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","claiming daily free raid pass (gym spin) fort=%s err=%d",s->stop.fort,r);
        return false;
    }
    if(s->rpc_kind==SILPH_RPC_SPIN||(s->free_pass_spin_result==1&&waited<UINT64_C(6000000000))||(!s->free_pass_spin_result&&waited<UINT64_C(10000000000))){
        if(log)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","waiting for claimed free pass in bag spin_result=%u waited=%llums",
            (unsigned)s->free_pass_spin_result,(unsigned long long)(waited/1000000));
        return false;
    }
    if(paid_n&&paid_ok){
        s->pass_seen=1;s->raid_pass_override=1402;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","no free pass after gym spin (result=%u); using premium pass premium=%u",(unsigned)s->free_pass_spin_result,paid_n);
        return true;
    }
    if(s->free_pass_spin_result==1||++s->free_claim_fails>=2){
        __android_log_print(ANDROID_LOG_WARN,"SilphRaid","free raid pass not claimable (spin=%u tries=%u): daily free pass already used; stopping",(unsigned)s->free_pass_spin_result,s->free_claim_fails);
        s->metrics.reason=14;phase(s,8,0,0);
    }else raid_clear_gym_target(s,now,"no raid pass and free pass claim failed");
    return false;
}
static bool raid_hot_check(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(s->hot_checked||s->claim_mode||!s->config.raid_hot_lobby)return true;
    if(!s->hot_seen_at)s->hot_seen_at=now;
    if(now-s->hot_seen_at<UINT64_C(1500000000))return false;
    int best=-1;
    if(s->approach_gui.serial){
        const char *fields[]={"playersInLobbyVerticalText","playersInLobbyHorizontalText","playersInLobbyRemoteVerticalText","playersInLobbyRemoteHorizontalText"};
        for(unsigned i=0;i<4;i++){SilphManagedRef lbl={0};char buf[32]={0};
            if(!fieldref(s,s->approach_gui,fields[i],&lbl)&&lbl.serial&&!text(s,lbl,"get_text",buf,sizeof(buf),true)){int v=-1;for(char *c=buf;*c;c++)if(*c>='0'&&*c<='9'){v=atoi(c);break;}if(v>best)best=v;}
            drop(s,&lbl);}
    }
    if(best<0&&now-s->hot_seen_at<UINT64_C(4000000000))return false;
    s->hot_checked=1;
    uint32_t req=raid_min_for_tier(s,s->grunt_character),need=req/2?req/2:1;
    if(best>=0&&(uint32_t)best<need&&s->cold_skips<6){
        s->cold_skips++;
        int closed=s->approach_gui.serial?action(s,permit,RK_GUI_CLOSE,s->approach_gui,NULL,0):-1;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","cold gym: lobby has %d players, need %u; skipping without using a pass (skip %u/6, close=%d)",best,need,s->cold_skips,closed);
        raid_done_add(s->stop.fort,"cold lobby");
        raid_clear_gym_target(s,now,"cold lobby");
        return false;
    }
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym lobby count=%d need=%u -> joining",best,need);
    return true;
}
static bool raid_join_press_allowed(Raid *s,const SilphSchedulePermit *permit,const char *name,uint64_t now){
    if(!name||!(strstr(name,"Approach")||strstr(name,"GymGui")||strstr(name,"GymDetails")||strstr(name,"GymInterior")||strstr(name,"GymHub")))return true;
    if(s->ui_joined||s->won||s->lobby_seen)return false;
    if(strstr(name,"Approach")&&!raid_hot_check(s,permit,now))return false;
    if(!raid_pass_ready(s,permit,now))return false;
    if(strstr(name,"Layers"))return true;
    if(!s->raid_screen_seen&&s->gym_press_at){
        if(now-s->gym_press_at<UINT64_C(6000000000))return false;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym battle press opened no raid screen; gym has no joinable raid");
        raid_done_add(s->stop.fort,"no raid screen");
        raid_clear_gym_target(s,now,"no joinable raid at gym");
        return false;
    }
    if(s->join_press_at&&now<s->join_press_at+UINT64_C(2500000000))return false;
    if(!s->raid_screen_seen)s->gym_press_at=now;
    s->join_press_at=now;return true;
}
static bool raid_at_gym(Raid *s){
    if(!s->stop.fort[0])return false;
    double plat=0,plon=0;int r=silph_game_read_rocket_position(s->game,&plat,&plon);
    if(r==SILPH_MANAGED_NOT_READY||r)return false;
    return raid_near_gym(s,plat,plon);
}
static int raid_ensure_place(Raid *s){
    if(s->place.serial)return 0;
    if(!s->stop.fort[0])return SILPH_MANAGED_ARGUMENT;
    int r=silph_game_read_gym_map(s->game,s->stop.fort,&s->place);
    if(!r&&s->place.serial){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym place resolved via map fort=%s",s->stop.fort);return 0;}
    if(s->place.serial)drop(s,&s->place);
    r=silph_game_read_gym_map(s->game,s->stop.fort,&s->place);
    if(!r&&s->place.serial){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym place resolved via map retry fort=%s",s->stop.fort);return 0;}
    if(s->place.serial)drop(s,&s->place);
    r=silph_game_read_place(s->game,s->stop.fort,&s->place);
    if(!r&&s->place.serial){
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym place resolved via emdp fort=%s",s->stop.fort);
        return 0;
    }
    if(!s->place.serial&&s->stop.fort[0]&&isfinite(s->stop.latitude)&&isfinite(s->stop.longitude)){
        SilphGameStop anchor={0};SilphGameGym repick={0};
        anchor.latitude=s->stop.latitude;anchor.longitude=s->stop.longitude;
        double radius=s->config.raid_radius_m;
        if(!isfinite(radius)||radius<=0)radius=500;
        int sr=silph_game_read_raid_gym(s->game,radius,127,0,&anchor,&repick);
        if(!sr&&repick.fort[0]&&!strcmp(repick.fort,s->stop.fort)&&repick.map_place.serial){
            s->place=repick.map_place;repick.map_place=(SilphManagedRef){0};
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym place resolved via rescan fort=%s",s->stop.fort);
            return 0;
        }
        if(repick.map_place.serial)drop(s,&repick.map_place);
        (void)sr;
    }
    return r?r:SILPH_MANAGED_NOT_READY;
}
static int raid_rpc_input(Raid *s,SilphRequestInput *input){
    if(!input||!s->stop.fort[0])return SILPH_MANAGED_ARGUMENT;
    memset(input,0,sizeof(*input));input->abi=1;memcpy(input->fort,s->stop.fort,sizeof(input->fort));
    input->latitude=s->stop.latitude;input->longitude=s->stop.longitude;input->encounter=s->raid_seed?s->raid_seed:1;
    input->player_latitude=s->stop.latitude;input->player_longitude=s->stop.longitude;
    SilphGameGym live={0};int lr=silph_game_read_raid_gym_joinable(s->game,s->stop.fort,&live);
    if(!lr&&live.raid_seed){
        if(live.raid_seed!=s->raid_seed)s->raid_seed=live.raid_seed;
        input->encounter=s->raid_seed;
        if(isfinite(live.latitude)&&isfinite(live.longitude)){
            s->stop.latitude=live.latitude;s->stop.longitude=live.longitude;
            input->latitude=live.latitude;input->longitude=live.longitude;
        }
    }
    double plat=0,plon=0;int pr=silph_game_read_rocket_position(s->game,&plat,&plon);
    if(!pr&&isfinite(plat)&&isfinite(plon)){
        input->player_latitude=plat;input->player_longitude=plon;
        if(!raid_near_gym(s,plat,plon))
            __android_log_print(ANDROID_LOG_WARN,"SilphRaid","raid rpc player far from fort=%s (mock?)",s->stop.fort);
    }
    raid_pick_pass(s,&input->item);
    raid_load_team(s);input->team_count=0;for(unsigned i=0;i<3;i++)if(s->team[i])input->team[input->team_count++]=s->team[i];
    return 0;
}
static uint64_t raid_join_backoff_until(const Raid *s,uint64_t now){
    if(!s||!s->join_failures||!s->action_log_at)return 0;
    uint64_t delay=s->join_failures>4?UINT64_C(8000000000):UINT64_C(3000000000);
    uint64_t until=s->action_log_at+delay;
    return now<until?until:0;
}
static int raid_pick_faction(Raid *s,int32_t *item){
    if(!item)return SILPH_MANAGED_ARGUMENT;
    uint32_t team=0;int r=silph_game_read_faction_team(s->game,&team);
    if(!r&&team){*item=(int32_t)team;return 0;}
    *item=s->config.raid_team>=1&&s->config.raid_team<=3?(int32_t)s->config.raid_team:1;return 0;
}
static int raid_team_rpc_input(Raid *s,SilphRequestInput *input){
    if(!input)return SILPH_MANAGED_ARGUMENT;
    memset(input,0,sizeof(*input));input->abi=1;
    return raid_pick_faction(s,&input->item);
}
static bool raid_team_needed(Raid *s,uint64_t now){
    if(s->team_setup==2||s->team_setup_tries>=3||now<s->team_setup_at)return false;
    uint32_t team=0;int r=silph_game_read_faction_team(s->game,&team);
    if(now>=s->team_log_at){s->team_log_at=now+UINT64_C(10000000000);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","account team read r=%d team=%u want=%u",r,(unsigned)team,(unsigned)s->config.raid_team);}
    if(!r&&team){s->team_setup=2;return false;}
    return true;
}
static int raid_rpc_send(Raid *s,const SilphSchedulePermit *permit,uint32_t kind,uint64_t now){
    if(kind==SILPH_RPC_JOIN_LOBBY&&(s->raid_seed<=1||s->ui_joined)){
        s->join_ui_fallback=1;
        if(now>=s->action_log_at){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","join rpc skipped seed=%llu ui_joined=%u; using game ui",(unsigned long long)s->raid_seed,s->ui_joined);s->action_log_at=now+UINT64_C(5000000000);}
        return SILPH_MANAGED_NOT_READY;
    }
    if(kind==SILPH_RPC_JOIN_LOBBY&&raid_join_backoff_until(s,now)){
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc join backoff op=%u",(unsigned)kind);
        return SILPH_MANAGED_NOT_READY;
    }
    SilphRequestInput input={0};
    int r=kind==SILPH_RPC_SET_TEAM?raid_team_rpc_input(s,&input):raid_rpc_input(s,&input);
    if(r){__android_log_print(ANDROID_LOG_WARN,"SilphRaid","raid rpc input err=%d op=%u",r,(unsigned)kind);return r;}
    r=rpc_send(s,permit,kind,&input,now);
    if(!r)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc send op=%u seed=%llu fort=%s player=%.6f,%.6f gym=%.6f,%.6f",kind,(unsigned long long)input.encounter,s->stop.fort,
        input.player_latitude,input.player_longitude,input.latitude,input.longitude);
    return r;
}
static bool raid_rpc_success(uint64_t code){return code==1||code==2;}
static bool raid_rpc_success_kind(uint32_t kind,uint64_t code){
    if(kind==SILPH_RPC_SET_TEAM)return code==1||code==2||code==0;
    return raid_rpc_success(code);
}
static bool raid_gym_fast(const Raid *s){return s&&!s->stop.incident[0]&&s->stop.fort[0];}
static bool raid_rpc_only(const Raid *s){(void)s;return false;}
static bool raid_pass_ui_assist(const Raid *s){
    return s&&raid_gym_fast(s)&&!s->stop.incident[0]&&(s->join_ui_fallback||s->raid_rpc_step>=1||s->metrics.stage==RS_UI);
}
static bool raid_gym_ui_top(const Raid *s){
    if(!s||!s->top_name[0])return false;
    return strstr(s->top_name,"GymApproach")||strstr(s->top_name,"RaidApproach")||strstr(s->top_name,"GymGui")
        ||strstr(s->top_name,"GymInterior")||strstr(s->top_name,"GymHub")||strstr(s->top_name,"RaidGym")
        ||strstr(s->top_name,"RaidLobby")||strstr(s->top_name,"BattleLobby")||strstr(s->top_name,"GlobalError");
}
static void raid_retain_approach_gui(Raid *s,SilphManagedRef gui){
    if(!s||!gui.serial)return;
    if(s->approach_gui.serial==gui.serial)return;
    void *object=NULL;
    if(silph_managed_target(s->managed,gui,&object)||!object)return;
    drop(s,&s->approach_gui);
    if(silph_managed_retain(s->managed,object,false,&s->approach_gui))memset(&s->approach_gui,0,sizeof(s->approach_gui));
}
static void raid_cancel_stuck_rpc(Raid *s,uint32_t kind,uint64_t now,const char *why){
    if(!s||(!s->rpc_active&&!silph_rpc_action_pending(s->rpc)))return;
    if(kind&&s->rpc_kind&&s->rpc_kind!=kind)return;
    uint32_t was=s->rpc_kind;
    silph_rpc_action_reset(s->rpc);
    s->rpc_active=0;s->rpc_kind=0;s->rpc_sent_at=0;s->next=now+UINT64_C(50000000);
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc reset (%s) kind=%u",why?why:"?",(unsigned)was);
}
static void raid_skip_details_for_ui(Raid *s,uint64_t now){
    if(!s||!s->join_ui_fallback||!s->raid_details_pending||!s->raid_details_ok)return;
    if(s->approach_at&&now-s->approach_at>UINT64_C(3000000000)){
        s->raid_details_pending=0;
        raid_cancel_stuck_rpc(s,SILPH_RPC_RAID_DETAILS,now,"skip details on approach");
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","skip details rpc; ui join on approach");
    }else if(s->metrics.stage==RS_UI&&now-s->ui_started>UINT64_C(6000000000)){
        s->raid_details_pending=0;
        raid_cancel_stuck_rpc(s,SILPH_RPC_RAID_DETAILS,now,"skip details timeout");
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","skip details rpc; ui join timeout");
    }
}
static int raid_pump_approach_gui(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(!s||!s->join_ui_fallback||!s->approach_at||s->ui_joined||s->won)return 0;
    if(now-s->approach_at>UINT64_C(240000000000)){drop(s,&s->approach_gui);return 0;}
    bool main=!s->top_name[0]||!strcmp(s->top_name,"MainMenuGuiController");
    if(main||s->lobby_layer_hint){
        raid_skip_details_for_ui(s,now);
        if(main&&s->approach_gui.serial&&now-s->approach_at>=UINT64_C(4000000000)){
            drop(s,&s->approach_gui);
            s->approach_at=0;
        }
        if(s->approach_gui.serial&&!s->won&&!s->lobby_seen&&!s->ui_joined&&!s->claim_mode&&s->raid_pass_override!=1402){
            if(!s->toast_seen_at){s->toast_seen_at=now;s->toast_taps=0;s->toast_tap_at=0;}
            if(now-s->toast_seen_at>UINT64_C(2500000000)&&s->toast_taps<3&&now>=s->toast_tap_at){
                s->toast_taps++;s->toast_tap_at=now+UINT64_C(4000000000);s->tap_request=1;s->dirty=1;
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","free pass popup: requesting close tap (%u/3)",s->toast_taps);
            }
        }else if(!s->approach_gui.serial)s->toast_seen_at=0;
        if(s->approach_gui.serial&&now>=s->approach_pump_at&&raid_join_press_allowed(s,permit,"Approach",now)){
            s->approach_pump_at=now+UINT64_C(280000000);
            (void)action(s,permit,RK_FAST_GUI,s->approach_gui,NULL,0);
            int r=action(s,permit,RK_RAID_GUI,s->approach_gui,NULL,0);
            if(r!=SILPH_MANAGED_NOT_READY||now-s->approach_at<UINT64_C(3000000000))
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","approach pump retained result=%d",r);
            if(raid_approach_result(s,permit,s->approach_gui,now,"RaidApproach",&r))return 0;
            if(r&&r!=SILPH_MANAGED_NOT_READY)return r;
        }
        if(main&&s->place.serial&&raid_gym_still_joinable(s)&&!s->rpc_active&&now>=s->ui_retry_at){
            (void)raid_ensure_place(s);
            int tr=raid_tap_place(s,permit,now);
            s->ui_retry_at=now+UINT64_C(1200000000);
            if(!tr){s->ui_taps++;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","approach gym retap fort=%s attempt=%u",s->stop.fort,(unsigned)s->ui_taps);}
            return tr==SILPH_MANAGED_NOT_READY?0:tr;
        }
        if(now<s->gui_at)return 0;
        s->gui_at=now+UINT64_C(450000000);
        int layer_r=raid_pump_approach_layers(s,permit,now);
        if(layer_r&&layer_r!=SILPH_MANAGED_NOT_READY)return layer_r;
        if(s->lobby_layer_hint&&!s->rpc_active&&!silph_rpc_action_pending(s->rpc)&&now>=s->ui_retry_at)
            (void)raid_rpc_kickoff(s,permit,now);
        return 0;
    }
    if(!s->approach_gui.serial)return 0;
    if(!raid_gym_ui_top(s)&&!strstr(s->top_name,"Approach")&&!strstr(s->top_name,"Interior")&&!strstr(s->top_name,"GymGui"))return 0;
    if(now<s->gui_at||!raid_join_press_allowed(s,permit,"Approach",now))return 0;
    s->gui_at=now+UINT64_C(35000000);
    int r=action(s,permit,RK_RAID_GUI,s->approach_gui,NULL,0);
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","approach pump top=%s result=%d",s->top_name[0]?s->top_name:"?",r);
    if(raid_approach_result(s,permit,s->approach_gui,now,"RaidApproach",&r))return 0;
    return r==SILPH_MANAGED_NOT_READY?0:r;
}
static void raid_kickoff_skip(Raid *s,uint64_t now,const char *why){
    static uint64_t last;static char last_why[48];
    if(!s->join_kickoff_pending&&!s->approach_at)return;
    if(now-last<UINT64_C(2000000000)&&!strcmp(last_why,why))return;
    last=now;snprintf(last_why,sizeof(last_why),"%s",why?why:"?");
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","kickoff skip=%s step=%u stage=%u det=%u ui_seen=%u rpc=%u tel=%u gps=%u seed=%llu",
        why?why:"?",(unsigned)s->raid_rpc_step,s->metrics.stage,(unsigned)s->raid_details_pending,s->ui_seen,(unsigned)s->rpc_active,
        (unsigned)s->teleport_ready,(unsigned)(s->gps_settled_at!=0),(unsigned long long)s->raid_seed);
}
static int raid_rpc_kickoff(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(s->ui_joined||s->won){raid_kickoff_skip(s,now,"ui_joined");return 0;}
    if(s->join_kickoff_pending&&s->raid_rpc_step>=2&&!s->lobby_joined_at)s->raid_rpc_step=0;
    bool urgent=s->approach_at&&s->raid_rpc_step<2&&!s->lobby_joined_at&&now>s->approach_at+UINT64_C(3000000000);
    if(now<s->ui_retry_at&&!urgent){raid_kickoff_skip(s,now,"ui_retry");return 0;}
    if(s->join_ui_fallback&&s->approach_at&&now-s->approach_at<UINT64_C(120000000000)&&!raid_gym_ui_top(s)
        &&!strstr(s->top_name,"RaidLobby")&&!strstr(s->top_name,"BattleLobby")&&!s->lobby_layer_hint&&!s->lobby_joined_at){
        uint64_t approach_age=now-s->approach_at;
        uint64_t block_ns=s->approach_gui.serial?UINT64_C(8000000000):UINT64_C(12000000000);
        if(approach_age<block_ns){raid_kickoff_skip(s,now,"approach_ui");return 0;}
    }
    raid_skip_details_for_ui(s,now);
    if(s->rpc_active){raid_kickoff_skip(s,now,"rpc_active");return 0;}
    if(!s->join_kickoff_pending&&s->join_ui_fallback&&s->raid_rpc_step>=2&&!raid_gym_ui_top(s)&&s->raid_details_pending){
        if(!s->ui_seen&&s->place.serial&&s->raid_rpc_step<3){raid_kickoff_skip(s,now,"lobby_ui");return 0;}
        if(!s->ui_seen&&s->place.serial&&!s->faction_team_attempted){raid_kickoff_skip(s,now,"faction");return 0;}
    }
    uint64_t join_hold=raid_join_backoff_until(s,now);if(join_hold){raid_kickoff_skip(s,now,"join_backoff");return 0;}
    if(!raid_at_gym(s)&&!s->teleport_ready&&!s->gps_settled_at){raid_kickoff_skip(s,now,"not_at_gym");return 0;}
    if(s->raid_rpc_step>=3){raid_kickoff_skip(s,now,"step_done");return 0;}
    if(s->raid_rpc_step==1&&now<s->ui_retry_at){raid_kickoff_skip(s,now,"step1_wait");return 0;}
    if(raid_rpc_only(s)&&!s->join_ui_fallback&&now>s->ui_started+UINT64_C(20000000000))s->join_ui_fallback=1;
    if(!s->team[0]){int t=raid_load_team(s);if(t&&t!=SILPH_MANAGED_NOT_READY)return t;}
    uint32_t op;
    if(s->raid_rpc_step==2){
        if(now<s->ui_retry_at){raid_kickoff_skip(s,now,"lobby_retry");return 0;}
        uint32_t req_players=raid_min_for_tier(s,s->grunt_character);
        if(s->game&&s->stop.fort[0]){
            SilphGameGym live_gym={0};
            if(!silph_game_read_raid_gym_joinable(s->game,s->stop.fort,&live_gym)){
                if(live_gym.lobby_players>s->lobby_players)s->lobby_players=live_gym.lobby_players;
            }
        }
        if(s->lobby_players<req_players){
            uint64_t wait_ns=(s->lobby_joined_at&&now>s->lobby_joined_at)?(now-s->lobby_joined_at):0;
            if(wait_ns>UINT64_C(105000000000)){
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","lobby players insufficient (%u < %u) after %llus; leaving lobby",
                    s->lobby_players,req_players,(unsigned long long)(wait_ns/1000000000));
                raid_clear_gym_target(s,now,"insufficient lobby players");
                return 0;
            }
            char skip_msg[64];
            snprintf(skip_msg,sizeof(skip_msg),"need_players_%u_have_%u",req_players,s->lobby_players);
            raid_kickoff_skip(s,now,skip_msg);
            s->ui_retry_at=now+UINT64_C(1500000000);
            return 0;
        }
        if(s->lobby_joined_at&&now<s->lobby_joined_at+UINT64_C(s->ui_seen?45000000000:8000000000)){raid_kickoff_skip(s,now,"lobby_timer");return 0;}
        op=SILPH_RPC_START_RAID;
        if(s->team[0]&&!s->lobby_pokemon_set)op=SILPH_RPC_SET_LOBBY;
    }else op=s->raid_details_pending?SILPH_RPC_RAID_DETAILS:SILPH_RPC_JOIN_LOBBY;
    if(op==SILPH_RPC_JOIN_LOBBY&&!raid_pass_ready(s,permit,now)){raid_kickoff_skip(s,now,"no_pass");return 0;}
    if(s->join_ui_fallback&&op==SILPH_RPC_START_RAID&&!raid_gym_ui_top(s)){raid_kickoff_skip(s,now,"start_needs_ui");return 0;}
    if(s->join_kickoff_pending)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","kickoff send op=%u step=%u",(unsigned)op,(unsigned)s->raid_rpc_step);
    int sent=raid_rpc_send(s,permit,op,now);
    if(sent==SILPH_MANAGED_CONTRACT&&op==SILPH_RPC_RAID_DETAILS&&s->raid_seed){
        s->raid_details_pending=0;s->raid_details_ok=1;
        op=SILPH_RPC_JOIN_LOBBY;
        sent=raid_rpc_send(s,permit,op,now);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid details contract; join with scan seed=%llu",(unsigned long long)s->raid_seed);
    }
    if(!sent&&op==SILPH_RPC_SET_TEAM)s->faction_team_attempted=1;
    else if(sent&&op==SILPH_RPC_SET_TEAM){
        s->faction_team_attempted=1;s->join_ui_fallback=1;s->metrics.stage=RS_UI;s->ui_seen=0;s->ui_retry_at=now+UINT64_C(300000000);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","set player team rpc err=%d; pick team ui",sent);
        return 0;
    }
    if(sent==SILPH_MANAGED_CONTRACT){
        s->ui_retry_at=now+UINT64_C(200000000);
        if(!raid_rpc_only(s)){s->join_ui_fallback=1;s->metrics.stage=RS_UI;s->ui_seen=0;}
        else __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc kickoff contract op=%u; retry rpc",op);
        if(!raid_rpc_only(s))__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc kickoff contract op=%u; gym ui join",op);
        return 0;
    }
    if(raid_rpc_soft_wait(sent)){
        if(sent==SILPH_MANAGED_STATE&&s->join_ui_fallback&&op==SILPH_RPC_JOIN_LOBBY&&s->rpc_kind==SILPH_RPC_RAID_DETAILS){
            raid_cancel_stuck_rpc(s,SILPH_RPC_RAID_DETAILS,now,"join blocked by details");
            sent=raid_rpc_send(s,permit,op,now);
            if(!sent){s->raid_rpc_step=1;s->ui_retry_at=now+UINT64_C(250000000);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc kickoff op=%u",op);return 0;}
        }
        if(raid_rpc_soft_wait(sent)){
            uint64_t delay=UINT64_C(sent==SILPH_MANAGED_NOT_READY?2000000000:500000000);
            if(op==SILPH_RPC_JOIN_LOBBY||op==SILPH_RPC_RAID_DETAILS){
                s->join_send_soft_fails++;
                if(s->join_send_soft_fails>=3&&!s->raid_details_pending&&s->raid_seed&&op==SILPH_RPC_JOIN_LOBBY){
                    s->raid_details_pending=1;s->raid_details_ok=0;s->join_send_soft_fails=0;delay=UINT64_C(500000000);
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","join soft-fail x3; retry via GetRaidDetails");
                }
            }
            s->ui_retry_at=now+delay;
            s->join_ui_fallback=1;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc waiting op=%u err=%d; map/ui join",op,sent);
            return 0;
        }
    }
    if(!sent){s->raid_rpc_step=1;s->ui_retry_at=now+UINT64_C(250000000);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc kickoff op=%u",op);return 0;}
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc kickoff err=%d op=%u",sent,op);
    return sent;
}
static int finish_encounter(Raid *s,const SilphSchedulePermit *permit,uint32_t reason){
    if(s->headless){
        char incident[161]={0};int r=text(s,s->incident,"get_CurrentIncidentId",incident,sizeof(incident),true);
        if(!r&&strcmp(incident,s->stop.incident))r=SILPH_MANAGED_STALE;
        SilphReadArgument args[]={{.kind=SILPH_READ_U64,.value.bits=s->metrics.encounter},{.kind=SILPH_READ_ENUM32,.value.bits=reason}};
        if(!r)r=action(s,permit,RK_REWARD_COMPLETE,s->incident,args,2);
        if(!r&&s->reward_gui.serial)r=action(s,permit,RK_REWARD_DISMISS,s->reward_gui,NULL,0);
        if(!r){s->headless=0;s->metrics.stage=RS_HEAL;s->next=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","native shadow result completed reason=%u",reason);}return r;
    }
    SilphReadArgument arg={.kind=SILPH_READ_ENUM32,.value.bits=reason};int r=action(s,permit,RK_ENCOUNTER_FINISH,s->encounter_state,&arg,1);
    s->metrics.stage=RS_HEAL;s->next=0;return r;
}
static int raid_map_capture(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(!s->catch_promise){s->catch_promise=calloc(1,silph_managed_promise_size());if(!s->catch_promise)return SILPH_MANAGED_CAPACITY;}
    if(s->catch_shown_at){
        bool still=!strcmp(s->top_name,"EncounterGuiController");
        bool settled=!strcmp(s->top_name,"MainMenuGuiController")||!strcmp(s->top_name,"PokemonInfoGuiController");
        if(!settled&&now-s->catch_shown_at<UINT64_C(15000000000))return 0;
        int fr=0;
        if(still&&s->encounter_state.serial){SilphReadArgument fin={.kind=SILPH_READ_ENUM32,.value.bits=s->catch_code==1?0u:2u};fr=action(s,permit,RK_ENCOUNTER_FINISH,s->encounter_state,&fin,1);}
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","encounter screen %s top=%s finish=%d",still?"still open; closing":"closed by game",s->top_name,fr);
        drop(s,&s->map_pokemon);s->raid_encounter=0;s->catch_shown_at=0;
        s->headless=0;s->won=0;result(s,0,s->catch_code==1);
        return 0;
    }
    if(s->catch_active){
        if(!s->rr_ready){
            if(!s->rpc_active&&now>s->catch_at+UINT64_C(25000000000)){__android_log_print(ANDROID_LOG_WARN,"SilphRaid","catch reply lost; throwing again");s->catch_active=0;s->next=now+UINT64_C(1000000000);}
            return 0;
        }
        SilphManagedRef reply=s->rr_reply;s->rr_reply=(SilphManagedRef){0};
        const uint64_t code=s->rr_code;const int err=s->rr_err;const uint32_t was=s->catch_active;s->rr_ready=0;s->catch_active=0;
        if(was==2){
            uint64_t active=0;if(reply.serial){(void)scalar(s,reply,"ActiveItem",SILPH_READ_ENUM32,&active,false);static int bd;if(!bd){bd=1;silph_dump_methods(s->read,reply,"UseItemEncounterOutProto");}}
            drop(s,&reply);
            uint32_t after=0;(void)silph_game_read_item(s->game,s->berry_item,&after);
            bool used=code==1||code==3||after<s->berry_before;
            s->berry_fed=1;if(!used)s->berry_fails++;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","berry result=%llu (1=fed 3=already active) err=%d active_item=%llu used=%d have=%u->%u",(unsigned long long)code,err,(unsigned long long)active,(int)used,s->berry_before,after);
            s->next=now+UINT64_C(800000000);return 0;
        }
        if(reply.serial){
            static int dumped;if(!dumped){dumped=1;silph_dump_methods(s->read,reply,"CatchPokemonOutProto");}
            uint64_t miss=0,reason=0;(void)scalar(s,reply,"MissPercent",SILPH_READ_F64,&miss,false);(void)scalar(s,reply,"CaptureReason",SILPH_READ_ENUM32,&reason,false);
            double mp=0;memcpy(&mp,&miss,sizeof(mp));
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","catch reply status=%llu (1=caught 2=hit but broke out 3=fled 4=missed) miss_percent=%.3f capture_reason=%llu",(unsigned long long)code,mp,(unsigned long long)reason);
        }
        if(code==1){uint64_t owned=0;(void)scalar(s,reply,"CapturedPokemonId",SILPH_READ_U64,&owned,false);s->metrics.captured=owned;s->metrics.catches++;
            if(owned){(void)raid_capture_watch(&s->record_gate,owned,now);(void)raid_capture_watch(&s->capture_gate,owned,now);s->result_scan_at=0;s->result_blocked=0;}}
        if(reply.serial&&s->encounter_state.serial&&(code==1||code==3)){
            SilphReadArgument cc={.kind=SILPH_READ_REFERENCE,.value.reference=reply};
            int cr=action(s,permit,RK_CAPTURE_COMPLETED,s->encounter_state,&cc,1);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","catch shown on screen result=%d",cr);
        }
        drop(s,&reply);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","catch result=%llu (1=caught 2=escaped 3=fled) err=%d balls_left=%u",(unsigned long long)code,err,s->balls);
        if(code==1||code==3||!s->balls){
            s->catch_shown_at=now;s->catch_code=code;raid_claim_set(NULL,0,0);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid catch finished result=%llu captured=%llu; waiting for catch screen",(unsigned long long)code,(unsigned long long)s->metrics.captured);
            return 0;
        }
        s->berry_fed=0;s->next=now+UINT64_C(2500000000);return 0;
    }
    if(!s->balls){s->metrics.stage=RS_HEAL;return 0;}
    SilphRequestInput in={.abi=1,.encounter=s->metrics.encounter};
    {const char *target=s->raid_spawn[0]?s->raid_spawn:s->stop.fort;const size_t len=strnlen(target,sizeof(in.fort)-1);
     bool token=len&&len<sizeof(in.location)&&strspn(target,"0123456789abcdef")==len&&target[0]<='b'&&strspn(target,"0")!=len;
     if(token)memcpy(in.location,target,len);else memcpy(in.fort,target,len);}
    if(!s->berry_fed&&s->berry_fails<3){
        static const int32_t order[]={706,708,701};int32_t berry=0;uint32_t c=0;
        uint32_t have[3]={0};int rr[3]={0};
        for(unsigned i=0;i<3;i++){rr[i]=silph_game_read_item(s->game,order[i],&have[i]);if(!berry&&!rr[i]&&have[i]){berry=order[i];c=have[i];}}
        if(!berry){s->berry_fed=1;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","no berry to feed golden=%u(r%d) silver=%u(r%d) razz=%u(r%d)",have[0],rr[0],have[1],rr[1],have[2],rr[2]);}
        else{
            in.item=berry;int br=rpc_send(s,permit,SILPH_RPC_KECLEON_BERRY,&in,now);
            if(!br){s->berry_item=berry;s->berry_before=c;s->catch_active=2;s->catch_at=now;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","feeding berry item=%d have=%u target=%s",berry,c,in.location[0]?in.location:in.fort);return 0;}
            __android_log_print(ANDROID_LOG_WARN,"SilphRaid","berry feed failed item=%d r=%d",berry,br);
            if(br==SILPH_MANAGED_NOT_READY){s->next=now+UINT64_C(1000000000);return 0;}
            s->berry_fails++;s->berry_fed=1;
        }
    }
    in.item=5;in.quality=4;in.curve=1;
    int r=rpc_send(s,permit,SILPH_RPC_ROCKET_CAPTURE,&in,now);
    if(!r){s->catch_active=1;s->catch_at=now;s->balls--;s->metrics.throws++;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","throw sent: excellent (reticle 1.95) + curveball (spin 1.0) + bullseye hit, premier ball, balls_left=%u",s->balls);}
    else{__android_log_print(ANDROID_LOG_WARN,"SilphRaid","throw failed r=%d; retrying",r);s->next=now+UINT64_C(2000000000);r=0;}
    return r;
}
static int capture(Raid *s,const SilphSchedulePermit *permit,const SilphGameSnapshot *snapshot,uint64_t now){
    if(s->metrics.phase!=5||s->returning||s->rpc_active||now<s->next)return 0;
    if(s->raid_encounter==2)return raid_map_capture(s,permit,now);
    if(!raid_catch_accept(s->config.filters,s->iv[0],s->iv[1],s->iv[2],s->shiny))return finish_encounter(s,permit,1);
    if(s->anchor_expected){
        double lat=0,lon=0;int r=silph_game_read_rocket_position(s->game,&lat,&lon);if(r&&r!=SILPH_MANAGED_NOT_READY)return r;
        double dy=(lat-s->anchor_latitude)*111195,dx=remainder(lon-s->anchor_longitude,360.0)*111195*cos(s->anchor_latitude*0.017453292519943295);
        if(r||hypot(dx,dy)>12){if(now>=s->anchor_until){s->metrics.error=118;phase(s,8,0,0);}return 0;}
        s->anchor_expected=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","capture location confirmed distance=%.2f",hypot(dx,dy));
    }
    if(snapshot->pokemon_count>=snapshot->max_pokemon){s->metrics.reason=8;phase(s,8,0,0);return 0;}
    if(!s->balls)return finish_encounter(s,permit,2);
    SilphRequestInput input={.abi=1,.encounter=s->metrics.encounter,.quality=s->config.quality,.curve=!!(s->config.flags&SILPH_CFG_CURVE),.item=5};memcpy(input.location,s->spawn,17);
    if(s->raid_encounter){input.quality=4;input.curve=1;if(!s->spawn[0])memcpy(input.fort,s->stop.fort,sizeof(input.fort));}
    if((s->config.flags&SILPH_CFG_BERRIES)&&!s->active_item&&!s->berry_attempted){
        s->berry_attempted=1;for(unsigned i=0;i<s->config.berry_count;i++){uint32_t count=0;int r=silph_game_read_item(s->game,(int32_t)s->config.berries[i],&count);if(r)return r;
            if(count){input.item=(int32_t)s->config.berries[i];return rpc_send(s,permit,SILPH_RPC_KECLEON_BERRY,&input,now);}}
    }
    int r=rpc_send(s,permit,SILPH_RPC_ROCKET_CAPTURE,&input,now);if(!r){s->metrics.throws++;s->balls--;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","throw sent (excellent+curve) request=%llu balls_left=%u",(unsigned long long)s->metrics.request,s->balls);}return r;
}
static bool raid_heal_party(Raid *s,const SilphSchedulePermit *permit,uint64_t now,int *err){
    *err=0;
    if(!s->heal_promise){s->heal_promise=calloc(1,silph_managed_promise_size());if(!s->heal_promise){*err=SILPH_MANAGED_CAPACITY;return false;}}
    if(s->heal_active){
        SilphPromiseMetrics m={0};int r=silph_managed_promise_poll(s->heal_promise,&s->identity,now,&m);
        const bool late=now>s->heal_at+UINT64_C(15000000000);
        if((r==SILPH_MANAGED_NOT_READY||(!r&&m.state==SILPH_PROMISE_WAITING))&&!late)return true;
        uint64_t hp=0,max=0;
        if(!r&&m.state==SILPH_PROMISE_RESOLVED){SilphManagedRef reply={0};bool fresh=false;
            if(!silph_managed_promise_take(s->heal_promise,&s->identity,now,&reply,&fresh)){(void)scalar(s,reply,"stamina_",SILPH_READ_I32,&hp,false);(void)scalar(s,reply,"maxStamina_",SILPH_READ_I32,&max,false);}
            drop(s,&reply);}
        if(!hp)s->heal_fails++;
        silph_managed_promise_close(s->heal_promise);s->heal_active=0;s->heal_check_at=now+UINT64_C(1500000000);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","heal result pokemon=%llu hp=%llu/%llu state=%u err=%d",(unsigned long long)s->heal_target,(unsigned long long)hp,(unsigned long long)max,(unsigned)m.state,r);
        return true;
    }
    if(now<s->heal_check_at)return false;
    uint64_t ids[6]={0};int32_t hp[6]={0},mx[6]={0};unsigned n=0;
    int r=silph_game_read_heal_list(s->game,ids,hp,mx,6,&n);
    if(r){s->heal_check_at=now+UINT64_C(10000000000);__android_log_print(ANDROID_LOG_WARN,"SilphRaid","heal list unavailable r=%d",r);return false;}
    static const int32_t items[]={101,102,103,104,201,202};static const int32_t amount[]={20,50,200,1000000};uint32_t c[6]={0};
    for(unsigned i=0;i<6;i++)(void)silph_game_read_item(s->game,items[i],&c[i]);
    int pick=-1;int32_t item=0;
    for(unsigned i=0;pick<0&&i<n;i++)if(hp[i]<=0&&(c[5]||c[4])){pick=(int)i;item=c[5]?202:201;}
    for(unsigned i=0;pick<0&&i<n;i++)if(hp[i]>0&&hp[i]<mx[i]){
        const int32_t missing=mx[i]-hp[i];int k=-1;
        for(int j=0;j<4&&k<0;j++)if(c[j]&&amount[j]>=missing)k=j;
        for(int j=3;j>=0&&k<0;j--)if(c[j])k=j;
        if(k>=0){pick=(int)i;item=items[k];}
    }
    if(pick<0){
        unsigned ready=0;for(unsigned i=0;i<n;i++)if(hp[i]>0)ready++;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","party check top=%u ready=%u potions=%u/%u/%u/%u revives=%u/%u",n,ready,c[0],c[1],c[2],c[3],c[4],c[5]);
        s->heal_check_at=now+UINT64_C(60000000000);s->heal_fails=0;return false;
    }
    if(s->heal_fails>=8){__android_log_print(ANDROID_LOG_WARN,"SilphRaid","healing keeps failing; continuing without it");s->heal_check_at=now+UINT64_C(60000000000);s->heal_fails=0;return false;}
    SilphManagedRef bag={0},promise={0};r=silph_game_read_item_bag(s->game,&bag);
    if(!r)r=silph_raid_medicine(s->read,permit,bag,ids[pick],(uint32_t)item,&promise);
    if(!r)r=silph_managed_promise_open(s->heal_promise,silph_managed_promise_size(),s->read,s->managed,promise,&s->identity,++s->sequence,ids[pick],now,"Holoholo.Rpc","PokemonProto");
    drop(s,&promise);drop(s,&bag);
    if(r){s->heal_fails++;s->heal_check_at=now+UINT64_C(3000000000);__android_log_print(ANDROID_LOG_WARN,"SilphRaid","heal request failed pokemon=%llu item=%d err=%d",(unsigned long long)ids[pick],item,r);return false;}
    s->heal_active=1;s->heal_at=now;s->heal_target=ids[pick];
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","healing pokemon=%llu rank=%d item=%d (%s) hp=%d/%d",(unsigned long long)ids[pick],pick+1,item,item>=201?"revive":"potion",hp[pick],mx[pick]);
    return true;
}
static int care(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(s->rpc_active||now<s->next)return 0;
    if(s->capture_finished_at&&now<s->capture_finished_at+UINT64_C(500000000))return 0;
    if(!s->team[0]){raid_load_team(s);
        if(!s->team[0]){if(s->metrics.encounter)s->metrics.encounter=0;
            if(s->metrics.phase==5&&(s->metrics.stage==RS_OPEN||s->metrics.stage==RS_UI))return 0;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","team heal deferred; no pokemon ids");
            return 0;}
    }
    if(s->medicine_active){
        SilphPromiseMetrics metrics={0};int r=silph_managed_promise_poll(s->medicine,&s->identity,now,&metrics);if(r||metrics.state==SILPH_PROMISE_WAITING)return r;
        if(metrics.state!=SILPH_PROMISE_RESOLVED){s->metrics.error=116;phase(s,8,0,0);return 0;}
        SilphManagedRef reply={0};bool fresh=false;uint64_t id=0,hp=0,max=0;
        r=silph_managed_promise_take(s->medicine,&s->identity,now,&reply,&fresh);if(!r&&!fresh)r=SILPH_MANAGED_STALE;
        if(!r)r=scalar(s,reply,"id_",SILPH_READ_U64,&id,false);if(!r)r=scalar(s,reply,"stamina_",SILPH_READ_I32,&hp,false);if(!r)r=scalar(s,reply,"maxStamina_",SILPH_READ_I32,&max,false);
        if(!r&&(id!=s->healing_target||hp<=s->healing_hp||hp>max))r=SILPH_MANAGED_CONTRACT;
        if(!r){s->healing_expected=(uint32_t)hp;s->healing_until=now+UINT64_C(8000000000);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","team medicine confirmed hp=%llu/%llu",(unsigned long long)hp,(unsigned long long)max);}
        drop(s,&reply);int d=silph_managed_promise_close(s->medicine);s->medicine_active=0;s->next=now+UINT64_C(100000000);return r?r:d;
    }
    RaidHealth health[3]={0};int r=0;
    for(unsigned i=0;!r&&i<3;i++){SilphManagedRef pokemon={0};uint64_t hp=0,max=0;r=silph_game_read_owned(s->game,s->team[i],&pokemon);
        if(!r)r=scalar(s,pokemon,"stamina_",SILPH_READ_I32,&hp,false);if(!r)r=scalar(s,pokemon,"maxStamina_",SILPH_READ_I32,&max,false);
        if(!r)health[i]=(RaidHealth){s->team[i],(int32_t)hp,(int32_t)max,true};drop(s,&pokemon);
    }
    if(!r&&s->healing_expected){for(unsigned i=0;i<3;i++)if(health[i].id==s->healing_target){
        if(health[i].hp<(int32_t)s->healing_expected){if(now>=s->healing_until){s->metrics.error=119;phase(s,8,0,0);}return 0;}s->healing_expected=0;
    }}
    RaidCareItems items={0};int32_t *counts[]={&items.potion,&items.super_potion,&items.hyper_potion,&items.max_potion,&items.revive,&items.max_revive};
    const int ids[]={101,102,103,104,201,202};
    for(unsigned i=0;!r&&i<6;i++){uint32_t count=0;r=silph_game_read_item(s->game,ids[i],&count);*counts[i]=(int32_t)count;}
    if(r)return r;RaidCareOrder order={0};memcpy(order.potions,s->config.potion_order,sizeof(order.potions));memcpy(order.revives,s->config.revive_order,sizeof(order.revives));RaidDecision decision;uint32_t kind=raid_care_ordered(s->team,health,&items,&order,true,&decision);
    if(kind==ROCKET_HEALTHY){if(s->preparing){s->preparing=0;s->team_ready=1;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","team ready hp=%d/%d,%d/%d,%d/%d",health[0].hp,health[0].max_hp,health[1].hp,health[1].max_hp,health[2].hp,health[2].max_hp);}
        else if(s->metrics.stage==RS_HEAL||s->metrics.phase!=5)result(s,s->loss_recovery?7:0,true);
        return 0;}
    if(kind==ROCKET_NO_ITEMS){s->metrics.reason=9;phase(s,8,0,0);return 0;}
    if(kind!=ROCKET_HEAL&&kind!=ROCKET_REVIVE)return SILPH_MANAGED_CONTRACT;
    SilphManagedRef bag={0},promise={0};r=silph_game_read_item_bag(s->game,&bag);
    if(!r)r=silph_raid_medicine(s->read,permit,bag,decision.incoming,(uint32_t)decision.item,&promise);
    if(!r)r=silph_managed_promise_open(s->medicine,silph_managed_promise_size(),s->read,s->managed,promise,&s->identity,++s->sequence,decision.incoming,now,"Holoholo.Rpc","PokemonProto");
    if(!r){s->medicine_active=1;s->healing_target=decision.incoming;for(unsigned i=0;i<3;i++)if(health[i].id==decision.incoming){s->healing_hp=(uint32_t)health[i].hp;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","team medicine requested slot=%u item=%d hp=%d/%d",i+1,decision.item,health[i].hp,health[i].max_hp);}}
    drop(s,&promise);drop(s,&bag);return r;
}
static int poll_rpc(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(!s->rpc_active&&!silph_rpc_action_pending(s->rpc))return 0;
    if(!s->rpc_active&&silph_rpc_action_pending(s->rpc))s->rpc_active=1;
    SilphPromiseMetrics p={0};int r=silph_rpc_action_poll(s->rpc,&s->identity,now,&p);
    if((r||p.state==SILPH_PROMISE_WAITING)&&s->rpc_sent_at){
        uint64_t age=now>s->rpc_sent_at?now-s->rpc_sent_at:0;
        if(age>UINT64_C(22000000000)&&(s->rpc_kind==SILPH_RPC_RAID_DETAILS||s->rpc_kind==SILPH_RPC_JOIN_LOBBY||s->rpc_kind==SILPH_RPC_SPIN)){
            if(s->rpc_kind==SILPH_RPC_RAID_DETAILS){s->raid_details_pending=0;s->raid_details_ok=1;}
            raid_cancel_stuck_rpc(s,0,now,"rpc wait timeout");
            return 0;
        }
    }
    if(r&&s->rpc_kind==SILPH_RPC_SET_TEAM&&s->team_setup==1){
        __android_log_print(ANDROID_LOG_WARN,"SilphRaid","account team rpc failed err=%d; continuing without it",r);
        silph_rpc_action_reset(s->rpc);s->rpc_active=0;s->rpc_kind=0;s->team_setup=0;s->team_setup_tries=3;return 0;
    }
    const bool raid_catch_rpc=s->raid_encounter==2&&(s->rpc_kind==SILPH_RPC_ROCKET_CAPTURE||s->rpc_kind==SILPH_RPC_KECLEON_BERRY);
    const bool raid_catch_late=!r&&p.state==SILPH_PROMISE_WAITING&&s->rpc_sent_at&&now>s->rpc_sent_at+UINT64_C(20000000000);
    if(raid_catch_rpc&&((r&&r!=SILPH_MANAGED_NOT_READY)||raid_catch_late||(!r&&p.state!=SILPH_PROMISE_WAITING&&p.state!=SILPH_PROMISE_RESOLVED))){
        __android_log_print(ANDROID_LOG_WARN,"SilphRaid","raid catch rpc op=%u failed err=%d state=%u",(unsigned)s->rpc_kind,r,(unsigned)p.state);
        s->rr_kind=s->rpc_kind;s->rr_code=0;s->rr_err=r?r:SILPH_MANAGED_NOT_READY;s->rr_ready=1;
        silph_rpc_action_reset(s->rpc);s->rpc_active=0;s->rpc_kind=0;s->rpc_sent_at=0;return 0;
    }
    if(r||p.state==SILPH_PROMISE_WAITING)return r;
    if(p.state!=SILPH_PROMISE_RESOLVED)return SILPH_MANAGED_NOT_READY;
    SilphManagedRef reply={0};bool fresh=false;r=silph_rpc_action_take(s->rpc,&s->identity,now,&reply,&fresh);uint64_t code=0;
    if(!r&&!fresh)r=SILPH_MANAGED_STALE;
    const uint32_t kind=s->rpc_kind;
    if(!r)r=scalar(s,reply,kind==SILPH_RPC_ROCKET_CAPTURE||kind==SILPH_RPC_KECLEON_BERRY?"Status":"Result",SILPH_READ_ENUM32,&code,false);
    if(r==SILPH_MANAGED_CONTRACT&&kind==SILPH_RPC_SET_TEAM)r=scalar(s,reply,"status_",SILPH_READ_ENUM32,&code,false);
    if(r==SILPH_MANAGED_CONTRACT&&kind==SILPH_RPC_SET_TEAM)r=scalar(s,reply,"Status",SILPH_READ_ENUM32,&code,true);
    if(r==SILPH_MANAGED_CONTRACT&&kind==SILPH_RPC_SET_TEAM)r=scalar(s,reply,"get_Status",SILPH_READ_ENUM32,&code,true);
    if(r&&kind==SILPH_RPC_SET_TEAM&&s->team_setup==1){
        __android_log_print(ANDROID_LOG_WARN,"SilphRaid","account team reply unreadable err=%d; assuming team set",r);
        drop(s,&reply);silph_rpc_action_reset(s->rpc);s->rpc_active=0;s->rpc_kind=0;s->team_setup=2;return 0;
    }
    if(raid_catch_rpc){
        s->rr_kind=kind;s->rr_code=r?0:code;s->rr_err=r;s->rr_reply=reply;s->rr_ready=1;
        silph_rpc_action_reset(s->rpc);s->rpc_active=0;s->rpc_kind=0;s->rpc_sent_at=0;s->next=now+UINT64_C(50000000);return 0;
    }
    if(!r)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc done op=%u result=%llu",(unsigned)kind,(unsigned long long)code);
    s->metrics.result=(uint32_t)code;
    if(!r&&kind==SILPH_RPC_KECLEON_BERRY){if(code==1){s->active_item=1;s->metrics.berries++;}}
    if(!r&&kind==SILPH_RPC_SPIN){
        s->free_pass_spin_result=(uint32_t)code;s->free_pass_wait_at=now;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","free pass gym spin result=%llu fort=%s",(unsigned long long)code,s->stop.fort);
    }
    uint32_t raid_next=0;
    if(!r&&kind==SILPH_RPC_RAID_DETAILS){
        uint64_t seed=0;int sr=scalar(s,reply,"RaidSeed",SILPH_READ_I64,&seed,false);if(sr==SILPH_MANAGED_CONTRACT)sr=0;
        if(sr==SILPH_MANAGED_CONTRACT||!seed){
            SilphManagedRef rinfo={0};
            if(!fieldref(s,reply,"raid_info_",&rinfo)||!fieldref(s,reply,"RaidInfo",&rinfo)){
                if(rinfo.serial){(void)scalar(s,rinfo,"RaidSeed",SILPH_READ_I64,&seed,false);drop(s,&rinfo);}
            }
        }
        if(!sr&&seed)s->raid_seed=seed;
        uint32_t lp=raid_read_lobby_players(s,reply);
        if(lp>s->lobby_players)s->lobby_players=lp;
        if(code==32||code==2||code==4||code==3||code==6||code==8){
            __android_log_print(ANDROID_LOG_WARN,"SilphRaid","raid details rejected code=%llu; clearing gym target",(unsigned long long)code);
            raid_clear_gym_target(s,now,"raid details unavailable/gym closed");
        }else if(code==7){
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid details code=7 (not in range); wait for map tap");
            s->join_ui_fallback=1;s->metrics.stage=RS_OPEN;s->ui_retry_at=now+UINT64_C(500000000);
        }else if(code==1){
            s->raid_details_pending=0;s->raid_details_ok=1;s->ui_retry_at=now+UINT64_C(300000000);
            raid_next=SILPH_RPC_JOIN_LOBBY;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid details success (code=1); join lobby");
        }else{
            s->raid_details_pending=1;
            __android_log_print(ANDROID_LOG_WARN,"SilphRaid","raid details result=%llu; will retry",(unsigned long long)code);
            s->raid_rpc_step=0;s->ui_retry_at=now+UINT64_C(1500000000);
        }
    }
    if(!r&&kind==SILPH_RPC_JOIN_LOBBY&&(s->ui_joined||s->won)){
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","join lobby rpc result=%llu ignored; joined via game ui",(unsigned long long)code);
    }else if(!r&&kind==SILPH_RPC_JOIN_LOBBY){
        if(raid_rpc_success(code)){s->join_failures=0;s->action_log_at=0;s->raid_details_pending=0;s->raid_rpc_step=2;s->lobby_pokemon_set=0;s->lobby_joined_at=now;
            s->metrics.stage=RS_OPEN;s->ui_seen=0;s->ui_retry_at=now+UINT64_C(50000000);s->teleport_ready=1;
            s->deadline=now+UINT64_C(240000000000);s->ui_started=now;
            (void)raid_ensure_place(s);
            uint32_t lp=raid_read_lobby_players(s,reply);
            if(lp>s->lobby_players)s->lobby_players=lp;
            uint32_t req=raid_min_for_tier(s,s->grunt_character);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","join lobby ok; lobby_players=%u tier=%u min_req=%u",s->lobby_players,s->grunt_character,req);
            s->join_ui_fallback=1;
            raid_load_team(s);
            if(s->team[0]){raid_next=SILPH_RPC_SET_LOBBY;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","join lobby ok; set lobby pokemon rpc");}
            else{s->lobby_pokemon_set=1;if(s->lobby_players>=req){raid_next=SILPH_RPC_START_RAID;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","join lobby ok; start rpc");}}
        }else{s->raid_rpc_step=0;s->raid_details_pending=1;
            if(code==4&&!s->faction_team_attempted){
                s->faction_team_attempted=1;
                raid_next=SILPH_RPC_SET_TEAM;s->metrics.stage=RS_OPEN;s->ui_retry_at=now+UINT64_C(500000000);
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","join lobby result=4; set player team rpc");
            }else if(code==3&&++s->join_failures>=4&&!s->ui_joined){
                raid_done_add(s->stop.fort,"join lobby result=3 x4");
                raid_clear_gym_target(s,now,"raid unavailable (join result=3 x4)");
                s->join_failures=s->action_log_at=0;s->scan_at=now;return 0;
            }else if(code==3){
                raid_try_next_pass(s);
                s->raid_details_pending=0;s->join_ui_fallback=1;s->metrics.stage=RS_UI;s->ui_seen=0;s->ui_taps=0;
                s->ui_retry_at=now+UINT64_C(300000000);
                raid_next=SILPH_RPC_JOIN_LOBBY;
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","join lobby result=3; retry join pass=%u",(unsigned)s->raid_pass_override);
            }else{
                if(code==19||code==3||code==4)raid_try_next_pass(s);
                s->join_failures++;s->action_log_at=now;
                s->ui_retry_at=now+UINT64_C(s->join_failures>4?8000000000:3000000000);
                if(code==19&&s->join_failures>=5){
                    raid_clear_gym_target(s,now,"join lobby result=19 x5");
                    s->join_failures=s->action_log_at=0;s->scan_at=now;return 0;
                }
                if(code==19&&!raid_at_gym(s)){s->latitude=s->stop.latitude;s->longitude=s->stop.longitude;s->dirty=1;phase(s,6,0,now+UINT64_C(2000000000));}
                else{s->latitude=s->stop.latitude;s->longitude=s->stop.longitude;s->dirty=1;s->teleport_ready=1;}
                if(raid_rpc_only(s)){
                    if(code==19){s->raid_details_pending=1;s->metrics.stage=RS_OPEN;s->raid_rpc_step=0;
                        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","join lobby result=19 x%u; retry via GetRaidDetails",(unsigned)s->join_failures);}
                    else{s->raid_details_pending=1;s->raid_rpc_step=0;s->metrics.stage=RS_OPEN;
                        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","join lobby result=%llu; backoff %llums",(unsigned long long)code,(unsigned long long)(s->ui_retry_at>now?(s->ui_retry_at-now)/1000000:0));}
                }else{
                    s->join_ui_fallback=1;s->metrics.stage=RS_UI;s->ui_seen=0;s->ui_taps=0;
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","join lobby result=%llu; gym ui join",(unsigned long long)code);
                    if(s->join_failures<=2){phase(s,6,0,now+UINT64_C(1000000000));s->ui_retry_at=now+UINT64_C(300000000);}
                }
            }
        }
    }
    if(!r&&kind==SILPH_RPC_SET_TEAM&&s->team_setup==1){
        bool ok=code==1||code==2;s->team_setup=ok?2:0;s->team_setup_at=now+UINT64_C(30000000000);
        __android_log_print(ok?ANDROID_LOG_INFO:ANDROID_LOG_WARN,"SilphRaid","account team setup result=%llu %s",(unsigned long long)code,ok?"ok":"failed");
    }else if(!r&&kind==SILPH_RPC_SET_TEAM){
        if(raid_rpc_success_kind(kind,code)){s->join_failures=0;s->join_ui_fallback=0;s->faction_team_attempted=0;s->raid_details_pending=1;
            s->metrics.stage=RS_OPEN;s->ui_seen=0;s->ui_retry_at=now;raid_next=SILPH_RPC_RAID_DETAILS;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","set player team ok; refresh raid details");}
        else{__android_log_print(ANDROID_LOG_INFO,"SilphRaid","set player team result=%llu; try pick team ui",(unsigned long long)code);
            s->join_ui_fallback=1;s->metrics.stage=RS_UI;s->ui_seen=0;s->ui_retry_at=now;}
    }
    if(!r&&kind==SILPH_RPC_SET_LOBBY){
        if(raid_rpc_success(code)){s->lobby_pokemon_set=1;raid_next=SILPH_RPC_START_RAID;s->raid_rpc_step=2;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","lobby pokemon set; start battle");}
        else{s->lobby_pokemon_set=0;s->ui_retry_at=now+UINT64_C(150000000);
            if(raid_rpc_only(s)){s->metrics.stage=RS_OPEN;s->lobby_pokemon_set=1;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","set lobby result=%llu; retry start rpc",(unsigned long long)code);}
            else{s->join_ui_fallback=1;s->ui_seen=0;s->metrics.stage=RS_UI;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","set lobby result=%llu; lobby ui",(unsigned long long)code);}}
    }
    if(!r&&kind==SILPH_RPC_START_RAID){
        if(raid_rpc_success(code)){s->metrics.raids++;s->metrics.stage=RS_BATTLE;s->ui_seen=1;s->battle_started=now;s->raid_rpc_step=3;s->join_ui_fallback=0;s->lobby_joined_at=0;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid battle rpc accepted raids=%u",(unsigned)s->metrics.raids);}
        else{s->raid_rpc_step=2;
            if(code==0&&s->lobby_joined_at&&now<s->lobby_joined_at+UINT64_C(45000000000)){
                s->metrics.stage=RS_OPEN;s->ui_retry_at=now+UINT64_C(500000000);
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","start raid waiting for lobby timer");
            }else if(!s->place.serial){
                s->join_failures++;
                if(s->join_failures<3&&code==0){
                    s->metrics.stage=RS_OPEN;s->join_ui_fallback=0;s->ui_seen=0;
                    s->ui_retry_at=now+UINT64_C(1500000000);
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","start raid result=%llu; retry start rpc (%u)",(unsigned long long)code,(unsigned)s->join_failures);
                }else{
                    s->join_ui_fallback=1;s->ui_seen=0;s->metrics.stage=RS_UI;s->ui_retry_at=now;s->gui_at=0;
                    s->deadline=now+UINT64_C(240000000000);s->ui_started=now;
                    (void)raid_ensure_place(s);
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","start raid result=%llu; lobby battle ui (no place)",(unsigned long long)code);
                }
            }else{s->join_ui_fallback=1;s->ui_seen=0;s->metrics.stage=RS_UI;s->ui_retry_at=now+UINT64_C(800000000);
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","start raid result=%llu; lobby ui retry",(unsigned long long)code);}}
    }
    if(!r&&kind==SILPH_RPC_ROCKET_CAPTURE){
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","catch result=%llu (1=caught 2=escaped 3=fled) balls_left=%u",(unsigned long long)code,s->balls);
        if(code==1){uint64_t owned=0;r=scalar(s,reply,"CapturedPokemonId",SILPH_READ_U64,&owned,false);if(!r&&!owned)r=SILPH_MANAGED_CONTRACT;
            if(!r){s->metrics.captured=owned;s->metrics.catches++;s->metrics.shinies+=s->shiny;s->capture_finished_at=now;if(!raid_capture_watch(&s->record_gate,owned,now)||!raid_capture_watch(&s->capture_gate,owned,now))r=SILPH_MANAGED_CONTRACT;s->result_scan_at=0;s->result_blocked=0;uint64_t xp=0,dust=0;int rewards=silph_capture_rewards(s->read,s->managed,reply,&xp,&dust);
                if(rewards)s->metrics.reward_error=(uint32_t)rewards;else{s->metrics.xp+=xp;s->metrics.stardust+=dust;}
                if(!r)r=finish_encounter(s,permit,0);
                if(!r)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","capture confirmed; visual reward sequence bypassed request=%llu",(unsigned long long)s->metrics.request);
            }
        }else if(code==3||code==0){r=finish_encounter(s,permit,code==3?2:3);}else{s->active_item=0;s->berry_attempted=0;}
    }
    drop(s,&reply);int reset=silph_rpc_action_reset(s->rpc);s->rpc_active=0;s->rpc_kind=0;s->rpc_sent_at=0;s->next=now+UINT64_C(50000000);
    if(!r&&raid_next){
        if(raid_next==SILPH_RPC_JOIN_LOBBY||raid_next==SILPH_RPC_RAID_DETAILS||raid_next==SILPH_RPC_SET_LOBBY||raid_next==SILPH_RPC_START_RAID||raid_next==SILPH_RPC_SET_TEAM){
            if(raid_next==SILPH_RPC_RAID_DETAILS)s->raid_details_pending=1;
            else if(raid_next==SILPH_RPC_JOIN_LOBBY)s->raid_details_pending=0;
            if(raid_rpc_only(s)){
                int chain=raid_rpc_send(s,permit,raid_next,now);
                if(chain)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc chain op=%u err=%d",raid_next,chain);
                if(!r)r=chain;
            }else{
                s->ui_retry_at=now+UINT64_C(raid_next==SILPH_RPC_JOIN_LOBBY?2000000000:30000000);
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc defer op=%u until next tick",(unsigned)raid_next);
            }
        }else{
            int chain=raid_rpc_send(s,permit,raid_next,now);
            if(chain&&raid_next==SILPH_RPC_SET_LOBBY){
                s->ui_retry_at=now+UINT64_C(150000000);
                if(raid_rpc_only(s)){s->lobby_pokemon_set=1;s->metrics.stage=RS_OPEN;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","set lobby build failed err=%d; start rpc",chain);}
                else{s->join_ui_fallback=1;s->ui_seen=0;s->metrics.stage=RS_UI;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","set lobby build failed err=%d; lobby ui",chain);}
            }
            if(chain&&raid_next!=SILPH_RPC_SET_LOBBY)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid rpc chain op=%u err=%d",raid_next,chain);
            if(!r)r=chain;
        }
    }
    return r?r:reset;
}
static bool raid_bag_full(const SilphGameSnapshot *snapshot){
    return snapshot&&snapshot->max_items>0&&snapshot->item_count>=snapshot->max_items;
}
static int raid_bag_discard_item(Raid *s,unsigned index,int32_t *item,uint32_t *floor){
    static const struct {int32_t item;uint32_t floor;} defs[]={
        {101,0},{102,0},{103,3},{104,0},{201,0},{202,0},{701,5},{703,5},{705,5},{706,5},{708,5},{111,0},{112,0},{110,0}
    };
    if(!item||!floor)return SILPH_MANAGED_ARGUMENT;
    if(s->config.limit_count&&index<s->config.limit_count){*item=(int32_t)s->config.limits[index].item;*floor=s->config.limits[index].floor;return *item?0:SILPH_MANAGED_ARGUMENT;}
    index%=sizeof(defs)/sizeof(defs[0]);*item=defs[index].item;*floor=defs[index].floor;return 0;
}
static int clean(Raid *s,const SilphSchedulePermit *permit,const SilphGameSnapshot *snapshot,uint64_t now,bool *busy){
    *busy=false;
    bool force=raid_bag_full(snapshot);
    if(!(s->config.flags&SILPH_CFG_CLEAN)&&!force)return 0;
    if(!s->clean_opened&&(s->metrics.phase==5||s->metrics.phase==6))return 0;
    if(!s->clean_opened&&now<s->clean_due)return 0;
    unsigned limit=s->config.limit_count?s->config.limit_count:14u;
    int r=0;if(!s->clean_opened){
        unsigned max_batches=force?12u:4u;
        if(s->clean_index>=limit||s->clean_batches>=max_batches){
            bool stuck=force&&s->clean_round_items&&snapshot->item_count>=s->clean_round_items;
            s->clean_index=s->clean_batches=0;s->clean_due=now+(stuck?UINT64_C(60000000000):force?UINT64_C(3000000000):UINT64_C(15000000000));
            if(force)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","item bag clean round done; items=%u/%u%s",(unsigned)snapshot->item_count,(unsigned)snapshot->max_items,stuck?" (no progress; backing off 60s)":"");
            if(stuck&&!s->clean_dumped){
                static const int32_t ids[]={1,2,3,4,5,101,102,103,104,201,202,301,401,402,501,502,503,504,602,701,702,703,704,705,706,707,708,709,801,901,902,903,1101,1102,1103,1104,1105,1106,1107,1201,1202,1203,1204,1301,1401,1402,1404,1408,1501,1502,1503,1600,1601,1602,1603,1604,1605,1606,1607};
                char line[900];size_t n=0;line[0]=0;
                for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0])&&n<sizeof(line)-24;i++){uint32_t c=0;if(!silph_game_read_item(s->game,ids[i],&c)&&c)n+=(size_t)snprintf(line+n,sizeof(line)-n," %d:%u",(int)ids[i],(unsigned)c);}
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","item bag contents%s",line);s->clean_dumped=1;
            }
            s->clean_round_items=force?snapshot->item_count:0;
            return 0;
        }
        int32_t item=0;uint32_t fl=0;r=raid_bag_discard_item(s,s->clean_index,&item,&fl);
        if(r||!item){s->clean_index++;return 0;}
        r=silph_maintenance_open(s->maintenance,silph_maintenance_size(),s->read,s->managed,s->game,&s->identity);
        if(r==SILPH_MANAGED_STATE){(void)silph_maintenance_close(s->maintenance);r=silph_maintenance_open(s->maintenance,silph_maintenance_size(),s->read,s->managed,s->game,&s->identity);}
        if(!r){s->clean_opened=1;s->clean_batches++;r=silph_maintenance_discard(s->maintenance,permit,snapshot,item,fl,50,now);
            if(!r)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","item bag discard item=%d floor=%u",(int)item,(unsigned)fl);}
    }
    SilphMaintenanceMetrics m={0};if(!r)r=silph_maintenance_poll(s->maintenance,&s->identity,now,&m);
    if(!r&&(m.state==3||m.state==4)){s->metrics.deleted+=m.discarded;if(!m.discarded||m.after<=m.floor)s->clean_index++;r=silph_maintenance_close(s->maintenance);if(!r)s->clean_opened=0;}
    *busy=!!s->clean_opened;return r;
}
static int capture_owner_match(Raid *s,SilphManagedRef gui,uint64_t expected,bool *match){
    *match=false;if(!istype(s,gui,"Niantic.Holoholo.UI.Pokemon","PokemonInfoGuiController"))return 0;
    uint64_t state=0,id=0,post=0;SilphManagedRef pokemon={0},data={0};int r=scalar(s,gui,"currentState",SILPH_READ_ENUM32,&state,false);
    if(!r&&state!=2)return 0;
    if(!r)r=callref(s,gui,"get_CurrentPokemon",&pokemon);if(!r&&pokemon.serial)r=scalar(s,pokemon,"id_",SILPH_READ_U64,&id,false);
    if(!r&&id==expected){r=fieldref(s,gui,"egbx",&data);if(!r&&data.serial)r=scalar(s,data,"IsPostEncounter",SILPH_READ_BOOL,&post,false);}
    if(!r)*match=id==expected&&post;int a=drop(s,&data),b=drop(s,&pokemon);return r?r:a?a:b;
}
static int capture_record_owner(Raid *s,const SilphSchedulePermit *permit,SilphManagedRef *out){
    const uint32_t layers[]={300,200,320,100,0,350,400,500,600,700,800,900,1000,1100};int r=0;*out=(SilphManagedRef){0};
    for(unsigned layer=0;!r&&!out->serial&&layer<14;layer++){
        SilphReadArgument argument={.kind=SILPH_READ_ENUM32,.value.bits=layers[layer]};SilphReadValue set={0},value={0};SilphManagedRef iterator={0},current={0};
        r=silph_raid_invoke(s->read,permit,RK_LAYER,s->gui,&argument,1,&set);if(!r&&!set.reference.serial)continue;
        if(!r){r=silph_read_call(s->read,set.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&value);if(r==SILPH_MANAGED_CONTRACT)r=silph_read_call(s->read,set.reference,"GetEnumerator",SILPH_READ_REFERENCE,NULL,0,&value);iterator=value.reference;}
        for(unsigned i=0;!r&&i<128;i++){
            uint64_t more=0;r=scalar(s,iterator,"MoveNext",SILPH_READ_BOOL,&more,true);if(r||!more)break;
            r=callref(s,iterator,"get_Current",&current);bool match=false;if(!r)r=capture_owner_match(s,current,s->capture_gate.captured,&match);
            if(!r&&match){*out=current;current=(SilphManagedRef){0};break;}int freed=drop(s,&current);if(!r)r=freed;
        }
        SilphManagedRef *refs[]={&current,&iterator,&set.reference};for(unsigned i=0;i<3;i++){int freed=drop(s,refs[i]);if(!r)r=freed;}
    }return r;
}
static int capture_result_ui(Raid *s,const SilphSchedulePermit *permit,uint64_t now,bool *blocked){
    *blocked=!!s->result_blocked;if(!s->capture_gate.captured)return 0;if(now<s->result_scan_at)return 0;s->result_scan_at=now+UINT64_C(250000000);
    bool previous=*blocked;*blocked=false;SilphReadValue top={0};SilphManagedRef pokemon={0},data={0};uint64_t id=0,post=0;
    int r=silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&top);
    bool transition=!r&&(istype(s,top.reference,"Niantic.Holoholo.UI.Transitions","ScreenFadeGuiController")||istype(s,top.reference,"Niantic.Holoholo.Encounter.ArPlus","ArPlusSafetyPromptGuiController"));
    if(transition){*blocked=true;if(!s->transition_since){s->transition_since=now;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","post-capture transition blocked; waiting for game UI");}
        if(now-s->transition_since>=UINT64_C(20000000000)){s->metrics.reason=7;phase(s,8,0,0);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","post-capture transition timed out; requesting game recovery");}}
    else if(!r)s->transition_since=0;
    if(!r&&istype(s,top.reference,"Niantic.Holoholo.UI.Pokemon","PokemonInfoGuiController")){
        r=callref(s,top.reference,"get_CurrentPokemon",&pokemon);
        if(!r&&pokemon.serial)r=scalar(s,pokemon,"id_",SILPH_READ_U64,&id,false);
        if(!r&&id==s->capture_gate.captured){r=fieldref(s,top.reference,"egbx",&data);if(!r&&data.serial)r=scalar(s,data,"IsPostEncounter",SILPH_READ_BOOL,&post,false);}
        if(!r&&id==s->capture_gate.captured&&post){
            *blocked=true;uint32_t decision=raid_capture_result(&s->capture_gate,id,true,now);
            if(decision==ROCKET_RESULT_DISMISS){SilphReadArgument expected={.kind=SILPH_READ_U64,.value.bits=id};r=action(s,permit,RK_CAPTURE_INFO,top.reference,&expected,1);
                if(r==SILPH_MANAGED_NOT_READY)r=0;
                else if(!r)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","captured result close requested attempt=%u request=%llu",s->capture_gate.attempts,(unsigned long long)s->metrics.request);
            }else if(decision==ROCKET_RESULT_EXPIRED){s->metrics.error=120;phase(s,8,0,0);}
        }
    }
    if(!r&&istype(s,top.reference,"Niantic.Holoholo.UI.Pokemon","PokemonSizeRecordBreakGuiController")){
        SilphManagedRef owner={0};r=capture_record_owner(s,permit,&owner);
        if(!r&&owner.serial){*blocked=true;uint32_t decision=raid_capture_result(&s->record_gate,s->capture_gate.captured,true,now);
            if(decision==ROCKET_RESULT_DISMISS){SilphReadArgument args[]={{.kind=SILPH_READ_U64,.value.bits=s->capture_gate.captured},{.kind=SILPH_READ_REFERENCE,.value.reference=owner}};r=action(s,permit,RK_CAPTURE_RECORD,top.reference,args,2);if(r==SILPH_MANAGED_NOT_READY)r=0;else if(!r)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","capture size record dismissed request=%llu",(unsigned long long)s->metrics.request);}
            else if(decision==ROCKET_RESULT_EXPIRED){s->metrics.error=120;phase(s,8,0,0);}
        }
        int freed=drop(s,&owner);if(!r)r=freed;
    }
    if(!r&&!*blocked&&now>=s->capture_gate.until)memset(&s->capture_gate,0,sizeof(s->capture_gate));
    if(!r&&previous&&!*blocked){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","captured result no longer blocking request=%llu",(unsigned long long)s->metrics.request);
        if(s->metrics.phase==3){s->deadline=now+UINT64_C(18000000000);s->scan_at=now;}
        else if(s->metrics.phase==5&&(s->metrics.stage==RS_OPEN||s->metrics.stage==RS_UI)){s->ui_started=now;s->ui_retry_at=now;s->deadline=now+UINT64_C(240000000000);}
    }
    s->result_blocked=*blocked;SilphManagedRef *refs[]={&data,&pokemon,&top.reference};for(unsigned i=0;i<3;i++){int freed=drop(s,refs[i]);if(!r)r=freed;}return r;
}
static int ready_to_open(Raid *s,bool require_range,bool *ready){
    *ready=false;double lat=0,lon=0;int r=silph_game_read_rocket_position(s->game,&lat,&lon);
    if(r==SILPH_MANAGED_NOT_READY)return r;
    if(r)return r;
    double dy=(lat-s->stop.latitude)*111195,dx=remainder(lon-s->stop.longitude,360.0)*111195*cos(s->stop.latitude*0.017453292519943295);
    double limit=s->stop.incident[0]?12.0:120.0;
    if(hypot(dx,dy)>limit)return 0;if(!require_range){*ready=true;return 0;}uint64_t in_range=0;
    r=scalar(s,s->place,"get_IsPlayerInRange",SILPH_READ_BOOL,&in_range,true);
    if(r==SILPH_MANAGED_CONTRACT)r=scalar(s,s->place,"get_IsIncidentActiveAndInRange",SILPH_READ_BOOL,&in_range,true);
    if(r==SILPH_MANAGED_CONTRACT){*ready=true;r=0;}
    else if(!r)*ready=!!in_range;return r;
}
static bool raid_startup_dialog_name(const char *name){
    if(!name||!name[0])return false;
    return strstr(name,"Welcome")||strstr(name,"FirstTimeUser")||strstr(name,"Referral")||strstr(name,"Onboarding")||strstr(name,"Willow")
        ||strstr(name,"Safety")||strstr(name,"Surroundings")||strstr(name,"Courteous")||strstr(name,"Aware")||strstr(name,"ArPlus")
        ||strstr(name,"News")||strstr(name,"Announcement")||strstr(name,"Details")||strstr(name,"Friend")||strstr(name,"GlobalError")
        ||strstr(name,"ItemInventory")||strstr(name,"ItemBag")||strstr(name,"StampRally")||strstr(name,"Rally")||strstr(name,"DailyReward")
        ||strstr(name,"ReturningPlayer")||strstr(name,"Tutorial")||strstr(name,"Reward")||strstr(name,"Popup")||strstr(name,"Modal")||strstr(name,"Dialog")
        ||strstr(name,"Dialogue")||strstr(name,"Conversation")||strstr(name,"Story")||strstr(name,"Npc")||strstr(name,"Narrative")
        ||strstr(name,"Speech")||strstr(name,"Interaction")||strstr(name,"Trainer")||strstr(name,"Power")||strstr(name,"BootSequence")||strstr(name,"Spot")
        ||strstr(name,"Buddy")||strstr(name,"Activity")||strstr(name,"Adventure")||strstr(name,"Toast")||strstr(name,"Notification")||strstr(name,"Dismiss");
}
static void raid_reanchor_config(Raid *s){
    if(!s||!isfinite(s->config.raid_anchor_lat)||!isfinite(s->config.raid_anchor_lon))return;
    s->latitude=s->config.raid_anchor_lat;s->longitude=s->config.raid_anchor_lon;
    if(!s->source[0])memcpy(s->source,"raid-anchor",12);
    s->dirty=1;
}
static int raid_dismiss_startup_ref(Raid *s,const SilphSchedulePermit *permit,SilphManagedRef gui,const char *label){
    if(!s||!gui.serial)return 0;
    char name[128]={0};
    if(silph_raid_class(s->read,gui,name,sizeof(name)))return 0;
    static uint64_t last_log_time;
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    uint64_t now_ms=(uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
    if(now_ms-last_log_time>2000&&name[0]&&!strstr(name,"MapGui")&&!strstr(name,"MapExplore")){
        last_log_time=now_ms;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","startup candidate gui (%s) name=%s matched=%d",label?label:"?",name,raid_startup_dialog_name(name));
    }
    if(!raid_startup_dialog_name(name))return 0;
    int dismissed=action(s,permit,RK_RAID_GUI,gui,NULL,0);
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","startup dialog dismiss (%s) gui=%s result=%d",label?label:"?",name,dismissed);
    if(strstr(name,"GlobalError"))raid_reanchor_config(s);
    return dismissed==SILPH_MANAGED_NOT_READY?0:dismissed;
}
static int raid_pump_startup_guis(Raid *s,const SilphSchedulePermit *permit,uint64_t now){
    if(!s||!s->gui.serial||(s->metrics.phase!=1&&s->metrics.phase!=3))return 0;
    int r=0;
    SilphReadValue top={0};
    if(!silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&top)&&top.reference.serial)
        r=raid_dismiss_startup_ref(s,permit,top.reference,"top");
    int freed=drop(s,&top.reference);if(!r)r=freed;
    SilphManagedRef stack={0};
    if(!fieldref(s,s->gui,"dddt",&stack)&&stack.serial){
        SilphManagedRef iterator={0},current={0};SilphReadValue value={0};
        int sr=silph_read_call(s->read,stack,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&value);
        if(sr==SILPH_MANAGED_CONTRACT)sr=silph_read_call(s->read,stack,"GetEnumerator",SILPH_READ_REFERENCE,NULL,0,&value);
        iterator=value.reference;
        for(unsigned i=0;!r&&i<48;i++){
            uint64_t more=0;if(scalar(s,iterator,"MoveNext",SILPH_READ_BOOL,&more,true)||!more)break;
            if(callref(s,iterator,"get_Current",&current))continue;
            int dr=raid_dismiss_startup_ref(s,permit,current,"stack");if(dr&&!r)r=dr;
            drop(s,&current);
        }
        drop(s,&iterator);
    }
    drop(s,&stack);
    const uint32_t layers[]={0,100,200,300,320,350,400,500,600,700,800,900,1000,1100};
    for(unsigned i=0;i<sizeof(layers)/sizeof(layers[0]);i++){
        SilphReadArgument argument={.kind=SILPH_READ_ENUM32,.value.bits=layers[i]};SilphReadValue value={0};
        if(silph_raid_invoke(s->read,permit,RK_LAYER,s->gui,&argument,1,&value))continue;
        SilphManagedRef iterator={0},current={0};SilphReadValue enumv={0};
        int sr=silph_read_call(s->read,value.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&enumv);
        if(sr==SILPH_MANAGED_CONTRACT)sr=silph_read_call(s->read,value.reference,"GetEnumerator",SILPH_READ_REFERENCE,NULL,0,&enumv);
        iterator=enumv.reference;
        for(unsigned j=0;j<32;j++){
            uint64_t more=0;if(scalar(s,iterator,"MoveNext",SILPH_READ_BOOL,&more,true)||!more)break;
            if(callref(s,iterator,"get_Current",&current))continue;
            int dr=raid_dismiss_startup_ref(s,permit,current,"layer");if(dr&&!r)r=dr;
            drop(s,&current);
        }
        drop(s,&iterator);drop(s,&value.reference);
    }
    (void)now;return 0;
}
int silph_raid_session_tick(void *memory,const SilphSchedulePermit *permit,const SilphGameSnapshot *snapshot,uint64_t now,uint64_t wall,SilphRaidMetrics *out){
    Raid *s=memory;if(!s||!s->opened||!out||!snapshot||!silph_schedule_permitted(permit,true))return SILPH_MANAGED_STATE;
    (void)raid_open_rpc(s,s->read,s->managed,s->game,&s->identity);
    (void)raid_open_services(s,s->game);
    *out=s->metrics;
    if(snapshot->account_hi!=s->identity.account_hi||snapshot->account_lo!=s->identity.account_lo){
        if(!s->identity.account_hi&&!s->identity.account_lo&&(snapshot->account_hi||snapshot->account_lo)){
            s->identity.account_hi=snapshot->account_hi;s->identity.account_lo=snapshot->account_lo;
            if(s->metrics.phase==1)s->dirty=1;
        }else{*out=s->metrics;return SILPH_MANAGED_STALE;}
    }
    silph_raid_flow_permit(permit);
    if(!s->visual_attempted){s->visual_attempted=1;int tuning=silph_game_read_raid_service(s->game,8,&s->visual_properties);if(!tuning)tuning=silph_raid_battle_visuals(s->read,permit,s->visual_properties,s->visual_saved,false);if(!tuning)s->visual_tuned=1;else drop(s,&s->visual_properties);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","battle transition timing prepared result=%d",tuning);}
    int r=0;for(unsigned i=0;i<8;i++){SilphKecleonCommand c;int popped=silph_session_link_pop(s->link,&c);if(popped==SILPH_SCHEDULE_BUSY||popped==SILPH_SCHEDULE_STATE)break;if(popped){r=popped;break;}r=command(s,&c,now);if(r)break;}
    if(!r&&s->metrics.phase==1&&s->dirty)silph_game_read_position(s->game,&s->latitude,&s->longitude);
    if(!r&&(s->metrics.phase==1||s->metrics.phase==3)&&s->gui.serial&&now>=s->inventory_at){
        int pumped=raid_pump_startup_guis(s,permit,now);
        if(pumped&&!r)r=pumped==SILPH_MANAGED_NOT_READY?0:pumped;
        if(pumped)s->dirty=1;
        s->inventory_at=now+UINT64_C(250000000);
    }
    if(!r&&s->metrics.phase==5&&s->gui.serial&&now>=s->inventory_at){
        SilphReadValue top={0};char gui_name[128]={0};
        int top_read=silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&top);
        if(!top_read&&top.reference.serial){static char top_logged[128];char tn[128]={0};
            if(!silph_raid_class(s->read,top.reference,tn,sizeof(tn))){
                if(strcmp(top_logged,tn)){snprintf(top_logged,sizeof(top_logged),"%s",tn);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","top gui now=%s stage=%u won=%u",tn,s->metrics.stage,(unsigned)s->won);}
                if(!strcmp(tn,"PokemonInfoGuiController")&&s->metrics.stage!=RS_CAPTURE&&!s->catch_shown_at&&now>=s->gui_at){
                    s->gui_at=now+UINT64_C(1500000000);
                    int closed=action(s,permit,RK_GUI_CLOSE,top.reference,NULL,0);
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","caught pokemon screen close result=%d",closed);
                }
                bool approach=!strcmp(tn,"RaidApproachUIController")&&!s->won&&!s->lobby_seen&&!s->ui_joined&&!s->claim_mode&&s->raid_pass_override==1401;
                if(!approach)s->toast_seen_at=0;
                else{
                    if(!s->toast_seen_at){s->toast_seen_at=now;s->toast_taps=0;}
                    if(now-s->toast_seen_at>UINT64_C(2000000000)&&s->toast_taps<3&&now>=s->toast_tap_at){
                        s->toast_taps++;s->toast_tap_at=now+UINT64_C(4000000000);s->tap_request=1;s->dirty=1;
                        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","free pass popup: requesting one screen tap (%u/3)",s->toast_taps);
                    }
                }
            }}
        if(!top_read&&!silph_raid_class(s->read,top.reference,gui_name,sizeof(gui_name))&&(strstr(gui_name,"Friend")||strstr(gui_name,"Courteous")||strstr(gui_name,"RaidPass")||strstr(gui_name,"FreeRaid")||strstr(gui_name,"Surroundings")||strstr(gui_name,"Aware")||strstr(gui_name,"ItemInventory")||strstr(gui_name,"ItemBag"))){
            int dismissed=action(s,permit,RK_RAID_GUI,top.reference,NULL,0);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid dialog dismiss gui=%s result=%d",gui_name,dismissed);
        }
        s->inventory_at=now+UINT64_C(400000000);
        int freed=drop(s,&top.reference);if(!r)r=freed;
    }
    if(s->metrics.phase!=1)s->phase1_at=0;
    else if(!r){
        bool empty=!raid_map_ready(snapshot)||snapshot->map_stops==0;
        if(!s->phase1_at)s->phase1_at=now;
        uint64_t waited=now-s->phase1_at;
        if(s->gui.serial&&now>=s->toast_tap_at){
            SilphReadValue top={0};char tn[128]={0};
            if(!silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&top)&&top.reference.serial&&!silph_raid_class(s->read,top.reference,tn,sizeof(tn))&&strstr(tn,"Referral")){
                s->toast_tap_at=now+UINT64_C(4000000000);s->tap_request=2;s->dirty=1;s->phase1_at=now;
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","welcome back screen %s: requesting CONTINUE tap",tn);
            }
            drop(s,&top.reference);
        }
        if(now>=s->phase1_log_at&&waited>UINT64_C(15000000000)){s->phase1_log_at=now+UINT64_C(15000000000);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","startup waiting for map ready=%d stops=%u waited=%llus",raid_map_ready(snapshot),(unsigned)snapshot->map_stops,(unsigned long long)(waited/1000000000));}
        if((empty&&waited>UINT64_C(60000000000))||waited>UINT64_C(180000000000)){
            __android_log_print(ANDROID_LOG_WARN,"SilphRaid","map never loaded (ready=%d stops=%u waited=%llus); restarting game",raid_map_ready(snapshot),(unsigned)snapshot->map_stops,(unsigned long long)(waited/1000000000));
            s->phase1_at=0;s->metrics.reason=10;phase(s,8,0,0);
        }
    }
    if(!r&&s->metrics.phase==1&&raid_map_ready(snapshot)&&snapshot->map_stops>=25){
        if(s->stop.fort[0]&&s->raid_seed){
            if(s->abandon_fort[0]&&now<s->abandon_until&&!strcmp(s->stop.fort,s->abandon_fort)){
                memset(s->stop.fort,0,sizeof(s->stop.fort));s->raid_seed=0;s->scan_at=now;
            }else if(!raid_gym_still_joinable(s)){
                raid_clear_gym_target(s,now,"resume blocked; raid not joinable");
            }
            if(s->stop.fort[0]&&s->raid_seed){
                s->scan_at=now;s->map_at=0;phase(s,3,0,now+UINT64_C(120000000000));
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","map ready; resume gym fort=%s",s->stop.fort);
            }else if(isfinite(s->config.raid_anchor_lat)&&isfinite(s->config.raid_anchor_lon)){
                s->latitude=s->config.raid_anchor_lat;s->longitude=s->config.raid_anchor_lon;
                if(!s->source[0])memcpy(s->source,"raid-anchor",12);
                s->map_at=0;s->scan_at=now+UINT64_C(3000000000);s->dirty=1;phase(s,3,0,now+UINT64_C(120000000000));
            }
        }else{
            if(isfinite(s->config.raid_anchor_lat)&&isfinite(s->config.raid_anchor_lon)){s->latitude=s->config.raid_anchor_lat;s->longitude=s->config.raid_anchor_lon;}
            if(!s->source[0])memcpy(s->source,"raid-anchor",12);
            s->map_at=0;s->scan_at=now+UINT64_C(3000000000);s->dirty=1;phase(s,3,0,now+UINT64_C(120000000000));
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","map ready; anchor teleport armed stops=%u",(unsigned)snapshot->map_stops);
        }
    }
    if(!r&&raid_bag_full(snapshot)){
        if(now>=s->inventory_at){
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","item bag full (%u/%u); discarding before raid",(unsigned)snapshot->item_count,(unsigned)snapshot->max_items);
            s->inventory_at=now+UINT64_C(5000000000);
        }
        s->clean_due=0;
        if(s->metrics.phase==5&&s->metrics.stage==RS_OPEN&&s->rpc_active)s->scan_at=now+UINT64_C(500000000);
    }
    if(!r&&(s->rpc_active||silph_rpc_action_pending(s->rpc)))r=poll_rpc(s,permit,now);
    bool busy=false;if(!r)r=clean(s,permit,snapshot,now,&busy);if(!r&&!busy&&!silph_rpc_action_pending(s->rpc))r=poll_rpc(s,permit,now);
    bool closing=false;if(!r)r=capture_result_ui(s,permit,now,&closing);
    if(!r&&s->metrics.phase==5&&s->stop.fort[0]&&s->gym_first_tap_at&&!s->raid_screen_seen&&!s->ui_joined&&!s->won&&!s->lobby_seen&&s->metrics.stage<RS_BATTLE
        &&now-s->gym_first_tap_at>UINT64_C(25000000000)&&(!s->top_name[0]||!strcmp(s->top_name,"MainMenuGuiController"))){
        __android_log_print(ANDROID_LOG_WARN,"SilphRaid","gym tapped 25s ago but no raid screen opened (raid probably ended); moving on fort=%s",s->stop.fort);
        raid_done_add(s->stop.fort,"no raid screen after tap");
        raid_clear_gym_target(s,now,"gym has no raid screen");
        s->gym_first_tap_at=0;
    }
    if(!r&&s->metrics.phase==5&&s->stop.fort[0]&&s->gym_press_at&&!s->raid_screen_seen&&!s->ui_joined&&!s->won&&now-s->gym_press_at>UINT64_C(6000000000)){
        SilphReadValue top={0};char name[128]={0};
        if(!silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&top)&&top.reference.serial){
            (void)silph_raid_class(s->read,top.reference,name,sizeof(name));
            int closed=strcmp(name,"MainMenuGuiController")?action(s,permit,RK_GUI_CLOSE,top.reference,NULL,0):0;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym battle press opened no raid screen top=%s close=%d; gym has no joinable raid",name[0]?name:"?",closed);
            if(name[0])memcpy(s->top_name,name,strlen(name)+1);
            drop(s,&top.reference);
        }
        raid_done_add(s->stop.fort,"no raid screen");
        raid_clear_gym_target(s,now,"no joinable raid at gym");
        r=0;
    }
    if(!r&&!closing&&((s->metrics.phase==3&&!silph_rpc_action_pending(s->rpc))||(s->metrics.phase==5&&s->metrics.stage==RS_OPEN&&!s->rpc_active&&s->raid_rpc_step<3&&!busy))){
        if(s->metrics.phase==3&&!s->stop.fort[0]&&(strstr(s->top_name,"Approach")||strstr(s->top_name,"UseRaidTicket")||strstr(s->top_name,"BattlePrep")||strstr(s->top_name,"Combat")||strstr(s->top_name,"Gym")))r=ui(s,permit,now);
        else if(s->metrics.phase==3&&!s->stop.fort[0]&&raid_heal_party(s,permit,now,&r)){}
        else if(s->metrics.phase==3&&!s->stop.fort[0]&&raid_team_needed(s,now)){
            SilphRequestInput input={0};r=raid_team_rpc_input(s,&input);
            if(!r)r=rpc_send(s,permit,SILPH_RPC_SET_TEAM,&input,now);
            s->team_setup_tries++;s->team_setup_at=now+UINT64_C(30000000000);
            if(!r)s->team_setup=1;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","account has no team; setting team=%d err=%d try=%u",(int)input.item,r,(unsigned)s->team_setup_tries);
            if(r==SILPH_MANAGED_NOT_READY)r=0;
        }
        else if(s->metrics.phase==3){if(!s->stop.fort[0])r=raid_bind_gym_from_gui(s,permit,now);if(!r){refresh_map(s,permit,snapshot,now);r=stop_at(s,snapshot,now,wall);}}
        else if(s->stop.fort[0]&&!raid_at_gym(s)&&!s->rpc_active&&!s->raid_rpc_step
            &&(!s->gps_settled_at||now-s->gps_settled_at>UINT64_C(45000000000))){
            s->teleport_ready=0;s->gps_settled_at=0;s->latitude=s->stop.latitude;s->longitude=s->stop.longitude;s->dirty=1;phase(s,6,0,now+UINT64_C(15000000000));}
    }
    if(s->metrics.phase==5&&!s->rpc_active&&!silph_rpc_action_pending(s->rpc)){
        bool want_join=(s->raid_seed>1)&&(s->join_kickoff_pending||(s->join_ui_fallback&&s->raid_rpc_step<2&&!s->lobby_joined_at&&(s->metrics.stage==RS_OPEN||s->approach_at)));
        if(want_join&&now>=s->approach_pump_at&&now>=s->ui_retry_at){
            s->approach_pump_at=now+UINT64_C(1500000000);
            int sent=raid_rpc_kickoff(s,permit,now);
            if(s->rpc_active||(sent&&!raid_rpc_soft_wait(sent)))s->join_kickoff_pending=0;
            if(sent&&!raid_rpc_soft_wait(sent)&&!r)r=sent;
        }
    }
    if(s->claim_mode&&!s->claim_ok&&!s->won&&s->metrics.phase==5&&!strcmp(s->top_name,"RaidApproachUIController")&&s->gui.serial){
        SilphReadValue top={0};char label[64]={0};int lr=SILPH_MANAGED_ARGUMENT;
        if(!silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&top)&&top.reference.serial){
            SilphManagedRef lbl={0};lr=fieldref(s,top.reference,"inPersonJoinButtonLabelText",&lbl);
            if(!lr&&lbl.serial)lr=text(s,lbl,"get_text",label,sizeof(label),true);
            drop(s,&lbl);
        }
        bool battle=!lr&&(strcasestr(label,"battle")||strcasestr(label,"join"));
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","revisit approach screen gui=%s button=\"%s\" read=%d -> %s",s->top_name,label,lr,battle||lr?"catch gone; leaving without using a pass":"catch available");
        if(battle||lr){
            if(top.reference.serial)(void)action(s,permit,RK_GUI_CLOSE,top.reference,NULL,0);
            drop(s,&top.reference);
            raid_claim_set(NULL,0,0);s->claim_mode=0;
            raid_done_add(s->stop.fort,"won raid catch no longer offered");
            raid_clear_gym_target(s,now,"won raid catch no longer offered");
            return 0;
        }
        drop(s,&top.reference);s->claim_ok=1;
    }
    if(s->claim_mode&&s->claim_ok&&!s->won&&s->metrics.phase==5&&s->top_name[0]&&strcmp(s->top_name,"MainMenuGuiController")
        &&(strstr(s->top_name,"Raid")||strstr(s->top_name,"Encounter")||strstr(s->top_name,"Reward"))){
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","won raid screen opened gui=%s; running post-win flow",s->top_name);
        s->won=1;s->metrics.stage=RS_ENCOUNTER;s->post_battle_at=now;s->next=now+UINT64_C(300000000);
        if(s->deadline&&s->deadline<now+UINT64_C(240000000000))s->deadline=now+UINT64_C(240000000000);
    }
    if(s->won&&s->metrics.phase==5&&(s->metrics.stage==RS_UI||s->metrics.stage==RS_OPEN))s->metrics.stage=RS_ENCOUNTER;
    if(!r&&!busy&&s->metrics.phase==5){
        if(!r)r=raid_post_battle_advance(s,permit,now);
        if(!closing&&s->metrics.stage==RS_OPEN&&now>=s->ui_retry_at){
            SilphReadValue top={0};char current[161]={0};r=silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&top);
            bool block_tap=false;
            if(!r){
                char name[128]={0};
                if(!silph_raid_class(s->read,top.reference,name,sizeof(name))&&strcmp(name,s->top_name)){
                    memcpy(s->top_name,name,strlen(name)+1);
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gui=%s request=%llu",name,(unsigned long long)s->metrics.request);
                    silph_dump_methods(s->read,top.reference,name);
                }
                if(s->join_ui_fallback&&raid_gym_ui_top(s)){s->metrics.stage=RS_UI;block_tap=true;}
                if(!r&&name[0]&&(strstr(name,"News")||strstr(name,"Details")||strstr(name,"Announcement"))){
                    int dismissed=action(s,permit,RK_RAID_GUI,top.reference,NULL,0);SilphManagedRef service={0};
                    if(!dismissed){dismissed=silph_game_read_raid_service(s->game,3,&service);if(!dismissed)dismissed=action(s,permit,RK_GEM,service,NULL,0);drop(s,&service);}
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","news dialog dismissed gui=%s result=%d",name,dismissed);
                    s->ui_retry_at=now+UINT64_C(100000000);block_tap=true;
                }
            }
            if(!r&&(raid_pass_ui_assist(s)||!raid_rpc_only(s))&&(istype(s,top.reference,"Niantic.Holoholo.Raid","ReceivedRaidTicketGuiController")||istype(s,top.reference,"Niantic.Holoholo.Raid","TooltipToastGuiController"))){
                int dismissed=action(s,permit,RK_RAID_GUI,top.reference,NULL,0);
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid popup dismissed result=%d",dismissed);
                s->ui_seen=1;s->ui_retry_at=now+UINT64_C(150000000);block_tap=true;
            }
            if(!r&&!block_tap&&s->join_ui_fallback&&s->stop.fort[0]&&s->place.serial&&s->ui_taps<16
                &&!(s->approach_at&&now-s->approach_at<UINT64_C(90000000000)&&s->metrics.stage==RS_UI)){
                bool main=!s->stale_pending&&(!s->top_name[0]||!strcmp(s->top_name,"MainMenuGuiController"));
                if(main){
                    (void)raid_ensure_place(s);
                    bool details_wait=(s->raid_seed>1)&&s->raid_details_pending&&!s->raid_details_ok&&now-s->ui_started<UINT64_C(12000000000)&&!s->approach_at;
                    if(details_wait){s->ui_retry_at=now+UINT64_C(250000000);}
                    else{
                    bool in_range=false;int range_r=ready_to_open(s,true,&in_range);
                    if(range_r==SILPH_MANAGED_NOT_READY){s->ui_retry_at=now+UINT64_C(300000000);}
                    else if(range_r)r=range_r;
                    else if(!in_range&&!raid_at_gym(s)&&(!s->gps_settled_at||now-s->gps_settled_at>UINT64_C(15000000000))){
                        s->teleport_ready=0;s->gps_settled_at=0;s->latitude=s->stop.latitude;s->longitude=s->stop.longitude;s->dirty=1;phase(s,6,0,now+UINT64_C(15000000000));
                        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym map tap blocked; mock teleport fort=%s",s->stop.fort);
                    }else if(s->place.serial&&(in_range||s->teleport_ready||raid_at_gym(s))){
                        int tap=raid_tap_place(s,permit,now);
                        if(!tap){s->metrics.stage=RS_UI;s->ui_taps++;s->ui_retry_at=now+UINT64_C(350000000);
                            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym map tap fort=%s attempt=%u gui=%s",s->stop.fort,(unsigned)s->ui_taps,s->top_name);}
                        else if(tap!=SILPH_MANAGED_NOT_READY)r=tap;
                    }else s->ui_retry_at=now+UINT64_C(500000000);
                    }
                }
            }
            if(!r&&!block_tap&&!s->stop.incident[0]&&s->raid_seed>1&&!s->rpc_active&&!silph_rpc_action_pending(s->rpc)&&now>=s->ui_retry_at){
                int sent=raid_rpc_kickoff(s,permit,now);
                if(sent&&!raid_rpc_soft_wait(sent))r=sent;
            }
            if(!r&&!s->stop.incident[0]){
                char pick_gui[128]={0};
                if(raid_pass_ui_assist(s)&&!silph_raid_class(s->read,top.reference,pick_gui,sizeof(pick_gui))&&(strstr(pick_gui,"PickTeam")||strstr(pick_gui,"TeamSelect")||strstr(pick_gui,"JoinTeam"))){
                    int picked=action(s,permit,RK_RAID_GUI,top.reference,NULL,0);
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","pick team gui=%s result=%d",pick_gui,picked);
                    s->ui_retry_at=now+UINT64_C(50000000);block_tap=true;
                }
            }
            bool prepared=!r&&(istype(s,top.reference,"Niantic.Holoholo.Combat.Preparation","CombatPreparationRootGuiController")||istype(s,top.reference,"Niantic.Holoholo.Combat.PokemonPartyState","CombatPokemonPartyWidget"));
            int same=prepared?text(s,s->incident,"get_CurrentIncidentId",current,sizeof(current),true):0;
            prepared=prepared&&!same&&!strcmp(current,s->stop.incident);            bool require_range=!prepared&&s->stop.incident[0];bool ready=false;if(!r&&!block_tap)r=ready_to_open(s,require_range,&ready);
            if(!r&&!block_tap&&!s->stop.incident[0]&&s->stop.fort[0]&&silph_game_read_gym_is_raid_egg(s->game,s->stop.fort)){
                raid_clear_gym_target(s,now,"tap blocked: raid egg");
                block_tap=true;
            }
            if(!r&&!block_tap&&!s->stop.incident[0]&&s->raid_seed&&!raid_gym_still_joinable(s)){
                if(s->gps_settled_at&&now-s->gps_settled_at<UINT64_C(20000000000))block_tap=true;
                else if(silph_game_read_gym_is_raid_egg(s->game,s->stop.fort)){
                    raid_clear_gym_target(s,now,"tap blocked: raid egg");
                    block_tap=true;
                }else if(s->ui_taps>=2||now-s->ui_started>UINT64_C(25000000000)){
                    if(s->stop.fort[0])raid_clear_gym_target(s,now,"tap blocked: raid no longer joinable");
                    block_tap=true;
                }else block_tap=true;
            }
            if(!r&&!block_tap&&!s->stop.incident[0]&&s->raid_rpc_step&&!s->ui_seen&&s->rpc_active)block_tap=true;
            if(!r&&!block_tap&&s->join_ui_fallback&&s->stop.fort[0])(void)raid_ensure_place(s);
            if(!r&&!block_tap&&s->join_ui_fallback&&s->place.serial&&s->stop.fort[0]&&s->ui_taps<16&&!prepared){
                bool main=!s->top_name[0]||!strcmp(s->top_name,"MainMenuGuiController");
                if(main&&(ready||!require_range)){
                    r=raid_tap_place(s,permit,now);
                    if(!r){s->metrics.stage=RS_UI;s->ui_taps++;s->ui_retry_at=now+UINT64_C(350000000);
                        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym map tap fort=%s ready=%u attempt=%u gui=%s",s->stop.fort,(unsigned)ready,(unsigned)s->ui_taps,s->top_name[0]?s->top_name:"?");}
                }
            }
            if(!r&&!block_tap&&(raid_pass_ui_assist(s)||!raid_rpc_only(s)||s->join_ui_fallback)&&(ready||!require_range)&&!prepared&&(s->join_ui_fallback||!raid_gym_fast(s))){
                if((s->join_ui_fallback||raid_pass_ui_assist(s))&&s->raid_rpc_step>=2){
                    s->metrics.stage=RS_UI;
                    if(!s->ui_seen&&!r)r=ui(s,permit,now);
                }
            }
            int freed=drop(s,&top.reference);if(!r)r=freed;
            if(!r&&!block_tap&&(!prepared||ready||!s->stop.incident[0])&&!raid_gym_fast(s)){s->metrics.stage=RS_UI;s->ui_taps++;s->ui_retry_at=now+UINT64_C(400000000);}
            else if(!r&&s->join_ui_fallback&&s->place.serial&&!block_tap){
                s->ui_taps++;s->ui_retry_at=now+UINT64_C(600000000);
                if(s->ui_taps>=4&&strstr(s->top_name,"MainMenu"))s->ui_retry_at=now+UINT64_C(2500000000);
            }
            else if(!r&&!block_tap&&!raid_gym_fast(s)&&now-s->ui_started>=UINT64_C(45000000000))result(s,3,false);
        }
        if(!r&&s->metrics.stage!=RS_OPEN)r=ui(s,permit,now);
        if(!r&&!closing&&raid_rpc_only(s)&&s->metrics.stage==RS_OPEN&&now>=s->ui_retry_at&&!s->rpc_active&&!silph_rpc_action_pending(s->rpc)&&s->raid_rpc_step<3){
            int sent=raid_rpc_kickoff(s,permit,now);if(sent)r=sent;
        }
        if(!r&&!closing&&s->metrics.stage==RS_UI&&now>=s->ui_retry_at&&(s->ui_seen==0||s->approach_at||raid_gym_ui_top(s))){
            SilphReadValue ui_top={0};
            int top_r=silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&ui_top);
            if(!top_r&&ui_top.reference.serial){
                char ui_name[128]={0};
                if(!silph_raid_class(s->read,ui_top.reference,ui_name,sizeof(ui_name))&&ui_name[0]&&strcmp(ui_name,s->top_name)){
                    memcpy(s->top_name,ui_name,strlen(ui_name)+1);
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gui=%s request=%llu",ui_name,(unsigned long long)s->metrics.request);
                }
            }
            if(s->join_ui_fallback&&raid_gym_ui_top(s)){
                raid_skip_details_for_ui(s,now);
                if(!r)r=ui(s,permit,now);
                if(!r&&!s->rpc_active&&!silph_rpc_action_pending(s->rpc)){
                    int sent=raid_rpc_kickoff(s,permit,now);
                    if(sent)r=sent;
                }
                s->ui_retry_at=now+UINT64_C(80000000);
            }else if(s->join_ui_fallback&&s->raid_rpc_step>=2&&!s->place.serial&&s->stop.fort[0]){
                int pr=raid_ensure_place(s);
                if(!pr&&s->place.serial&&!strcmp(s->top_name,"MainMenuGuiController")){
                    r=raid_tap_place(s,permit,now);
                    if(!r){s->ui_taps++;s->ui_retry_at=now+UINT64_C(400000000);
                        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym open tap after lobby rpc fort=%s attempt=%u",s->stop.fort,(unsigned)s->ui_taps);}
                }else if(pr!=SILPH_MANAGED_NOT_READY)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym place pending for tap err=%d",pr);
            }
            if(raid_gym_fast(s)&&s->join_ui_fallback&&s->raid_rpc_step>=2&&!s->rpc_active){
                if(!s->place.serial&&s->ui_taps>0&&(s->ui_taps%3u)==0u)refresh_map(s,permit,snapshot,now);
                (void)raid_ensure_place(s);
                const bool main=!strcmp(s->top_name,"MainMenuGuiController");
                if(main&&!raid_onboarding_gui(s->top_name)){
                    bool ready=false;if(!r)r=ready_to_open(s,false,&ready);
                    if(s->place.serial&&!r&&s->ui_taps<12&&(ready||!s->stop.incident[0])&&s->raid_seed&&raid_gym_still_joinable(s)){
                        r=raid_tap_place(s,permit,now);
                        if(!r){s->ui_taps++;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym tap attempt=%u fort=%s",(unsigned)s->ui_taps,s->stop.fort);}
                    }else if(!r&&!raid_gym_still_joinable(s))raid_clear_gym_target(s,now,"gym tap aborted");
                    else if(!s->place.serial&&!r)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym tap waiting for map place fort=%s attempt=%u",s->stop.fort,(unsigned)s->ui_taps);
                    s->ui_retry_at=now+UINT64_C(400000000);
                    if(s->ui_taps>=8)s->ui_retry_at=now+UINT64_C(1500000000);
                }else if(!s->ui_seen&&!r)r=ui(s,permit,now);
            }else if(raid_gym_fast(s)&&!s->join_ui_fallback&&!s->rpc_active&&s->raid_rpc_step<3){
                int sent=raid_rpc_kickoff(s,permit,now);if(sent)r=sent;
            }else if(s->join_ui_fallback&&s->approach_at&&now-s->approach_at<UINT64_C(8000000000)){
                raid_skip_details_for_ui(s,now);
                if(!r)r=ui(s,permit,now);
                if(!r)r=raid_pump_approach_gui(s,permit,now);
                s->ui_retry_at=now+UINT64_C(80000000);
            }else if((raid_gym_ui_top(s)||(s->approach_at&&now-s->approach_at<UINT64_C(120000000000)))&&!s->ui_seen){
                s->ui_started=now;
                if(!r)r=ui(s,permit,now);
                s->ui_retry_at=now+UINT64_C(120000000);
            }else if(s->join_ui_fallback&&s->approach_at&&(strstr(s->top_name,"Approach")||strstr(s->top_name,"GymGui"))){
                if(!r)r=ui(s,permit,now);
                s->ui_retry_at=now+UINT64_C(200000000);
            }else if((!s->approach_at||now-s->approach_at>UINT64_C(600000000000))&&(now-s->ui_started>=(s->approach_at?UINT64_C(600000000000):(s->join_ui_fallback?UINT64_C(240000000000):UINT64_C(45000000000)))||(s->ui_taps>=36&&!strcmp(s->top_name,"MainMenuGuiController")&&!s->approach_at))){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","Raid opening not verified after %u attempts gui=%s",s->ui_taps,s->top_name);if(s->stop.fort[0])raid_clear_gym_target(s,now,"raid ui never opened");else result(s,3,false);}
            else if(!raid_rpc_only(s)&&!strcmp(s->top_name,"MainMenuGuiController")&&!(s->join_ui_fallback&&s->raid_rpc_step<2)){bool require_range=s->stop.incident[0]!=0;bool ready=false;r=ready_to_open(s,require_range,&ready);
                if(!r&&(ready||!require_range)&&s->raid_seed&&raid_gym_still_joinable(s)){r=raid_tap_place(s,permit,now);if(!r){s->ui_taps++;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","retrying same Raid attempt=%u seed=%llu",s->ui_taps,(unsigned long long)s->raid_seed);}}
                else if(!r&&s->stop.fort[0]&&!raid_gym_still_joinable(s))raid_clear_gym_target(s,now,"retry aborted");
                s->ui_retry_at=now+UINT64_C(200000000);}
            if(ui_top.reference.serial){int ui_f=drop(s,&ui_top.reference);if(!r)r=ui_f;}
        }
        if(!r&&!s->ui_joined&&s->metrics.stage==RS_UI&&s->join_ui_fallback&&s->raid_pass_override&&!s->rpc_active&&now>=s->ui_retry_at){
            s->raid_details_pending=0;s->raid_rpc_step=0;
            int sent=raid_rpc_kickoff(s,permit,now);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","pass retry join rpc pass=%u sent=%d",(unsigned)s->raid_pass_override,sent);
            if(sent)r=sent;
            s->ui_retry_at=now+UINT64_C(600000000);
        }
        if(!r&&s->metrics.stage==RS_UI&&s->join_ui_fallback&&now>=s->ui_retry_at){
            bool mainmenu=!strcmp(s->top_name,"MainMenuGuiController");
            bool approach_ui=raid_gym_ui_top(s)||(s->approach_at&&now-s->approach_at<UINT64_C(120000000000));
            if(approach_ui||mainmenu||!s->rpc_active||!silph_rpc_action_pending(s->rpc))r=ui(s,permit,now);
            if(!r&&(s->approach_at||s->lobby_layer_hint)&&(mainmenu||s->approach_gui.serial))r=raid_pump_approach_gui(s,permit,now);
        }
        if(!r&&s->metrics.stage==RS_LOSS&&s->gui.serial){
            SilphReadValue top={0};char name[128]={0};
            if(!silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&top)&&top.reference.serial&&!silph_raid_class(s->read,top.reference,name,sizeof(name))){
                memcpy(s->top_name,name,strlen(name)+1);
                static char loss_seen_name[128];
                if(strcmp(loss_seen_name,name)){snprintf(loss_seen_name,sizeof(loss_seen_name),"%s",name);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","after battle status=%u top gui=%s",s->metrics.result,name);}
                bool victory=raid_post_battle_gui(name)||strstr(name,"Encounter")||strstr(name,"Reward")||strstr(name,"Victory")||strstr(name,"RaidEnd");
                if(!victory&&s->director.serial){
                    SilphManagedRef st={0};uint64_t bs=0;
                    if(!callref(s,s->director,"get_CurrentState",&st)&&st.serial&&!scalar(s,st,"BattleStatus",SILPH_READ_ENUM32,&bs,false)&&(bs==2||bs==7))victory=true;
                    drop(s,&st);
                }
                if(!victory&&s->metrics.result==5&&now>=s->loss_ok_at&&strcmp(name,"MainMenuGuiController")&&!strstr(name,"Lobby")&&!strstr(name,"Combat")){
                    s->loss_ok_at=now+UINT64_C(2000000000);
                    int ok=action(s,permit,RK_RAID_GUI,top.reference,NULL,0);
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","fainted dialog ok gui=%s result=%d; staying in raid for the win",name,ok);
                }
                if(victory){
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","battle status=%u was a win (gui=%s); continuing to encounter",s->metrics.result,name);
                    s->lost=0;s->metrics.result=0;s->won=1;raid_mark_won(s,"battle won");s->metrics.raids++;
                    s->metrics.stage=RS_ENCOUNTER;s->next=now+UINT64_C(300000000);s->post_battle_at=now;
                }
            }
            drop(s,&top.reference);
        }
        if(!r&&s->metrics.stage==RS_LOSS){
            if(!strcmp(s->top_name,"MainMenuGuiController")){
                if(!s->loss_clear_at)s->loss_clear_at=now;
                if((s->loss_seen||now-s->loss_started>=UINT64_C(5000000000))&&now-s->loss_clear_at>=(s->metrics.result==5?UINT64_C(10000000000):UINT64_C(1000000000))){s->metrics.stage=RS_HEAL;s->loss_recovery=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","loss screens closed; healing team");}
            }else s->loss_clear_at=0;
            if(s->metrics.stage==RS_LOSS&&now>=s->loss_until){s->metrics.stage=RS_HEAL;s->loss_recovery=1;}
        }
        if(!r&&(s->metrics.stage==RS_UI||s->metrics.stage==RS_BATTLE))r=find_director(s,permit,now);
        if(!r&&s->director.serial&&(s->metrics.stage==RS_UI||s->metrics.stage==RS_BATTLE)){s->metrics.stage=RS_BATTLE;r=battle(s,permit,now);}
        if(!r&&s->won&&raid_gym_fast(s)&&s->metrics.stage==RS_ENCOUNTER)r=open_gym_encounter(s,permit,now);
        if(!r&&(s->metrics.stage==RS_ENCOUNTER||(s->metrics.stage==RS_UI&&!strcmp(s->top_name,"EncounterGuiController"))))r=open_encounter(s,now);
        if(!r&&s->metrics.phase==5&&s->metrics.stage==RS_CAPTURE)r=capture(s,permit,snapshot,now);
        if(!r&&s->metrics.stage==RS_HEAL)r=care(s,permit,now);
    }
    if(!r&&!closing&&s->deadline&&now>s->deadline&&s->metrics.phase!=7&&s->metrics.phase!=8){
        if(s->metrics.phase==3&&!raid_map_ready(snapshot)){s->deadline=now+UINT64_C(120000000000);}
        else if(s->metrics.phase==5){s->metrics.error=100+s->metrics.stage;phase(s,8,0,0);}
        else result(s,3,false);
    }
    if(r==SILPH_MANAGED_CONTRACT&&s->metrics.phase==5&&(s->metrics.stage==RS_OPEN||s->metrics.stage==RS_UI))r=0;
    if(r==SILPH_MANAGED_CONTRACT&&(s->metrics.phase==3||(s->metrics.phase==5&&(s->metrics.stage==RS_BATTLE||s->metrics.stage==RS_ENCOUNTER||s->metrics.stage==RS_CAPTURE)))){
        if(now>=s->contract_log_at){s->contract_log_at=now+UINT64_C(5000000000);__android_log_print(ANDROID_LOG_WARN,"SilphRaid","transient contract error phase=%u stage=%u; continuing",s->metrics.phase,s->metrics.stage);}
        r=0;
    }
    if(r==SILPH_MANAGED_ARGUMENT&&s->metrics.phase==5&&s->metrics.stage==RS_UI&&s->join_ui_fallback)r=0;
    if(r&&r!=SILPH_MANAGED_NOT_READY&&!(r==SILPH_MANAGED_ARGUMENT&&(s->metrics.phase==1||s->metrics.phase==3))){s->metrics.error=(uint32_t)r;__android_log_print(ANDROID_LOG_ERROR,"SilphRaid","native request=%llu phase=%u stage=%u error=%d",(unsigned long long)s->metrics.request,s->metrics.phase,s->metrics.stage,r);}
    if(r==SILPH_MANAGED_NOT_READY||(r==SILPH_MANAGED_ARGUMENT&&(s->metrics.phase==1||s->metrics.phase==3)))r=0;
    if(r==SILPH_MANAGED_STATE&&s->metrics.phase==1)r=0;
    if(s->timing_stage!=s->metrics.stage||s->timing_request!=s->metrics.request){if(s->timing_at)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","stage completed request=%llu stage=%u ms=%llu next=%u",(unsigned long long)s->timing_request,s->timing_stage,(unsigned long long)((now-s->timing_at)/1000000),s->metrics.stage);s->timing_stage=s->metrics.stage;s->timing_request=s->metrics.request;s->timing_at=now;}
    if(s->tap_request&&now+UINT64_C(2000000000)>=s->toast_tap_at){s->tap_request=0;s->dirty=1;}
    if(s->stale_pending&&s->gui.serial&&now>=s->stale_at){
        s->stale_at=now+UINT64_C(1000000000);
        SilphReadValue top={0};char tn[128]={0};
        if(!silph_raid_invoke(s->read,permit,RK_TOP,s->gui,NULL,0,&top)&&top.reference.serial&&!silph_raid_class(s->read,top.reference,tn,sizeof(tn))){
            memcpy(s->top_name,tn,strlen(tn)+1);
            if(!strcmp(tn,"MainMenuGuiController")||now-s->stale_since>UINT64_C(20000000000)){s->stale_pending=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","old gym screens closed; top=%s",tn);}
            else if(strstr(tn,"Raid")||strstr(tn,"Gym")||strstr(tn,"Lobby")||strstr(tn,"Approach")||strstr(tn,"Ticket")||strstr(tn,"BattlePrep")){
                int closed=action(s,permit,RK_GUI_CLOSE,top.reference,NULL,0);
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","closing old gym screen gui=%s result=%d",tn,closed);
            }
        }
        drop(s,&top.reference);
    }
    *out=s->metrics;
    if(!r)(void)publish_event(s);
    return r;
}
int silph_raid_session_close(void *memory){Raid *s=memory;if(!s||!s->opened)return 0;int r=release_candidate(s);
    if(s->visual_tuned){int restored=silph_raid_battle_visuals(s->read,NULL,s->visual_properties,s->visual_saved,true);if(!r)r=restored;}int visual_drop=drop(s,&s->visual_properties);if(!r)r=visual_drop;
    if(s->map_settings.serial){int restored=silph_raid_map_settings(s->read,NULL,s->map_settings,s->map_saved,true);if(!r)r=restored;}
    int ma=drop(s,&s->map_settings),mb=drop(s,&s->map_handler);if(!r)r=ma?ma:mb;
    SilphManagedRef *refs[]={&s->incident,&s->gui,&s->encounter_state};for(unsigned i=0;i<3;i++){int d=drop(s,refs[i]);if(!r)r=d;}
    int a=s->rpc?silph_rpc_action_close(s->rpc):0,b=s->clean_opened?silph_maintenance_close(s->maintenance):0,c=s->medicine?silph_managed_promise_close(s->medicine):0;
    if(!r)r=a?a:b?b:c;if(!r){free(s->rpc);free(s->maintenance);free(s->medicine);memset(s,0,sizeof(*s));}return r;
}
