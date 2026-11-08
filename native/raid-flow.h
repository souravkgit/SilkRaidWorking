#ifndef SILPH_ROCKET_FLOW_H
#define SILPH_ROCKET_FLOW_H
#include "visual-tools.h"
#include "scheduler.h"
size_t silph_raid_flow_size(void);
int silph_raid_flow_open(void *,size_t,const SilphManagedApi *,SilphManagedResolver,void *,uint32_t,SilphVisualAllowed,void *,SilphVisualAddress);
int silph_raid_flow_bind(void *,SilphManagedRef,const char *,uint64_t,uint32_t);
void silph_raid_flow_permit(const SilphSchedulePermit *);
void silph_raid_flow_unbind(void);
bool silph_raid_flow_busy(void);
bool silph_raid_flow_challenged(void *,SilphManagedRef);
int silph_raid_flow_close(void *);
#endif
