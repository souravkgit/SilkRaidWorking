#include "raid-bridge.h"
#include <stdio.h>
#include <android/log.h>
#ifdef SILPH_PILOT_RAID
static const void *lobby_field(Read *s,void *klass,const char *name){
    for(unsigned d=0;klass&&d<16;d++,klass=s->api.class_get_parent(klass)){
        void *it=NULL;const void *f;
        for(unsigned n=0;n<1024&&(f=s->api.class_get_fields(klass,&it));n++){
            if(s->api.field_get_flags(f)&0x50u)continue;
            const char *fn=s->api.field_get_name(f);if(fn&&!strcmp(fn,name))return f;
        }
    }
    return NULL;
}
static bool lobby_reference_field(Read *s,const void *field){
    const void *t=s->api.field_get_type(field);if(!t||s->api.type_is_byref(t))return false;
    int kind=s->base.type_get_type(t);
    if(kind==18||kind==28)return true;
    if(kind!=21)return false;
    void *k=s->api.class_from_type(t);return k&&!s->api.class_is_valuetype(k);
}
static void *lobby_search(Read *s,void *root,const char *cls,const char *field,unsigned max_depth){
    enum{CAP=256};void *queue[CAP];unsigned char depth[CAP];unsigned head=0,tail=0;
    queue[tail]=root;depth[tail++]=0;
    while(head<tail){
        void *o=queue[head];unsigned d=depth[head++];void *k=s->base.object_get_class(o);if(!k)continue;
        const char *cn=s->api.class_get_name(k);
        if((cls&&cn&&!strcmp(cn,cls))||(field&&lobby_field(s,k,field)))return o;
        if(d>=max_depth)continue;
        for(void *kk=k;kk;kk=s->api.class_get_parent(kk)){
            const char *kn=s->api.class_get_name(kk);if(kn&&(!strcmp(kn,"MonoBehaviour")||!strcmp(kn,"Object")))break;
            void *it=NULL;const void *f;
            for(unsigned n=0;n<1024&&(f=s->api.class_get_fields(kk,&it));n++){
                if((s->api.field_get_flags(f)&0x50u)||!lobby_reference_field(s,f))continue;
                void *child=NULL;s->api.field_get_value(o,f,&child);if(!child)continue;
                bool dup=false;for(unsigned i=0;i<tail&&!dup;i++)dup=queue[i]==child;
                if(!dup&&tail<CAP){queue[tail]=child;depth[tail++]=(unsigned char)(d+1);}
            }
        }
    }
    return NULL;
}
static bool lobby_read_u32(Read *s,void *object,const char *name,uint32_t *out){
    const void *f=lobby_field(s,s->base.object_get_class(object),name);if(!f)return false;
    int kind=s->base.type_get_type(s->api.field_get_type(f));
    if(kind==8||kind==9){int32_t v=0;s->api.field_get_value(object,f,&v);*out=(uint32_t)v;return true;}
    if(kind==10||kind==11){int64_t v=0;s->api.field_get_value(object,f,&v);*out=(uint32_t)v;return true;}
    return false;
}
static bool lobby_list_count(Read *s,void *object,const char *name,uint32_t *out){
    const void *f=lobby_field(s,s->base.object_get_class(object),name);if(!f||!lobby_reference_field(s,f))return false;
    void *list=NULL;s->api.field_get_value(object,f,&list);if(!list)return false;
    const void *m=research_method(s,s->base.object_get_class(list),"get_Count",0,false);if(!m)return false;
    void *exception=NULL;void *boxed=s->base.runtime_invoke(m,list,NULL,&exception);if(exception||!boxed)return false;
    int32_t *v=s->base.object_unbox(boxed);if(!v)return false;*out=(uint32_t)*v;return true;
}
static int raid_child_active(Read *s,void *object,const char *name){
    const void *f=lobby_field(s,s->base.object_get_class(object),name);if(!f||!lobby_reference_field(s,f))return -1;
    void *child=NULL;s->api.field_get_value(object,f,&child);if(!child)return -1;
    void *go=child;const void *active=research_method(s,s->base.object_get_class(go),"get_activeInHierarchy",0,false);
    if(!active){const void *g=research_method(s,s->base.object_get_class(child),"get_gameObject",0,false);if(!g)return -1;
        void *exception=NULL;go=s->base.runtime_invoke(g,child,NULL,&exception);if(exception||!go)return -1;
        active=research_method(s,s->base.object_get_class(go),"get_activeInHierarchy",0,false);if(!active)return -1;}
    void *exception=NULL;void *boxed=s->base.runtime_invoke(active,go,NULL,&exception);if(exception||!boxed)return -1;
    uint8_t *v=s->base.object_unbox(boxed);return v&&*v?1:0;
}
static int raid_object_active(Read *s,void *obj){
    void *go=obj;const void *active=research_method(s,s->base.object_get_class(go),"get_activeInHierarchy",0,false);
    if(!active){const void *g=research_method(s,s->base.object_get_class(obj),"get_gameObject",0,false);if(!g)return -1;
        void *exception=NULL;go=s->base.runtime_invoke(g,obj,NULL,&exception);if(exception||!go)return -1;
        active=research_method(s,s->base.object_get_class(go),"get_activeInHierarchy",0,false);if(!active)return -1;}
    void *exception=NULL;void *boxed=s->base.runtime_invoke(active,go,NULL,&exception);if(exception||!boxed)return -1;
    uint8_t *v=s->base.object_unbox(boxed);return v&&*v?1:0;
}
static bool raid_button_class(Read *s,void *klass){
    for(unsigned d=0;klass&&d<12;d++,klass=s->api.class_get_parent(klass)){
        const char *kn=s->api.class_get_name(klass);if(!kn)continue;
        if(!strcmp(kn,"MonoBehaviour")||!strcmp(kn,"Object"))break;
        if(strstr(kn,"Button"))return true;
    }
    return false;
}
static bool raid_click_button(Read *s,void *button){
    const void *on_click=research_method(s,s->base.object_get_class(button),"get_onClick",0,false);
    if(on_click){
        void *exception=NULL;void *event=s->base.runtime_invoke(on_click,button,NULL,&exception);
        if(!exception&&event){const void *invoke=research_method(s,s->base.object_get_class(event),"Invoke",0,false);
            if(invoke){exception=NULL;s->base.runtime_invoke(invoke,event,NULL,&exception);if(!exception)return true;}}
    }
    const char *names[]={"Press","OnClick","Click","OnPress","HandleClick"};
    for(unsigned i=0;i<5;i++){const void *m=research_method(s,s->base.object_get_class(button),names[i],0,false);if(!m)continue;
        void *exception=NULL;s->base.runtime_invoke(m,button,NULL,&exception);if(!exception)return true;}
    return false;
}
static void raid_object_name(Read *s,void *obj,char *out,size_t size){
    out[0]=0;const void *m=research_method(s,s->base.object_get_class(obj),"get_name",0,false);if(!m)return;
    void *exception=NULL;void *str=s->base.runtime_invoke(m,obj,NULL,&exception);if(exception||!str)return;
    int32_t length=s->base.string_length(str);const uint16_t *chars=s->api.string_chars(str);if(!chars||length<0)return;
    size_t n=0;for(int32_t i=0;i<length&&n+1<size;i++)out[n++]=chars[i]>=32&&chars[i]<127?(char)chars[i]:'?';out[n]=0;
}
static bool raid_click_child_button_ex(Read *s,void *object,const char *cls,char *used,size_t used_size,const char *const *only,unsigned only_n);
static bool raid_click_child_button(Read *s,void *object,const char *cls,char *used,size_t used_size){return raid_click_child_button_ex(s,object,cls,used,used_size,NULL,0);}
static bool raid_click_child_button_ex(Read *s,void *object,const char *cls,char *used,size_t used_size,const char *const *only,unsigned only_n){
    static const char *prefer[]={"Summary","Bonus","Challenge","Continue","Next","Primary","Proceed","Done","Confirm","Ok","Go"};
    static const char *avoid[]={"Close","Share","Back","Info","Help","Friend","Profile","Report","Settings","Exit","Quit","Leave","Cancel","Surrender","Item","Ability"};
    if(!s->array_length||!s->array_addr_with_size)return false;
    void *bk=lookup_class(s,"UnityEngine.UI.dll","UnityEngine.UI","Button");if(!bk)return false;
    void *type=s->api.type_get_object(s->base.class_get_type(bk));if(!type)return false;
    const void *gc=research_method(s,s->base.object_get_class(object),"GetComponentsInChildren",2,false);
    if(!gc){
        void *ck=lookup_class(s,"UnityEngine.CoreModule.dll","UnityEngine","Component");
        void *it=NULL;for(unsigned i=0;ck&&i<512;i++){const void *m=s->base.class_get_methods(ck,&it);if(!m)break;const char *mn=s->base.method_get_name(m);
            if(mn&&!strcmp(mn,"GetComponentsInChildren")&&s->base.method_get_param_count(m)==2&&!s->base.method_is_generic(m)){gc=m;break;}}
    }
    if(!gc)return false;
    uint8_t include_inactive=0;void *argv[2]={type,&include_inactive};void *exception=NULL;
    void *array=s->base.runtime_invoke(gc,object,argv,&exception);if(exception||!array)return false;
    uintptr_t count=s->array_length(array);if(count>64)count=64;
    void *best=NULL;char best_name[64]={0};int best_rank=-1;unsigned usable=0;
    char seen[700];size_t n=0;seen[0]=0;
    for(uintptr_t i=0;i<count;i++){
        void **slot=s->array_addr_with_size(array,(int32_t)sizeof(void*),i);void *button=slot?*slot:NULL;if(!button)continue;
        char name[64];raid_object_name(s,button,name,sizeof(name));
        uint64_t interactable=1;const void *im=research_method(s,s->base.object_get_class(button),"IsInteractable",0,false);
        if(im){void *ex=NULL;void *boxed=s->base.runtime_invoke(im,button,NULL,&ex);if(!ex&&boxed){uint8_t *v=s->base.object_unbox(boxed);interactable=v&&*v;}}
        if(n<sizeof(seen)-72)n+=(size_t)snprintf(seen+n,sizeof(seen)-n," %s/%u",name[0]?name:"?",(unsigned)interactable);
        if(!interactable)continue;
        bool bad=false;for(unsigned j=0;j<sizeof(avoid)/sizeof(*avoid);j++)if(strstr(name,avoid[j]))bad=true;
        if(only){bad=true;for(unsigned j=0;j<only_n;j++)if(strstr(name,only[j]))bad=false;}
        if(bad)continue;
        usable++;
        int rank=0;for(unsigned j=0;j<sizeof(prefer)/sizeof(*prefer);j++)if(strstr(name,prefer[j])){rank=(int)(sizeof(prefer)/sizeof(*prefer)-j);break;}
        if(rank>best_rank){best_rank=rank;best=button;snprintf(best_name,sizeof(best_name),"%s",name);}
    }
    static char logged[700];
    if(strcmp(logged,seen)){snprintf(logged,sizeof(logged),"%s",seen);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","post-battle child buttons gui=%s count=%u:%s",cls,(unsigned)count,seen[0]?seen:" none");}
    if(!best||(!only&&best_rank==0&&usable!=1))return false;
    if(!raid_click_button(s,best))return false;
    snprintf(used,used_size,"child:%s",best_name);
    return true;
}
static bool raid_click_gui_button(Read *s,void *object,const char *cls,char *used,size_t used_size){
    if(raid_click_child_button(s,object,cls,used,used_size))return true;
    static const char *prefer[]={"Summary","Bonus","Continue","Next","Primary","Proceed","Done","Confirm","Ok","Go"};
    static const char *avoid[]={"Close","Share","Back","Info","Help","Friend","Profile","Report","Settings","Exit","Quit","Leave","Cancel"};
    void *best=NULL;const char *best_name=NULL;int best_rank=-1;unsigned active_count=0;
    char seen[600];size_t n=0;seen[0]=0;
    for(void *k=s->base.object_get_class(object);k;k=s->api.class_get_parent(k)){
        const char *kn=s->api.class_get_name(k);if(kn&&(!strcmp(kn,"MonoBehaviour")||!strcmp(kn,"Object")))break;
        void *it=NULL;const void *f;
        for(unsigned c=0;c<512&&(f=s->api.class_get_fields(k,&it));c++){
            if(s->api.field_get_flags(f)&0x10u)continue;
            if(!lobby_reference_field(s,f))continue;
            void *fk=s->api.class_from_type(s->api.field_get_type(f));if(!fk||!raid_button_class(s,fk))continue;
            const char *fn=s->api.field_get_name(f);if(!fn)continue;
            void *child=NULL;s->api.field_get_value(object,f,&child);if(!child)continue;
            int active=raid_object_active(s,child);
            if(n<sizeof(seen)-48)n+=(size_t)snprintf(seen+n,sizeof(seen)-n," %s=%d",fn,active);
            if(active!=1)continue;
            bool bad=false;for(unsigned i=0;i<sizeof(avoid)/sizeof(*avoid);i++)if(strstr(fn,avoid[i]))bad=true;
            if(bad)continue;
            active_count++;
            int rank=0;for(unsigned i=0;i<sizeof(prefer)/sizeof(*prefer);i++)if(strstr(fn,prefer[i])){rank=(int)(sizeof(prefer)/sizeof(*prefer)-i);break;}
            if(rank>best_rank){best_rank=rank;best=child;best_name=fn;}
        }
    }
    static char logged[128];
    if(strcmp(logged,cls)){snprintf(logged,sizeof(logged),"%s",cls);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","post-battle buttons gui=%s:%s",cls,seen[0]?seen:" none");}
    if(!best||(best_rank==0&&active_count!=1))return false;
    if(!raid_click_button(s,best))return false;
    snprintf(used,used_size,"button:%s",best_name);
    return true;
}
static void raid_dump_class(Read *s,void *klass,const char *cls){
    for(void *k=klass;k;k=s->api.class_get_parent(k)){const char *kn=s->api.class_get_name(k);if(kn&&(!strcmp(kn,"MonoBehaviour")||!strcmp(kn,"Object")))break;
        void *iterator=NULL;char line[900];size_t n=0;line[0]=0;
        for(unsigned scan=0;scan<768;scan++){const void *method=s->base.class_get_methods(k,&iterator);if(!method)break;const char *mn=s->base.method_get_name(method);if(!mn)continue;
            n+=(size_t)snprintf(line+n,sizeof(line)-n," %s/%u",mn,(unsigned)s->base.method_get_param_count(method));
            if(n>sizeof(line)-80){__android_log_print(ANDROID_LOG_INFO,"SilphDump","gui %s.%s:%s",cls,kn?kn:"?",line);n=0;line[0]=0;}}
        if(n)__android_log_print(ANDROID_LOG_INFO,"SilphDump","gui %s.%s:%s",cls,kn?kn:"?",line);
        void *it=NULL;const void *f;n=0;line[0]=0;
        for(unsigned c=0;c<512&&(f=s->api.class_get_fields(k,&it));c++){const char *fn=s->api.field_get_name(f);if(!fn)continue;
            n+=(size_t)snprintf(line+n,sizeof(line)-n," %s",fn);
            if(n>sizeof(line)-80){__android_log_print(ANDROID_LOG_INFO,"SilphDump","gui fields %s.%s:%s",cls,kn?kn:"?",line);n=0;line[0]=0;}}
        if(n)__android_log_print(ANDROID_LOG_INFO,"SilphDump","gui fields %s.%s:%s",cls,kn?kn:"?",line);
    }
}
int silph_raid_battle_visuals(void *memory,const SilphSchedulePermit *permit,SilphManagedRef ref,float saved[4],bool restore){
    if(!saved||(!restore&&!silph_schedule_permitted(permit,true)))return SILPH_MANAGED_ARGUMENT;
    Read *s=memory;int status=enter_read(s);if(status)return status;void *object=NULL;status=silph_managed_target(s->managed,ref,&object);
    if(!status&&!research_type(s,object,"Niantic.Holoholo.Combat.Util","CombatDirectorProperties"))status=SILPH_MANAGED_CONTRACT;
    const char *names[]={"startCombatFadeTransitionSeconds","cameraMovementDurationSeconds","endCombatFadeOutSeconds","endCombatPreFadeOutSeconds"};const void *fields[4]={0};float before[4]={0};
    for(unsigned i=0;!status&&i<4;i++){fields[i]=find_field(s,s->base.object_get_class(object),names[i],NULL,NULL,&status);if(!status&&!matches_kind(s,s->api.field_get_type(fields[i]),SILPH_READ_F32))status=SILPH_MANAGED_CONTRACT;if(!status){s->api.field_get_value(object,fields[i],&before[i]);if(!isfinite(before[i])||before[i]<0||before[i]>120||!isfinite(saved[i])||saved[i]<0||saved[i]>120)status=SILPH_MANAGED_CONTRACT;}}
    if(!status)for(unsigned i=0;i<4;i++){if(restore){if(before[i]==0)s->field_set_value(object,fields[i],&saved[i]);}else{saved[i]=before[i];float zero=0;s->field_set_value(object,fields[i],&zero);}}
    return leave_read(s,status);
}
#endif
int silph_raid_invoke(void *memory,const SilphSchedulePermit *permit,uint32_t operation,SilphManagedRef receiver,
    const SilphReadArgument *args,uint32_t count,SilphReadValue *out){
    if(!out)return SILPH_MANAGED_ARGUMENT;clear_value(out);
    if(operation<1||operation>
#ifdef SILPH_PILOT_RAID
        31
#else
        26
#endif
        ||!silph_schedule_permitted(permit,true))return SILPH_MANAGED_STATE;
    Read *s=memory;int status=enter_read(s);if(status)return status;
#ifdef SILPH_PILOT_RAID
    if(operation==RK_REWARD_DISMISS){
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        if(!status&&(count||!research_type(s,object,"Niantic.Holoholo.Incident","IncidentCombatResolveGuiController")))status=SILPH_MANAGED_CONTRACT;
        const void *state=status?NULL:find_field(s,s->base.object_get_class(object),"currentState",NULL,NULL,&status);int32_t active=0;
        if(!status&&!matches_kind(s,s->api.field_get_type(state),SILPH_READ_ENUM32))status=SILPH_MANAGED_CONTRACT;
        if(!status)s->api.field_get_value(object,state,&active);
        uintptr_t native=0;const void *alive=status?NULL:find_field(s,s->base.object_get_class(object),"m_CachedPtr",NULL,NULL,&status);
        if(!status&&(s->api.type_is_byref(s->api.field_get_type(alive))||s->base.type_get_type(s->api.field_get_type(alive))!=24))status=SILPH_MANAGED_CONTRACT;
        if(!status)s->api.field_get_value(object,alive,&native);
        if(!status&&active==2&&native)status=call_internal(s,receiver,NULL,"Dismiss",SILPH_READ_VOID,NULL,0,out,permit);
        return leave_read(s,status);
    }
    if(operation==RK_REWARD_COMPLETE){
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        if(!status&&(count!=2||args[0].kind!=SILPH_READ_U64||!args[0].value.bits||args[1].kind!=SILPH_READ_ENUM32||args[1].value.bits>3||!research_type(s,object,"Niantic.Holoholo.Incident","IncidentService")))status=SILPH_MANAGED_CONTRACT;
        const void *field=status?NULL:find_field(s,s->base.object_get_class(object),NULL,"Holoholo.Rpc","InvasionEncounterOutProto",&status);void *cached=NULL;
        if(!status)s->api.field_get_value(object,field,&cached);
        if(!status&&(!cached||!research_type(s,cached,"Holoholo.Rpc","InvasionEncounterOutProto")))status=SILPH_MANAGED_NOT_READY;
        uint64_t encounter=0;const void *id=status?NULL:find_field(s,s->base.object_get_class(cached),"EncounterId",NULL,NULL,&status);
        if(!status&&!matches_kind(s,s->api.field_get_type(id),SILPH_READ_U64))status=SILPH_MANAGED_CONTRACT;
        if(!status){s->api.field_get_value(cached,id,&encounter);if(encounter!=args[0].value.bits)status=SILPH_MANAGED_STALE;}
        if(!status)status=call_internal(s,receiver,NULL,"betq",SILPH_READ_VOID,&args[1],1,out,permit);
        return leave_read(s,status);
    }
#endif
    if(operation==RK_PLAYER_ALIAS){
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        if(!status&&(count||!research_type(s,object,"Niantic.Holoholo.Combat.UI","CombatVNextGuiController")))status=SILPH_MANAGED_CONTRACT;
        SilphManagedRef label={0};if(!status)status=silph_managed_ascii(s->managed,"SilphMod",8,&label);
        SilphReadArgument values[]={{.kind=SILPH_READ_REFERENCE,.value.reference=label},{.kind=SILPH_READ_BOOL,.value.bits=1}};
        if(!status)status=call_internal(s,receiver,NULL,"DisplayNameplateText",SILPH_READ_VOID,values,2,out,permit);
        int freed=release_rpc_ref(s,&label);return leave_read(s,status?status:freed);
    }
    if(operation==RK_FAST_GUI){
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);if(status)return leave_read(s,status);
        if(count)return leave_read(s,SILPH_MANAGED_ARGUMENT);
        const char *fields[3]={0};unsigned total=0;bool animate=false;
        if(research_type(s,object,"Niantic.Holoholo.Combat.Preparation","CombatPreparationRootGuiController")){fields[0]="backgroundTransitionWaitDurationSec";fields[1]="versusScreenDurationSec";fields[2]="combatTransitionDurationSec";total=3;}
        else if(research_type(s,object,"Niantic.Holoholo.Combat.NpcTrainers","NpcTrainerInterstitialGui")){fields[0]="backdropFadeInDuration";total=1;}
        else if(research_type(s,object,"Niantic.Holoholo.Incident","IncidentInvasionDialogGui")){fields[0]="bubbleDelaySeconds";total=1;}
        else if(research_type(s,object,"Niantic.Holoholo.Incident","IncidentCombatResolveGuiController"))animate=true;
        else if(research_type(s,object,"Niantic.Holoholo.Combat.Resolution","CombatResolveGuiController"))animate=true;
        else{
            char fast_cls[160]={0};
            if(!status&&object){void *k=s->base.object_get_class(object);const char *cn=k?s->api.class_get_name(k):NULL;if(cn)snprintf(fast_cls,sizeof(fast_cls),"%s",cn);}
            if(!fast_cls[0])(void)silph_raid_class(s,receiver,fast_cls,sizeof(fast_cls));
            if(fast_cls[0]&&(strstr(fast_cls,"Achievement")||strstr(fast_cls,"Summary")||strstr(fast_cls,"Bonus")||strstr(fast_cls,"RaidReward")||strstr(fast_cls,"GymApproach")||strstr(fast_cls,"RaidApproach")))animate=true;
            else return leave_read(s,SILPH_MANAGED_NOT_READY);
        }
        const void *validated[3]={0};float previous[3]={0};
        for(unsigned i=0;!status&&i<total;i++){validated[i]=find_field(s,s->base.object_get_class(object),fields[i],NULL,NULL,&status);if(!status&&!matches_kind(s,s->api.field_get_type(validated[i]),SILPH_READ_F32))status=SILPH_MANAGED_CONTRACT;if(!status){s->api.field_get_value(object,validated[i],&previous[i]);if(!isfinite(previous[i])||previous[i]<0||previous[i]>120)status=SILPH_MANAGED_CONTRACT;}}
        if(!status)for(unsigned i=0;i<total;i++){float value=0;s->field_set_value(object,validated[i],&value);}
        if(!status&&animate){SilphReadValue animator={0};const void *field=find_field(s,s->base.object_get_class(object),"animator",NULL,NULL,&status);if(!status){void *value=NULL;s->api.field_get_value(object,field,&value);status=accept_reference(s,s->api.field_get_type(field),value,&animator);}
            if(!status&&!animator.reference.serial)status=SILPH_MANAGED_NOT_READY;
            if(!status){float speed=16;SilphReadArgument argument={.kind=SILPH_READ_F32};memcpy(&argument.value.bits,&speed,sizeof(speed));status=call_internal(s,animator.reference,NULL,"set_speed",SILPH_READ_VOID,&argument,1,out,permit);}
            int freed=release_rpc_ref(s,&animator.reference);if(!status)status=freed;
        }
        return leave_read(s,status);
    }
    if(operation==RK_PARTY_RECOMMEND){
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        if(!status&&(count||!research_type(s,object,"Niantic.Holoholo.Combat.PokemonPartyState","CombatPokemonPartyWidget")))status=SILPH_MANAGED_CONTRACT;
        SilphReadValue service={0},league={0},scroll={0},page={0},party={0};
        const char *fields[]={"dnoc","dnoq","dynoScrollRect"};SilphReadValue *values[]={&service,&league,&scroll};
        for(unsigned i=0;!status&&i<3;i++){
            const void *field=find_field(s,s->base.object_get_class(object),fields[i],NULL,NULL,&status);
            if(!status){void *v=NULL;s->api.field_get_value(object,field,&v);status=accept_reference(s,s->api.field_get_type(field),v,values[i]);}
            if(!status&&i!=1&&!values[i]->reference.serial)status=SILPH_MANAGED_NOT_READY;
        }
        if(!status)status=call_internal(s,scroll.reference,NULL,"CurrentPageGui",SILPH_READ_REFERENCE,NULL,0,&page,permit);
        if(!status&&!page.reference.serial)status=SILPH_MANAGED_NOT_READY;
        if(!status)status=silph_managed_target(s->managed,page.reference,&object);
        if(!status&&!research_type(s,object,"Niantic.Holoholo.Gym","CombatPrepPartyPanel"))status=SILPH_MANAGED_CONTRACT;
        if(!status&&!league.reference.serial){
            SilphReadValue gate={0},rolled_back={0};SilphManagedRef key={0};const char *template="NPC_LEAGUE_SUFFIX_ROLL_BACK";
            void *widget=NULL;status=silph_managed_target(s->managed,receiver,&widget);
            const void *field=status?NULL:find_field(s,s->base.object_get_class(widget),"dnom",NULL,NULL,&status);
            if(!status){void *v=NULL;s->api.field_get_value(widget,field,&v);status=accept_reference(s,s->api.field_get_type(field),v,&gate);}
            if(!status)status=silph_managed_ascii(s->managed,template,(uint32_t)strlen(template),&key);
            SilphReadArgument argument={.kind=SILPH_READ_REFERENCE,.value.reference=key};
            if(!status)status=call_internal(s,gate.reference,NULL,"IsRolledBack",SILPH_READ_BOOL,&argument,1,&rolled_back,permit);
            const char *name=rolled_back.bits?"COMBAT_LEAGUE_DEFAULT_MASTER":"COMBAT_LEAGUE_DEFAULT_MASTER_NPC";
            if(!status)status=silph_managed_ascii(s->managed,name,(uint32_t)strlen(name),&league.reference);
            int freed=release_rpc_ref(s,&key),other=release_rpc_ref(s,&gate.reference);if(!status)status=freed?freed:other;
        }
        SilphReadArgument args[]={{.kind=SILPH_READ_REFERENCE,.value.reference=league.reference},{.kind=SILPH_READ_BOOL,.value.bits=1}};
        if(!status)status=call_internal(s,service.reference,NULL,"ConstructPartyForLeague",SILPH_READ_REFERENCE,args,2,&party,permit);
        if(!status)status=silph_managed_target(s->managed,party.reference,&object);
        if(!status&&!research_type(s,object,"Niantic.Holoholo.Battle","BattleParty"))status=SILPH_MANAGED_CONTRACT;
        SilphReadArgument selected[]={{.kind=SILPH_READ_REFERENCE,.value.reference=party.reference},{.kind=SILPH_READ_REFERENCE,.value.reference=league.reference}};
        if(!status)status=call_internal(s,page.reference,NULL,"SetBattleParty",SILPH_READ_VOID,selected,2,out,permit);
        SilphReadArgument flag={.kind=SILPH_READ_BOOL};if(!status)status=call_internal(s,page.reference,NULL,"RefreshPickers",SILPH_READ_VOID,&flag,1,out,permit);
        if(!status)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","recommended team regenerated");
        SilphManagedRef *refs[]={&party.reference,&page.reference,&scroll.reference,&league.reference,&service.reference};for(unsigned i=0;i<5;i++){int freed=release_rpc_ref(s,refs[i]);if(!status)status=freed;}
        return leave_read(s,status);
    }
    if(operation>=RK_SHIELD&&operation<=RK_INCIDENT_CLOSE){
        const char *spaces[]={"Niantic.Holoholo.Combat.InputChallenge","Niantic.Holoholo.Combat.NpcTrainers","Niantic.Holoholo.Combat.PokemonPartyState","Niantic.Holoholo.Combat.Resolution","Niantic.Holoholo.Incident"};
        const char *names[]={"ShieldModalGuiController","NpcTrainerInterstitialGui","CombatPokemonPartyWidget","CombatResolveGuiController","IncidentCombatResolveGuiController"};
        const char *methods[]={"ShieldUiClicked","PressCancel","Cancel","CloseButtonPressed","Close"};unsigned index=operation-RK_SHIELD;
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        if(!status&&(count||!research_type(s,object,spaces[index],names[index])))status=SILPH_MANAGED_CONTRACT;
        SilphReadValue button={0},enabled={0};
        if(!status&&operation==RK_SHIELD){
            const void *f=find_field(s,s->base.object_get_class(object),"shieldButton",NULL,NULL,&status);
            if(!status){void *v=NULL;s->api.field_get_value(object,f,&v);status=accept_reference(s,s->api.field_get_type(f),v,&button);}
            if(!status)status=call_internal(s,button.reference,NULL,"get_interactable",SILPH_READ_BOOL,NULL,0,&enabled,permit);
            if(!status&&!enabled.bits)status=SILPH_MANAGED_NOT_READY;
        }
        if(!status)status=call_internal(s,receiver,NULL,methods[index],SILPH_READ_VOID,NULL,0,out,permit);
        int freed=release_rpc_ref(s,&button.reference);return leave_read(s,status?status:freed);
    }
    if(operation==RK_CAPTURE_INFO||operation==RK_CAPTURE_RECORD){
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        bool record=operation==RK_CAPTURE_RECORD;
        if(!status&&(count!=(record?2u:1u)||args[0].kind!=SILPH_READ_U64||!args[0].value.bits||!research_type(s,object,"Niantic.Holoholo.UI.Pokemon",record?"PokemonSizeRecordBreakGuiController":"PokemonInfoGuiController")))status=SILPH_MANAGED_CONTRACT;
        SilphManagedRef source=receiver;if(!status&&record){if(args[1].kind!=SILPH_READ_REFERENCE||!args[1].value.reference.serial)status=SILPH_MANAGED_ARGUMENT;else source=args[1].value.reference;}
        if(!status&&record)status=silph_managed_target(s->managed,source,&object);
        if(!status&&record&&!research_type(s,object,"Niantic.Holoholo.UI.Pokemon","PokemonInfoGuiController"))status=SILPH_MANAGED_CONTRACT;
        if(!status&&record){const void *state=find_field(s,s->base.object_get_class(object),"currentState",NULL,NULL,&status);int32_t active=0;if(!status&&!matches_kind(s,s->api.field_get_type(state),SILPH_READ_ENUM32))status=SILPH_MANAGED_CONTRACT;if(!status){s->api.field_get_value(object,state,&active);if(active!=2)status=SILPH_MANAGED_NOT_READY;}}
        SilphReadValue pokemon={0},data={0};uint64_t id=0;uint8_t post=0;
        if(!status)status=call_internal(s,source,NULL,"get_CurrentPokemon",SILPH_READ_REFERENCE,NULL,0,&pokemon,permit);
        if(!status&&!pokemon.reference.serial)status=SILPH_MANAGED_NOT_READY;
        const void *field=status?NULL:find_field(s,s->base.object_get_class(object),"egbx",NULL,NULL,&status);
        if(!status){void *v=NULL;s->api.field_get_value(object,field,&v);status=accept_reference(s,s->api.field_get_type(field),v,&data);}
        if(!status&&!data.reference.serial)status=SILPH_MANAGED_NOT_READY;
        if(!status)status=silph_managed_target(s->managed,pokemon.reference,&object);
        field=status?NULL:find_field(s,s->base.object_get_class(object),"id_",NULL,NULL,&status);
        if(!status&&!matches_kind(s,s->api.field_get_type(field),SILPH_READ_U64))status=SILPH_MANAGED_CONTRACT;
        if(!status)s->api.field_get_value(object,field,&id);
        if(!status)status=silph_managed_target(s->managed,data.reference,&object);
        if(!status&&!research_type(s,object,"Niantic.Holoholo.UI.Pokemon","PokemonInfoData"))status=SILPH_MANAGED_CONTRACT;
        field=status?NULL:find_field(s,s->base.object_get_class(object),"IsPostEncounter",NULL,NULL,&status);
        if(!status&&!matches_kind(s,s->api.field_get_type(field),SILPH_READ_BOOL))status=SILPH_MANAGED_CONTRACT;
        if(!status)s->api.field_get_value(object,field,&post);
        if(!status&&(!post||id!=args[0].value.bits))status=SILPH_MANAGED_NOT_READY;
        if(!status)status=call_internal(s,receiver,NULL,record?"Dismiss":"DismissPokemonInfoPanel",SILPH_READ_VOID,NULL,0,out,permit);
        int a=release_rpc_ref(s,&data.reference),b=release_rpc_ref(s,&pokemon.reference);return leave_read(s,status?status:a?a:b);
    }
    if(operation==RK_MAP_REFRESH){
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        if(!status&&(count||!research_type(s,object,"Niantic.Holoholo.Map","MapContentHandler")))status=SILPH_MANAGED_CONTRACT;
        SilphReadArgument argument={.kind=SILPH_READ_BOOL,.value.bits=1};
        if(!status)status=call_internal(s,receiver,NULL,"set_ForceImmediateGetObjects",SILPH_READ_VOID,&argument,1,out,permit);return leave_read(s,status);
    }
    if(operation==RK_PARTY_REFRESH){
        const char *step="widget";
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        if(!status&&(count!=1||args[0].kind!=SILPH_READ_REFERENCE||!args[0].value.reference.serial||!research_type(s,object,"Niantic.Holoholo.Combat.PokemonPartyState","CombatPokemonPartyWidget")))status=SILPH_MANAGED_CONTRACT;
        SilphReadValue scroll={0},page={0},party={0},refreshed={0};
        const void *f=status?NULL:find_field(s,s->base.object_get_class(object),"dynoScrollRect",NULL,NULL,&status);
        if(!status){void *v=NULL;s->api.field_get_value(object,f,&v);status=accept_reference(s,s->api.field_get_type(f),v,&scroll);}
        if(!status){step="page";status=call_internal(s,scroll.reference,NULL,"CurrentPageGui",SILPH_READ_REFERENCE,NULL,0,&page,permit);}
        if(!status){step="party";status=call_internal(s,page.reference,NULL,"get_BattleParty",SILPH_READ_REFERENCE,NULL,0,&party,permit);}
        if(!status){step="refresh";status=call_internal(s,party.reference,NULL,"Refresh",SILPH_READ_REFERENCE,args,1,&refreshed,permit);}
        if(!status){
            step="set_party";void *panel=NULL,*team=NULL;status=silph_managed_target(s->managed,page.reference,&panel);
            if(!status)status=silph_managed_target(s->managed,refreshed.reference,&team);
            const void *method=status?NULL:research_method(s,s->base.object_get_class(panel),"SetBattleParty",2,false);
            if(!status&&(!method||!research_type(s,panel,"Niantic.Holoholo.Gym","CombatPrepPartyPanel")||!research_type(s,team,"Niantic.Holoholo.Battle","BattleParty")
                ||!matches_kind(s,s->base.method_get_return_type(method),SILPH_READ_VOID)||!matches_kind(s,s->api.method_get_param(method,0),SILPH_READ_REFERENCE)
                ||s->base.type_get_type(s->api.method_get_param(method,1))!=14||!s->api.class_is_assignable_from(s->api.class_from_type(s->api.method_get_param(method,0)),s->base.object_get_class(team))))status=SILPH_MANAGED_CONTRACT;
            if(!status){void *values[]={team,NULL},*exception=NULL;s->base.runtime_invoke(method,panel,values,&exception);if(exception)status=SILPH_MANAGED_EXCEPTION;}
        }
        SilphReadArgument flag={.kind=SILPH_READ_BOOL};if(!status){step="pickers";status=call_internal(s,page.reference,NULL,"RefreshPickers",SILPH_READ_VOID,&flag,1,out,permit);}
        if(status)__android_log_print(ANDROID_LOG_ERROR,"SilphRaid","team refresh step=%s error=%d",step,status);
        else __android_log_print(ANDROID_LOG_INFO,"SilphRaid","team selection refreshed from inventory");
        SilphManagedRef *refs[]={&refreshed.reference,&party.reference,&page.reference,&scroll.reference};for(unsigned i=0;i<4;i++){int freed=release_rpc_ref(s,refs[i]);if(!status)status=freed;}
        return leave_read(s,status);
    }
    if(operation==RK_LAYER){
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        if(!status&&!research_type(s,object,"Niantic.Holoholo.NUI","GuiLayerService"))status=SILPH_MANAGED_CONTRACT;
        if(!status&&(count!=1||args[0].kind!=SILPH_READ_ENUM32||args[0].value.bits>1100))status=SILPH_MANAGED_ARGUMENT;
        if(!status)status=call_internal(s,receiver,NULL,"GetRegisteredGuisForLayer",SILPH_READ_REFERENCE,args,count,out,permit);
        return leave_read(s,status);
    }
    if(operation==RK_LOBBY){
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        if(!status&&(!object||count>1||(count&&args[0].kind!=SILPH_READ_ENUM32)))status=SILPH_MANAGED_ARGUMENT;
        uint32_t total=0,listed=0,found=0,pressed=0;int32_t left=-1;
        uint64_t want=count?args[0].value.bits:0;
        if(!status){
            void *counter=lobby_search(s,object,NULL,"TotalPlayerCountInLobby",4);
            if(counter&&lobby_read_u32(s,counter,"TotalPlayerCountInLobby",&total))found|=1;
            void *lobby=lobby_search(s,object,NULL,"PokemonSelectionEndMs",4);
            if(lobby&&lobby_list_count(s,lobby,"Players",&listed))found|=2;
            const void *ef=lobby?lobby_field(s,s->base.object_get_class(lobby),"PokemonSelectionEndMs"):NULL;
            if(ef){int kind=s->base.type_get_type(s->api.field_get_type(ef));
                if(kind==10||kind==11){int64_t end=0;s->api.field_get_value(lobby,ef,&end);struct timespec ts={0};clock_gettime(CLOCK_REALTIME,&ts);
                    int64_t wall=(int64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;int64_t sec=end>0?(end-wall)/1000:-1;left=sec<-1000?-1000:sec>30000?30000:(int32_t)sec;found|=8;}}
            if(want==2||want==3){
                const char *names2[]={"OnRejoinClicked"},*names3[]={"LeaveLobby","ClickExit"};
                const char **names=want==2?names2:names3;unsigned n=want==2?1:2;
                void *target=object;
                if(want==2){
                    target=lobby_search(s,object,"BattlePrepPartyPanel",NULL,4);
                    if(!target){void *pk=lookup_class(s,"holo-game.dll","Niantic.Holoholo.Gym","BattlePrepPartyPanel");void *uo=lookup_class(s,"UnityEngine.CoreModule.dll","UnityEngine","Object");
                        void *type=pk?s->api.type_get_object(s->base.class_get_type(pk)):NULL;const void *find=uo?research_method(s,uo,"FindObjectOfType",1,false):NULL;
                        if(type&&find){void *argv[1]={type};void *exception=NULL;void *hit=s->base.runtime_invoke(find,NULL,argv,&exception);if(!exception)target=hit;}}
                }
                if(want==3){void *holder=lobby_search(s,object,NULL,"leaveButton",4);
                    const void *lf=holder?lobby_field(s,s->base.object_get_class(holder),"leaveButton"):NULL;void *btn=NULL;
                    if(lf)s->api.field_get_value(holder,lf,&btn);
                    if(btn){found|=4;pressed=raid_click_button(s,btn);__android_log_print(ANDROID_LOG_INFO,"SilphRaid","lobby leaveButton press ok=%u",pressed);}}
                for(unsigned i=0;target&&!pressed&&i<n;i++){const void *m=research_method(s,s->base.object_get_class(target),names[i],0,false);if(!m)continue;
                    found|=4;void *exception=NULL;s->base.runtime_invoke(m,target,NULL,&exception);pressed=!exception;
                    if(pressed)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","lobby action %s ok",names[i]);}
            }
            if(want==1){
                void *panel=lobby_search(s,object,"BattlePrepPartyPanel",NULL,4);
                if(!panel){
                    void *pk=lookup_class(s,"holo-game.dll","Niantic.Holoholo.Gym","BattlePrepPartyPanel");
                    void *uo=lookup_class(s,"UnityEngine.CoreModule.dll","UnityEngine","Object");
                    void *type=pk?s->api.type_get_object(s->base.class_get_type(pk)):NULL;
                    const void *find=uo?research_method(s,uo,"FindObjectOfType",1,false):NULL;
                    if(type&&find){void *argv[1]={type};void *exception=NULL;void *hit=s->base.runtime_invoke(find,NULL,argv,&exception);if(!exception)panel=hit;}
                    if(!panel)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","lobby ready panel lookup failed class=%d object=%d find=%d",pk?1:0,uo?1:0,find?1:0);
                }
                const void *m=panel?research_method(s,s->base.object_get_class(panel),"OnReadyClicked",0,false):NULL;
                if(m){found|=4;void *exception=NULL;s->base.runtime_invoke(m,panel,NULL,&exception);pressed=!exception;}
            }
        }
        if(!status)out->bits=(uint64_t)(total&0xffff)|(uint64_t)(listed&0xffff)<<16|(uint64_t)found<<32|(uint64_t)pressed<<40|(uint64_t)(uint16_t)(int16_t)(left<-32000?-32000:left>32000?32000:left)<<48;
        return leave_read(s,status);
    }
    if(operation==RK_GUI_CLOSE){
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        if(!status&&(count||!object))status=SILPH_MANAGED_ARGUMENT;
        char cls[160]={0};void *klass=status?NULL:s->base.object_get_class(object);
        if(klass){const char *cn=s->api.class_get_name(klass);if(cn)snprintf(cls,sizeof(cls),"%s",cn);}
        const char *close_methods[]={"OnBackButtonPressed","BackPressed","OnBack","HandleBack","ClosePressed","OnClose","CloseButtonPressed","OnCloseButtonPressed","CancelPressed","OnCancel","OnCancelButtonPressed","SecondaryPressed","OnSecondaryPressed","OnSecondaryButtonPressed","DismissPressed","OnDismiss","Dismiss","Close"};
        bool invoked=false;
        for(unsigned i=0;!status&&klass&&!invoked&&i<sizeof(close_methods)/sizeof(close_methods[0]);i++){
            const void *method=research_method(s,klass,close_methods[i],0,false);if(!method)continue;
            void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);
            if(!exception){invoked=true;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gui closed gui=%s method=%s",cls,close_methods[i]);}
        }
        if(!status&&!invoked)status=SILPH_MANAGED_NOT_READY;
        return leave_read(s,status);
    }
    if(operation==RK_RAID_GUI){
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        if(!status&&count)status=SILPH_MANAGED_ARGUMENT;
        char cls[160]={0};
        if(!status&&object){void *k=s->base.object_get_class(object);const char *cn=k?s->api.class_get_name(k):NULL;if(cn)snprintf(cls,sizeof(cls),"%s",cn);}
        if(!cls[0]&&!status)silph_raid_class(s,receiver,cls,sizeof(cls));
        if(!status&&cls[0]&&object){
            static char seen[24][96];static unsigned seen_n;bool known=false;
            for(unsigned i=0;i<seen_n&&!known;i++)known=!strcmp(seen[i],cls);
            if(!known&&seen_n<24){snprintf(seen[seen_n++],96,"%s",cls);raid_dump_class(s,s->base.object_get_class(object),cls);}
        }
        if(!status&&cls[0]&&strstr(cls,"UseRaidTicket")){
            static void *started;static unsigned tries;
            if(started==object&&++tries>=2){
                const void *f=lobby_field(s,s->base.object_get_class(object),"InteractionComplete");void *del=NULL;
                if(f)s->api.field_get_value(object,f,&del);
                const void *inv=del?research_method(s,s->base.object_get_class(del),"Invoke",0,false):NULL;if(del&&!inv)inv=research_method(s,s->base.object_get_class(del),"Invoke",1,false);
                int pc=inv?(int)s->base.method_get_param_count(inv):-1;
                if(inv&&pc==0){void *exception=NULL;s->base.runtime_invoke(inv,del,NULL,&exception);
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid pass InteractionComplete invoked ok=%d",exception?0:1);
                    return leave_read(s,exception?SILPH_MANAGED_NOT_READY:0);}
                if(inv&&pc==1){uint8_t yes=1;void *argv[1]={&yes};void *exception=NULL;s->base.runtime_invoke(inv,del,argv,&exception);
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid pass InteractionComplete(1) invoked ok=%d",exception?0:1);
                    return leave_read(s,exception?SILPH_MANAGED_NOT_READY:0);}
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid pass InteractionComplete unavailable field=%d delegate=%d params=%d",f?1:0,del?1:0,pc);
            }
            if(started!=object)tries=0;
            const char *first[]={"StartAnimation","OnUseButtonPressed","UseButtonPressed","OnUsePassPressed","OnConfirmButtonPressed","ConfirmPressed","OnPrimaryButtonPressed","PrimaryPressed"};
            const char *again[]={"FinishAnimation","StartAnimation"};
            bool repeat=started==object;
            const char **use_methods=repeat?again:first;unsigned use_n=repeat?2:sizeof(first)/sizeof(*first);
            void *klass=s->base.object_get_class(object);bool invoked=false;started=object;
            for(unsigned i=0;klass&&!invoked&&i<use_n;i++){
                const void *method=research_method(s,klass,use_methods[i],0,false);if(!method)continue;
                void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);
                if(!exception){invoked=true;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid pass used gui=%s method=%s",cls,use_methods[i]);}
            }
            if(!invoked){static char dumped_ticket[8];if(!dumped_ticket[0]){dumped_ticket[0]=1;raid_dump_class(s,klass,cls);}}
            return leave_read(s,invoked?0:SILPH_MANAGED_NOT_READY);
        }
        bool team_gui=cls[0]&&(strstr(cls,"PickTeam")||strstr(cls,"TeamSelect")||strstr(cls,"JoinTeam")||strstr(cls,"GymTeam"));
        if(!status&&team_gui){
            const char *team_methods[]={"SelectTeam","OnTeamSelected","TeamSelected","JoinTeam","SetTeam","ConfirmTeam"};
            int32_t team=1;void *team_arg[]={&team};
            for(unsigned i=0;!status&&i<6;i++){
                const void *one=research_method(s,s->base.object_get_class(object),team_methods[i],1,false);
                if(one){void *exception=NULL;s->base.runtime_invoke(one,object,team_arg,&exception);if(!exception){status=0;break;}status=SILPH_MANAGED_EXCEPTION;}
                const void *zero=research_method(s,s->base.object_get_class(object),team_methods[i],0,false);
                if(!status||status==SILPH_MANAGED_EXCEPTION){if(zero){void *exception=NULL;s->base.runtime_invoke(zero,object,NULL,&exception);if(!exception){status=0;break;}status=SILPH_MANAGED_EXCEPTION;}}
            }
            if(!status)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","faction team selected gui=%s",cls);
        }
        bool ticket_gui=cls[0]&&(strstr(cls,"ReceivedRaidTicket")||strstr(cls,"TooltipToast")||strstr(cls,"FreeRaidPass"));
        /* Free-pass selection UI (appears 5-10s after entering the gym when no premium pass is equipped) */
        bool free_pass_gui=cls[0]&&(strstr(cls,"RaidPassSelection")||strstr(cls,"UseRaidPass")||strstr(cls,"PassSelection")||strstr(cls,"SelectPass")||strstr(cls,"FreeRaidPassGui")||strstr(cls,"RaidFreePass")||strstr(cls,"RaidPassGui"));
        /* Paid-pass shop that appears when the free pass hasn't loaded yet — dismiss it */
        bool pass_shop_gui=cls[0]&&(strstr(cls,"Shop")||strstr(cls,"Purchase")||strstr(cls,"GetMorePasses")||strstr(cls,"AcquirePass")||strstr(cls,"IAPGui")||strstr(cls,"PaymentGui")||strstr(cls,"PassStore")||(strstr(cls,"Store")&&strstr(cls,"Raid")));
        bool approach_layers=cls[0]&&(strstr(cls,"GymApproachLayers")||strstr(cls,"RaidApproachLayers"));
        bool approach_gui=cls[0]&&(strstr(cls,"RaidApproach")||strstr(cls,"GymApproach")||strstr(cls,"GymInterior")||strstr(cls,"GymHub"))&&!approach_layers;
        bool gym_gui=cls[0]&&(strstr(cls,"GymGui")||strstr(cls,"GymDetails")||strstr(cls,"MapPlaceGui")||strstr(cls,"RaidGym")||strstr(cls,"GymInterior"));
        if(!status&&approach_gui&&strstr(cls,"GymApproach")){
            static void *noted;if(noted!=object){noted=object;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym approach open gui=%s; waiting for raid panel (no gym battle press)",cls);}
            return leave_read(s,SILPH_MANAGED_NOT_READY);
        }
        if(!status&&approach_layers){
            const char *layer_methods[]={"Advance","AdvanceStage","NextStage","CompleteStage","ProceedToNextStage","ShowBattleButton","RevealBattleButton","OnApproachComplete","FinishApproach","ProceedToLobby","EnterLobby","BattlePressed","JoinRaid","PrimaryPressed","SkipCutscene","OnSkip","ContinuePressed","OnContinue","ScreenTapped","OnScreenTapped","TapToContinue","HandlePrimaryAction","OnPrimaryButtonPressed","OpenLobby","StartRaid"};
            void *klass=s->base.object_get_class(object);bool invoked=false;
            for(unsigned i=0;!status&&!invoked&&i<25;i++){
                if(strstr(cls,"GymApproach")&&strstr(layer_methods[i],"Battle"))continue;
                const void *method=research_method(s,klass,layer_methods[i],0,false);if(!method)continue;
                void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);
                if(!exception){invoked=true;status=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid approach layers method ok gui=%s method=%s",cls,layer_methods[i]);break;}
                status=SILPH_MANAGED_EXCEPTION;
            }
            if(!invoked&&!status){
                void *iterator=NULL;
                for(unsigned scan=0;!invoked&&scan<384;scan++){
                    const void *field=s->api.class_get_fields(klass,&iterator);if(!field)break;
                    if((s->api.field_get_flags(field)&0x50u)||s->base.type_get_type(s->api.field_get_type(field))!=18)continue;
                    void *child=NULL;s->api.field_get_value(object,field,&child);if(!child)continue;
                    char child_cls[160]={0};SilphReadValue child_box={0};
                    if(accept_reference(s,s->api.field_get_type(field),child,&child_box))continue;
                    void *child_obj=NULL;if(silph_managed_target(s->managed,child_box.reference,&child_obj))continue;
                    void *child_klass=s->base.object_get_class(child_obj);
                    const char *child_cn=child_klass?s->api.class_get_name(child_klass):NULL;
                    if(child_cn)snprintf(child_cls,sizeof(child_cls),"%s",child_cn);
                    if(!child_cls[0])silph_raid_class(s,child_box.reference,child_cls,sizeof(child_cls));
                    if(!child_cls[0]||(!strstr(child_cls,"Approach")&&!strstr(child_cls,"GymGui")&&!strstr(child_cls,"RaidLobby"))||strstr(child_cls,"GymApproach")){release_rpc_ref(s,&child_box.reference);continue;}
                    void *miter=NULL;
                    for(unsigned mi=0;!invoked&&mi<512;mi++){
                        const void *method=s->base.class_get_methods(child_klass,&miter);if(!method)break;
                        const char *mn=s->base.method_get_name(method);if(!mn||strchr(mn,'<'))continue;
                        if(s->base.method_get_param_count(method)!=0)continue;
                        uint32_t flags=0;if(s->base.method_get_flags(method,&flags)&0x10u)continue;
                        if(strstr(mn,"Shop")||strstr(mn,"Store")||strstr(mn,"Purchase")||strstr(mn,"Buy")||strstr(mn,"Exchange")||strstr(mn,"Remote")||strstr(mn,"Private")||strstr(mn,"Invite")||strstr(mn,"Friend")||strstr(mn,"Share")||strstr(mn,"Back")||strstr(mn,"Close")||strstr(mn,"Destroy"))continue;
                        if(strstr(mn,"HowToPlay")||strstr(mn,"Tutorial")||strstr(mn,"Info")||strstr(mn,"Help")||strstr(mn,"Tag")||strstr(mn,"Elite")||strstr(mn,"News")||strstr(mn,"Banner")||strstr(mn,"Detail")||strstr(mn,"Tooltip"))continue;
                        if(!strstr(mn,"Battle")&&!strstr(mn,"Join")&&!strstr(mn,"Raid")&&!strstr(mn,"Lobby")&&!strstr(mn,"Approach")&&!strstr(mn,"Continue")&&!strstr(mn,"Press")&&!strstr(mn,"Primary")&&!strstr(mn,"Skip")&&!strstr(mn,"Proceed"))continue;
                        void *exception=NULL;s->base.runtime_invoke(method,child_obj,NULL,&exception);
                        if(!exception){invoked=true;status=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid approach layers child ok parent=%s child=%s method=%s",cls,child_cls,mn);break;}
                    }
                    release_rpc_ref(s,&child_box.reference);
                    if(invoked)break;
                }
            }
            if(!invoked&&!status){
                void *miter=NULL;
                for(unsigned scan=0;!invoked&&scan<768;scan++){
                    const void *method=s->base.class_get_methods(klass,&miter);if(!method)break;
                    const char *mn=s->base.method_get_name(method);if(!mn||strchr(mn,'<'))continue;
                    if(s->base.method_get_param_count(method)!=0)continue;
                    uint32_t flags=0;if(s->base.method_get_flags(method,&flags)&0x10u)continue;
                    if(strstr(mn,"Shop")||strstr(mn,"Store")||strstr(mn,"Purchase")||strstr(mn,"Buy")||strstr(mn,"Exchange")||strstr(mn,"Remote")||strstr(mn,"Private")||strstr(mn,"Invite")||strstr(mn,"Friend")||strstr(mn,"Share")||strstr(mn,"Back")||strstr(mn,"Close")||strstr(mn,"Destroy"))continue;
                    if(strstr(mn,"HowToPlay")||strstr(mn,"Tutorial")||strstr(mn,"Info")||strstr(mn,"Help")||strstr(mn,"Tag")||strstr(mn,"Elite")||strstr(mn,"News")||strstr(mn,"Banner")||strstr(mn,"Detail")||strstr(mn,"Tooltip"))continue;
                    if(strstr(cls,"GymApproach")&&(strstr(mn,"Battle")||strstr(mn,"Deploy")))continue;
                    void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);
                    if(!exception){invoked=true;status=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid approach layers dyn ok gui=%s method=%s",cls,mn);break;}
                }
            }
            if(status==SILPH_MANAGED_EXCEPTION)status=invoked?0:SILPH_MANAGED_NOT_READY;
            if(!invoked&&!status){status=SILPH_MANAGED_NOT_READY;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid approach layers no method gui=%s",cls);}
            return leave_read(s,status);
        }
        if(!status&&ticket_gui){
            const char *ticket_methods[]={"OkPressed","GotItPressed","ClosePressed","OnClose","Dismiss","TapToContinue","OnTapToContinue","ContinuePressed","OnContinue","PrimaryPressed","OnPrimaryPressed","ConfirmPressed","AcceptPressed","ScreenTapped","OnScreenTapped","CloseButtonPressed","NextPressed","OnNext","ClaimPressed","DonePressed","OnDone"};
            void *klass=s->base.object_get_class(object);
            for(unsigned i=0;!status&&i<21;i++){
                const void *method=research_method(s,klass,ticket_methods[i],0,false);
                if(!method)continue;
                void *exception=NULL;
                s->base.runtime_invoke(method,object,NULL,&exception);
                if(!exception){status=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid ticket dismissed gui=%s method=%s",cls,ticket_methods[i]);break;}
                status=SILPH_MANAGED_EXCEPTION;
            }
            if(status==SILPH_MANAGED_EXCEPTION)status=SILPH_MANAGED_NOT_READY;
            return leave_read(s,status);
        }
        /* Free raid pass selection UI: try to claim/use the free pass before Battle */
        if(!status&&free_pass_gui){
            /* Priority: UseFreePass / ClaimFreePass methods first, then generic claim/primary */
            const char *free_pass_methods[]={
                "OnUseFreePassButtonPressed","UseFreePassPressed","OnUseFreePass","UseFreePass",
                "ClaimFreePassPressed","OnClaimFreePass","ClaimFreePass",
                "OnFreePassClaimPressed","FreePassClaimPressed","OnFreePassButton","FreePassButtonPressed",
                "OnFreeButtonPressed","FreeButtonPressed","OnUsePassPressed","UsePassPressed",
                "OnUsePass","UsePass","OnSelectFreePass","SelectFreePass",
                "PrimaryPressed","OnPrimaryPressed","PrimaryButtonPressed","OnPrimaryButtonPressed",
                "HandlePrimaryAction","ConfirmPressed","AcceptPressed","OkPressed","GotItPressed",
                "ClaimPressed","OnClaim","OnConfirm","UseItem","OnUseItem"};
            void *klass=s->base.object_get_class(object);bool invoked=false;
            for(unsigned i=0;!status&&!invoked&&i<34;i++){
                const void *method=research_method(s,klass,free_pass_methods[i],0,false);if(!method)continue;
                void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);
                if(!exception){invoked=true;status=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","free pass claimed gui=%s method=%s",cls,free_pass_methods[i]);break;}
                status=SILPH_MANAGED_EXCEPTION;
            }
            if(!invoked&&klass){
                /* Dynamic scan: look for any 0-arg method with Free/Pass/Claim/Use in name */
                void *miter=NULL;
                for(unsigned mi=0;!invoked&&mi<512;mi++){
                    const void *method=s->base.class_get_methods(klass,&miter);if(!method)break;
                    const char *mn=s->base.method_get_name(method);if(!mn||strchr(mn,'<'))continue;
                    if(s->base.method_get_param_count(method)!=0)continue;
                    uint32_t flags=0;if(s->base.method_get_flags(method,&flags)&0x10u)continue;
                    if(strstr(mn,"Shop")||strstr(mn,"Store")||strstr(mn,"Purchase")||strstr(mn,"Buy")||strstr(mn,"Exchange")||strstr(mn,"Remote")||strstr(mn,"Private")||strstr(mn,"Invite")||strstr(mn,"Friend")||strstr(mn,"Share")||strstr(mn,"Back")||strstr(mn,"Close")||strstr(mn,"Destroy")||strstr(mn,"Cancel"))continue;
                    if(!strstr(mn,"Free")&&!strstr(mn,"Claim")&&!strstr(mn,"UsePass")&&!strstr(mn,"UseItem")&&!strstr(mn,"Pass")&&!strstr(mn,"Primary")&&!strstr(mn,"Confirm")&&!strstr(mn,"Accept"))continue;
                    if(strstr(mn,"HowToPlay")||strstr(mn,"Tutorial")||strstr(mn,"Info")||strstr(mn,"Help"))continue;
                    void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);
                    if(!exception){invoked=true;status=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","free pass dyn claimed gui=%s method=%s",cls,mn);break;}
                }
            }
            if(status==SILPH_MANAGED_EXCEPTION)status=SILPH_MANAGED_NOT_READY;
            return leave_read(s,status);
        }
        /* Paid-pass shop / purchase screen: dismiss it (free pass not yet ready) */
        if(!status&&pass_shop_gui){
            const char *close_shop_methods[]={
                "ClosePressed","OnClose","Close","OnCloseButton","CloseButtonPressed",
                "BackPressed","OnBack","Back","CancelPressed","OnCancel","Cancel",
                "DismissPressed","Dismiss","OnDismiss","OkPressed","GotItPressed"};
            void *klass=s->base.object_get_class(object);bool invoked=false;
            for(unsigned i=0;!status&&!invoked&&i<16;i++){
                const void *method=research_method(s,klass,close_shop_methods[i],0,false);if(!method)continue;
                void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);
                if(!exception){invoked=true;status=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","pass shop closed gui=%s method=%s",cls,close_shop_methods[i]);break;}
                status=SILPH_MANAGED_EXCEPTION;
            }
            if(status==SILPH_MANAGED_EXCEPTION)status=SILPH_MANAGED_NOT_READY;
            return leave_read(s,status);
        }
        bool welcome_gui=cls[0]&&(strstr(cls,"Welcome")||strstr(cls,"FirstTimeUser")||strstr(cls,"ReferralEntry")||strstr(cls,"Onboarding")||strstr(cls,"Willow")||strstr(cls,"Dialogue")||strstr(cls,"Conversation")||strstr(cls,"Story")||strstr(cls,"Npc")||strstr(cls,"Narrative")||strstr(cls,"Speech")||strstr(cls,"Trainer")||strstr(cls,"Safety")||strstr(cls,"Surroundings")||strstr(cls,"Courteous")||strstr(cls,"Aware")||strstr(cls,"ArPlus")||strstr(cls,"StampRally")||strstr(cls,"Rally")||strstr(cls,"DailyReward")||strstr(cls,"ReturningPlayer")||strstr(cls,"Tutorial")||strstr(cls,"Reward")||strstr(cls,"Popup")||strstr(cls,"Modal")||strstr(cls,"Dialog")||strstr(cls,"News")||strstr(cls,"Announcement")||strstr(cls,"Power")||strstr(cls,"BootSequence")||strstr(cls,"Spot")||strstr(cls,"Buddy")||strstr(cls,"Activity")||strstr(cls,"Adventure")||strstr(cls,"Toast")||strstr(cls,"Notification")||strstr(cls,"Dismiss")||strstr(cls,"Faint")||strstr(cls,"Fainted")||strstr(cls,"Defeat")||strstr(cls,"Defeated")||strstr(cls,"BattleUIV")||strstr(cls,"BattleResult"));
        bool item_gui=cls[0]&&(strstr(cls,"ItemInventory")||strstr(cls,"ItemBag"));
        bool global_err=cls[0]&&strstr(cls,"GlobalError");
        if(!status&&welcome_gui){
            const char *welcome_methods[]={"ContinuePressed","OnContinue","GetStartedPressed","OnGetStarted","LetsGoPressed","OnLetsGo","StartPlaying","OnStartPlaying","PrimaryButtonPressed","OnPrimaryButton","PrimaryPressed","OnPrimaryPressed","NextPressed","OnNext","OkPressed","OnOk","HandleOk","ConfirmPressed","AcceptPressed","GotItPressed","SkipPressed","OnSkip","DonePressed","OnDone","ClaimRewardPressed","OnClaimReward","ClaimPressed","OnClaim","TapToContinue","ScreenTapped","OnScreenTapped","Dismiss","DismissPressed","ClosePressed","OnClose","Close","OnDismiss","Advance","Complete","AdvanceDialogue","OnAdvanceDialogue","DialogueAdvance","OnAdvance","Next","Proceed","OnProceed","NextClicked","AdvancePressed","HandleTap","OnTap","OnRejoinClicked","RejoinClicked","Rejoin","OnRejoin","OkClicked","OnOkClicked","NextClicked","OnNextClicked","AcceptButtonPressed","OnAccept"};
            void *klass=s->base.object_get_class(object);bool invoked=false;
            for(unsigned i=0;!status&&i<sizeof(welcome_methods)/sizeof(welcome_methods[0]);i++){const void *method=research_method(s,klass,welcome_methods[i],0,false);if(!method)continue;
                void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);if(!exception){invoked=true;status=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","welcome dismissed gui=%s method=%s",cls,welcome_methods[i]);break;}status=SILPH_MANAGED_EXCEPTION;}
            if(!invoked&&klass){
                void *iterator=NULL;
                for(unsigned mi=0;!invoked&&mi<384;mi++){
                    const void *method=s->base.class_get_methods(klass,&iterator);if(!method)break;
                    const char *mn=s->base.method_get_name(method);if(!mn||strchr(mn,'<'))continue;
                    if(s->base.method_get_param_count(method)!=0)continue;
                    if(strstr(mn,"Advance")||strstr(mn,"Next")||strstr(mn,"Continue")||strstr(mn,"Tap")||strstr(mn,"Proceed")||strstr(mn,"Skip")||strstr(mn,"Dismiss")||strstr(mn,"Close")){
                        void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);
                        if(!exception){invoked=true;status=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","welcome dynamic invoke gui=%s method=%s",cls,mn);break;}
                    }
                }
            }
            if(status==SILPH_MANAGED_EXCEPTION)status=invoked?0:SILPH_MANAGED_NOT_READY;
            return leave_read(s,status);
        }
        if(!status&&item_gui){
            const char *close_methods[]={"ClosePressed","OnClose","BackPressed","Dismiss","CloseButtonPressed","OnBack","HandleBack"};
            void *klass=s->base.object_get_class(object);
            for(unsigned i=0;!status&&i<7;i++){const void *method=research_method(s,klass,close_methods[i],0,false);if(!method)continue;
                void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);if(!exception){status=0;break;}status=SILPH_MANAGED_EXCEPTION;}
            if(!status)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","item bag gui closed gui=%s",cls);
            if(status==SILPH_MANAGED_EXCEPTION)status=SILPH_MANAGED_NOT_READY;
            return leave_read(s,status);
        }
        bool lobby_gui=cls[0]&&(strstr(cls,"RaidLobby")||strstr(cls,"BattleLobby")||strstr(cls,"IncomingRaid"));
        if(!status&&lobby_gui&&strstr(cls,"RaidLobbyGui"))return leave_read(s,0);
        if(!status&&lobby_gui){
            const char *lobby_methods[]={"PrimaryPressed","JoinRaid","EnterRaid","OnJoin","JoinButtonPressed","BattlePressed","OnBattle","StartRaid","ConfirmPressed","AcceptPressed","ProceedPressed","OnProceed","ScreenTapped","OnScreenTapped","TapToContinue","OkPressed","GotItPressed"};
            void *klass=s->base.object_get_class(object);
            for(unsigned i=0;!status&&i<sizeof(lobby_methods)/sizeof(lobby_methods[0]);i++){const void *method=research_method(s,klass,lobby_methods[i],0,false);if(!method)continue;
                void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);if(!exception){status=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid lobby gui method ok gui=%s method=%s",cls,lobby_methods[i]);break;}status=SILPH_MANAGED_EXCEPTION;}
            if(status==SILPH_MANAGED_EXCEPTION)status=SILPH_MANAGED_NOT_READY;
            else if(status)status=SILPH_MANAGED_NOT_READY;
            return leave_read(s,status);
        }
        bool post_gui=cls[0]&&(strstr(cls,"RaidResolve")||strstr(cls,"Achievement")||strstr(cls,"Summary")||strstr(cls,"Bonus")||strstr(cls,"RaidReward")||strstr(cls,"TrainerAchievement")||strstr(cls,"RaidResults")||strstr(cls,"PostRaid")||strstr(cls,"RewardScreen"));
        if(!post_gui&&cls[0]&&!strstr(cls,"MainMenu")&&!strstr(cls,"Encounter")&&!strstr(cls,"Lobby")&&!strstr(cls,"Approach")){
            void *pk=s->base.object_get_class(object);
            for(void *k=pk;k&&!post_gui;k=s->api.class_get_parent(k)){void *it=NULL;
                for(unsigned m=0;m<768;m++){const void *method=s->base.class_get_methods(k,&it);if(!method)break;const char *mn=s->base.method_get_name(method);
                    if(mn&&!strchr(mn,'<')&&(strstr(mn,"Summary")||strstr(mn,"BonusChallenge"))){post_gui=true;break;}}}
            for(void *k=pk;k&&!post_gui;k=s->api.class_get_parent(k)){void *it=NULL;const void *f;
                for(unsigned c=0;c<512&&(f=s->api.class_get_fields(k,&it));c++){const char *fn=s->api.field_get_name(f);
                    if(fn&&(strstr(fn,"Summary")||strstr(fn,"summary")||strstr(fn,"Bonus")||strstr(fn,"bonus")||strstr(fn,"Achievement")||strstr(fn,"achievement"))){post_gui=true;break;}}}
        }
        if(!status&&post_gui){
            const char *post_methods[]={"GoToBonusChallengePressed","OnGoToBonusChallenge","GoToBonusChallenge","StartBonusChallenge","ProceedToBonusChallenge","BeginBonusChallenge","NavigateToBonusChallenge","BonusChallengeButtonPressed","OnBonusChallengeButton","GoToSummaryPressed","OnGoToSummary","GoToSummary","SummaryContinuePressed","ContinueToSummary","SkipAllPressed","SkipAnimation","FastForwardPressed","ContinuePressed","OnContinue","NextPressed","OnNext","PrimaryPressed","OnPrimaryPressed","ProceedPressed","OkPressed","ConfirmPressed","TapToContinue","OnTapToContinue","ClaimPressed","GotItPressed","DonePressed","OnDone","Dismiss","ClosePressed","HandlePrimaryAction","OnScreenTapped","ScreenTapped","UIButtonPressed","OnButtonPressed"};
            void *klass=s->base.object_get_class(object);bool invoked=false;const char *used=NULL;
            static char button_used[96];
            if(raid_click_gui_button(s,object,cls,button_used,sizeof(button_used))){invoked=true;used=button_used;status=0;}
            if(!invoked&&strstr(cls,"RaidResolve")){
                static void *resolve_object;static uint64_t resolve_since;
                struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);uint64_t now_ms=(uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
                if(resolve_object!=object){resolve_object=object;resolve_since=now_ms;}
                if(now_ms-resolve_since>8000){
                    const void *m=research_method(s,klass,"AnimateSummaryScreen",0,false);
                    void *exception=NULL;if(m)s->base.runtime_invoke(m,object,NULL,&exception);
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid resolve no button yet; AnimateSummaryScreen found=%d ok=%d",m!=NULL,m&&!exception);
                    resolve_since=now_ms;
                }
                return leave_read(s,SILPH_MANAGED_NOT_READY);
            }
            for(unsigned i=0;!invoked&&i<sizeof(post_methods)/sizeof(post_methods[0]);i++){const void *method=research_method(s,klass,post_methods[i],0,false);if(!method)continue;
                void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);if(!exception){invoked=true;used=post_methods[i];status=0;break;}status=SILPH_MANAGED_EXCEPTION;}
            if(!invoked){
                for(void *k=klass;k&&!invoked;k=s->api.class_get_parent(k)){
                    const char *kn=s->api.class_get_name(k);if(kn&&(!strcmp(kn,"MonoBehaviour")||!strcmp(kn,"Object")||!strcmp(kn,"Behaviour")))break;
                    void *iterator=NULL;
                    for(unsigned scan=0;!invoked&&scan<768;scan++){
                        const void *method=s->base.class_get_methods(k,&iterator);if(!method)break;
                        const char *mn=s->base.method_get_name(method);if(!mn||strchr(mn,'<'))continue;
                        if(s->base.method_get_param_count(method)!=0)continue;
                        uint32_t flags=0;if(s->base.method_get_flags(method,&flags)&0x10u)continue;
                        if(strncmp(mn,"get_",4)==0||strncmp(mn,"set_",4)==0||strstr(mn,"Awake")||strstr(mn,"Destroy")||strstr(mn,"Enable")||strstr(mn,"Disable")||strstr(mn,"Update")||strstr(mn,"Init")||strstr(mn,"Share")||strstr(mn,"Friend")||strstr(mn,"Profile")||strstr(mn,"Info")||strstr(mn,"Help"))continue;
                        if(!strstr(mn,"Summary")&&!strstr(mn,"Bonus")&&!strstr(mn,"Continue")&&!strstr(mn,"Skip")&&!strstr(mn,"Next")&&!strstr(mn,"Proceed")&&!strstr(mn,"Done")&&!strstr(mn,"Finish")&&!strstr(mn,"Complete")&&!strstr(mn,"Exit"))continue;
                        void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);
                        if(!exception){invoked=true;used=mn;status=0;break;}
                    }
                }
            }
            if(invoked)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","post-battle gui action gui=%s method=%s",cls,used?used:"?");
            else{
                static char dumped[128];
                if(strcmp(dumped,cls)){snprintf(dumped,sizeof(dumped),"%s",cls);raid_dump_class(s,klass,cls);}
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","post-battle gui no method matched gui=%s",cls);
                if(!status)status=SILPH_MANAGED_NOT_READY;
            }
            if(status==SILPH_MANAGED_EXCEPTION)status=SILPH_MANAGED_NOT_READY;
            return leave_read(s,status);
        }
        const char *methods[]={"JoinRaid","EnterRaid","OnJoin","JoinButtonPressed","PrimaryPressed","BattlePressed","OnBattle","StartRaid","ReadyPressed","OnReady","ClickReady","SetReady","ContinuePressed","OnContinue","Advance","Complete","OkPressed","ConfirmPressed","AcceptPressed","OnAccept","ClaimPressed","GotItPressed","NextPressed","OnNext","TapToContinue","SkipPressed","OnSkip","DonePressed","OnDone","ScreenTapped","OnScreenTapped","DialogueAdvance","AdvanceDialogue","OnDialogueComplete","ConversationComplete","OnApproachComplete","ApproachComplete","FinishApproach","ProceedToLobby","EnterLobby","ShowLobby","OnCutsceneComplete","CutsceneComplete","SkipCutscene","OnSkipCutscene","RaidButtonPressed","OnRaidButton","BattleButtonPressed","ProceedPressed","OnProceed"};
        const char *approach_methods[]={"OnJoinButtonPressed","BattlePressed","OnBattle","BattleButtonPressed","JoinRaid","EnterRaid","OnJoin","JoinButtonPressed","PrimaryPressed","RaidButtonPressed","OnRaidButton","ProceedPressed","OnProceed","ProceedToLobby","EnterLobby","OnApproachComplete","ApproachComplete","FinishApproach","ContinuePressed","OnContinue","ConfirmPressed","AcceptPressed","ScreenTapped","OnScreenTapped","TapToContinue","SkipCutscene","OnSkipCutscene","CutsceneComplete","OnCutsceneComplete","OnPrimaryButtonPressed","PrimaryButtonPressed","HandlePrimaryAction","OnBattleButton","ShowBattleButton","RevealBattleButton","EnableBattleButton","TransitionToBattle","OpenBattleView","OnViewReady","ViewReady","HandleViewReady","OnRaidButtonPressed","OnPrimaryClicked","BeginRaid","OpenRaid","HandleRaidJoin","JoinRaidLobby","EnterRaidLobby","OnJoinRaid","OnEnterRaid","StartBattle","BeginBattle","OnBeginBattle","OnRaidJoin","OnLobbyJoin","HandleJoin","OnHandleJoin","OnStageComplete","StageComplete","CompleteStage","AdvanceStage","NextStage","MoveToNextStage","ProgressStage","OnStageFinished","StageFinished","HandleStageComplete","TransitionToNextStage","OnTransitionComplete","TransitionComplete","OnIntroComplete","IntroComplete","OnWalkComplete","WalkComplete","OnArrivalComplete","ArrivalComplete"};
        const char *gym_methods[]={"OnRaidButtonPressed","RaidButtonPressed","StartRaid","JoinRaid","EnterRaid","BattleButtonPressed","PrimaryButtonPressed","OnPrimaryClicked","PrimaryPressed","ShowRaidDetails","OpenRaid","BeginRaid","OnBattle","BattlePressed","ProceedToLobby","EnterLobby","JoinButtonPressed","OnJoin","ConfirmPressed","AcceptPressed","ScreenTapped","OnScreenTapped"};
        const char *error_methods[]={"OkPressed","GotItPressed","ClosePressed","OnClose","Dismiss","ConfirmPressed","ContinuePressed","OnContinue","RetryPressed","OnRetry","TryAgainPressed","AcceptPressed"};
        if(!status&&approach_gui&&strstr(cls,"RaidApproach")){
            const char *toasts[][2]={{"ticketToastRoot","CloseTicketToast"},{"remoteTicketToastRoot","CloseRemoteTicketToast"}};
            static void *toast_object;static unsigned toast_calls;static uint64_t toast_at;
            struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);uint64_t now_ms=(uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
            if(toast_object!=object){toast_object=object;toast_calls=0;toast_at=0;}
            if(now_ms>=toast_at){
                toast_calls++;toast_at=now_ms+(toast_calls<12?700:1500);
                static const char *const toast_names[]={"Toast","toast","Ticket","ticket","Pass","Claim","Reward","FullScreen","Fullscreen","Overlay","Blocker","TapCatcher"};
                char tused[96]={0};
                if(raid_click_child_button_ex(s,object,cls,tused,sizeof(tused),toast_names,sizeof(toast_names)/sizeof(*toast_names)))
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid pass toast tapped via %s call=%u",tused,toast_calls);
                for(unsigned i=0;i<2;i++){
                    int active=raid_child_active(s,object,toasts[i][0]);
                    const void *method=research_method(s,s->base.object_get_class(object),toasts[i][1],0,false);
                    void *exception=NULL;if(method)s->base.runtime_invoke(method,object,NULL,&exception);
                    if(toast_calls==1||active==1)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid pass toast close via %s call=%u active=%d found=%d ok=%d",toasts[i][1],toast_calls,active,method!=NULL,method&&!exception);
                }
            }
        }
        if(!status&&approach_gui&&strstr(cls,"RaidApproach")&&raid_child_active(s,object,"lockoutBox")==1){
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid approach lockout visible gui=%s (gym closed)",cls);
            return leave_read(s,SILPH_MANAGED_STATE);
        }
        const char **use=global_err?error_methods:(gym_gui?gym_methods:(approach_gui?approach_methods:methods));
        unsigned use_n=global_err?sizeof(error_methods)/sizeof(*error_methods):(gym_gui?sizeof(gym_methods)/sizeof(*gym_methods):(approach_gui?sizeof(approach_methods)/sizeof(*approach_methods):sizeof(methods)/sizeof(*methods)));
        bool invoked=false;
        if(!status){
            void *klass=s->base.object_get_class(object);
            for(unsigned i=0;!status&&i<use_n;i++){
                const void *method=research_method(s,klass,use[i],0,false);
                if(!method)continue;
                void *exception=NULL;
                s->base.runtime_invoke(method,object,NULL,&exception);
                if(!exception){
                    invoked=true;
                    status=0;
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid gui method ok gui=%s method=%s",cls,use[i]);
                    break;
                }
                status=SILPH_MANAGED_EXCEPTION;
            }
        }
        if(status==SILPH_MANAGED_EXCEPTION)status=SILPH_MANAGED_NOT_READY;
        else if(!invoked&&(approach_gui||gym_gui)){
            void *klass=s->base.object_get_class(object);void *iterator=NULL;
            for(unsigned scan=0;!invoked&&scan<768;scan++){
                const void *method=s->base.class_get_methods(klass,&iterator);if(!method)break;
                const char *mn=s->base.method_get_name(method);if(!mn||strchr(mn,'<'))continue;
                if(s->base.method_get_param_count(method)!=0)continue;
                uint32_t flags=0;if(s->base.method_get_flags(method,&flags)&0x10u)continue;
                if(strstr(mn,"Shop")||strstr(mn,"Store")||strstr(mn,"Purchase")||strstr(mn,"Buy")||strstr(mn,"Exchange")||strstr(mn,"Remote")||strstr(mn,"Private")||strstr(mn,"Invite")||strstr(mn,"Friend")||strstr(mn,"Share")||strstr(mn,"Back")||strstr(mn,"Close")||strstr(mn,"Destroy")||strstr(mn,"Dismiss")||strstr(mn,"Leave"))continue;
                /* Exclude UI-info / tag-click methods that don't advance the raid join flow */
                if(strstr(mn,"HowToPlay")||strstr(mn,"Tutorial")||strstr(mn,"Info")||strstr(mn,"Help")||strstr(mn,"Tag")||strstr(mn,"Elite")||strstr(mn,"News")||strstr(mn,"Banner")||strstr(mn,"Announce")||strstr(mn,"Detail")||strstr(mn,"Description")||strstr(mn,"Tooltip")||strstr(mn,"Hint")||(strstr(mn,"Click")&&!strstr(mn,"ClickBattle")&&!strstr(mn,"ClickJoin")&&!strstr(mn,"ClickRaid")))continue;
                if(!strstr(mn,"Battle")&&!strstr(mn,"Join")&&!strstr(mn,"Raid")&&!strstr(mn,"Lobby")&&!strstr(mn,"Approach")&&!strstr(mn,"Continue")&&!strstr(mn,"Press")&&!strstr(mn,"Primary")&&!strstr(mn,"Ready")&&!strstr(mn,"Tap")&&!strstr(mn,"Proceed")&&!strstr(mn,"Skip"))continue;
                void *exception=NULL;s->base.runtime_invoke(method,object,NULL,&exception);
                if(!exception){invoked=true;status=0;__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid approach dyn ok gui=%s method=%s",cls,mn);break;}
            }
        }
        if(status==SILPH_MANAGED_EXCEPTION)status=SILPH_MANAGED_NOT_READY;
        if(!invoked&&(approach_gui||gym_gui)){
            status=SILPH_MANAGED_NOT_READY;
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid approach no method gui=%s",cls);
        }else if(invoked&&approach_gui)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid approach gui method ok gui=%s",cls);
        return leave_read(s,status);
    }
    if(operation==RK_PARTY){
        void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
        if(!status&&(count||!research_type(s,object,"Niantic.Holoholo.Combat.Preparation","CombatPreparationRootGuiController")))status=SILPH_MANAGED_CONTRACT;
        SilphReadValue root={0};SilphManagedRef reflection={0};
        if(!status)status=call_internal(s,receiver,NULL,"get_RootTransform",SILPH_READ_REFERENCE,NULL,0,&root,permit);
        void *klass=lookup_class(s,"holo-game.dll","Niantic.Holoholo.Combat.PokemonPartyState","CombatPokemonPartyWidget");
        void *type=klass?s->api.type_get_object(s->base.class_get_type(klass)):NULL;
        if(!status)status=type?silph_managed_retain(s->managed,type,false,&reflection):SILPH_MANAGED_CONTRACT;
        SilphReadArgument argument={.kind=SILPH_READ_REFERENCE,.value.reference=reflection};
        if(!status)status=call_internal(s,root.reference,NULL,"GetComponentInChildren",SILPH_READ_REFERENCE,&argument,1,out,permit);
        int a=release_rpc_ref(s,&root.reference),b=release_rpc_ref(s,&reflection);return leave_read(s,status?status:a?a:b);
    }
    const char *spaces[]={"Niantic.Holoholo.NUI","Niantic.Holoholo.Pokestop","Niantic.Holoholo.Combat.NpcTrainers","Niantic.Holoholo.Combat.PokemonPartyState","Niantic.Holoholo.Incident","Niantic.Holoholo.UI","Niantic.Holoholo.Incident","Niantic.Holoholo.Incident","Niantic.Holoholo.Encounter","Niantic.Holoholo.Encounter","Niantic.Holoholo.Incident","Niantic.Holoholo.Pokestop"};
    const char *names[]={"GuiLayerService","MapPokestop","NpcTrainerInterstitialGui","CombatPokemonPartyWidget","IncidentCombatResolveGuiController","ItemFragmentAssemblyService","IncidentService","IncidentService","EncounterState","EncounterState","IncidentInvasionDialogGui","PokestopSpinnerInteractionGuiController"};
    const char *methods[]={"TopGui","OnTap","PressChallenge","SubmitButtonPressed","DoneButtonClicked","DismissAllOpenModals","StartIncidentEncounter","StopCurrentIncident","EncounterStateComplete","CaptureCompleted","ClickedToContinue","ClickExit"};
    void *object=NULL;status=silph_managed_target(s->managed,receiver,&object);
    if(!status&&operation==RK_TAP){
        if(count)status=SILPH_MANAGED_ARGUMENT;
        if(!status){bool ok=research_type(s,object,"Niantic.Holoholo.Pokestop","MapPokestop")||research_type(s,object,"Niantic.Holoholo.Gym","MapGym")||research_type(s,object,"Niantic.Holoholo.Gym","MapGymDetails")||research_type(s,object,"Niantic.Holoholo.Map","MapPlace");
            if(!ok)status=SILPH_MANAGED_CONTRACT;}
        const char *tap_names[]={"OnTapped","OnTap","OnClick","Tap","Interact","Open","ShowDetails","Enter","EnterGym","OpenGym","Select","ForceClick"};
        if(!status){
            void *klass=s->base.object_get_class(object);
            const char *cname=klass?s->api.class_get_name(klass):"?";
            for(unsigned i=0;!status&&i<12;i++){
                const void *method=research_method(s,klass,tap_names[i],0,false);
                uint32_t pcount=0;
                if(!method&&!strcmp(tap_names[i],"OnTap")){
                    method=research_method(s,klass,tap_names[i],2,false);
                    pcount=2;
                }
                if(!method)continue;
                void *values[2]={0},*exception=NULL;
                s->base.runtime_invoke(method,object,pcount>0?values:NULL,&exception);
                if(!exception){
                    status=0;
                    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","map tap ok target=%s method=%s params=%u",cname,tap_names[i],pcount);
                    break;
                }
                status=SILPH_MANAGED_EXCEPTION;
            }
        }
        if(status==SILPH_MANAGED_EXCEPTION)status=SILPH_MANAGED_NOT_READY;
    }else if(!status&&!research_type(s,object,spaces[operation-1],names[operation-1]))status=SILPH_MANAGED_CONTRACT;
    else if(!status){
        if(operation==RK_ENCOUNTER_FINISH&&(count!=1||args[0].kind!=SILPH_READ_ENUM32||args[0].value.bits>3))status=SILPH_MANAGED_ARGUMENT;
        if(!status)status=call_internal(s,receiver,NULL,methods[operation-1],operation==RK_TOP?SILPH_READ_REFERENCE:SILPH_READ_VOID,args,count,out,permit);
    }
    return leave_read(s,status);
}
int silph_raid_class(void *memory,SilphManagedRef ref,char *output,uint32_t capacity){
    if(!output||capacity<2||capacity>256)return SILPH_MANAGED_ARGUMENT;output[0]=0;
    Read *s=memory;int status=enter_read(s);if(status)return status;void *object=NULL;
    status=silph_managed_target(s->managed,ref,&object);const char *name=status?NULL:s->api.class_get_name(s->base.object_get_class(object));
    if(!status&&(!name||strnlen(name,capacity)>=capacity))status=SILPH_MANAGED_CONTRACT;
    if(!status)memcpy(output,name,strlen(name)+1);return leave_read(s,status);
}
int silph_raid_dict_val_type(void *memory,SilphManagedRef ref,char *output,uint32_t capacity){
    if(!output||capacity<2||capacity>256)return SILPH_MANAGED_ARGUMENT;output[0]=0;
    Read *s=memory;int status=enter_read(s);if(status)return status;void *object=NULL;
    status=silph_managed_target(s->managed,ref,&object);
    if(!status&&object){
        void *klass=s->base.object_get_class(object);
        void *iter=NULL;const void *method=NULL;
        while((method=s->base.class_get_methods(klass,&iter))!=NULL){
            const char *mname=s->base.method_get_name(method);
            if(mname&&!strcmp(mname,"get_Item")){
                const void *ret_type=s->base.method_get_return_type(method);
                void *val_class=ret_type?s->api.class_from_type(ret_type):NULL;
                const char *vname=val_class?s->api.class_get_name(val_class):NULL;
                if(vname&&strnlen(vname,capacity)<capacity){
                    memcpy(output,vname,strlen(vname)+1);
                    break;
                }
            }
        }
    }
    return leave_read(s,status);
}
void silph_dump_methods(void *memory,SilphManagedRef ref,const char *tag){
    Read *s=memory;void *obj=NULL;
    if(silph_managed_target(s->managed,ref,&obj)!=0||!obj)return;
    void *klass=s->base.object_get_class(obj);if(!klass)return;
    const char *cname=s->api.class_get_name(klass);
    __android_log_print(ANDROID_LOG_INFO,"SilphDump","[%s] class: %s",tag,cname?cname:"?");
    void *iter=NULL;const void *method=NULL;
    while((method=s->base.class_get_methods(klass,&iter))!=NULL){
        const char *mname=s->base.method_get_name(method);
        const void *ret_type=s->base.method_get_return_type(method);
        void *ret_klass=ret_type?s->api.class_from_type(ret_type):NULL;
        const char *ret_name=ret_klass?s->api.class_get_name(ret_klass):"?";
        uint32_t pcount=s->base.method_get_param_count(method);
        char params[128]={0};
        for(uint32_t p=0;p<pcount&&p<3;p++){
            const void *pt=s->api.method_get_param(method,p);
            void *pk=pt?s->api.class_from_type(pt):NULL;
            const char *pn=pk?s->api.class_get_name(pk):"?";
            size_t len=strlen(params);
            snprintf(params+len,sizeof(params)-len,"%s%s",p>0?",":"",pn?pn:"?");
        }
        if(mname)__android_log_print(ANDROID_LOG_INFO,"SilphDump","[%s] method: %s(%s) -> %s",tag,mname,params,ret_name?ret_name:"?");
        if(mname&&!strcmp(mname,"get_CurrentRaidState")&&ret_klass){
            void *fiter=NULL;const void *ef=NULL;
            while((ef=s->api.class_get_fields(ret_klass,&fiter))!=NULL){
                const char *efname=s->api.field_get_name(ef);
                __android_log_print(ANDROID_LOG_INFO,"SilphDump","RaidState enum field: %s",efname?efname:"?");
            }
        }
    }
    iter=NULL;const void *field=NULL;
    while((field=s->api.class_get_fields(klass,&iter))!=NULL){
        const char *fname=s->api.field_get_name(field);
        const void *ftype=s->api.field_get_type(field);
        void *fklass=ftype?s->api.class_from_type(ftype):NULL;
        const char *ft_name=fklass?s->api.class_get_name(fklass):"?";
        const char *ft_ns=fklass?s->api.class_get_namespace(fklass):"";
        if(fname)__android_log_print(ANDROID_LOG_INFO,"SilphDump","[%s] field: %s (%s.%s)",tag,fname,ft_ns?ft_ns:"",ft_name?ft_name:"?");
    }
}
int silph_raid_probe_return_type(void *memory,SilphManagedRef ref,const char *ret_type_name,uint32_t *out){
    if(!out||!ret_type_name||!memory||!ref.serial)return SILPH_MANAGED_ARGUMENT;
    *out=0;
    Read *s=memory;int status=enter_read(s);if(status)return status;
    void *obj=NULL;status=silph_managed_target(s->managed,ref,&obj);
    if(status||!obj)return leave_read(s,status?status:SILPH_MANAGED_ARGUMENT);
    void *klass=s->base.object_get_class(obj);if(!klass)return leave_read(s,SILPH_MANAGED_CONTRACT);
    void *iter=NULL;const void *method=NULL;
    while((method=s->base.class_get_methods(klass,&iter))!=NULL){
        uint32_t pcount=s->base.method_get_param_count(method);
        if(pcount!=0)continue;
        const void *ret_type=s->base.method_get_return_type(method);
        void *ret_klass=ret_type?s->api.class_from_type(ret_type):NULL;
        const char *ret_name=ret_klass?s->api.class_get_name(ret_klass):NULL;
        if(ret_name&&!strcmp(ret_name,ret_type_name)){
            const char *mname=s->base.method_get_name(method);
            if(mname){
                SilphReadValue val={0};
                int call_res=call_internal(s,ref,NULL,mname,SILPH_READ_ENUM32,NULL,0,&val,NULL);
                if(!call_res){
                    *out=(uint32_t)val.bits;
                    return leave_read(s,0);
                }
            }
        }
    }
    return leave_read(s,SILPH_MANAGED_NOT_READY);
}
int silph_raid_medicine(void *memory,const SilphSchedulePermit *permit,SilphManagedRef bag,uint64_t id,uint32_t item,SilphManagedRef *out){
    if(!out||!id||!((item>=101&&item<=104)||item==201||item==202)||!silph_schedule_permitted(permit,true))return SILPH_MANAGED_ARGUMENT;
    *out=(SilphManagedRef){0};Read *s=memory;int status=enter_read(s);if(status)return status;void *object=NULL;
    status=silph_managed_target(s->managed,bag,&object);if(!status&&!research_type(s,object,"Niantic.Holoholo.Internal","ItemBagImpl"))status=SILPH_MANAGED_CONTRACT;
    SilphReadArgument args[]={{.kind=SILPH_READ_ENUM32,.value.bits=item},{.kind=SILPH_READ_U64,.value.bits=id}};SilphReadValue value={0};
    if(!status)status=call_internal(s,bag,NULL,"UseItemOnPokemon",SILPH_READ_REFERENCE,args,2,&value,permit);
    if(!status){*out=value.reference;value.reference=(SilphManagedRef){0};}int freed=release_rpc_ref(s,&value.reference);return leave_read(s,status?status:freed);
}
int silph_raid_prepared_team(void *memory,SilphManagedRef widget,uint64_t out[3],bool *usable){
    if(!out||!usable)return SILPH_MANAGED_ARGUMENT;*usable=true;memset(out,0,3*sizeof(*out));Read *s=memory;int status=enter_read(s);if(status)return status;
    void *object=NULL;status=silph_managed_target(s->managed,widget,&object);
    if(!status&&!research_type(s,object,"Niantic.Holoholo.Combat.PokemonPartyState","CombatPokemonPartyWidget"))status=SILPH_MANAGED_CONTRACT;
    SilphManagedRef scroll={0};SilphReadValue page={0},party={0},array={0},length={0},pokemon={0};
    const void *field=status?NULL:find_field(s,s->base.object_get_class(object),"dynoScrollRect",NULL,NULL,&status);
    if(!status){void *v=NULL;s->api.field_get_value(object,field,&v);status=accept_reference(s,s->api.field_get_type(field),v,&page);scroll=page.reference;clear_value(&page);}
    if(!status)status=call_internal(s,scroll,NULL,"CurrentPageGui",SILPH_READ_REFERENCE,NULL,0,&page,NULL);
    if(!status&&!page.reference.serial)status=SILPH_MANAGED_NOT_READY;
    if(!status)status=call_internal(s,page.reference,NULL,"get_BattleParty",SILPH_READ_REFERENCE,NULL,0,&party,NULL);
    if(!status)status=silph_managed_target(s->managed,party.reference,&object);
    field=status?NULL:find_field(s,s->base.object_get_class(object),"TeamProtos",NULL,NULL,&status);
    if(!status){void *v=NULL;s->api.field_get_value(object,field,&v);status=accept_reference(s,s->api.field_get_type(field),v,&array);}
    if(!status)status=call_internal(s,array.reference,NULL,"get_Length",SILPH_READ_I32,NULL,0,&length,NULL);
    if(!status&&length.bits!=3)status=SILPH_MANAGED_CONTRACT;
    for(unsigned i=0;!status&&i<3;i++){
        SilphReadArgument argument={.kind=SILPH_READ_I32,.value.bits=i};status=call_internal(s,array.reference,NULL,"GetValue",SILPH_READ_REFERENCE,&argument,1,&pokemon,NULL);
        if(!status)status=silph_managed_target(s->managed,pokemon.reference,&object);
        if(!status&&!research_type(s,object,"Holoholo.Rpc","PokemonProto"))status=SILPH_MANAGED_CONTRACT;
        field=status?NULL:find_field(s,s->base.object_get_class(object),"id_",NULL,NULL,&status);
        if(!status&&!matches_kind(s,s->api.field_get_type(field),SILPH_READ_U64))status=SILPH_MANAGED_CONTRACT;
        if(!status){s->api.field_get_value(object,field,&out[i]);if(!out[i])status=SILPH_MANAGED_CONTRACT;for(unsigned j=0;j<i;j++)if(out[j]==out[i])status=SILPH_MANAGED_CONTRACT;}
        uint32_t hp=0,max=0;field=status?NULL:find_field(s,s->base.object_get_class(object),"stamina_",NULL,NULL,&status);
        if(!status&&!matches_kind(s,s->api.field_get_type(field),SILPH_READ_I32))status=SILPH_MANAGED_CONTRACT;
        if(!status)s->api.field_get_value(object,field,&hp);field=status?NULL:find_field(s,s->base.object_get_class(object),"maxStamina_",NULL,NULL,&status);
        if(!status&&!matches_kind(s,s->api.field_get_type(field),SILPH_READ_I32))status=SILPH_MANAGED_CONTRACT;
        if(!status){s->api.field_get_value(object,field,&max);if(!hp||!max||hp!=max)*usable=false;}
        int d=release_rpc_ref(s,&pokemon.reference);if(!status)status=d;
    }
    SilphManagedRef *refs[]={&pokemon.reference,&array.reference,&party.reference,&page.reference,&scroll};for(unsigned i=0;i<5;i++){int d=release_rpc_ref(s,refs[i]);if(!status)status=d;}
    return leave_read(s,status);
}
int silph_raid_map_settings(void *memory,const SilphSchedulePermit *permit,SilphManagedRef ref,float saved[3],bool restore){
    if(!saved||(!restore&&!silph_schedule_permitted(permit,true)))return SILPH_MANAGED_ARGUMENT;
    Read *s=memory;int status=enter_read(s);if(status)return status;void *object=NULL;
    status=silph_managed_target(s->managed,ref,&object);if(!status&&!research_type(s,object,"Holoholo.Rpc","MapSettingsProto"))status=SILPH_MANAGED_CONTRACT;
    const char *names[]={"GetMapObjectsMinRefreshSeconds","GetMapObjectsMaxRefreshSeconds","GetMapObjectsMinDistanceMeters"};
    const float applied[]={5,10,8};float current[3]={0};const void *fields[3]={0};
    for(unsigned i=0;!status&&i<3;i++){
        fields[i]=find_field(s,s->base.object_get_class(object),names[i],NULL,NULL,&status);
        if(!status&&!matches_kind(s,s->api.field_get_type(fields[i]),SILPH_READ_F32))status=SILPH_MANAGED_CONTRACT;
        if(!status){s->api.field_get_value(object,fields[i],&current[i]);if(!isfinite(current[i])||current[i]<0||current[i]>3600||(restore&&(!isfinite(saved[i])||saved[i]<0||saved[i]>3600)))status=SILPH_MANAGED_CONTRACT;}
    }
    if(!status)for(unsigned i=0;i<3;i++){if(!restore)saved[i]=current[i];if(!restore||current[i]==applied[i]){float value=restore?saved[i]:applied[i];s->field_set_value(object,fields[i],&value);}}
    return leave_read(s,status);
}
static void *raid_nested(Read *s,void *parent,const char *wanted){
    if(!s->research_nested)return NULL;void *iterator=NULL,*found=NULL;
    for(unsigned i=0;parent&&i<256;i++){void *type=s->research_nested(parent,&iterator);if(!type)break;const char *name=s->api.class_get_name(type);
        if(name&&!strcmp(name,wanted)){if(found)return NULL;found=type;}}
    return found;
}
int silph_raid_attack(void *memory,const SilphSchedulePermit *permit,SilphManagedRef director,uint32_t attack,float score,uint64_t outgoing,uint64_t incoming,SilphManagedRef *out){
    if(!out)return SILPH_MANAGED_ARGUMENT;memset(out,0,sizeof(*out));
    if(!silph_schedule_permitted(permit,true)||!isfinite(score)||score<0||score>1||attack<1||attack>7)return SILPH_MANAGED_STATE;
    if(attack==7&&(!outgoing||!incoming||outgoing==incoming))return SILPH_MANAGED_ARGUMENT;
    if(attack>4&&attack!=7)return SILPH_MANAGED_ARGUMENT;
    Read *s=memory;int status=enter_read(s);if(status)return status;void *receiver=NULL;
    status=silph_managed_target(s->managed,director,&receiver);
    if(!status&&!research_type(s,receiver,"Niantic.Holoholo.VNext","BattleDirector"))status=SILPH_MANAGED_CONTRACT;
    SilphManagedRef event={0},payload={0};SilphReadValue value={0};
    if(!status)status=fresh_proto(s,"BattleEventProto",&event);
    void *parent=lookup_class(s,"holo-protos.dll","Holoholo.Rpc","BattleEventProto");
    void *nested=raid_nested(s,raid_nested(s,parent,"Types"),attack==7?"SwapPokemon":"Attack");
    if(!status&&!nested)status=SILPH_MANAGED_CONTRACT;
    if(!status){void *object=s->object_new(nested);status=object?silph_managed_retain(s->managed,object,false,&payload):SILPH_MANAGED_CAPACITY;}
    if(!status)status=call_internal(s,payload,NULL,".ctor",SILPH_READ_VOID,NULL,0,&value,permit);
    if(!status&&attack==7){status=fresh_scalar(s,payload,"OutgoingPokemonId",SILPH_READ_U64,outgoing);if(!status)status=fresh_scalar(s,payload,"IncomingPokemonId",SILPH_READ_U64,incoming);}
    if(!status&&attack!=7){uint32_t bits;memcpy(&bits,&score,4);status=fresh_scalar(s,payload,"AttackType",SILPH_READ_ENUM32,attack);if(!status)status=fresh_scalar(s,payload,"Score",SILPH_READ_F32,bits);}
    SilphReadArgument argument={.kind=SILPH_READ_REFERENCE,.value.reference=payload};
    if(!status)status=call_internal(s,event,NULL,attack==7?"set_SwapPokemon":"set_Attack",SILPH_READ_VOID,&argument,1,&value,permit);
    if(!status)status=fresh_scalar(s,event,"Type",SILPH_READ_ENUM32,attack==7?7:4);
    SilphReadArgument args[]={{.kind=SILPH_READ_REFERENCE,.value.reference=event},{.kind=SILPH_READ_BOOL,.value.bits=1}};
    if(!status)status=call_internal(s,director,NULL,"TrySubmitPlayerAction",SILPH_READ_REFERENCE,args,2,&value,permit);
    if(!status){*out=value.reference;value.reference=(SilphManagedRef){0};}
    int a=release_rpc_ref(s,&payload),b=release_rpc_ref(s,&event),c=release_rpc_ref(s,&value.reference);
    return leave_read(s,status?status:a?a:b?b:c);
}
