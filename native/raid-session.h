#ifndef SILPH_RAID_SESSION_H
#define SILPH_RAID_SESSION_H
#include "game-read.h"
#include "session-link.h"
typedef struct {
    uint64_t raids,encounters,catches,shinies,berries,throws,deleted,xp,stardust,encounter,captured,request;
    uint32_t phase,stage,reason,error,result,verified,reward_error;
} SilphRaidMetrics;
size_t silph_raid_session_size(void);
int silph_raid_session_open(void *,size_t,void *,void *,void *,void *,const SilphScheduleIdentity *,const SilphSessionConfig *);
int silph_raid_session_tick(void *,const SilphSchedulePermit *,const SilphGameSnapshot *,uint64_t,uint64_t,SilphRaidMetrics *);
int silph_raid_session_publish(void *);
int silph_raid_session_close(void *);
bool silph_raid_inventory_due(void *);
#endif
