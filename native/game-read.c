#define _GNU_SOURCE
#include "game-read.h"
#include <android/log.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <time.h>
#include <stdio.h>

int silph_raid_class(void *memory,SilphManagedRef ref,char *output,uint32_t capacity);
int silph_raid_dict_val_type(void *memory,SilphManagedRef ref,char *output,uint32_t capacity);
void silph_dump_methods(void *memory,SilphManagedRef ref,const char *tag);
int silph_raid_probe_return_type(void *memory,SilphManagedRef ref,const char *ret_type_name,uint32_t *out);

typedef struct {
    void *read,*managed;
    SilphManagedApi api;
    SilphManagedRef map,container,player,pokemon_bag,item_bag,directory,directory_type;
    char account[1025];
    uint64_t samples,account_hi,account_lo;
    SilphGameSnapshot previous;
    unsigned opened,stage,snapshot_ready,compact;
    char raid_skip[32][161];
    unsigned raid_skip_count;
    char field_pokemon[16];
    char field_stops[16];
    char field_gyms[16];
} GameRead;
static bool gym_id_skipped(GameRead *s,const char *id){
    if(!s||!id||!id[0])return false;
    for(unsigned i=0;i<s->raid_skip_count&&i<32;i++)if(!strcmp(id,s->raid_skip[i]))return true;
    return false;
}
void silph_game_read_raid_gym_clear_skips(void *memory){
    GameRead *s=memory;if(!s)return;s->raid_skip_count=0;memset(s->raid_skip,0,sizeof(s->raid_skip));
}
int silph_game_read_raid_gym_add_skip(void *memory,const char *fort){
    GameRead *s=memory;if(!s||!fort||!fort[0])return SILPH_MANAGED_ARGUMENT;
    if(gym_id_skipped(s,fort))return 0;
    if(s->raid_skip_count>=32)return SILPH_MANAGED_CAPACITY;
    memcpy(s->raid_skip[s->raid_skip_count],fort,161);s->raid_skip_count++;
    return 0;
}
size_t silph_game_read_size(void) { return sizeof(GameRead); }
unsigned silph_game_read_stage(void *memory) { return memory?((GameRead *)memory)->stage:0; }
uint64_t silph_game_read_samples(void *memory){GameRead *s=memory;return s&&s->opened?s->samples:0;}
int silph_game_read_compact(void *memory){GameRead *s=memory;if(!s||!s->opened||s->samples)return SILPH_MANAGED_STATE;s->compact=1;return 0;}
static int release(GameRead *state,SilphManagedRef *ref) {
    if(!ref->serial)return 0;
    int status=silph_managed_release(state->managed,*ref);if(!status)memset(ref,0,sizeof(*ref));return status;
}
static int scalar(GameRead *s,SilphManagedRef ref,const char *name,uint32_t *output) {
    SilphReadValue value;int status=silph_read_call(s->read,ref,name,SILPH_READ_I32,NULL,0,&value);
    int32_t count=0;if(!status)memcpy(&count,&value.bits,4);
    if(!status&&(count<0||count>100000))status=SILPH_MANAGED_CONTRACT;
    if(!status)*output=(uint32_t)count;return status;
}
static void init_directory_fields(GameRead *s){
    if(s->field_stops[0]&&strcmp(s->field_stops,"emdp")!=0&&s->field_pokemon[0]&&strcmp(s->field_pokemon,"emds")!=0)return;
    const char *candidates[]={"emkv","emkw","emkx","emky","emdp","emds","emdg",NULL};
    for(int i=0;candidates[i];i++){
        SilphReadValue dict={0};
        int r=silph_read_field(s->read,s->directory,candidates[i],SILPH_READ_REFERENCE,&dict);
        if(r||!dict.reference.serial)continue;
        uint32_t count=0;
        (void)scalar(s,dict.reference,"get_Count",&count);
        char val_type[128]={0};
        silph_raid_dict_val_type(s->read,dict.reference,val_type,sizeof(val_type));
        if(!val_type[0]){
            SilphReadValue vals={0};
            int vr=silph_read_call(s->read,dict.reference,"get_Values",SILPH_READ_REFERENCE,NULL,0,&vals);
            if(!vr&&vals.reference.serial){
                SilphReadValue iter={0},current={0},more={0};
                int ir=silph_read_call(s->read,vals.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iter);
                if(ir==SILPH_MANAGED_CONTRACT)ir=silph_read_call(s->read,vals.reference,"GetEnumerator",SILPH_READ_REFERENCE,NULL,0,&iter);
                if(!ir&&iter.reference.serial){
                    int mr=silph_read_call(s->read,iter.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&more);
                    if(!mr&&more.bits){
                        silph_read_call(s->read,iter.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
                        if(current.reference.serial){
                            silph_raid_class(s->read,current.reference,val_type,sizeof(val_type));
                            release(s,&current.reference);
                        }
                    }
                    silph_read_call(s->read,iter.reference,"Dispose",SILPH_READ_VOID,NULL,0,&more);
                    release(s,&iter.reference);
                }
                release(s,&vals.reference);
            }
        }
        release(s,&dict.reference);
        if(count>0||val_type[0])__android_log_print(ANDROID_LOG_INFO,"SilphRead","directory field '%s': count=%u val_type=%s",candidates[i],count,val_type);
        if(strcasestr(val_type,"pokemon")||strcasestr(val_type,"wild")){
            strncpy(s->field_pokemon,candidates[i],sizeof(s->field_pokemon)-1);
        }else if(strcasestr(val_type,"gym")){
            strncpy(s->field_gyms,candidates[i],sizeof(s->field_gyms)-1);
        }else if(strcasestr(val_type,"stop")||strcasestr(val_type,"place")||strcasestr(val_type,"fort")){
            strncpy(s->field_stops,candidates[i],sizeof(s->field_stops)-1);
        }
    }
    if(s->field_stops[0]&&s->field_pokemon[0]){
        __android_log_print(ANDROID_LOG_INFO,"SilphRead","directory mapped: pokemon=%s stops=%s gyms=%s",s->field_pokemon,s->field_stops,s->field_gyms);
    }
}
static int account(GameRead *state,char *output,size_t bytes) {
    SilphReadValue value;int status=silph_read_call(state->read,state->player,"get_PlayerId",SILPH_READ_REFERENCE,NULL,0,&value);
    if(!status&&!value.reference.serial)status=SILPH_MANAGED_CONTRACT;
    SilphReadValue size={0};
    if(!status)status=silph_read_call(state->read,value.reference,"get_Length",SILPH_READ_I32,NULL,0,&size);
    if(!status&&(!size.bits||size.bits>256))status=SILPH_MANAGED_CONTRACT;
    uint32_t length=0;
    if(!status)status=silph_read_utf8(state->read,value.reference,output,(uint32_t)bytes,&length);
    if(!status&&(!length||length>1024))status=SILPH_MANAGED_CONTRACT;
    if(!status)for(uint32_t i=0;i<length;i++)if((unsigned char)output[i]<=32||output[i]==127){status=SILPH_MANAGED_CONTRACT;break;}
    int freed=release(state,&value.reference);return status?status:freed;
}
static uint64_t hash_account(const char *text,uint64_t hash) {
    for(const unsigned char *p=(const void *)text;*p;p++){hash^=*p;hash*=UINT64_C(1099511628211);}return hash;
}
int silph_game_read_close(void *memory) {
    GameRead *s=memory;if(!s)return SILPH_MANAGED_ARGUMENT;
    SilphManagedRef *refs[]={&s->directory_type,&s->directory,&s->item_bag,&s->pokemon_bag,&s->player,&s->container,&s->map};int first=0;
    for(unsigned i=0;i<7;i++){int status=release(s,refs[i]);if(!first)first=status;}
    if(!first)memset(s,0,sizeof(*s));return first;
}
int silph_game_read_open(void *memory,size_t bytes,void *read,const SilphManagedApi *api,void *managed,void *map) {
    if(!memory||bytes!=sizeof(GameRead)||!read||!api||!managed||!map)return SILPH_MANAGED_ARGUMENT;
    GameRead *s=memory;if(s->opened)return SILPH_MANAGED_STATE;
    memset(s,0,bytes);s->read=read;s->managed=managed;s->api=*api;s->opened=1;
    s->stage=10;int status=silph_managed_retain(managed,map,false,&s->map);
    if(!status){s->stage=11;status=silph_read_field_type(read,s->map,"Zenject","DiContainer",&s->container);}
    if(!status){s->stage=12;status=silph_read_field_type(read,s->map,"Niantic.Holoholo","IPlayerService",&s->player);}
    if(!status){s->stage=13;status=silph_read_field_type(read,s->map,"Niantic.Holoholo","IPokemonBag",&s->pokemon_bag);}
    if(!status){s->stage=14;status=silph_read_field_type(read,s->map,"Niantic.Holoholo","IItemBag",&s->item_bag);}
    if(!status&&(!s->container.serial||!s->player.serial||!s->pokemon_bag.serial||!s->item_bag.serial))status=SILPH_MANAGED_CONTRACT;
    if(!status){s->stage=15;status=silph_read_type_object(read,"holo-game.dll","Niantic.Holoholo.Map","IMapPlaceDirectoryService",&s->directory_type);}
    if(!status) {
        SilphReadArgument arg={.kind=SILPH_READ_REFERENCE,.value.reference=s->directory_type};SilphReadValue value;
        s->stage=16;status=silph_read_call(read,s->container,"TryResolve",SILPH_READ_REFERENCE,&arg,1,&value);s->directory=value.reference;
        if(!status&&!s->directory.serial)status=SILPH_MANAGED_CONTRACT;
    }
    if(!status){s->stage=17;status=account(s,s->account,sizeof(s->account));}
    if(!status) {
        s->account_hi=hash_account(s->account,UINT64_C(14695981039346656037));
        s->account_lo=hash_account(s->account,UINT64_C(7809847782465536322));
    }else {unsigned stage=s->stage;silph_game_read_close(s);s->stage=stage;}
    return status;
}
static int same_field(GameRead *s,void *map,const char *space,const char *name,SilphManagedRef previous) {
    SilphManagedRef fresh={0};int status=silph_read_field_type(s->read,s->map,space,name,&fresh);
    void *a=NULL,*b=NULL,*held_map=NULL;
    if(!status)status=silph_managed_target(s->managed,s->map,&held_map);
    if(!status&&held_map!=map)status=SILPH_MANAGED_STALE;
    if(!status)status=silph_managed_target(s->managed,fresh,&a);
    if(!status)status=silph_managed_target(s->managed,previous,&b);
    if(!status&&a!=b)status=SILPH_MANAGED_STALE;
    int freed=release(s,&fresh);return status?status:freed;
}
static int candidates(GameRead *s,SilphManagedRef dictionary,SilphGameSnapshot *snapshot) {
    SilphReadValue collection={0},iterator={0},current={0},value={0};
    s->stage=41;int status=silph_read_call(s->read,dictionary,"get_Values",SILPH_READ_REFERENCE,NULL,0,&collection);
    if(!status){s->stage=42;status=silph_read_call(s->read,collection.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);}
    for(unsigned i=0;!status&&i<8;i++) {
        s->stage=43;status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&value);
        if(status||!value.bits)break;
        s->stage=44;status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
        if(!status){s->stage=45;status=silph_read_call(s->read,current.reference,"get_EncounterId",SILPH_READ_U64,NULL,0,&value);}
        uint64_t encounter=value.bits;
        if(!status&&!encounter)status=SILPH_MANAGED_CONTRACT;
        uint32_t species=0;
        if(!status){s->stage=46;status=scalar(s,current.reference,"get_PokemonId",&species);}
        if(!status&&!species)status=SILPH_MANAGED_CONTRACT;
        if(!status) {
            if(!snapshot->candidates_read){snapshot->first_encounter=encounter;snapshot->first_species=species;}
            snapshot->candidates_read++;
        }
        int freed=release(s,&current.reference);if(!status)status=freed;
    }
    int disposed=0;
    if(iterator.reference.serial){if(!status)s->stage=47;disposed=silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&value);}
    int a=release(s,&current.reference),b=release(s,&iterator.reference),c=release(s,&collection.reference);
    return status?status:disposed?disposed:a?a:b?b:c;
}
int silph_game_read_poll(void *memory,void *map,bool inventory,SilphGameSnapshot *output) {
    GameRead *s=memory;if(!s||!s->opened||!map||!output)return SILPH_MANAGED_ARGUMENT;
    memset(output,0,sizeof(*output));
    s->stage=20;int status=same_field(s,map,"Zenject","DiContainer",s->container);
    if(!status)status=same_field(s,map,"Niantic.Holoholo","IPlayerService",s->player);
    if(!status)status=same_field(s,map,"Niantic.Holoholo","IPokemonBag",s->pokemon_bag);
    if(!status)status=same_field(s,map,"Niantic.Holoholo","IItemBag",s->item_bag);
    if(!status) {
        SilphReadArgument arg={.kind=SILPH_READ_REFERENCE,.value.reference=s->directory_type};SilphReadValue fresh;
        status=silph_read_call(s->read,s->container,"TryResolve",SILPH_READ_REFERENCE,&arg,1,&fresh);
        void *a=NULL,*b=NULL;
        if(!status)status=silph_managed_target(s->managed,fresh.reference,&a);
        if(!status)status=silph_managed_target(s->managed,s->directory,&b);
        if(!status&&a!=b)status=SILPH_MANAGED_STALE;
        int freed=release(s,&fresh.reference);if(!status)status=freed;
    }
    char id[sizeof(s->account)]={0};if(!status)status=account(s,id,sizeof(id));
    if(!status&&strcmp(id,s->account))status=SILPH_MANAGED_STALE;
    if(!status&&!inventory&&s->snapshot_ready){*output=s->previous;output->samples=++s->samples;return 0;}
    if(!status)status=scalar(s,s->pokemon_bag,"get_Count",&output->pokemon_count);
    if(!status)status=scalar(s,s->player,"get_MaxPokemon",&output->max_pokemon);
    if(!status)status=scalar(s,s->item_bag,"GetTotalItems",&output->item_count);
    if(!status)status=scalar(s,s->player,"get_MaxItems",&output->max_items);
    const int32_t items[]={1,2,3,701,703,705,706,708};
    for(unsigned i=0;!status&&i<8;i++) {
        SilphReadArgument arg={.kind=SILPH_READ_ENUM32,.value.bits=(uint32_t)items[i]};SilphReadValue value;
        s->stage=30+i;status=silph_read_call(s->read,s->item_bag,"GetItemCount",SILPH_READ_I32,&arg,1,&value);
        int32_t count=0;if(!status)memcpy(&count,&value.bits,4);
        if(!status&&(count<0||count>100000))status=SILPH_MANAGED_CONTRACT;
        if(!status)output->items[i]=(uint32_t)count;
    }
    SilphReadValue pokemon={0},stops={0};
    init_directory_fields(s);
    if(!s->field_pokemon[0]||!s->field_stops[0]){
        if(!s->field_pokemon[0])strncpy(s->field_pokemon,"emky",sizeof(s->field_pokemon)-1);
        if(!s->field_stops[0])strncpy(s->field_stops,"emkv",sizeof(s->field_stops)-1);
        if(!s->field_gyms[0])strncpy(s->field_gyms,"emkw",sizeof(s->field_gyms)-1);
    }
    const char *pfield = s->field_pokemon;
    const char *sfield = s->field_stops;
    if(!status){s->stage=38;status=silph_read_field(s->read,s->directory,pfield,SILPH_READ_REFERENCE,&pokemon);}
    if(!status){s->stage=39;status=silph_read_field(s->read,s->directory,sfield,SILPH_READ_REFERENCE,&stops);}
    if(status==SILPH_MANAGED_CONTRACT&&!s->samples){
        if(pokemon.reference.serial)release(s,&pokemon.reference);
        if(stops.reference.serial)release(s,&stops.reference);
        return SILPH_MANAGED_NOT_READY;
    }
    if(!status)status=scalar(s,pokemon.reference,"get_Count",&output->map_pokemon);
    if(!status)status=scalar(s,stops.reference,"get_Count",&output->map_stops);
    if(!status&&!s->compact)status=candidates(s,pokemon.reference,output);
    int a=release(s,&stops.reference),b=release(s,&pokemon.reference);if(!status)status=a?a:b;
    if(!status){output->samples=++s->samples;output->account_hi=s->account_hi;output->account_lo=s->account_lo;s->previous=*output;s->snapshot_ready=1;}
    else memset(output,0,sizeof(*output));return status;
}
int silph_game_read_sample(void *memory,void *map,SilphGameSnapshot *output){return silph_game_read_poll(memory,map,true,output);}
int silph_game_read_refresh(void *memory,bool inventory,SilphGameSnapshot *output){
    GameRead *s=memory;if(!s||!s->opened||!output)return SILPH_MANAGED_ARGUMENT;
    void *map=NULL;int status=silph_managed_target(s->managed,s->map,&map);return status?status:silph_game_read_poll(s,map,inventory,output);
}
static int location(GameRead *s,SilphManagedRef object,double *lat,double *lon) {
    if(!s||!object.serial||!lat||!lon)return SILPH_MANAGED_ARGUMENT;
    *lat=NAN;*lon=NAN;
    SilphReadValue box={0},a={0},b={0};
    int status=silph_read_call(s->read,object,"get_Location",SILPH_READ_BOXED,NULL,0,&box);
    if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,object,"get_Location",SILPH_READ_REFERENCE,NULL,0,&box);
    if(!status&&box.reference.serial){
        if(silph_read_field(s->read,box.reference,"Latitude",SILPH_READ_F64,&a))
            silph_read_call(s->read,box.reference,"get_Latitude",SILPH_READ_F64,NULL,0,&a);
        if(silph_read_field(s->read,box.reference,"Longitude",SILPH_READ_F64,&b))
            silph_read_call(s->read,box.reference,"get_Longitude",SILPH_READ_F64,NULL,0,&b);
        release(s,&box.reference);
        if(a.bits&&b.bits){
            memcpy(lat,&a.bits,8);memcpy(lon,&b.bits,8);
            if(isfinite(*lat)&&isfinite(*lon)&&fabs(*lat)<=90.0&&fabs(*lon)<=180.0)return 0;
        }
    }
    if(!silph_read_call(s->read,object,"get_Latitude",SILPH_READ_F64,NULL,0,&a)&&
       !silph_read_call(s->read,object,"get_Longitude",SILPH_READ_F64,NULL,0,&b)){
        memcpy(lat,&a.bits,8);memcpy(lon,&b.bits,8);
        if(isfinite(*lat)&&isfinite(*lon)&&fabs(*lat)<=90.0&&fabs(*lon)<=180.0)return 0;
    }
    SilphReadValue fort_box={0};
    if(!silph_read_call(s->read,object,"get_FortData",SILPH_READ_REFERENCE,NULL,0,&fort_box)&&fort_box.reference.serial){
        int fr=location(s,fort_box.reference,lat,lon);
        release(s,&fort_box.reference);
        if(!fr)return 0;
    }
    if(!silph_read_call(s->read,object,"get_FortProto",SILPH_READ_REFERENCE,NULL,0,&fort_box)&&fort_box.reference.serial){
        int fr=location(s,fort_box.reference,lat,lon);
        release(s,&fort_box.reference);
        if(!fr)return 0;
    }
    return SILPH_MANAGED_CONTRACT;
}
static int player_location(GameRead *s,double *lat,double *lon) {
    SilphManagedRef type={0};SilphReadValue avatar={0},ready={0};
    int status=silph_read_type_object(s->read,"holo-game.dll","Niantic.Holoholo.Avatar","IMapAvatar",&type);
    SilphReadArgument arg={.kind=SILPH_READ_REFERENCE,.value.reference=type};
    if(!status)status=silph_read_call(s->read,s->container,"TryResolve",SILPH_READ_REFERENCE,&arg,1,&avatar);
    if(!status)status=silph_read_call(s->read,avatar.reference,"get_InitialLocationSet",SILPH_READ_BOOL,NULL,0,&ready);
    if(!status&&!ready.bits)status=SILPH_MANAGED_NOT_READY;
    if(!status)status=location(s,avatar.reference,lat,lon);
    int a=release(s,&avatar.reference),b=release(s,&type);return status?status:a?a:b;
}
static double distance_meters(double lat,double lon,double other_lat,double other_lon) {
    const double radians=0.017453292519943295;
    double a=sin((lat-other_lat)*radians/2),b=sin((lon-other_lon)*radians/2);
    double value=a*a+cos(lat*radians)*cos(other_lat*radians)*b*b;
    value=fmax(0,fmin(1,value));return 12742000*asin(sqrt(value));
}
int silph_game_read_position(void *memory,double *lat,double *lon){
    GameRead *s=memory;if(!s||!s->opened||!lat||!lon)return SILPH_MANAGED_ARGUMENT;return player_location(s,lat,lon);
}
static int read_text(GameRead *s,SilphManagedRef owner,const char *name,char *output,uint32_t capacity){
    SilphReadValue value={0};int status=silph_read_call(s->read,owner,name,SILPH_READ_REFERENCE,NULL,0,&value);uint32_t length=0;
    if(!status&&value.reference.serial)status=silph_read_utf8(s->read,value.reference,output,capacity,&length);
    else if(!status)status=SILPH_MANAGED_CONTRACT;
    if(!status&&!length)status=SILPH_MANAGED_CONTRACT;
    int freed=release(s,&value.reference);return status?status:freed;
}
int silph_game_read_item_bag(void *memory,SilphManagedRef *output){
    GameRead *s=memory;if(!s||!s->opened||!output)return SILPH_MANAGED_ARGUMENT;memset(output,0,sizeof(*output));
    void *object=NULL;int status=silph_managed_target(s->managed,s->item_bag,&object);return status?status:silph_managed_retain(s->managed,object,false,output);
}
int silph_game_read_item(void *memory,int32_t item,uint32_t *output){
    GameRead *s=memory;if(!s||!s->opened||!output||item<=0)return SILPH_MANAGED_ARGUMENT;*output=0;
    SilphReadArgument arg={.kind=SILPH_READ_ENUM32,.value.bits=(uint32_t)item};SilphReadValue value={0};
    int status=silph_read_call(s->read,s->item_bag,"GetItemCount",SILPH_READ_I32,&arg,1,&value);
    if(!status&&value.bits>100000)status=SILPH_MANAGED_CONTRACT;if(!status)*output=(uint32_t)value.bits;return status;
}
static int read_stop(void *memory,double maximum,uint32_t incident_type,SilphStopPredicate accept,void *context,const SilphGameStop *target,SilphGameStop *output){
    GameRead *s=memory;if(!s||!s->opened||!output||!isfinite(maximum)||maximum<=0||maximum>80||(incident_type!=0&&incident_type!=8))return SILPH_MANAGED_ARGUMENT;
    memset(output,0,sizeof(*output));output->meters=INFINITY;
    double lat=0,lon=0;int status=player_location(s,&lat,&lon);
    SilphReadValue dictionary={0},values={0},iterator={0},current={0},value={0};
    if(!status)status=silph_read_field(s->read,s->directory,"emdp",SILPH_READ_REFERENCE,&dictionary);
    if(!status)status=silph_read_call(s->read,dictionary.reference,"get_Values",SILPH_READ_REFERENCE,NULL,0,&values);
    if(!status)status=silph_read_call(s->read,values.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
    for(unsigned i=0;!status&&i<512;i++){
        status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&value);
        if(status||!value.bits){if(!status)output->scan_complete=1;break;}output->scanned++;
        status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
        double a=0,b=0;if(!status)status=location(s,current.reference,&a,&b);double meters=distance_meters(lat,lon,a,b);
        if(!status&&target&&distance_meters(a,b,target->latitude,target->longitude)>target->meters){status=release(s,&current.reference);continue;}
        if(!status&&target&&isfinite(meters)&&meters<=maximum)output->matches++;
        if(!status&&isfinite(meters)&&meters<=maximum&&meters<output->meters){
            SilphGameStop row={.latitude=a,.longitude=b,.player_latitude=lat,.player_longitude=lon,.meters=meters};
            status=read_text(s,current.reference,"get_Id",row.fort,sizeof(row.fort));
            if(!status&&accept&&!accept(context,row.fort)){status=release(s,&current.reference);continue;}
            if(!status&&(incident_type||target)){
                status=silph_read_call(s->read,current.reference,"get_ActiveIncidentType",SILPH_READ_ENUM32,NULL,0,&value);row.incident_type=(uint32_t)value.bits;
                if(!status&&incident_type&&row.incident_type!=incident_type){status=release(s,&current.reference);continue;}
                if(!status)status=silph_read_call(s->read,current.reference,"get_IsIncidentActive",SILPH_READ_BOOL,NULL,0,&value);row.active=(uint32_t)value.bits;
                if(!status)status=silph_read_call(s->read,current.reference,"get_IsIncidentComplete",SILPH_READ_BOOL,NULL,0,&value);row.complete=(uint32_t)value.bits;
                if(!status&&incident_type&&(!row.active||row.complete)){status=release(s,&current.reference);continue;}
                if(!status&&row.incident_type==8&&row.active&&!row.complete){
                if(!status)status=read_text(s,current.reference,"get_CurrentIncidentId",row.incident,sizeof(row.incident));
                SilphManagedRef display={0};
                if(!status)status=silph_read_field_type(s->read,current.reference,"Holoholo.Rpc","PokestopIncidentDisplayProto",&display);
                if(!status)status=silph_read_field(s->read,display,"IncidentDisplayType",SILPH_READ_ENUM32,&value);
                if(!status&&value.bits!=8)status=SILPH_MANAGED_CONTRACT;
                if(!status)status=silph_read_field(s->read,display,"IncidentCompleted",SILPH_READ_BOOL,&value);
                if(!status&&value.bits)status=SILPH_MANAGED_CONTRACT;
                SilphReadValue incident={0};char incident_id[161]={0};uint32_t length=0;
                if(!status)status=silph_read_field(s->read,display,"IncidentId",SILPH_READ_REFERENCE,&incident);
                if(!status)status=silph_read_utf8(s->read,incident.reference,incident_id,sizeof(incident_id),&length);
                if(!status&&strcmp(incident_id,row.incident))status=SILPH_MANAGED_CONTRACT;
                int released=release(s,&incident.reference);if(!status)status=released;
                if(!status)status=silph_read_field(s->read,display,"IncidentExpirationMs",SILPH_READ_I64,&value);
                if(!status)row.expires_ms=value.bits;
                int freed=release(s,&display);if(!status)status=freed;
                }
            }
            if(!status){row.scanned=output->scanned;row.matches=output->matches;*output=row;}
        }
        int freed=release(s,&current.reference);if(!status)status=freed;
    }
    int disposed=0;if(iterator.reference.serial)disposed=silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&value);
    SilphManagedRef *refs[]={&current.reference,&iterator.reference,&values.reference,&dictionary.reference};
    for(unsigned i=0;i<4;i++){int freed=release(s,refs[i]);if(!status)status=freed;}if(!status)status=disposed;
    if(status){memset(output,0,sizeof(*output));return status;}return output->fort[0]?0:SILPH_MANAGED_NOT_READY;
}
int silph_game_read_stop(void *memory,double maximum,uint32_t type,SilphStopPredicate accept,void *context,SilphGameStop *out){return read_stop(memory,maximum,type,accept,context,NULL,out);}
int silph_game_read_stop_at(void *memory,double maximum,double latitude,double longitude,double tolerance,SilphGameStop *out){
    if(!isfinite(latitude)||!isfinite(longitude)||fabs(latitude)>90||fabs(longitude)>180||!isfinite(tolerance)||tolerance<=0||tolerance>10)return SILPH_MANAGED_ARGUMENT;
    SilphGameStop target={.latitude=latitude,.longitude=longitude,.meters=tolerance};return read_stop(memory,maximum,0,NULL,NULL,&target,out);
}
static int fort_dictionary(GameRead *s,const char *field,const char *fort,SilphManagedRef *out);
static int gym_dictionary_emdp(GameRead *s,SilphReadValue *dictionary){
    init_directory_fields(s);
    return silph_read_field(s->read,s->directory,s->field_stops[0]?s->field_stops:"emdp",SILPH_READ_REFERENCE,dictionary);
}
static int gym_dictionary_emdg(GameRead *s,SilphReadValue *dictionary){
    init_directory_fields(s);
    return silph_read_field(s->read,s->directory,s->field_gyms[0]?s->field_gyms:"emdg",SILPH_READ_REFERENCE,dictionary);
}
static int gym_dictionary_emds(GameRead *s,SilphReadValue *dictionary){
    init_directory_fields(s);
    return silph_read_field(s->read,s->directory,s->field_pokemon[0]?s->field_pokemon:"emds",SILPH_READ_REFERENCE,dictionary);
}
static int gym_is_map_gym(GameRead *s,SilphManagedRef object,bool *ok){
    *ok=silph_read_is_type(s->read,object,"Niantic.Holoholo.Gym","MapGym")||silph_read_is_type(s->read,object,"Niantic.Holoholo.Gym","MapGymDetails")||silph_read_is_type(s->read,object,"Niantic.Holoholo.Map","MapPlace");
    return 0;
}
static int gym_raid_level(GameRead *s,SilphManagedRef gym,uint32_t *level);
static uint32_t gym_read_raid_tier_stars(GameRead *s,SilphManagedRef gym);
static void gym_normalize_star_level(GameRead *s,SilphManagedRef gym,uint32_t *level);
static int gym_players(GameRead *s,SilphManagedRef gym,uint32_t *players);
static int gym_fill_raid_seed(GameRead *s,SilphManagedRef gym,uint32_t *level,uint64_t *seed);
static int gym_probe_enum32(GameRead *s,SilphManagedRef object,const char *const *methods,uint32_t *out){
    if(!object.serial||!out)return 0;
    for(unsigned i=0;methods[i];i++){
        SilphReadValue value={0};if(silph_read_call(s->read,object,methods[i],SILPH_READ_ENUM32,NULL,0,&value))continue;
        if(value.bits&&value.bits<=15){*out=(uint32_t)value.bits;return 0;}
    }
    return 0;
}
static int gym_probe_ref(GameRead *s,SilphManagedRef object,const char *const *methods,SilphManagedRef *out){
    if(!object.serial||!out)return SILPH_MANAGED_CONTRACT;
    for(unsigned i=0;methods[i];i++){
        SilphReadValue value={0};if(silph_read_call(s->read,object,methods[i],SILPH_READ_REFERENCE,NULL,0,&value))continue;
        if(value.reference.serial){*out=value.reference;return 0;}
        release(s,&value.reference);
    }
    return SILPH_MANAGED_CONTRACT;
}
static int gym_lookup_by_id(GameRead *s,const char *id,SilphManagedRef *gym){
    if(!id||!id[0]||!gym)return SILPH_MANAGED_ARGUMENT;
    SilphManagedRef key={0};int status=silph_managed_ascii(s->managed,id,strlen(id),&key);if(status)return status;
    SilphReadArgument arg={.kind=SILPH_READ_REFERENCE,.value.reference=key};SilphReadValue value={0};
    static const char *methods[]={"GetGym","TryGetGym","GetMapGym","TryGetMapGym","GetMapPlace","GetGymDetails",NULL};
    for(unsigned m=0;methods[m];m++){
        status=silph_read_call(s->read,s->directory,methods[m],SILPH_READ_REFERENCE,&arg,1,&value);
        if(status==SILPH_MANAGED_CONTRACT)continue;
        if(!status&&value.reference.serial)break;
    }
    int freed=release(s,&key);if(!status)status=freed;
    if(status)return status;
    if(value.reference.serial){*gym=value.reference;return 0;}
    SilphManagedRef fort={0};
    if(s->field_gyms[0]&&!fort_dictionary(s,s->field_gyms,id,&fort)&&fort.serial){*gym=fort;return 0;}
    if(!fort_dictionary(s,"emkw",id,&fort)&&fort.serial){*gym=fort;return 0;}
    if(!fort_dictionary(s,"emdp",id,&fort)&&fort.serial){*gym=fort;return 0;}
    if(!fort_dictionary(s,"emdg",id,&fort)&&fort.serial){*gym=fort;return 0;}
    return SILPH_MANAGED_NOT_READY;
}
static int gym_fort_id_text(GameRead *s,SilphManagedRef entry,char *id,size_t cap){
    if(!entry.serial||!id||!cap)return SILPH_MANAGED_ARGUMENT;
    id[0]=0;uint32_t n=0;
    if(!silph_read_utf8(s->read,entry,id,cap,&n)&&id[0])return 0;
    if(!read_text(s,entry,"get_Id",id,cap)&&id[0])return 0;
    static const char *id_methods[]={"get_FortId","get_PoiId","get_GymId","get_Key",NULL};
    for(unsigned i=0;id_methods[i];i++){
        if(!read_text(s,entry,id_methods[i],id,cap)&&id[0])return 0;
    }
    SilphReadValue sf={0};
    if(!silph_read_field(s->read,entry,"dywf",SILPH_READ_REFERENCE,&sf)&&sf.reference.serial){
        if(!silph_read_utf8(s->read,sf.reference,id,cap,&n)&&id[0]){
            release(s,&sf.reference);
            return 0;
        }
        release(s,&sf.reference);
    }
    if(!read_text(s,entry,"ToString",id,cap)&&id[0]&&(strchr(id,'-')||strlen(id)>8))return 0;
    return id[0]?0:SILPH_MANAGED_CONTRACT;
}
static int gym_lookup_from_key(GameRead *s,SilphManagedRef key,SilphManagedRef *gym){
    if(!key.serial||!gym)return SILPH_MANAGED_ARGUMENT;
    SilphReadArgument arg={.kind=SILPH_READ_REFERENCE,.value.reference=key};SilphReadValue value={0};int status=0;
    static const char *methods[]={"GetGym","TryGetGym","GetMapGym","TryGetMapGym","GetMapPlace","GetGymDetails",NULL};
    for(unsigned m=0;methods[m];m++){
        status=silph_read_call(s->read,s->directory,methods[m],SILPH_READ_REFERENCE,&arg,1,&value);
        if(status==SILPH_MANAGED_CONTRACT)continue;
        if(!status&&value.reference.serial){*gym=value.reference;return 0;}
        if(value.reference.serial)release(s,&value.reference);
    }
    char id[161]={0};
    if(!gym_fort_id_text(s,key,id,sizeof(id)))return gym_lookup_by_id(s,id,gym);
    return SILPH_MANAGED_NOT_READY;
}
static int gym_resolve(GameRead *s,SilphManagedRef entry,SilphManagedRef *gym);
static int gym_resolve_entry(GameRead *s,SilphManagedRef entry,SilphManagedRef *gym);
static void gym_scan_try_pick(GameRead *s,SilphManagedRef gym,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,unsigned *scanned,bool active_list,bool uncompleted_only);
static int gym_raid_is_egg(GameRead *s,SilphManagedRef gym,SilphManagedRef info);
static bool gym_raid_hatched_boss(GameRead *s,SilphManagedRef gym);
static bool gym_raid_live_map(GameRead *s,SilphManagedRef gym);
static bool gym_par_has_raid(GameRead *s,SilphManagedRef gym,uint32_t *level,uint32_t *players,uint64_t *seed);
static bool gym_raid_unhatched_egg(GameRead *s,SilphManagedRef gym);
static int gym_resolve_any(GameRead *s,SilphManagedRef entry,SilphManagedRef *gym){
    if(!entry.serial||!gym)return SILPH_MANAGED_ARGUMENT;
    *gym=(SilphManagedRef){0};
    int r=gym_resolve(s,entry,gym);
    if(!r&&gym->serial)return 0;
    if(gym->serial){release(s,gym);*gym=(SilphManagedRef){0};}
    r=gym_resolve_entry(s,entry,gym);
    if(!r&&gym->serial)return 0;
    if(gym->serial){release(s,gym);*gym=(SilphManagedRef){0};}
    r=gym_lookup_from_key(s,entry,gym);
    if(!r&&gym->serial)return 0;
    if(gym->serial){release(s,gym);*gym=(SilphManagedRef){0};}
    char id[161]={0};
    if(!gym_fort_id_text(s,entry,id,sizeof(id)))return gym_lookup_by_id(s,id,gym);
    return SILPH_MANAGED_NOT_READY;
}
static int gym_consider_active_fallback(GameRead *s,SilphManagedRef gym,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,unsigned *scanned,bool active_list);
static int gym_consider(GameRead *s,SilphManagedRef gym,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,unsigned *scanned);
static int gym_retain_tap_target(GameRead *s,SilphManagedRef gym,SilphManagedRef *out);
static void gym_release_map_place(GameRead *s,SilphGameGym *g){
    if(s&&g&&g->map_place.serial){release(s,&g->map_place);}
}
static void gym_pick_retain_place(GameRead *s,SilphManagedRef gym,SilphGameGym *output){
    if(!s||!output||!gym.serial)return;
    SilphManagedRef place={0};(void)gym_retain_tap_target(s,gym,&place);
    if(!place.serial){
        bool map=false;(void)gym_is_map_gym(s,gym,&map);
        void *object=NULL;
        if(!silph_managed_target(s->managed,gym,&object)&&object)
            (void)silph_managed_retain(s->managed,object,false,&place);
    }
    if(!place.serial)return;
    gym_release_map_place(s,output);output->map_place=place;
}
static int gym_resolve_entry(GameRead *s,SilphManagedRef entry,SilphManagedRef *gym){
    SilphReadValue key={0},val={0};
    if(!silph_read_call(s->read,entry,"get_Value",SILPH_READ_REFERENCE,NULL,0,&val)&&val.reference.serial){
        int r=gym_resolve(s,val.reference,gym);int f=release(s,&val.reference);return r?r:f;
    }
    if(!silph_read_call(s->read,entry,"get_Key",SILPH_READ_REFERENCE,NULL,0,&key)&&key.reference.serial){
        char id[161]={0};uint32_t n=0;
        int r=silph_read_utf8(s->read,key.reference,id,sizeof(id),&n);
        int f=release(s,&key.reference);
        if(!r&&id[0]){int l=gym_lookup_by_id(s,id,gym);return l?l:f;}
        return r?r:f;
    }
    char id[161]={0};uint32_t n=0;
    if(!silph_read_utf8(s->read,entry,id,sizeof(id),&n)&&id[0]&&strchr(id,'-'))
        return gym_lookup_by_id(s,id,gym);
    return SILPH_MANAGED_CONTRACT;
}
static int gym_resolve(GameRead *s,SilphManagedRef entry,SilphManagedRef *gym){
    bool map=false;int status=gym_is_map_gym(s,entry,&map);if(status)return status;
    char id[161]={0};int id_status=read_text(s,entry,"get_Id",id,sizeof(id));
    if(!id_status&&id[0]){
        SilphManagedRef resolved={0};int lookup=gym_lookup_by_id(s,id,&resolved);
        if(!lookup&&resolved.serial){*gym=resolved;return 0;}
        if(lookup!=SILPH_MANAGED_NOT_READY&&lookup!=SILPH_MANAGED_CONTRACT&&lookup)return lookup;
        if(resolved.serial)release(s,&resolved);
    }
    if(map){
        if(silph_read_is_type(s->read,entry,"Niantic.Holoholo.Gym","MapGym")||silph_read_is_type(s->read,entry,"Niantic.Holoholo.Gym","MapGymDetails")){
            void *object=NULL;status=silph_managed_target(s->managed,entry,&object);if(!status)status=silph_managed_retain(s->managed,object,false,gym);return status;
        }
        if(id[0]){
            SilphManagedRef resolved={0};int lookup=gym_lookup_by_id(s,id,&resolved);
            if(!lookup&&resolved.serial){*gym=resolved;return 0;}
            if(resolved.serial)release(s,&resolved);
        }
        void *object=NULL;status=silph_managed_target(s->managed,entry,&object);if(!status)status=silph_managed_retain(s->managed,object,false,gym);return status;
    }
    if(id_status)return id_status;
    return gym_lookup_by_id(s,id,gym);
}
static uint64_t gym_wall_ms(void){
    struct timespec t={0};if(clock_gettime(CLOCK_REALTIME,&t))return 0;
    return(uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static int gym_probe_bool(GameRead *s,SilphManagedRef object,const char *const *methods,bool *set){
    if(!object.serial||!set)return 0;
    for(unsigned i=0;methods[i];i++){
        SilphReadValue value={0};if(silph_read_call(s->read,object,methods[i],SILPH_READ_BOOL,NULL,0,&value))continue;
        if(value.bits){*set=true;return 0;}
    }
    return 0;
}
static int read_i64_any(GameRead *s,SilphManagedRef obj,const char *prop,const char *field,uint64_t *out){
    if(!obj.serial||!out)return SILPH_MANAGED_ARGUMENT;
    SilphReadValue v={0};
    if(prop&&!silph_read_call(s->read,obj,prop,SILPH_READ_I64,NULL,0,&v)&&v.bits){*out=v.bits;return 0;}
    if(field&&!silph_read_field(s->read,obj,field,SILPH_READ_I64,&v)&&v.bits){*out=v.bits;return 0;}
    if(prop&&!silph_read_field(s->read,obj,prop,SILPH_READ_I64,&v)&&v.bits){*out=v.bits;return 0;}
    return SILPH_MANAGED_CONTRACT;
}
static int read_enum_any(GameRead *s,SilphManagedRef obj,const char *prop,const char *field,uint32_t *out){
    if(!obj.serial||!out)return SILPH_MANAGED_ARGUMENT;
    SilphReadValue v={0};
    if(prop&&!silph_read_call(s->read,obj,prop,SILPH_READ_ENUM32,NULL,0,&v)&&v.bits){*out=(uint32_t)v.bits;return 0;}
    if(prop&&!silph_read_call(s->read,obj,prop,SILPH_READ_I32,NULL,0,&v)&&v.bits){*out=(uint32_t)v.bits;return 0;}
    if(field&&!silph_read_field(s->read,obj,field,SILPH_READ_ENUM32,&v)&&v.bits){*out=(uint32_t)v.bits;return 0;}
    if(field&&!silph_read_field(s->read,obj,field,SILPH_READ_I32,&v)&&v.bits){*out=(uint32_t)v.bits;return 0;}
    if(prop&&!silph_read_field(s->read,obj,prop,SILPH_READ_ENUM32,&v)&&v.bits){*out=(uint32_t)v.bits;return 0;}
    if(prop&&!silph_read_field(s->read,obj,prop,SILPH_READ_I32,&v)&&v.bits){*out=(uint32_t)v.bits;return 0;}
    return SILPH_MANAGED_CONTRACT;
}
#define GYM_RAID_MIN_LEFT_MS UINT64_C(360000)
static bool gym_raid_ending(GameRead *s,SilphManagedRef gym,const char *id){
    if(!gym.serial)return false;
    uint64_t end=0;
    (void)read_i64_any(s,gym,"get_RaidEndMs","raid_end_ms_",&end);
    if(end<1000000000000ULL){
        end=0;SilphManagedRef info={0};
        static const char *nest[]={"get_RaidInfo","get_Raid","get_RaidBattle",NULL};
        if(!gym_probe_ref(s,gym,nest,&info)&&info.serial){
            (void)read_i64_any(s,info,"get_RaidEndMs","raid_end_ms_",&end);
            release(s,&info);
        }
    }
    uint64_t now=gym_wall_ms();
    bool ending=end>=1000000000000ULL&&now&&end<=now+GYM_RAID_MIN_LEFT_MS;
    static char last[161];
    if(id&&strncmp(last,id,sizeof(last)-1)){
        snprintf(last,sizeof(last),"%s",id);
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym raid time fort=%s end=%llu left=%llds%s",id,(unsigned long long)end,end>now?(long long)((end-now)/1000):0LL,ending?"; ending soon, skipping":"");
    }
    return ending;
}
static int gym_raid_apply_info(GameRead *s,SilphManagedRef info,uint32_t *level,uint64_t *battle_ms,uint64_t *end_ms,uint64_t *seed,SilphReadValue *boss,bool *has_raid){
    if(!info.serial)return SILPH_MANAGED_CONTRACT;
    if(level&&!*level){
        uint32_t lv=0;
        if(!read_enum_any(s,info,"get_RaidLevel","raid_level_",&lv)&&lv)*level=lv;
        if(!*level&&!read_enum_any(s,info,"get_Level","level_",&lv)&&lv)*level=lv;
        if(!*level&&!read_enum_any(s,info,"get_RaidTier","raid_tier_",&lv)&&lv)*level=lv;
    }
    if(battle_ms&&!*battle_ms){
        (void)read_i64_any(s,info,"get_RaidBattleMs","raid_battle_ms_",battle_ms);
        if(!*battle_ms)(void)read_i64_any(s,info,"get_BattleMs","battle_ms_",battle_ms);
    }
    if(end_ms&&!*end_ms){
        (void)read_i64_any(s,info,"get_RaidEndMs","raid_end_ms_",end_ms);
        if(!*end_ms)(void)read_i64_any(s,info,"get_EndMs","end_ms_",end_ms);
    }
    if(seed&&!*seed){
        (void)read_i64_any(s,info,"get_RaidSeed","raid_seed_",seed);
        if(!*seed)(void)read_i64_any(s,info,"get_RaidBattleSeed","raid_battle_seed_",seed);
        if(!*seed)(void)read_i64_any(s,info,"get_Seed","seed_",seed);
    }
    if(boss&&!boss->bits){
        SilphReadValue pokev={0};
        if(!silph_read_call(s->read,info,"get_RaidPokemon",SILPH_READ_REFERENCE,NULL,0,&pokev)&&pokev.reference.serial){
            uint32_t pid=0;
            if(!read_enum_any(s,pokev.reference,"get_PokemonId","PokemonId",&pid)&&pid)boss->bits=pid;
            if(!boss->bits&&!read_enum_any(s,pokev.reference,"get_PokemonId","pokemon_id_",&pid)&&pid)boss->bits=pid;
            {static int dumped_poke;if(!dumped_poke){dumped_poke=1;silph_dump_methods(s->read,pokev.reference,"RaidPokemonProto");}}
            release(s,&pokev.reference);
        }
        if(!boss->bits&&!silph_read_field(s->read,info,"raid_pokemon_",SILPH_READ_REFERENCE,&pokev)&&pokev.reference.serial){
            uint32_t pid=0;
            if(!read_enum_any(s,pokev.reference,"get_PokemonId","PokemonId",&pid)&&pid)boss->bits=pid;
            if(!boss->bits&&!read_enum_any(s,pokev.reference,"get_PokemonId","pokemon_id_",&pid)&&pid)boss->bits=pid;
            release(s,&pokev.reference);
        }
        if(!boss->bits&&!silph_read_field(s->read,info,"RaidPokemon",SILPH_READ_REFERENCE,&pokev)&&pokev.reference.serial){
            uint32_t pid=0;
            if(!read_enum_any(s,pokev.reference,"get_PokemonId","PokemonId",&pid)&&pid)boss->bits=pid;
            if(!boss->bits&&!read_enum_any(s,pokev.reference,"get_PokemonId","pokemon_id_",&pid)&&pid)boss->bits=pid;
            if(!boss->bits){
                SilphReadValue ev={0};
                if(!silph_read_field(s->read,pokev.reference,"PokemonId",SILPH_READ_ENUM32,&ev)&&ev.bits)boss->bits=(uint32_t)ev.bits;
            }
            {static int dumped_poke2;if(!dumped_poke2){dumped_poke2=1;silph_dump_methods(s->read,pokev.reference,"RaidPokemonProto2");}}
            release(s,&pokev.reference);
        }
        if(!boss->bits){
            uint32_t pid=0;
            if(!read_enum_any(s,info,"get_RaidPokemon","raid_pokemon_",&pid)&&pid)boss->bits=pid;
            if(!boss->bits&&!read_enum_any(s,info,"get_PokemonId","pokemon_id_",&pid)&&pid)boss->bits=pid;
        }
        if(boss->bits&&has_raid)*has_raid=true;
    }
    if(has_raid&&(seed&&*seed))*has_raid=true;
    return 0;
}
static int gym_raid_is_egg(GameRead *s,SilphManagedRef gym,SilphManagedRef info){
    if(gym.serial){
        SilphReadValue rstate={0};
        if(!silph_read_call(s->read,gym,"get_CurrentRaidState",SILPH_READ_ENUM32,NULL,0,&rstate)){
            if(rstate.bits==1)return 1;
            if(rstate.bits==2)return 0;
        }
    }
    uint64_t battle_ms=0;
    if(info.serial)(void)read_i64_any(s,info,"get_RaidBattleMs","raid_battle_ms_",&battle_ms);
    if(!battle_ms&&gym.serial)(void)read_i64_any(s,gym,"get_RaidBattleMs","raid_battle_ms_",&battle_ms);
    uint64_t now=gym_wall_ms();
    if(battle_ms>1000000000000ULL&&now&&now<battle_ms)return 1;
    if(battle_ms>1000000000000ULL&&now&&now>=battle_ms)return 0;

    if(gym.serial){
        SilphReadValue el={0};
        if(!silph_read_call(s->read,gym,"get_EggLevel",SILPH_READ_ENUM32,NULL,0,&el)&&el.bits>=1&&el.bits<=7){
            SilphReadValue act={0};
            if(!silph_read_call(s->read,gym,"get_IsRaidActive",SILPH_READ_BOOL,NULL,0,&act)&&!act.bits)return 1;
        }
    }
    char boss[96]={0};
    if(gym.serial&&!read_text(s,gym,"get_RaidBossName",boss,sizeof(boss))){
        for(unsigned i=0;boss[i];i++)if(boss[i]>='A'&&boss[i]<='Z')boss[i]=(char)(boss[i]-'A'+'a');
        if(strstr(boss,"egg"))return 1;
    }
    return 0;
}
static bool gym_raid_unhatched_egg(GameRead *s,SilphManagedRef gym){
    if(!gym.serial)return false;
    uint32_t rstate_val=0;
    if(!silph_raid_probe_return_type(s->read,gym,"RaidState",&rstate_val)){
        if(rstate_val==1)return true;
        if(rstate_val==2||rstate_val==0)return false;
    }
    SilphReadValue rstate={0};
    if(!silph_read_call(s->read,gym,"get_CurrentRaidState",SILPH_READ_ENUM32,NULL,0,&rstate)){
        if(rstate.bits==1)return true;
        if(rstate.bits==2||rstate.bits==0)return false;
    }
    uint64_t battle_ms=0;
    (void)read_i64_any(s,gym,"get_RaidBattleMs","raid_battle_ms_",&battle_ms);
    uint64_t now=gym_wall_ms();
    if(battle_ms>1000000000000ULL&&now){
        if(now<battle_ms)return true;
        return false;
    }
    if(gym_raid_is_egg(s,gym,(SilphManagedRef){0}))return true;

    SilphReadValue has_raid={0},active={0};
    bool has=false,is_act=false;
    if(!silph_read_call(s->read,gym,"get_HasRaid",SILPH_READ_BOOL,NULL,0,&has_raid)&&has_raid.bits)has=true;
    if(!silph_read_call(s->read,gym,"get_IsRaidActive",SILPH_READ_BOOL,NULL,0,&active)&&active.bits)is_act=true;
    if(!is_act&&!silph_read_call(s->read,gym,"get_HasActiveRaid",SILPH_READ_BOOL,NULL,0,&active)&&active.bits)is_act=true;
    if(has&&!is_act)return true;

    return false;
}
static int gym_raid_active(GameRead *s,SilphManagedRef gym,uint32_t *level,uint32_t *players,bool *active,uint64_t *raid_seed,uint32_t *boss_species);
static bool gym_raid_hatched_boss(GameRead *s,SilphManagedRef gym){
    if(!gym.serial)return false;
    uint32_t rstate_val=0;
    if(!silph_raid_probe_return_type(s->read,gym,"RaidState",&rstate_val)){
        if(rstate_val==2){
            uint32_t lv=0;gym_raid_level(s,gym,&lv);
            if(lv>=1&&lv<=7)return true;
        }
        if(rstate_val==1||rstate_val==0)return false;
    }
    SilphReadValue rstate={0};
    if(!silph_read_call(s->read,gym,"get_CurrentRaidState",SILPH_READ_ENUM32,NULL,0,&rstate)){
        if(rstate.bits==2){
            uint32_t lv=0;gym_raid_level(s,gym,&lv);
            if(lv>=1&&lv<=7)return true;
        }
        if(rstate.bits==1||rstate.bits==0)return false;
    }
    if(gym_raid_unhatched_egg(s,gym))return false;

    uint64_t battle_ms=0,end_ms=0;
    (void)read_i64_any(s,gym,"get_RaidBattleMs","raid_battle_ms_",&battle_ms);
    (void)read_i64_any(s,gym,"get_RaidEndMs","raid_end_ms_",&end_ms);
    uint64_t now=gym_wall_ms();
    if(battle_ms>1000000000000ULL&&now&&now>=battle_ms){
        if(end_ms>1000000000000ULL&&now>=end_ms)return false;
        uint32_t lv=0;gym_raid_level(s,gym,&lv);
        return lv>=1&&lv<=7;
    }

    SilphReadValue active={0};
    static const char *active_methods[]={"get_IsRaidActive","IsRaidActive","get_RaidActive","get_HasActiveRaid",NULL};
    for(unsigned i=0;active_methods[i];i++){
        if(!silph_read_call(s->read,gym,active_methods[i],SILPH_READ_BOOL,NULL,0,&active)&&active.bits){
            uint32_t lv=0;gym_raid_level(s,gym,&lv);
            return lv>=1&&lv<=7;
        }
    }

    char boss[96]={0};
    if(!read_text(s,gym,"get_RaidBossName",boss,sizeof(boss))&&boss[0]){
        for(unsigned i=0;boss[i];i++)if(boss[i]>='A'&&boss[i]<='Z')boss[i]=(char)(boss[i]-'A'+'a');
        if(!strstr(boss,"egg")){
            uint32_t lv=0;gym_raid_level(s,gym,&lv);
            return lv>=1&&lv<=7;
        }
    }
    uint32_t pid=0;
    static const char *boss_methods[]={"get_PokemonId","get_RaidPokemon","get_BossPokemonId",NULL};
    gym_probe_enum32(s,gym,boss_methods,&pid);
    if(pid>0&&pid<8192){
        uint32_t lv=0;gym_raid_level(s,gym,&lv);
        return lv>=1&&lv<=7;
    }

    return false;
}
static bool gym_raid_live_map(GameRead *s,SilphManagedRef gym){
    if(!gym.serial||gym_raid_unhatched_egg(s,gym))return false;
    return gym_raid_hatched_boss(s,gym);
}
static int gym_raid_active(GameRead *s,SilphManagedRef gym,uint32_t *level,uint32_t *players,bool *active,uint64_t *raid_seed,uint32_t *boss_species){
    *active=false;if(raid_seed)*raid_seed=0;if(boss_species)*boss_species=0;if(!level||!players)return SILPH_MANAGED_ARGUMENT;
    int status=gym_raid_level(s,gym,level);if(status==SILPH_MANAGED_CONTRACT)status=0;
    if(!status)status=gym_players(s,gym,players);if(status==SILPH_MANAGED_CONTRACT){status=0;*players=0;}
    uint64_t battle_ms=0,end_ms=0,seed=0;SilphReadValue boss={0};bool has_raid=false;

    (void)read_i64_any(s,gym,"get_RaidBattleMs","raid_battle_ms_",&battle_ms);
    (void)read_i64_any(s,gym,"get_RaidEndMs","raid_end_ms_",&end_ms);
    (void)read_i64_any(s,gym,"get_RaidSeed","raid_seed_",&seed);

    SilphManagedRef info={0};
    static const char *nest[]={"get_RaidInfo","get_Raid","get_RaidBattle","get_IncomingRaid","get_RaidEgg",NULL};
    if(!gym_probe_ref(s,gym,nest,&info)&&info.serial){
        {static int dumped_raid_info;if(!dumped_raid_info){dumped_raid_info=1;silph_dump_methods(s->read,info,"RaidInfoProto");}}
        gym_raid_apply_info(s,info,level,&battle_ms,&end_ms,&seed,&boss,&has_raid);
    }
    if(!info.serial){
        (void)silph_read_field_type(s->read,gym,"Holoholo.Rpc","RaidInfoProto",&info);
        if(info.serial)gym_raid_apply_info(s,info,level,&battle_ms,&end_ms,&seed,&boss,&has_raid);
    }
    SilphManagedRef fort={0};
    if(silph_read_field_type(s->read,gym,"Holoholo.Rpc","PokemonFortProto",&fort)!=0||!fort.serial){
        static const char *fort_methods[]={"get_FortData","get_PokemonFort","get_Fort","get_ClientFortProto","get_CachedFortProto",NULL};
        for(unsigned i=0;!fort.serial&&fort_methods[i];i++){
            SilphReadValue tmp={0};
            if(!silph_read_call(s->read,gym,fort_methods[i],SILPH_READ_REFERENCE,NULL,0,&tmp)&&tmp.reference.serial)fort=tmp.reference;
        }
    }
    if(fort.serial){
        {static int dumped_fort;if(!dumped_fort){dumped_fort=1;silph_dump_methods(s->read,fort,"PokemonFortProto");}}
        {
            SilphReadValue gd={0};
            if(!silph_read_field(s->read,fort,"GymDisplay",SILPH_READ_REFERENCE,&gd)&&gd.reference.serial){
                {static int dumped_gd;if(!dumped_gd){dumped_gd=1;silph_dump_methods(s->read,gd.reference,"GymDisplayProto");}}
                release(s,&gd.reference);
            }
        }
        if(!seed)(void)read_i64_any(s,fort,"get_RaidSeed","raid_seed_",&seed);
        if(!info.serial){
            SilphReadValue finfo_val={0};
            if(!silph_read_call(s->read,fort,"get_RaidInfo",SILPH_READ_REFERENCE,NULL,0,&finfo_val)&&finfo_val.reference.serial){
                info=finfo_val.reference;{static int dumped_ri2;if(!dumped_ri2){dumped_ri2=1;silph_dump_methods(s->read,info,"RaidInfoProto2");}}gym_raid_apply_info(s,info,level,&battle_ms,&end_ms,&seed,&boss,&has_raid);
            }else if(!silph_read_field_type(s->read,fort,"Holoholo.Rpc","RaidInfoProto",&info)&&info.serial){
                {static int dumped_ri3;if(!dumped_ri3){dumped_ri3=1;silph_dump_methods(s->read,info,"RaidInfoProto3");}}gym_raid_apply_info(s,info,level,&battle_ms,&end_ms,&seed,&boss,&has_raid);
            }else{
                SilphReadValue rinfo_by_name={0};
                if(!silph_read_field(s->read,fort,"RaidInfo",SILPH_READ_REFERENCE,&rinfo_by_name)&&rinfo_by_name.reference.serial){
                    info=rinfo_by_name.reference;{static int dumped_ri4;if(!dumped_ri4){dumped_ri4=1;silph_dump_methods(s->read,info,"RaidInfoProto4");}}gym_raid_apply_info(s,info,level,&battle_ms,&end_ms,&seed,&boss,&has_raid);
                }else if(rinfo_by_name.reference.serial)release(s,&rinfo_by_name.reference);
            }
        }
        release(s,&fort);
    }
    if(!boss.bits){
        static const char *boss_methods[]={"get_PokemonId","get_RaidPokemon","get_BossPokemonId","get_Pokemon",NULL};
        uint32_t bid=0;gym_probe_enum32(s,info.serial?info:gym,boss_methods,&bid);
        if(bid>0&&bid<8192){boss.bits=bid;has_raid=true;}
    }

    uint64_t now=gym_wall_ms();
    if(!now){struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);now=(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;}

    if(end_ms>1000000000000ULL&&now>=end_ms){
        if(info.serial)release(s,&info);
        return 0;
    }
    if(battle_ms>1000000000000ULL&&now<battle_ms){
        if(info.serial)release(s,&info);
        return 0;
    }
    if(gym_raid_is_egg(s,gym,info)){
        if(info.serial)release(s,&info);
        return 0;
    }

    if(battle_ms>1000000000000ULL&&now>=battle_ms&&seed>0){
        gym_normalize_star_level(s,gym,level);
        if(*level>=1&&*level<=7){
            *active=true;if(raid_seed)*raid_seed=seed;if(boss_species)*boss_species=(uint32_t)boss.bits;
            if(info.serial)release(s,&info);
            return 0;
        }
    }

    uint32_t rstate_probed=0;
    if(!silph_raid_probe_return_type(s->read,gym,"RaidState",&rstate_probed)){
        if(rstate_probed==2){
            gym_normalize_star_level(s,gym,level);
            if(*level>=1&&*level<=7){
                *active=true;if(raid_seed&&seed)*raid_seed=seed;if(boss_species)*boss_species=(uint32_t)boss.bits;
                if(info.serial)release(s,&info);
                return 0;
            }
        }else if(rstate_probed==1||rstate_probed==0){
            if(info.serial)release(s,&info);
            return 0;
        }
    }

    SilphReadValue rstate={0};
    if(!silph_read_call(s->read,gym,"get_CurrentRaidState",SILPH_READ_ENUM32,NULL,0,&rstate)){
        if(rstate.bits==2){
            gym_normalize_star_level(s,gym,level);
            if(*level>=1&&*level<=7){
                *active=true;if(raid_seed&&seed)*raid_seed=seed;if(boss_species)*boss_species=(uint32_t)boss.bits;
                if(info.serial)release(s,&info);
                return 0;
            }
        }else if(rstate.bits==1||rstate.bits==0){
            if(info.serial)release(s,&info);
            return 0;
        }
    }

    bool is_act=false;
    static const char *active_flags[]={"get_IsRaidActive","IsRaidActive","get_RaidActive","get_HasActiveRaid",NULL};
    for(unsigned i=0;!is_act&&active_flags[i];i++){
        SilphReadValue flag={0};
        if(!silph_read_call(s->read,gym,active_flags[i],SILPH_READ_BOOL,NULL,0,&flag)&&flag.bits)is_act=true;
    }
    if(is_act&&(boss.bits||seed)){
        if(!gym_raid_unhatched_egg(s,gym)){
            gym_normalize_star_level(s,gym,level);
            if(*level>=1&&*level<=7){
                *active=true;if(raid_seed)*raid_seed=seed;if(boss_species)*boss_species=(uint32_t)boss.bits;
                if(info.serial)release(s,&info);
                return 0;
            }
        }
    }
    if(info.serial)release(s,&info);
    return 0;
}
void silph_game_read_raid_boss_diag(void *memory){
    GameRead *s=memory;if(!s||!s->opened)return;
    SilphReadValue dict={0};
    if(silph_read_call(s->read,s->directory,"get_UncompletedRaidGyms",SILPH_READ_REFERENCE,NULL,0,&dict)||!dict.reference.serial)return;
    SilphReadValue arr={0};
    if(silph_read_call(s->read,dict.reference,"ToArray",SILPH_READ_REFERENCE,NULL,0,&arr)||!arr.reference.serial){release(s,&dict.reference);return;}
    uint32_t n=0;SilphReadValue cnt={0};
    if(!silph_read_call(s->read,arr.reference,"get_Count",SILPH_READ_I32,NULL,0,&cnt))n=(uint32_t)cnt.bits;
    if(!n&&!silph_read_call(s->read,arr.reference,"get_Length",SILPH_READ_I32,NULL,0,&cnt))n=(uint32_t)cnt.bits;
    if(n>8)n=8;
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","BOSS_DIAG start count=%u",(unsigned)n);
    for(uint32_t i=0;i<n;i++){
        SilphReadArgument arg={.kind=SILPH_READ_I32,.value.bits=i};SilphReadValue item={0};
        int rr=silph_read_call(s->read,arr.reference,"get_Item",SILPH_READ_REFERENCE,&arg,1,&item);
        if(rr==SILPH_MANAGED_CONTRACT)rr=silph_read_call(s->read,arr.reference,"get_Item",SILPH_READ_BOXED,&arg,1,&item);
        if(rr==SILPH_MANAGED_CONTRACT){
            SilphManagedRef direct={0};
            int ar=silph_read_array_item(s->read,arr.reference,i,&direct);
            if(!ar&&direct.serial){item.reference=direct;rr=0;}
            else if(direct.serial)release(s,&direct);
        }
        if(rr||!item.reference.serial)continue;
        SilphManagedRef gym={0};
        (void)gym_resolve_entry(s,item.reference,&gym);
        if(!gym.serial)(void)gym_lookup_from_key(s,item.reference,&gym);
        if(!gym.serial)(void)gym_resolve_any(s,item.reference,&gym);
        char id[161]={0};uint32_t level=0,players=0,boss=0;bool active=false;uint64_t seed=0;
        if(gym.serial){
            gym_fort_id_text(s,gym,id,sizeof(id));
            (void)gym_raid_active(s,gym,&level,&players,&active,&seed,&boss);
        }
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","BOSS_DIAG[%u] id=%s level=%u boss=%u seed=%llu",(unsigned)i,id[0]?id:"?",(unsigned)level,(unsigned)boss,(unsigned long long)seed);
        release(s,&gym);release(s,&item.reference);
    }
    release(s,&arr.reference);release(s,&dict.reference);
}
static bool gym_scan_joinable_raid(GameRead *s,SilphManagedRef gym,bool is_live,uint64_t seed){
    (void)seed;
    if(!gym.serial)return false;
    if(gym_raid_unhatched_egg(s,gym)||gym_raid_is_egg(s,gym,(SilphManagedRef){0}))return false;
    if(gym_raid_hatched_boss(s,gym))return true;
    SilphReadValue egg_lvl={0};
    if(!silph_read_call(s->read,gym,"get_EggLevel",SILPH_READ_ENUM32,NULL,0,&egg_lvl)&&egg_lvl.bits>=1&&egg_lvl.bits<=5)return false;
    return is_live;
}
static bool gym_tier_allowed(uint32_t tier_mask,uint32_t level){
    if(!level)return false;
    if(!tier_mask)return true;
    if(level>=1&&level<=5)return ((tier_mask>>(level-1))&1)!=0;
    if(level==6)return (tier_mask>>5)&1;
    if(level==7)return (tier_mask>>6)&1;
    return false;
}
static inline uint32_t raid_min_players_for_level(uint32_t lv){
    if(lv==5)return 8;
    if(lv==1)return 3;
    if(lv>=4)return 6;
    if(lv==3)return 4;
    return 1;
}
static bool gym_pick_better(const SilphGameGym *cur,uint32_t level,uint32_t players,double meters,uint32_t live){
    if(!cur->fort[0])return true;
    if(live&&!cur->raid_live)return true;
    if(!live&&cur->raid_live)return false;
    uint32_t req_cand=raid_min_players_for_level(level);
    uint32_t req_cur=raid_min_players_for_level(cur->raid_level);
    bool cand_meets=(players>=req_cand);
    bool cur_meets=(cur->lobby_players>=req_cur);
    if(cand_meets&&!cur_meets)return true;
    if(!cand_meets&&cur_meets)return false;
    if(players>cur->lobby_players)return true;
    if(cur->lobby_players>players)return false;
    return meters<cur->meters;
}
static int gym_consider_active_fallback(GameRead *s,SilphManagedRef gym,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,unsigned *scanned,bool active_list){
    (void)active_list;(void)prefer_players;
    double a=0,b=0;int status=location(s,gym,&a,&b);if(status)return status;
    double meters=anchor?distance_meters(anchor->latitude,anchor->longitude,a,b):distance_meters(lat,lon,a,b);
    if(!isfinite(meters)||meters>maximum)return 0;
    if(gym_raid_unhatched_egg(s,gym))return 0;
    SilphReadValue live={0};bool is_live=false;
    uint32_t rstate_val=0;
    if(!silph_raid_probe_return_type(s->read,gym,"RaidState",&rstate_val)){
        if(rstate_val==2)is_live=true;
        else if(rstate_val==1||rstate_val==0)return 0;
    }else{
        SilphReadValue rstate={0};
        if(!silph_read_call(s->read,gym,"get_CurrentRaidState",SILPH_READ_ENUM32,NULL,0,&rstate)){
            if(rstate.bits==2)is_live=true;
            else if(rstate.bits==1||rstate.bits==0)return 0;
        }
    }
    static const char *live_methods[]={"get_IsRaidActive","IsRaidActive","get_HasActiveRaid",NULL};
    for(unsigned i=0;!is_live&&live_methods[i];i++){
        if(!silph_read_call(s->read,gym,live_methods[i],SILPH_READ_BOOL,NULL,0,&live)&&live.bits)is_live=true;
    }
    if(!is_live&&!gym_raid_live_map(s,gym)&&!gym_raid_hatched_boss(s,gym)){
        uint32_t pl=0,pp=0;uint64_t ps=0;
        if(!gym_par_has_raid(s,gym,&pl,&pp,&ps)||gym_raid_unhatched_egg(s,gym))return 0;
    }
    uint32_t level=0,players=0,boss=0;bool active=false;uint64_t seed=0;
    status=gym_raid_active(s,gym,&level,&players,&active,&seed,&boss);
    if(status==SILPH_MANAGED_CONTRACT)status=0;
    if(!level)gym_raid_level(s,gym,&level);
    if(!level||level>7)return 0;
    if(is_live&&!active)active=true;
    if(!seed&&is_live){
        (void)gym_fill_raid_seed(s,gym,&level,&seed);
    }
    gym_normalize_star_level(s,gym,&level);
    if(!level||level>7)return 0;
    if(status||(!active&&!is_live))return 0;
    if(!seed&&!is_live&&!active)return 0;
    if(!gym_scan_joinable_raid(s,gym,is_live,seed))return 0;
    if(!gym_tier_allowed(tier_mask,level))return 0;
    char id[161]={0};
    status=gym_fort_id_text(s,gym,id,sizeof(id));
    if(status||!id[0])status=read_text(s,gym,"get_Id",id,sizeof(id));
    if(!id[0])return 0;
    if(gym_id_skipped(s,id))return 0;
    if(gym_raid_ending(s,gym,id))return 0;
    bool better=gym_pick_better(output,level,players,meters,1);
    if(!better)return 0;
    SilphGameGym row={.latitude=a,.longitude=b,.player_latitude=lat,.player_longitude=lon,.meters=meters,.raid_level=level,.players=players,.lobby_players=players,.raid_seed=seed,.raid_live=1,.boss_species=boss};
    memcpy(row.fort,id,sizeof(row.fort));
    row.scanned=*scanned;row.matches=output->matches;
    gym_pick_retain_place(s,gym,&row);
    *output=row;
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym hatched pick fort=%s tier=%u dist=%.0fm seed=%llu",id,(unsigned)level,meters,(unsigned long long)seed);
    return 0;
}
static bool gym_place_is_gym(GameRead *s,SilphManagedRef place){
    if(silph_read_is_type(s->read,place,"Niantic.Holoholo.Gym","MapGym")||silph_read_is_type(s->read,place,"Niantic.Holoholo.Gym","MapGymDetails"))return true;
    SilphReadValue value={0};
    if(!silph_read_call(s->read,place,"get_IsGym",SILPH_READ_BOOL,NULL,0,&value)&&value.bits)return true;
    if(!silph_read_call(s->read,place,"get_FortType",SILPH_READ_ENUM32,NULL,0,&value)&&(value.bits==1||value.bits==2))return true;
    if(!silph_read_call(s->read,place,"get_HasRaid",SILPH_READ_BOOL,NULL,0,&value)&&value.bits)return true;
    if(!silph_read_call(s->read,place,"get_RaidLevel",SILPH_READ_ENUM32,NULL,0,&value)&&value.bits)return true;
    if(silph_read_is_type(s->read,place,"Niantic.Holoholo.Map","MapPlace")){
        if(!silph_read_call(s->read,place,"get_Pokestop",SILPH_READ_BOOL,NULL,0,&value)&&value.bits)return false;
        if(!silph_read_call(s->read,place,"get_IsGym",SILPH_READ_BOOL,NULL,0,&value)&&value.bits)return true;
        if(!silph_read_call(s->read,place,"get_FortType",SILPH_READ_ENUM32,NULL,0,&value)&&(value.bits==1||value.bits==2))return true;
        if(!silph_read_call(s->read,place,"get_HasActiveRaid",SILPH_READ_BOOL,NULL,0,&value)&&value.bits)return true;
        if(!silph_read_call(s->read,place,"get_IsRaidActive",SILPH_READ_BOOL,NULL,0,&value)&&value.bits)return true;
        if(!silph_read_call(s->read,place,"get_RaidLevel",SILPH_READ_ENUM32,NULL,0,&value)&&value.bits>=1&&value.bits<=5)return true;
        if(!silph_read_call(s->read,place,"get_RaidTier",SILPH_READ_ENUM32,NULL,0,&value)&&value.bits>=1&&value.bits<=5)return true;
        if(!silph_read_call(s->read,place,"get_HasRaid",SILPH_READ_BOOL,NULL,0,&value)&&value.bits){
            if(!silph_read_call(s->read,place,"get_RaidLevel",SILPH_READ_ENUM32,NULL,0,&value)||!value.bits)return false;
            return value.bits>=1&&value.bits<=5;
        }
        return false;
    }
    return false;
}
static bool gym_par_has_raid(GameRead *s,SilphManagedRef gym,uint32_t *level,uint32_t *players,uint64_t *seed){
    if(!gym.serial)return false;
    if(level)*level=0;
    if(players)*players=0;
    bool has=false;
    static const char *flags[]={"get_HasRaid","HasRaid","get_IsRaidActive","IsRaidActive","get_RaidActive","get_HasActiveRaid",NULL};
    gym_probe_bool(s,gym,flags,&has);
    static const char *nest[]={"get_RaidInfo","get_Raid","get_RaidBattle","get_IncomingRaid",NULL};
    SilphManagedRef raid={0};
    if(!gym_probe_ref(s,gym,nest,&raid)&&raid.serial){
        has=true;
        if(level){
            uint32_t probed=0;
            if(!silph_raid_probe_return_type(s->read,raid,"RaidLevel",&probed)&&probed>=1&&probed<=7)*level=probed;
            else{
                static const char *tier_methods[]={"caop","get_RaidLevel","get_RaidTier","get_EggLevel",NULL};
                gym_probe_enum32(s,raid,tier_methods,level);
            }
        }
        if(seed){
            SilphReadValue seedv={0};
            if(!silph_read_call(s->read,raid,"get_RaidSeed",SILPH_READ_I64,NULL,0,&seedv)&&seedv.bits)*seed=seedv.bits;
            if(!*seed&&!silph_read_field(s->read,raid,"RaidSeed",SILPH_READ_I64,&seedv)&&seedv.bits)*seed=seedv.bits;
        }
        if(players)(void)gym_players(s,raid,players);
        release(s,&raid);
    }
    if(level&&!*level)(void)gym_raid_level(s,gym,level);
    if(level&&!*level){
        SilphReadValue rl={0};
        if(!silph_read_call(s->read,gym,"get_RaidLevel",SILPH_READ_ENUM32,NULL,0,&rl)&&rl.bits)*level=(uint32_t)rl.bits;
        if(*level<1&&!silph_read_call(s->read,gym,"get_RaidTier",SILPH_READ_ENUM32,NULL,0,&rl)&&rl.bits)*level=(uint32_t)rl.bits;
    }
    if(!has&&level&&*level>=1&&*level<=5)has=true;
    if(!has&&gym_place_is_gym(s,gym)){
        SilphReadValue rl={0};
        if(!silph_read_call(s->read,gym,"get_RaidEndMs",SILPH_READ_I64,NULL,0,&rl)&&rl.bits)has=true;
        if(!has&&!silph_read_call(s->read,gym,"get_RaidBossName",SILPH_READ_REFERENCE,NULL,0,&rl)&&rl.reference.serial)has=true;
    }
    if(!has)return false;
    if(level&&(*level<1||*level>5))*level=(*level>5)?5:1;
    if(level&&!*level)*level=1;
    if(players&&!*players)(void)gym_players(s,gym,players);
    if(seed&&!*seed){
        SilphReadValue seedv={0};
        if(!silph_read_call(s->read,gym,"get_RaidSeed",SILPH_READ_I64,NULL,0,&seedv)&&seedv.bits)*seed=seedv.bits;
    }
    if(gym_raid_is_egg(s,gym,(SilphManagedRef){0}))return false;
    if(gym_raid_live_map(s,gym))return true;
    return gym_raid_hatched_boss(s,gym)&&((seed&&*seed)||has);
}
static int gym_scan_emdp(GameRead *s,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output){
    SilphReadValue dictionary={0},values={0},iterator={0},current={0},value={0};
    int status=gym_dictionary_emdp(s,&dictionary);
    if(status&&s->field_gyms[0])status=gym_dictionary_emdg(s,&dictionary);
    if(!status)status=silph_read_call(s->read,dictionary.reference,"get_Values",SILPH_READ_REFERENCE,NULL,0,&values);
    if(!status)status=silph_read_call(s->read,values.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
    if(status){release(s,&dictionary.reference);return status;}
    for(unsigned i=0;!status&&i<1024;i++){
        status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&value);
        if(status||!value.bits)break;
        status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
        if(status||!current.reference.serial)continue;
        SilphReadValue poke={0};
        if(!silph_read_call(s->read,current.reference,"get_Pokestop",SILPH_READ_BOOL,NULL,0,&poke)&&poke.bits){
            release(s,&current.reference);continue;
        }
        (void)gym_consider(s,current.reference,lat,lon,anchor,maximum,tier_mask,prefer_players,output,&output->scanned);
        release(s,&current.reference);
        status=0;
    }
    if(iterator.reference.serial)silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&value);
    release(s,&iterator.reference);release(s,&values.reference);release(s,&dictionary.reference);
    if(output->fort[0])__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan emdp pick fort=%s tier=%u dist=%.0fm",output->fort,(unsigned)output->raid_level,output->meters);
    return output->fort[0]?0:SILPH_MANAGED_NOT_READY;
}
static int gym_scan_nearest_par(GameRead *s,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,SilphGameGym *output){
    SilphReadValue dictionary={0},collection={0};
    int status=gym_dictionary_emds(s,&dictionary);
    if(status||!dictionary.reference.serial)return status?status:SILPH_MANAGED_NOT_READY;
    status=silph_read_call(s->read,dictionary.reference,"get_Values",SILPH_READ_REFERENCE,NULL,0,&collection);
    if(status||!collection.reference.serial){release(s,&dictionary.reference);return status?status:SILPH_MANAGED_NOT_READY;}
    SilphReadValue iterator={0},current={0},value={0};
    status=silph_read_call(s->read,collection.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
    if(status){release(s,&collection.reference);release(s,&dictionary.reference);return status;}
    for(unsigned i=0;!status&&i<512;i++){
        status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&value);
        if(status||!value.bits)break;
        status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
        if(status||!current.reference.serial)continue;
        SilphManagedRef gym={0};
        if(gym_resolve(s,current.reference,&gym)||!gym.serial){
            char fid[161]={0};
            if(!read_text(s,current.reference,"get_Id",fid,sizeof(fid))&&fid[0])gym_lookup_by_id(s,fid,&gym);
        }
        SilphManagedRef probe=gym.serial?gym:current.reference;
        char lookup_id[161]={0};
        if(!read_text(s,probe,"get_Id",lookup_id,sizeof(lookup_id))&&lookup_id[0]){
            SilphManagedRef resolved={0};
            if(!gym_lookup_by_id(s,lookup_id,&resolved)&&resolved.serial){if(gym.serial&&gym.serial!=resolved.serial)release(s,&gym);probe=resolved;gym=resolved;}
            else if(resolved.serial)release(s,&resolved);
        }
        uint32_t level=0,players=0;uint64_t seed=0;
        bool par=gym_par_has_raid(s,probe,&level,&players,&seed);
        double a=0,b=0;
        if(!par||location(s,probe,&a,&b))par=false;
        double meters=anchor?distance_meters(anchor->latitude,anchor->longitude,a,b):distance_meters(lat,lon,a,b);
        if(par&&isfinite(meters)&&meters<=maximum&&gym_tier_allowed(tier_mask,level?level:1)&&!gym_raid_ending(s,probe,lookup_id)){
            bool better=gym_pick_better(output,level?level:1,players,meters,1);
            if(better){
                SilphGameGym row={.latitude=a,.longitude=b,.player_latitude=lat,.player_longitude=lon,.meters=meters,.raid_level=level?level:1,.players=players,.lobby_players=players,.raid_seed=seed,.raid_live=1};
                if(lookup_id[0])memcpy(row.fort,lookup_id,sizeof(row.fort));
                else read_text(s,probe,"get_Id",row.fort,sizeof(row.fort));
                gym_pick_retain_place(s,current.reference,&row);
                *output=row;
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym par nearest pick fort=%s tier=%u dist=%.0fm seed=%llu",row.fort,(unsigned)row.raid_level,meters,(unsigned long long)seed);
            }
        }
        if(gym.serial&&gym.serial!=current.reference.serial)release(s,&gym);
        release(s,&current.reference);
    }
    if(iterator.reference.serial)silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&value);
    release(s,&iterator.reference);release(s,&collection.reference);release(s,&dictionary.reference);
    return output->fort[0]?0:SILPH_MANAGED_NOT_READY;
}
static int gym_consider(GameRead *s,SilphManagedRef gym,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,unsigned *scanned){
    (void)prefer_players;
    SilphReadValue poke={0};
    if(!silph_read_call(s->read,gym,"get_Pokestop",SILPH_READ_BOOL,NULL,0,&poke)&&poke.bits)return 0;
    double a=0,b=0;int status=location(s,gym,&a,&b);if(status)return status;
    double meters=anchor?distance_meters(anchor->latitude,anchor->longitude,a,b):distance_meters(lat,lon,a,b);
    if(anchor&&meters>maximum)return 0;
    if(isfinite(meters)&&meters<=maximum){
        if(gym_place_is_gym(s,gym))output->matches++;
        else{SilphReadValue tier={0};if(!silph_read_call(s->read,gym,"get_RaidLevel",SILPH_READ_ENUM32,NULL,0,&tier)&&tier.bits>=1&&tier.bits<=5)output->matches++;}
    }
    SilphManagedRef probe=gym;
    char lookup_id[161]={0};
    if(!gym_fort_id_text(s,gym,lookup_id,sizeof(lookup_id))&&lookup_id[0]){}
    else if(!read_text(s,gym,"get_Id",lookup_id,sizeof(lookup_id))&&lookup_id[0]){}
    if(lookup_id[0]){
        SilphManagedRef resolved={0};
        if(!gym_lookup_by_id(s,lookup_id,&resolved)&&resolved.serial)probe=resolved;
        else if(resolved.serial)release(s,&resolved);
    }
    uint32_t level=0,players=0,boss=0;bool active=false;uint64_t seed=0;
    status=gym_raid_active(s,probe,&level,&players,&active,&seed,&boss);
    if(status==SILPH_MANAGED_CONTRACT)status=0;
    if(!seed||!level){
        uint32_t pl=level,pp=players;uint64_t ps=seed;
        if(gym_par_has_raid(s,probe,&pl,&pp,&ps)){
            if(!level)level=pl;
            if(!players)players=pp;
            if(!seed)seed=ps;
        }
    }
    if(gym_raid_unhatched_egg(s,probe)||gym_raid_unhatched_egg(s,gym)){
        if(probe.serial!=gym.serial)release(s,&probe);
        return 0;
    }
    if(!active){
        if(gym_raid_hatched_boss(s,probe)||gym_raid_hatched_boss(s,gym))active=true;
    }
    if(!active&&!seed&&!gym_raid_live_map(s,probe)&&!gym_raid_live_map(s,gym)){
        if(probe.serial!=gym.serial)release(s,&probe);
        return 0;
    }
    if(!active){
        SilphReadValue live={0};
        static const char *live_methods[]={"get_IsRaidActive","IsRaidActive","get_HasActiveRaid",NULL};
        for(unsigned i=0;!active&&live_methods[i];i++){
            if(!silph_read_call(s->read,probe,live_methods[i],SILPH_READ_BOOL,NULL,0,&live)&&live.bits)active=true;
        }
        if(!active){
            for(unsigned i=0;!active&&live_methods[i];i++){
                if(!silph_read_call(s->read,gym,live_methods[i],SILPH_READ_BOOL,NULL,0,&live)&&live.bits)active=true;
            }
        }
    }
    if(!active&&!seed&&!gym_raid_hatched_boss(s,probe)&&!gym_raid_hatched_boss(s,gym)){
        if(probe.serial!=gym.serial)release(s,&probe);
        return 0;
    }
    if(gym_raid_unhatched_egg(s,probe)||gym_raid_unhatched_egg(s,gym)){
        if(probe.serial!=gym.serial)release(s,&probe);
        return 0;
    }
    if(!seed){
        uint32_t lv=level?level:1;
        (void)gym_fill_raid_seed(s,probe,&lv,&seed);
        if(lv&&!level)level=lv;
    }
    if(!seed&&meters<=250.0){
        uint32_t probed=0;
        if(!silph_raid_probe_return_type(s->read,probe,"RaidLevel",&probed)&&probed>=1&&probed<=7){
            level=probed;
            (void)gym_fill_raid_seed(s,probe,&level,&seed);
        }
        SilphReadValue tier={0};
        if(!seed&&!silph_read_call(s->read,probe,"get_RaidTier",SILPH_READ_ENUM32,NULL,0,&tier)&&tier.bits>=1&&tier.bits<=5){
            level=(uint32_t)tier.bits;
            (void)gym_fill_raid_seed(s,probe,&level,&seed);
        }
    }
    bool raid_live=active||(seed>0);
    if(!seed){
        SilphReadValue live={0};
        static const char *live_methods[]={"get_IsRaidActive","IsRaidActive","get_HasActiveRaid",NULL};
        for(unsigned i=0;!raid_live&&live_methods[i];i++){
            if(!silph_read_call(s->read,probe,live_methods[i],SILPH_READ_BOOL,NULL,0,&live)&&live.bits)raid_live=true;
        }
        if(!raid_live&&gym_raid_hatched_boss(s,probe))raid_live=true;
        if(!raid_live&&gym_raid_hatched_boss(s,gym))raid_live=true;
        if(!raid_live){
            if(probe.serial!=gym.serial)release(s,&probe);
            return 0;
        }
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym hatched pending seed fort=%s tier=%u dist=%.0fm",lookup_id[0]?lookup_id:"?",(unsigned)level,meters);
    }
    gym_normalize_star_level(s,probe,&level);
    if(!level)gym_normalize_star_level(s,gym,&level);
    if(!level||level>7){if(probe.serial!=gym.serial)release(s,&probe);return 0;}
    if(!gym_scan_joinable_raid(s,probe,raid_live,seed)&&!gym_scan_joinable_raid(s,gym,raid_live,seed)){
        if(probe.serial!=gym.serial)release(s,&probe);
        return 0;
    }
    if(!gym_tier_allowed(tier_mask,level))return 0;
    if(!isfinite(meters)||meters>maximum)return 0;
    bool better=gym_pick_better(output,level,players,meters,1);
    if(!better){if(probe.serial!=gym.serial)release(s,&probe);return 0;}
    char id[161]={0};
    if(lookup_id[0])memcpy(id,lookup_id,strlen(lookup_id)+1);
    else{
        status=gym_fort_id_text(s,probe,id,sizeof(id));
        if(status||!id[0])status=gym_fort_id_text(s,gym,id,sizeof(id));
        if(status||!id[0])status=read_text(s,probe,"get_Id",id,sizeof(id));
        if(status||!id[0])status=read_text(s,gym,"get_Id",id,sizeof(id));
    }
    if(probe.serial!=gym.serial)release(s,&probe);
    if(!id[0])return 0;
    if(gym_id_skipped(s,id))return 0;
    if(gym_raid_ending(s,gym,id))return 0;
    if(output->fort[0]&&output->raid_live&&level&&output->raid_level&&level<output->raid_level&&output->meters+15.0>=meters)
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym pick nearer live fort=%s tier=%u over tier=%u dist=%.0fm",id,(unsigned)level,(unsigned)output->raid_level,meters);
    SilphGameGym row={.latitude=a,.longitude=b,.player_latitude=lat,.player_longitude=lon,.meters=meters,.raid_level=level,.players=players,.lobby_players=players,.raid_seed=seed,.raid_live=1,.boss_species=boss};
    memcpy(row.fort,id,sizeof(row.fort));
    row.scanned=*scanned;row.matches=output->matches;
    gym_pick_retain_place(s,gym,&row);
    *output=row;
    if(!seed)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym map-visible pick fort=%s tier=%u dist=%.0fm (seed pending)",id,(unsigned)level,meters);
    return 0;
}
static int gym_scan_enumerator(GameRead *s,SilphReadValue *dictionary,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output){
    SilphReadValue values={0},iterator={0},current={0},value={0};int status=silph_read_call(s->read,dictionary->reference,"get_Values",SILPH_READ_REFERENCE,NULL,0,&values);
    if(status==SILPH_MANAGED_CONTRACT)status=0;
    if(!status&&values.reference.serial)status=silph_read_call(s->read,values.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
    if(status==SILPH_MANAGED_CONTRACT){
        SilphReadValue keys={0};status=silph_read_call(s->read,dictionary->reference,"get_Keys",SILPH_READ_REFERENCE,NULL,0,&keys);
        if(!status&&keys.reference.serial){values=keys;status=silph_read_call(s->read,values.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);}
    }
    if(status)return status;
    for(unsigned i=0;!status&&i<512;i++){
        status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&value);
        if(status||!value.bits){if(!status)output->scan_complete=1;break;}output->scanned++;
        status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
        SilphManagedRef gym={0};int gr=gym_resolve(s,current.reference,&gym);
        if(!gym.serial)gr=gym_resolve_entry(s,current.reference,&gym);
        if(!gym.serial)gr=gym_lookup_from_key(s,current.reference,&gym);
        if(!gym.serial){
            char fid[161]={0};uint32_t fn=0;
            if(!read_text(s,current.reference,"get_Id",fid,sizeof(fid))&&fid[0])gr=gym_lookup_by_id(s,fid,&gym);
            else if(!silph_read_utf8(s->read,current.reference,fid,sizeof(fid),&fn)&&fid[0]&&strchr(fid,'-'))gr=gym_lookup_by_id(s,fid,&gym);
            else if(fid[0]){
                const char *fg=s->field_gyms[0]?s->field_gyms:"emkw";
                int em=fort_dictionary(s,fg,fid,&gym);
                if((em||!gym.serial)&&strcmp(fg,"emdp")!=0)em=fort_dictionary(s,"emdp",fid,&gym);
                (void)em;
            }
        }
        if(!gym.serial){
            void *object=NULL;if(!silph_managed_target(s->managed,current.reference,&object)&&object)gr=silph_managed_retain(s->managed,object,false,&gym);
        }
        int drop=release(s,&current.reference);if(!status)status=drop;
        if(!gym.serial){status=0;continue;}
        int pick=gym_consider(s,gym,lat,lon,anchor,maximum,tier_mask,prefer_players,output,&output->scanned);
        drop=release(s,&gym);if(!status)status=drop;(void)pick;(void)gr;status=0;
    }
    int disposed=iterator.reference.serial?silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&value):0;
    SilphManagedRef *refs[]={&iterator.reference,&values.reference,&dictionary->reference};for(unsigned i=0;i<3;i++){int freed=release(s,refs[i]);if(!status)status=freed;}
    if(!status)status=disposed;return status;
}
static void gym_scan_try_pick(GameRead *s,SilphManagedRef gym,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,unsigned *scanned,bool active_list,bool uncompleted_only){
    if(uncompleted_only||active_list){
        (void)gym_consider_active_fallback(s,gym,lat,lon,anchor,maximum,tier_mask,prefer_players,output,scanned,active_list);
        if(!output->fort[0])(void)gym_consider(s,gym,lat,lon,anchor,maximum,tier_mask,prefer_players,output,scanned);
    }else{
        (void)gym_consider(s,gym,lat,lon,anchor,maximum,tier_mask,prefer_players,output,scanned);
    }
}
static int gym_fill_raid_seed(GameRead *s,SilphManagedRef gym,uint32_t *level,uint64_t *seed){
    if(!gym.serial||!seed||*seed)return 0;
    SilphReadValue seedv={0};
    static const char *seed_methods[]={"get_RaidSeed","get_RaidBattleSeed","get_BattleSeed","get_RaidBattleId",NULL};
    for(unsigned i=0;seed_methods[i];i++){
        if(!silph_read_call(s->read,gym,seed_methods[i],SILPH_READ_I64,NULL,0,&seedv)&&seedv.bits){*seed=seedv.bits;return 0;}
    }
    SilphManagedRef fort={0};
    if(!silph_read_field_type(s->read,gym,"Holoholo.Rpc","PokemonFortProto",&fort)&&fort.serial){
        if(!silph_read_field(s->read,fort,"RaidSeed",SILPH_READ_I64,&seedv)&&seedv.bits)*seed=seedv.bits;
        if(!*seed&&!silph_read_field(s->read,fort,"RaidBattleSeed",SILPH_READ_I64,&seedv)&&seedv.bits)*seed=seedv.bits;
        release(s,&fort);
    }
    if(!*seed&&level&&*level>=1&&*level<=5){
        uint32_t pl=0;bool ac=false;uint64_t sd=0;
        if(!gym_raid_active(s,gym,level,&pl,&ac,&sd,NULL)&&ac&&sd)*seed=sd;
    }
    return *seed?0:SILPH_MANAGED_NOT_READY;
}
static int gym_scan_values_resolved(GameRead *s,SilphManagedRef values,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,bool active_list,bool uncompleted_only){
    if(!values.serial)return 0;
    SilphReadValue iterator={0},current={0},value={0};int status=0;
    if(!silph_read_call(s->read,values,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator)&&iterator.reference.serial){
        if(active_list)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active values enumerator");
        for(unsigned i=0;!status&&i<512;i++){
            status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&value);
            if(status||!value.bits)break;
            output->scanned++;
            status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
            SilphManagedRef gym={0};int gr=gym_resolve_any(s,current.reference,&gym);
            int drop=release(s,&current.reference);if(!status)status=drop;
            if(!gym.serial){status=0;continue;}
            if(active_list&&i<5){
                char gid[161]={0};gym_fort_id_text(s,gym,gid,sizeof(gid));
                SilphReadValue ac={0};bool live=false;
                if(!silph_read_call(s->read,gym,"get_IsRaidActive",SILPH_READ_BOOL,NULL,0,&ac)&&ac.bits)live=true;
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active value[%u] id=%s live=%u egg=%d",(unsigned)i,gid[0]?gid:"?",(unsigned)live,gym_raid_is_egg(s,gym,(SilphManagedRef){0}));
            }
            gym_scan_try_pick(s,gym,lat,lon,anchor,maximum,tier_mask,prefer_players,output,&output->scanned,active_list,uncompleted_only);
            drop=release(s,&gym);if(!status)status=drop;(void)gr;status=0;
            if(output->fort[0]&&output->raid_live)break;
        }
        if(iterator.reference.serial)silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&value);
        release(s,&iterator.reference);
        if(output->fort[0])return 0;
    }
    SilphReadValue count={0};uint32_t n=0;
    if(!silph_read_call(s->read,values,"get_Count",SILPH_READ_I32,NULL,0,&count))n=(uint32_t)count.bits;
    if(!n&&!silph_read_call(s->read,values,"get_Length",SILPH_READ_I32,NULL,0,&count))n=(uint32_t)count.bits;
    if(!n||n>512)return 0;
    if(active_list)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active values indexed count=%u",(unsigned)n);
    for(uint32_t i=0;!status&&i<n;i++){
        SilphReadArgument arg={.kind=SILPH_READ_I32,.value.bits=i};SilphReadValue item={0};
        status=silph_read_call(s->read,values,"get_Item",SILPH_READ_REFERENCE,&arg,1,&item);
        if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,values,"get_Item",SILPH_READ_BOXED,&arg,1,&item);
        if(status)break;
        output->scanned++;
        SilphManagedRef gym={0};(void)gym_resolve_any(s,item.reference,&gym);
        int drop=release(s,&item.reference);if(!status)status=drop;
        if(!gym.serial){status=0;continue;}
        gym_scan_try_pick(s,gym,lat,lon,anchor,maximum,tier_mask,prefer_players,output,&output->scanned,active_list,uncompleted_only);
        drop=release(s,&gym);if(!status)status=drop;status=0;
        if(output->fort[0]&&output->raid_live)break;
    }
    return output->fort[0]?0:status;
}
static int gym_scan_dict_kvp(GameRead *s,SilphManagedRef dict,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,bool active_list,bool uncompleted_only);
static int gym_scan_dict_keys(GameRead *s,SilphManagedRef dict,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,bool active_list,bool uncompleted_only);
static int gym_scan_indexed(GameRead *s,SilphManagedRef list,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,bool active_list,bool uncompleted_only);
static int gym_scan_dict_toarray(GameRead *s,SilphManagedRef dict,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,bool active_list,bool uncompleted_only,const char *label){
    SilphManagedRef flat={0};int mat=SILPH_MANAGED_CONTRACT;
    SilphReadValue keys={0};
    if(!silph_read_call(s->read,dict,"get_Keys",SILPH_READ_REFERENCE,NULL,0,&keys)&&keys.reference.serial)
        mat=silph_read_materialize_collection(s->read,keys.reference,&flat,512);
    if((mat||!flat.serial)&&keys.reference.serial){release(s,&keys.reference);keys.reference=(SilphManagedRef){0};}
    if(!flat.serial){
        SilphReadValue vals={0};
        if(!silph_read_call(s->read,dict,"get_Values",SILPH_READ_REFERENCE,NULL,0,&vals)&&vals.reference.serial)
            mat=silph_read_materialize_collection(s->read,vals.reference,&flat,512);
        if(vals.reference.serial)release(s,&vals.reference);
    }
    if(!flat.serial)mat=silph_read_materialize_collection(s->read,dict,&flat,512);
    if(!mat&&flat.serial){
        SilphReadValue ac={0};uint32_t an=0;
        if(!silph_read_call(s->read,flat,"get_Length",SILPH_READ_I32,NULL,0,&ac))an=(uint32_t)ac.bits;
        if(!an&&!silph_read_call(s->read,flat,"get_Count",SILPH_READ_I32,NULL,0,&ac))an=(uint32_t)ac.bits;
        if(active_list&&label)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active %s CopyTo ok len=%u",(const char *)label,(unsigned)an);
        (void)gym_scan_indexed(s,flat,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);
        release(s,&flat);
        return output->fort[0]?0:0;
    }
    if(flat.serial)release(s,&flat);
    SilphReadValue arr={0};int tar=silph_read_call(s->read,dict,"ToArray",SILPH_READ_REFERENCE,NULL,0,&arr);
    if(!tar&&arr.reference.serial){
        if(active_list&&label)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active %s ToArray ok",(const char *)label);
        (void)gym_scan_indexed(s,arr.reference,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);
        release(s,&arr.reference);
        return output->fort[0]?0:0;
    }
    if(active_list&&label)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active %s materialize err=%d ToArray err=%d",label,(int)mat,(int)tar);
    if(arr.reference.serial)release(s,&arr.reference);
    return 0;
}
static int gym_scan_key_collection(GameRead *s,SilphManagedRef dict,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,bool active_list,bool uncompleted_only){
    if(active_list&&!output->fort[0])(void)gym_scan_dict_toarray(s,dict,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only,"dict");
    SilphReadValue keys={0},count={0};uint32_t n=0;
    int status=silph_read_call(s->read,dict,"get_Keys",SILPH_READ_REFERENCE,NULL,0,&keys);
    if((status||!keys.reference.serial)&&!silph_read_field(s->read,dict,"Keys",SILPH_READ_REFERENCE,&keys))status=0;
    if((status||!keys.reference.serial)&&active_list){
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active get_Keys err=%d",(int)status);
        if(keys.reference.serial)release(s,&keys.reference);
        SilphReadValue vals={0};int ver=silph_read_call(s->read,dict,"get_Values",SILPH_READ_REFERENCE,NULL,0,&vals);
        if((ver||!vals.reference.serial)&&!silph_read_field(s->read,dict,"Values",SILPH_READ_REFERENCE,&vals))ver=0;
        if(!ver&&vals.reference.serial){
            SilphReadValue flat={0};
            int tar=silph_read_call(s->read,vals.reference,"ToArray",SILPH_READ_REFERENCE,NULL,0,&flat);
            if(!tar&&flat.reference.serial){
                if(active_list)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active values ToArray");
                (void)gym_scan_indexed(s,flat.reference,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);
                release(s,&flat.reference);
            }else{
                if(active_list)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active values ToArray err=%d",(int)tar);
                if(flat.reference.serial)release(s,&flat.reference);
            }
            if(!output->fort[0])(void)gym_scan_values_resolved(s,vals.reference,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);
            release(s,&vals.reference);
        }else if(active_list)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active get_Values err=%d",(int)ver);
        if(!output->fort[0])(void)gym_scan_dict_kvp(s,dict,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);
        if(!output->fort[0])(void)gym_scan_dict_keys(s,dict,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);
        return output->fort[0]?0:0;
    }
    if(status||!keys.reference.serial)return status?status:0;
    SilphReadValue keyarr={0};
    if(active_list&&!silph_read_call(s->read,keys.reference,"ToArray",SILPH_READ_REFERENCE,NULL,0,&keyarr)&&keyarr.reference.serial){
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active keys ToArray");
        (void)gym_scan_indexed(s,keyarr.reference,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);
        release(s,&keyarr.reference);
        if(output->fort[0]){release(s,&keys.reference);return 0;}
    }else if(keyarr.reference.serial)release(s,&keyarr.reference);
    if(!silph_read_call(s->read,keys.reference,"get_Count",SILPH_READ_I32,NULL,0,&count))n=(uint32_t)count.bits;
    if(!n&&!silph_read_call(s->read,keys.reference,"get_Length",SILPH_READ_I32,NULL,0,&count))n=(uint32_t)count.bits;
    if(active_list&&n)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active keys count=%u",(unsigned)n);
    if(!n||n>512){release(s,&keys.reference);return 0;}
    for(uint32_t i=0;i<n;i++){
        SilphReadArgument arg={.kind=SILPH_READ_I32,.value.bits=i};SilphReadValue key={0};
        status=silph_read_call(s->read,keys.reference,"get_Item",SILPH_READ_REFERENCE,&arg,1,&key);
        if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,keys.reference,"get_Item",SILPH_READ_BOXED,&arg,1,&key);
        if(status||!key.reference.serial)continue;
        output->scanned++;
        SilphManagedRef gym={0};
        if(gym_lookup_from_key(s,key.reference,&gym)||!gym.serial){
            char id[161]={0};uint32_t idn=0;
            if(!read_text(s,key.reference,"get_Id",id,sizeof(id))&&id[0]){}
            else if(!silph_read_utf8(s->read,key.reference,id,sizeof(id),&idn)&&id[0]){}
            else read_text(s,key.reference,"ToString",id,sizeof(id));
            if(id[0])(void)gym_lookup_by_id(s,id,&gym);
        }
        release(s,&key.reference);
        if(!gym.serial)continue;
        if(active_list&&i<5){
            char gid[161]={0};read_text(s,gym,"get_Id",gid,sizeof(gid));
            uint32_t lv=0;gym_raid_level(s,gym,&lv);
            uint64_t sd=0;uint32_t pl=0;bool ac=false;
            gym_raid_active(s,gym,&lv,&pl,&ac,&sd,NULL);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active key[%u] id=%s tier=%u seed=%llu",(unsigned)i,gid,(unsigned)lv,(unsigned long long)sd);
        }
        gym_scan_try_pick(s,gym,lat,lon,anchor,maximum,tier_mask,prefer_players,output,&output->scanned,active_list,uncompleted_only);
        release(s,&gym);
        if(output->fort[0]&&output->raid_live)break;
    }
    release(s,&keys.reference);
    return output->fort[0]?0:0;
}
static int gym_scan_dict_kvp(GameRead *s,SilphManagedRef dict,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,bool active_list,bool uncompleted_only){
    SilphReadValue iterator={0},current={0},value={0};
    int status=silph_read_call(s->read,dict,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
    if(status)status=silph_read_call(s->read,dict,"GetEnumerator",SILPH_READ_REFERENCE,NULL,0,&iterator);
    if(status==SILPH_MANAGED_CONTRACT){
        if(active_list)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active dict enum err=%d",(int)status);
        return status;
    }
    if(status||!iterator.reference.serial)return 0;
    if(active_list)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active dict enumerator");
    for(unsigned i=0;!status&&i<512;i++){
        status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&value);
        if(status||!value.bits){if(!status)output->scan_complete=1;break;}
        output->scanned++;
        status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_BOXED,NULL,0,&current);
        if(status||!current.reference.serial)continue;
        SilphManagedRef gym={0};
        if(gym_resolve_entry(s,current.reference,&gym)||!gym.serial){
            SilphReadValue keyv={0};
            if(!silph_read_call(s->read,current.reference,"get_Key",SILPH_READ_REFERENCE,NULL,0,&keyv)&&keyv.reference.serial){
                if(!gym.serial){
                    (void)gym_lookup_from_key(s,keyv.reference,&gym);
                }
                if(!gym.serial){
                    char id[161]={0};uint32_t n=0;
                    if(!read_text(s,keyv.reference,"get_Id",id,sizeof(id))&&id[0]){}
                    else if(!silph_read_utf8(s->read,keyv.reference,id,sizeof(id),&n)&&id[0]){}
                    else read_text(s,keyv.reference,"ToString",id,sizeof(id));
                    if(id[0])(void)gym_lookup_by_id(s,id,&gym);
                }
                release(s,&keyv.reference);
            }
        }
        if(!gym.serial)(void)gym_resolve(s,current.reference,&gym);
        uint32_t dict_boss=0;
        {
            SilphReadValue val={0},bossv={0};
            if(!silph_read_call(s->read,current.reference,"get_Value",SILPH_READ_REFERENCE,NULL,0,&val)&&val.reference.serial){
                gym_raid_apply_info(s,val.reference,NULL,NULL,NULL,NULL,&bossv,NULL);
                dict_boss=(uint32_t)bossv.bits;
                release(s,&val.reference);
            }
        }
        int drop=release(s,&current.reference);if(!status)status=drop;
        if(!gym.serial)continue;
        if(active_list&&i<3){
            char id[161]={0};read_text(s,gym,"get_Id",id,sizeof(id));
            uint32_t lv=0;gym_raid_level(s,gym,&lv);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active kvp[%u] id=%s level=%u boss=%u",(unsigned)i,id,(unsigned)lv,(unsigned)dict_boss);
        }
        gym_scan_try_pick(s,gym,lat,lon,anchor,maximum,tier_mask,prefer_players,output,&output->scanned,active_list,uncompleted_only);
        if(output->fort[0]&&!output->boss_species)output->boss_species=dict_boss;
        drop=release(s,&gym);if(!status)status=drop;
        if(output->fort[0]&&output->raid_live)break;
    }
    if(iterator.reference.serial)silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&value);
    release(s,&iterator.reference);
    return output->fort[0]?0:status;
}
static int gym_scan_dict_keys(GameRead *s,SilphManagedRef dict,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,bool active_list,bool uncompleted_only){
    SilphReadValue keys={0},iterator={0},current={0},value={0};int status=silph_read_call(s->read,dict,"get_Keys",SILPH_READ_REFERENCE,NULL,0,&keys);
    if(status==SILPH_MANAGED_CONTRACT)status=0;
    if(!status&&keys.reference.serial)status=silph_read_call(s->read,keys.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
    for(unsigned i=0;!status&&i<512;i++){
        status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&value);
        if(status||!value.bits){if(!status)output->scan_complete=1;break;}output->scanned++;
        status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
        char id[161]={0};uint32_t n=0;
        if(!status&&!read_text(s,current.reference,"get_Id",id,sizeof(id))&&id[0]){}
        else if(!status&&!silph_read_utf8(s->read,current.reference,id,sizeof(id),&n)&&id[0]){}
        else if(!status)gym_fort_id_text(s,current.reference,id,sizeof(id));
        SilphManagedRef gym={0};int lk=0;
        if(!status)lk=gym_lookup_from_key(s,current.reference,&gym);
        if(!gym.serial&&id[0])lk=gym_lookup_by_id(s,id,&gym);
        if(!gym.serial&&id[0]){
            const char *fg=s->field_gyms[0]?s->field_gyms:"emkw";
            int em=fort_dictionary(s,fg,id,&gym);
            if((em||!gym.serial)&&strcmp(fg,"emdp")!=0)em=fort_dictionary(s,"emdp",id,&gym);
            if(em&&em!=SILPH_MANAGED_NOT_READY&&gym.serial)release(s,&gym);(void)em;
        }
        int drop=release(s,&current.reference);if(!status)status=drop;
        if(!gym.serial){
            if(active_list&&id[0]&&i==0)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active key unresolved sample=%s",id);
            status=0;continue;
        }
        if(!id[0])(void)gym_fort_id_text(s,gym,id,sizeof(id));
        if(active_list&&i<5){
            if(i==0){
                silph_dump_methods(s->read,gym,"SampleGym");
                SilphReadValue rinfo={0};
                if(!silph_read_call(s->read,gym,"get_RaidInfo",SILPH_READ_REFERENCE,NULL,0,&rinfo)&&rinfo.reference.serial){
                    silph_dump_methods(s->read,rinfo.reference,"SampleRaidInfo");
                    release(s,&rinfo.reference);
                }
            }
            uint32_t lv=0,rt=0,gtier=0,rstate=0,instate=0;gym_raid_level(s,gym,&lv);
            SilphReadValue tier={0};if(!silph_read_call(s->read,gym,"get_RaidTier",SILPH_READ_ENUM32,NULL,0,&tier))rt=(uint32_t)tier.bits;
            SilphReadValue vtier={0};if(!silph_read_call(s->read,gym,"get_Tier",SILPH_READ_ENUM32,NULL,0,&vtier))gtier=(uint32_t)vtier.bits;
            SilphReadValue vrstate={0};if(!silph_read_call(s->read,gym,"get_CurrentRaidState",SILPH_READ_ENUM32,NULL,0,&vrstate))rstate=(uint32_t)vrstate.bits;
            SilphReadValue vinstate={0};if(!silph_read_call(s->read,gym,"get_InGymState",SILPH_READ_ENUM32,NULL,0,&vinstate))instate=(uint32_t)vinstate.bits;
            double a=NAN,b=NAN,m=INFINITY;if(!location(s,gym,&a,&b)&&isfinite(a))m=anchor?distance_meters(anchor->latitude,anchor->longitude,a,b):distance_meters(lat,lon,a,b);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active sample[%u] id=%s level=%u tier=%u gtier=%u rstate=%u instate=%u dist=%.0fm",
                (unsigned)i,id[0]?id:"?",(unsigned)lv,(unsigned)rt,(unsigned)gtier,(unsigned)rstate,(unsigned)instate,m);
        }
        gym_scan_try_pick(s,gym,lat,lon,anchor,maximum,tier_mask,prefer_players,output,&output->scanned,active_list,uncompleted_only);
        drop=release(s,&gym);if(!status)status=drop;(void)lk;status=0;
    }
    int disposed=iterator.reference.serial?silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&value):0;
    if(keys.reference.serial)release(s,&keys.reference);
    if(!status)status=disposed;return status;
}
static int gym_scan_indexed(GameRead *s,SilphManagedRef list,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,bool active_list,bool uncompleted_only){
    SilphReadValue count={0};uint32_t n=0;int status=silph_read_call(s->read,list,"get_Count",SILPH_READ_I32,NULL,0,&count);
    if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,list,"get_Length",SILPH_READ_I32,NULL,0,&count);
    if(status)return status;n=(uint32_t)count.bits;if(!n||n>512)return 0;
    if(active_list)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active indexed count=%u",(unsigned)n);
    for(uint32_t i=0;!status&&i<n;i++){
        SilphReadArgument arg={.kind=SILPH_READ_I32,.value.bits=i};SilphReadValue item={0};
        status=silph_read_call(s->read,list,"get_Item",SILPH_READ_REFERENCE,&arg,1,&item);
        if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,list,"get_Item",SILPH_READ_BOXED,&arg,1,&item);
        if(status==SILPH_MANAGED_CONTRACT&&active_list){
            SilphManagedRef direct={0};
            int ar=silph_read_array_item(s->read,list,i,&direct);
            if(!ar&&direct.serial){item.reference=direct;status=0;}
            else{
                if(i==0)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active array_get err=%d",(int)ar);
                if(direct.serial)release(s,&direct);
            }
        }
        if(status){
            if(active_list&&i==0)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active get_Item err=%d",(int)status);
            break;
        }output->scanned++;
        char id[161]={0};uint32_t idn=0;
        if(item.reference.serial){
            if(!gym_fort_id_text(s,item.reference,id,sizeof(id))&&id[0]){}
            else if(!read_text(s,item.reference,"get_Id",id,sizeof(id))&&id[0]){}
            else if(!silph_read_utf8(s->read,item.reference,id,sizeof(id),&idn)&&id[0]){}
        }
        SilphManagedRef gym={0};int lk=0;
        if(item.reference.serial)(void)gym_resolve_entry(s,item.reference,&gym);
        if(!gym.serial&&item.reference.serial)lk=gym_lookup_from_key(s,item.reference,&gym);
        if(!gym.serial&&id[0])lk=gym_lookup_by_id(s,id,&gym);
        if(!gym.serial&&item.reference.serial)(void)gym_resolve_any(s,item.reference,&gym);
        if(!gym.serial&&id[0]){
            const char *fg=s->field_gyms[0]?s->field_gyms:"emkw";
            int em=fort_dictionary(s,fg,id,&gym);
            if((em||!gym.serial)&&strcmp(fg,"emdp")!=0)em=fort_dictionary(s,"emdp",id,&gym);
            if(em&&em!=SILPH_MANAGED_NOT_READY&&gym.serial)release(s,&gym);(void)em;
        }
        if(!gym.serial&&item.reference.serial){
            bool is_map=false;gym_is_map_gym(s,item.reference,&is_map);
            if(is_map){
                void *obj=NULL;if(!silph_managed_target(s->managed,item.reference,&obj)&&obj)
                    (void)silph_managed_retain(s->managed,obj,false,&gym);
            }
        }
        uint32_t dict_boss=0;
        if(item.reference.serial){
            {static int dumped_item;if(!dumped_item){dumped_item=1;silph_dump_methods(s->read,item.reference,"DictValue");}}
            SilphReadValue bossv={0};
            gym_raid_apply_info(s,item.reference,NULL,NULL,NULL,NULL,&bossv,NULL);
            dict_boss=(uint32_t)bossv.bits;
            if(!dict_boss){
                SilphReadValue val={0};
                if(!silph_read_call(s->read,item.reference,"get_Value",SILPH_READ_REFERENCE,NULL,0,&val)&&val.reference.serial){
                    gym_raid_apply_info(s,val.reference,NULL,NULL,NULL,NULL,&bossv,NULL);
                    dict_boss=(uint32_t)bossv.bits;
                    release(s,&val.reference);
                }
            }
        }
        int drop=release(s,&item.reference);if(!status)status=drop;
        if(!gym.serial){
            if(active_list&&i<3)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active item[%u] unresolved id=%s",(unsigned)i,id[0]?id:"?");
            continue;
        }
        if(!id[0])(void)gym_fort_id_text(s,gym,id,sizeof(id));
        if(active_list&&i<5){
            if(i==0){
                silph_dump_methods(s->read,gym,"SampleGym");
                SilphReadValue rinfo={0};
                if(!silph_read_call(s->read,gym,"get_RaidInfo",SILPH_READ_REFERENCE,NULL,0,&rinfo)&&rinfo.reference.serial){
                    silph_dump_methods(s->read,rinfo.reference,"SampleRaidInfo");
                    release(s,&rinfo.reference);
                }
            }
            uint32_t lv=0,rt=0,gtier=0,rstate=0,instate=0;gym_raid_level(s,gym,&lv);
            SilphReadValue tier={0};if(!silph_read_call(s->read,gym,"get_RaidTier",SILPH_READ_ENUM32,NULL,0,&tier))rt=(uint32_t)tier.bits;
            SilphReadValue vtier={0};if(!silph_read_call(s->read,gym,"get_Tier",SILPH_READ_ENUM32,NULL,0,&vtier))gtier=(uint32_t)vtier.bits;
            SilphReadValue vrstate={0};if(!silph_read_call(s->read,gym,"get_CurrentRaidState",SILPH_READ_ENUM32,NULL,0,&vrstate))rstate=(uint32_t)vrstate.bits;
            SilphReadValue vinstate={0};if(!silph_read_call(s->read,gym,"get_InGymState",SILPH_READ_ENUM32,NULL,0,&vinstate))instate=(uint32_t)vinstate.bits;
            double a=NAN,b=NAN,m=INFINITY;if(!location(s,gym,&a,&b)&&isfinite(a))m=anchor?distance_meters(anchor->latitude,anchor->longitude,a,b):distance_meters(lat,lon,a,b);
            uint32_t sb=0,spl=0;bool sac=false;uint64_t ssd=0;
            (void)gym_raid_active(s,gym,&spl,&spl,&sac,&ssd,&sb);
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active sample[%u] id=%s level=%u tier=%u gtier=%u rstate=%u instate=%u dist=%.0fm boss=%u seed=%llu",
                (unsigned)i,id[0]?id:"?",(unsigned)lv,(unsigned)rt,(unsigned)gtier,(unsigned)rstate,(unsigned)instate,m,(unsigned)sb,(unsigned long long)ssd);
        }
        gym_scan_try_pick(s,gym,lat,lon,anchor,maximum,tier_mask,prefer_players,output,&output->scanned,active_list,uncompleted_only);
        if(output->fort[0]&&!output->boss_species)output->boss_species=dict_boss;
        drop=release(s,&gym);if(!status)status=drop;(void)lk;status=0;
    }
    if(active_list&&!output->fort[0])
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active indexed done scanned=%u matches=%u err=%d fort=%s",
            (unsigned)output->scanned,(unsigned)output->matches,status,output->fort[0]?output->fort:"none");
    return status;
}
static int gym_scan_dict_or_list(GameRead *s,SilphManagedRef list,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output,bool active_list,bool uncompleted_only){
    int r=0;
    if(active_list&&!output->fort[0]){
        r=gym_scan_key_collection(s,list,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);
        if(output->fort[0]&&output->raid_live)return r;
        SilphReadValue values_early={0};
        if(!silph_read_call(s->read,list,"get_Values",SILPH_READ_REFERENCE,NULL,0,&values_early)&&values_early.reference.serial){
            int vr=gym_scan_values_resolved(s,values_early.reference,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);
            release(s,&values_early.reference);
            if(!r)r=vr;
            if(output->fort[0]&&output->raid_live)return r;
        }else if(values_early.reference.serial)release(s,&values_early.reference);
        r=gym_scan_dict_kvp(s,list,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);
        if(output->fort[0]&&output->raid_live)return r;
    }
    SilphReadValue keys_probe={0};
    if(!silph_read_call(s->read,list,"get_Keys",SILPH_READ_REFERENCE,NULL,0,&keys_probe)&&keys_probe.reference.serial){
        r=gym_scan_dict_keys(s,list,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);
        release(s,&keys_probe.reference);
        if(output->fort[0]&&output->raid_live)return r;
    }else if(keys_probe.reference.serial)release(s,&keys_probe.reference);
    if(active_list&&!output->fort[0]){int ix=gym_scan_indexed(s,list,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);if(!r)r=ix;if(output->fort[0]&&output->raid_live)return r;}
    SilphReadValue values={0};int status=silph_read_call(s->read,list,"get_Values",SILPH_READ_REFERENCE,NULL,0,&values);
    if(!status&&values.reference.serial){
        SilphReadValue dict={0};dict.reference=list;
        r=gym_scan_enumerator(s,&dict,lat,lon,anchor,maximum,tier_mask,prefer_players,output);
        if(active_list&&!output->fort[0]){
            SilphReadValue vdict={0};vdict.reference=values.reference;
            int vr=gym_scan_enumerator(s,&vdict,lat,lon,anchor,maximum,tier_mask,prefer_players,output);
            if(!r)r=vr;
        }
        if(!output->fort[0]){int k=gym_scan_dict_keys(s,list,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted_only);if(!r)r=k;}
        int freed=release(s,&values.reference);return r?r:freed;
    }
    SilphReadValue count={0};uint32_t n=0;status=silph_read_call(s->read,list,"get_Count",SILPH_READ_I32,NULL,0,&count);
    if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,list,"get_Length",SILPH_READ_I32,NULL,0,&count);
    if(status)return status;n=(uint32_t)count.bits;if(n>512)n=512;
    for(uint32_t i=0;!status&&i<n;i++){
        SilphReadArgument arg={.kind=SILPH_READ_I32,.value.bits=i};SilphReadValue item={0};
        status=silph_read_call(s->read,list,"get_Item",SILPH_READ_REFERENCE,&arg,1,&item);
        if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,list,"get_Item",SILPH_READ_BOXED,&arg,1,&item);
        if(status)break;output->scanned++;
        SilphManagedRef gym={0};int gr=gym_resolve(s,item.reference,&gym);
        if(!gym.serial){
            void *object=NULL;if(!silph_managed_target(s->managed,item.reference,&object)&&object)gr=silph_managed_retain(s->managed,object,false,&gym);
        }
        int drop=release(s,&item.reference);if(!status)status=drop;
        if(!gym.serial){status=0;continue;}
        int pick=gym_consider(s,gym,lat,lon,anchor,maximum,tier_mask,prefer_players,output,&output->scanned);
        drop=release(s,&gym);if(!status)status=drop;(void)pick;(void)gr;status=0;
    }
    return status;
}
static int gym_scan_directory(GameRead *s,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output){
    static const char *methods[]={"get_UncompletedRaidGyms","get_ActiveMapPlaces","GatherNearbyGyms","GatherNearbyForts","GetNearbyGyms","GatherNearbyMapGyms","get_NearbyGyms","get_ActiveGyms","get_ActiveMapGyms",NULL};
    int status=0;for(unsigned m=0;methods[m];m++){
        bool uncompleted=!strcmp(methods[m],"get_UncompletedRaidGyms");
        if(uncompleted&&output->fort[0]&&output->raid_live)continue;
        SilphReadValue list={0};int r=silph_read_call(s->read,s->directory,methods[m],SILPH_READ_REFERENCE,NULL,0,&list);
        if(r==SILPH_MANAGED_CONTRACT)continue;
        unsigned before=output->scanned;
        bool active_list=!strcmp(methods[m],"get_ActiveGyms")||!strcmp(methods[m],"get_ActiveMapPlaces")||!strcmp(methods[m],"get_ActiveMapGyms")||!strcmp(methods[m],"get_UncompletedRaidGyms")||uncompleted;
        if(!r&&list.reference.serial){
            SilphReadValue count={0};uint32_t n=0;
            if(!silph_read_call(s->read,list.reference,"get_Count",SILPH_READ_I32,NULL,0,&count))n=(uint32_t)count.bits;
            if(!n&&!silph_read_call(s->read,list.reference,"get_Length",SILPH_READ_I32,NULL,0,&count))n=(uint32_t)count.bits;
            if(n)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan method=%s count=%u",methods[m],n);
        }
        if(!r)r=gym_scan_dict_or_list(s,list.reference,lat,lon,anchor,maximum,tier_mask,prefer_players,output,active_list,uncompleted);
        if(output->fort[0]&&output->raid_live&&!before)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan picked via %s",methods[m]);
        int freed=release(s,&list.reference);if(!status)status=r?r:freed;
        if(output->fort[0]&&output->raid_live)break;
    }
    if(output->fort[0]){
        if(status&&status!=SILPH_MANAGED_NOT_READY)
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan chosen fort=%s tier=%u live=%u dist=%.0fm (ignored scan err=%d)",output->fort,(unsigned)output->raid_level,(unsigned)output->raid_live,output->meters,status);
        else
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan chosen fort=%s tier=%u live=%u dist=%.0fm matches=%u",output->fort,(unsigned)output->raid_level,(unsigned)output->raid_live,output->meters,output->matches);
        return 0;
    }
    return status;
}
static int gym_scan_emds_values(GameRead *s,SilphManagedRef collection,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output){
    SilphReadValue iterator={0},current={0},value={0};int status=silph_read_call(s->read,collection,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
    if(status==SILPH_MANAGED_CONTRACT)status=0;
    if(status)return status;
    for(unsigned i=0;!status&&i<512;i++){
        status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&value);
        if(status||!value.bits){if(!status)output->scan_complete=1;break;}output->scanned++;
        status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
        SilphManagedRef gym={0};if(!status)status=gym_resolve(s,current.reference,&gym);
        int drop=release(s,&current.reference);if(!status)status=drop;
        if(status==SILPH_MANAGED_NOT_READY||status==SILPH_MANAGED_CONTRACT){status=0;continue;}if(status)break;
        if(!gym.serial)continue;
        status=gym_consider(s,gym,lat,lon,anchor,maximum,tier_mask,prefer_players,output,&output->scanned);
        drop=release(s,&gym);if(!status)status=drop;
    }
    int disposed=iterator.reference.serial?silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&value):0;
    if(iterator.reference.serial)release(s,&iterator.reference);
    if(!status)status=disposed;return status;
}
static int gym_scan_emds(GameRead *s,double lat,double lon,const SilphGameStop *anchor,double maximum,uint32_t tier_mask,uint32_t prefer_players,SilphGameGym *output){
    SilphReadValue dictionary={0},collection={0};int status=gym_dictionary_emds(s,&dictionary);
    if(status||!dictionary.reference.serial)return status?status:SILPH_MANAGED_NOT_READY;
    status=silph_read_call(s->read,dictionary.reference,"get_Values",SILPH_READ_REFERENCE,NULL,0,&collection);
    if(!status&&collection.reference.serial)status=gym_scan_emds_values(s,collection.reference,lat,lon,anchor,maximum,tier_mask,prefer_players,output);
    int freed=release(s,&collection.reference);int fd=release(s,&dictionary.reference);
    if(!status)status=freed?freed:fd;return status;
}
static uint32_t gym_read_raid_tier_stars(GameRead *s,SilphManagedRef gym){
    SilphReadValue tier={0};
    uint32_t probed=0;
    if(!silph_raid_probe_return_type(s->read,gym,"RaidLevel",&probed)&&probed>=1&&probed<=7)return probed;
    static const char *star_methods[]={"get_RaidTier","get_EggLevel","get_RaidLevel",NULL};
    for(unsigned i=0;star_methods[i];i++){
        if(silph_read_call(s->read,gym,star_methods[i],SILPH_READ_ENUM32,NULL,0,&tier))continue;
        if(tier.bits>=1&&tier.bits<=7)return (uint32_t)tier.bits;
    }
    SilphManagedRef info={0};
    static const char *nest[]={"get_RaidInfo","get_Raid","get_RaidBattle","get_IncomingRaid",NULL};
    if(!gym_probe_ref(s,gym,nest,&info)&&info.serial){
        if(!silph_raid_probe_return_type(s->read,info,"RaidLevel",&probed)&&probed>=1&&probed<=7){release(s,&info);return probed;}
        for(unsigned i=0;star_methods[i];i++){
            if(silph_read_call(s->read,info,star_methods[i],SILPH_READ_ENUM32,NULL,0,&tier))continue;
            if(tier.bits>=1&&tier.bits<=7){release(s,&info);return (uint32_t)tier.bits;}
        }
        if(!silph_read_field(s->read,info,"RaidLevel",SILPH_READ_ENUM32,&tier)&&tier.bits>=1&&tier.bits<=7){release(s,&info);return (uint32_t)tier.bits;}
        release(s,&info);
    }
    return 0;
}
static void gym_normalize_star_level(GameRead *s,SilphManagedRef gym,uint32_t *level){
    if(!level)return;
    uint32_t stars=gym_read_raid_tier_stars(s,gym);
    if(stars){*level=stars;return;}
    if(*level>=1&&*level<=7)return;
    *level=0;
}
static int gym_raid_level(GameRead *s,SilphManagedRef gym,uint32_t *level){
    SilphReadValue value={0},raid={0};*level=0;
    uint32_t probed=0;
    if(!silph_raid_probe_return_type(s->read,gym,"RaidLevel",&probed)&&probed>=1&&probed<=7){*level=probed;return 0;}
    uint32_t stars=gym_read_raid_tier_stars(s,gym);
    if(stars){*level=stars;return 0;}
    static const char *tier_methods[]={"caop","get_RaidLevel","get_RaidTier","get_EggLevel",NULL};
    gym_probe_enum32(s,gym,tier_methods,level);
    if(*level>=1&&*level<=7)return 0;
    *level=0;
    int status=0;
    if(!*level){
        static const char *nest[]={"get_Raid","get_RaidInfo","get_RaidBattle","get_IncomingRaid","get_RaidEgg",NULL};
        SilphManagedRef probe={0};if(!gym_probe_ref(s,gym,nest,&probe)){
            if(!silph_raid_probe_return_type(s->read,probe,"RaidLevel",&probed)&&probed>=1&&probed<=7){*level=probed;}
            else{
                gym_probe_enum32(s,probe,tier_methods,level);
                if(!*level){silph_read_field(s->read,probe,"RaidLevel",SILPH_READ_ENUM32,&value);if(value.bits>=1&&value.bits<=7)*level=(uint32_t)value.bits;}
            }
            release(s,&probe);
        }
    }
    if(!*level){
        status=silph_read_call(s->read,gym,"get_RaidInfo",SILPH_READ_REFERENCE,NULL,0,&raid);
        if(!status&&raid.reference.serial){
            status=silph_read_field(s->read,raid.reference,"RaidLevel",SILPH_READ_ENUM32,&value);
            int freed=release(s,&raid.reference);if(!status)status=freed;
            if(value.bits>=1&&value.bits<=7)*level=(uint32_t)value.bits;
        }
    }
    if(!status&&*level>7)*level=0;
    gym_normalize_star_level(s,gym,level);
    return status;
}
static int gym_players(GameRead *s,SilphManagedRef gym,uint32_t *players){
    SilphReadValue raid={0},value={0};*players=0;
    static const char *nest[]={"get_Raid","get_RaidInfo","get_RaidBattle",NULL};
    SilphManagedRef probe={0};int status=gym_probe_ref(s,gym,nest,&probe);
    if(!status&&probe.serial)raid.reference=probe;
    else status=silph_read_call(s->read,gym,"get_Raid",SILPH_READ_REFERENCE,NULL,0,&raid);
    if(status==SILPH_MANAGED_CONTRACT)status=silph_read_field(s->read,gym,"raid_",SILPH_READ_REFERENCE,&raid);
    if(!status&&raid.reference.serial){
        static const char *player_fields[]={"NumPlayers","PlayersJoined","PlayerCount","LobbySize",NULL};
        for(unsigned i=0;player_fields[i];i++){
            if(!silph_read_field(s->read,raid.reference,player_fields[i],SILPH_READ_I32,&value)&&value.bits){*players=(uint32_t)value.bits;break;}
        }
        static const char *player_methods[]={"get_PlayerCount","get_PlayersJoined","get_NumPlayers","get_LobbySize",NULL};
        for(unsigned i=0;!*players&&player_methods[i];i++){
            if(!silph_read_call(s->read,raid.reference,player_methods[i],SILPH_READ_I32,NULL,0,&value)&&value.bits){*players=(uint32_t)value.bits;break;}
        }
        if(!*players){
            SilphReadValue lobby_val={0};
            if(!silph_read_call(s->read,raid.reference,"get_Lobby",SILPH_READ_REFERENCE,NULL,0,&lobby_val)&&lobby_val.reference.serial){
                SilphReadValue p_val={0};
                if(!silph_read_call(s->read,lobby_val.reference,"get_PlayerCount",SILPH_READ_I32,NULL,0,&p_val)&&p_val.bits)*players=(uint32_t)p_val.bits;
                if(!*players){
                    SilphReadValue rf={0};
                    if(!silph_read_call(s->read,lobby_val.reference,"get_Player",SILPH_READ_REFERENCE,NULL,0,&rf)&&rf.reference.serial){
                        SilphReadValue cnt={0};
                        if(!silph_read_call(s->read,rf.reference,"get_Count",SILPH_READ_I32,NULL,0,&cnt))*players=(uint32_t)cnt.bits;
                        release(s,&rf.reference);
                    }
                }
                release(s,&lobby_val.reference);
            }
        }
        int freed=release(s,&raid.reference);if(!status)status=freed;
    }
    if(probe.serial&&probe.serial!=raid.reference.serial)release(s,&probe);
    return status;
}
int silph_game_read_raid_gym(void *memory,double maximum,uint32_t tier_mask,uint32_t prefer_players,const SilphGameStop *anchor,SilphGameGym *output){
    GameRead *s=memory;
    if(output&&output->map_place.serial&&s&&s->opened)gym_release_map_place(s,output);
    if(!s||!s->opened||!output||!isfinite(maximum)||maximum<=0||maximum>5000){
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid gym scan reject opened=%u max=%.0f",(unsigned)(s?s->opened:0),maximum);
        return SILPH_MANAGED_ARGUMENT;
    }
    memset(output,0,sizeof(*output));output->meters=INFINITY;
    double lat=0,lon=0;int status=player_location(s,&lat,&lon);
    if((status==SILPH_MANAGED_NOT_READY||status==SILPH_MANAGED_CONTRACT||status==SILPH_MANAGED_ARGUMENT)&&anchor){
        lat=anchor->latitude;lon=anchor->longitude;status=0;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan using anchor lat=%.6f lon=%.6f",lat,lon);
    }
    int dir_err=0;
    if(!status){
        SilphReadValue active={0};
        if(!silph_read_call(s->read,s->directory,"get_ActiveGyms",SILPH_READ_REFERENCE,NULL,0,&active)&&active.reference.serial){
            unsigned before=output->scanned;
            (void)gym_scan_dict_or_list(s,active.reference,lat,lon,anchor,maximum,tier_mask,prefer_players,output,true,false);
            release(s,&active.reference);
            if(output->fort[0]&&output->raid_live){
                __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan picked via get_ActiveGyms early matches=%u",output->matches);
                goto gym_scan_finish;
            }
            if(before!=output->scanned)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym active early scan=%u matches=%u",output->scanned-before,output->matches);
        }else if(active.reference.serial)release(s,&active.reference);
        dir_err=gym_scan_directory(s,lat,lon,anchor,maximum,tier_mask,prefer_players,output);
        if(output->fort[0]&&output->raid_live)goto gym_scan_finish;
        if(output->matches<80)(void)gym_scan_emdp(s,lat,lon,anchor,maximum,tier_mask,prefer_players,output);
        if(output->fort[0]&&output->raid_live)goto gym_scan_finish;
        (void)gym_scan_emds(s,lat,lon,anchor,maximum,tier_mask,prefer_players,output);
        if(!output->fort[0]||!output->raid_live){
            SilphReadValue emdg={0};
            if(!gym_dictionary_emdg(s,&emdg)&&emdg.reference.serial){
                SilphReadValue dict={0};dict.reference=emdg.reference;
                (void)gym_scan_enumerator(s,&dict,lat,lon,anchor,maximum,tier_mask,prefer_players,output);
                release(s,&emdg.reference);
            }
        }
    }else dir_err=status;
    status=0;
    if(!output->fort[0]){SilphReadValue dictionary={0};
        if(!gym_dictionary_emdg(s,&dictionary)){
            status=gym_scan_enumerator(s,&dictionary,lat,lon,anchor,maximum,tier_mask,prefer_players,output);
            if(!output->fort[0]){int k=gym_scan_dict_keys(s,dictionary.reference,lat,lon,anchor,maximum,tier_mask,prefer_players,output,false,false);if(!status)status=k;}
            __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan emdg scanned=%u matches=%u chosen=%s",output->scanned,output->matches,output->fort[0]?output->fort:"");
        }else if(!gym_dictionary_emdp(s,&dictionary)){
            status=gym_scan_enumerator(s,&dictionary,lat,lon,anchor,maximum,tier_mask,prefer_players,output);
            if(!output->fort[0]){int k=gym_scan_dict_keys(s,dictionary.reference,lat,lon,anchor,maximum,tier_mask,prefer_players,output,false,false);if(!status)status=k;}
        }
        else if(dir_err&&!output->scanned)status=dir_err;}
    if(!output->fort[0]){int emds=gym_scan_emds(s,lat,lon,anchor,maximum,tier_mask,prefer_players,output);if(!status&&emds)status=emds;}
    if(!output->fort[0]){int par=gym_scan_nearest_par(s,lat,lon,anchor,maximum,tier_mask,output);if(!status&&par)status=par;}
gym_scan_finish:
    if(status==SILPH_MANAGED_CONTRACT)status=SILPH_MANAGED_NOT_READY;
    if((status==SILPH_MANAGED_REFERENCE||status==SILPH_MANAGED_ARGUMENT)&&output->scanned)status=0;
    if(status&&!output->fort[0]){
        unsigned scanned=output->scanned,matches=output->matches;
        if(!scanned&&(status==SILPH_MANAGED_NOT_READY||status==SILPH_MANAGED_CONTRACT))return status;
        memset(output,0,sizeof(*output));output->meters=INFINITY;output->scanned=scanned;output->matches=matches;
        if(status==SILPH_MANAGED_ARGUMENT)status=SILPH_MANAGED_NOT_READY;
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan empty err=%d within %.0fm scanned=%u matches=%u lat=%.6f lon=%.6f",status,maximum,scanned,matches,lat,lon);
        return status;}
    if(status&&output->fort[0])status=0;
    if(!output->fort[0])__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym scan no joinable tier_mask=%u within %.0fm scanned=%u matches=%u at lat=%.6f lon=%.6f",(unsigned)tier_mask,maximum,output->scanned,output->matches,lat,lon);
    return output->fort[0]?0:SILPH_MANAGED_NOT_READY;
}
int silph_game_read_gym_is_raid_egg(void *memory,const char *fort){
    GameRead *s=memory;if(!s||!s->opened||!fort||!fort[0])return 0;
    SilphManagedRef gym={0};int status=silph_game_read_gym_map(memory,fort,&gym);
    if(status){return 0;}
    bool unhatched=gym_raid_unhatched_egg(s,gym);
    int egg=gym_raid_is_egg(s,gym,(SilphManagedRef){0});
    release(s,&gym);
    return (egg||unhatched)?1:0;
}
int silph_game_read_raid_gym_joinable(void *memory,const char *fort,SilphGameGym *output){
    GameRead *s=memory;if(!s||!s->opened||!fort||!fort[0]||!output)return SILPH_MANAGED_ARGUMENT;
    memset(output,0,sizeof(*output));output->meters=INFINITY;
    SilphManagedRef gym={0};int status=silph_game_read_gym_map(memory,fort,&gym);
    if(status)return status;
    double lat=0,lon=0;status=location(s,gym,&lat,&lon);
    uint32_t level=0,players=0;bool active=false;uint64_t seed=0;
    if(!status)status=gym_raid_active(s,gym,&level,&players,&active,&seed,NULL);
    char id[161]={0};
    if(!status){
        int tr=gym_fort_id_text(s,gym,id,sizeof(id));
        if(tr||!id[0])(void)read_text(s,gym,"get_Id",id,sizeof(id));
    }
    int egg=gym_raid_is_egg(s,gym,(SilphManagedRef){0});
    bool hatched=gym_raid_hatched_boss(s,gym);
    bool live_map=gym_raid_live_map(s,gym);
    if(!seed&&!status){
        uint32_t lv=level?(uint32_t)level:1;
        (void)gym_fill_raid_seed(s,gym,&lv,&seed);
        if(lv&&!level)level=lv;
    }
    gym_normalize_star_level(s,gym,&level);
    bool unhatched=gym_raid_unhatched_egg(s,gym);
    int freed=release(s,&gym);if(!status)status=freed;
    if(status)return status;
    if(egg||unhatched)return SILPH_MANAGED_CONTRACT;
    if(!level&&(seed||hatched||active||live_map))level=1;
    if((seed||hatched||live_map||active)&&level>=1&&level<=7){
        /* joinable raid */
    }else{
        return SILPH_MANAGED_CONTRACT;
    }
    output->latitude=lat;output->longitude=lon;output->raid_level=level;output->players=players;
    output->lobby_players=players;output->raid_seed=seed;output->raid_live=(hatched||live_map||active)?1:0;
    if(id[0])memcpy(output->fort,id,sizeof(output->fort));
    else{size_t n=strlen(fort);if(n>=sizeof(output->fort))n=sizeof(output->fort)-1;memcpy(output->fort,fort,n);}
    return 0;
}
int silph_game_read_distance(void *memory,SilphManagedRef object,double *meters) {
    GameRead *s=memory;if(!s||!s->opened||!meters)return SILPH_MANAGED_ARGUMENT;*meters=INFINITY;
    double a=0,b=0,c=0,d=0;int status=player_location(s,&a,&b);
    if(!status)status=location(s,object,&c,&d);if(!status)*meters=distance_meters(a,b,c,d);return status;
}
int silph_game_read_select(void *memory,double maximum,SilphPokemonPredicate accept,void *context,SilphGameTarget *output) {
    GameRead *s=memory;if(!s||!s->opened||!output||!isfinite(maximum)||maximum<=0||maximum>50)return SILPH_MANAGED_ARGUMENT;
    memset(output,0,sizeof(*output));output->meters=INFINITY;
    double lat=0,lon=0;int status=player_location(s,&lat,&lon);
    SilphReadValue dictionary={0},collection={0},iterator={0},current={0},value={0};
    if(!status)status=silph_read_field(s->read,s->directory,"emds",SILPH_READ_REFERENCE,&dictionary);
    if(!status)status=silph_read_call(s->read,dictionary.reference,"get_Values",SILPH_READ_REFERENCE,NULL,0,&collection);
    if(!status)status=silph_read_call(s->read,collection.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
    for(unsigned i=0;!status&&i<128;i++) {
        status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&value);if(status||!value.bits)break;
        status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
        uint32_t incense=0;
        if(!status&&silph_read_is_type(s->read,current.reference,"Niantic.Holoholo.Map","WildMapPokemon")) {
            if(silph_read_is_type(s->read,current.reference,"Niantic.Holoholo.Map","IncenseMapPokemon")){status=release(s,&current.reference);continue;}incense=1;
        }
        if(!status)status=silph_read_call(s->read,current.reference,"get_Destroyed",SILPH_READ_BOOL,NULL,0,&value);
        if(!status&&value.bits){status=release(s,&current.reference);continue;}
        double a=0,b=0;if(!status)status=location(s,current.reference,&a,&b);double meters=distance_meters(lat,lon,a,b);
        if(!status&&isfinite(meters)&&meters<=maximum&&meters<output->meters) {
            status=silph_read_call(s->read,current.reference,"get_EncounterId",SILPH_READ_U64,NULL,0,&value);uint64_t id=value.bits;uint32_t species=0;
            if(!status)status=scalar(s,current.reference,"get_PokemonId",&species);
            if(!status&&(!id||!species))status=SILPH_MANAGED_CONTRACT;
            #ifdef SILPH_PRESENT_SCOPED
            if(!status&&accept&&!accept(context,id,species,current.reference))
#else
            if(!status&&accept&&!accept(context,id,species))
#endif
            {status=release(s,&current.reference);continue;}
            if(!status)status=release(s,&output->object);
            if(!status){output->object=current.reference;memset(&current.reference,0,sizeof(current.reference));output->encounter=id;output->species=species;output->spawn_kind=incense;output->meters=meters;}
        }
        int freed=release(s,&current.reference);if(!status)status=freed;
    }
    int disposed=0;if(iterator.reference.serial)disposed=silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&value);
    int a=release(s,&current.reference),b=release(s,&iterator.reference),c=release(s,&collection.reference),d=release(s,&dictionary.reference);
    if(!status)status=disposed?disposed:a?a:b?b:c?c:d;
    if(status){release(s,&output->object);memset(output,0,sizeof(*output));return status;}
    return output->object.serial?0:SILPH_MANAGED_NOT_READY;
}
#ifdef SILPH_PRESENT_SCOPED
int silph_game_read_pokemon_form(void *memory,SilphManagedRef pokemon,bool map,uint32_t *out){
    GameRead *s=memory;if(!s||!s->opened||!out)return SILPH_MANAGED_ARGUMENT;*out=0;SilphReadValue display={0},form={0};
    int r=map?silph_read_call(s->read,pokemon,"get_PokemonDisplay",SILPH_READ_REFERENCE,NULL,0,&display):silph_read_field(s->read,pokemon,"pokemonDisplay_",SILPH_READ_REFERENCE,&display);
    if(!r&&!display.reference.serial)r=SILPH_MANAGED_NOT_READY;
    if(!r)r=silph_read_is_type(s->read,display.reference,"Holoholo.Rpc","PokemonDisplayProto");
    if(!r)r=silph_read_field(s->read,display.reference,"Form",SILPH_READ_ENUM32,&form);
    if(!r)*out=(uint32_t)form.bits;int d=release(s,&display.reference);return r?r:d;
}
int silph_game_read_buddy_service(void *memory,uint32_t kind,SilphManagedRef *out);
#endif
int silph_game_read_nearest(void *memory,double maximum,SilphGameTarget *output){return silph_game_read_select(memory,maximum,NULL,NULL,output);}
int silph_game_read_owned(void *memory,uint64_t id,SilphManagedRef *output){
    GameRead *s=memory;if(!s||!s->opened||!id||!output)return SILPH_MANAGED_ARGUMENT;memset(output,0,sizeof(*output));
    SilphReadArgument arg={.kind=SILPH_READ_U64,.value.bits=id};SilphReadValue value;
    int status=silph_read_call(s->read,s->pokemon_bag,"GetPokemon",SILPH_READ_REFERENCE,&arg,1,&value);
    if(!status)*output=value.reference;return status;
}
static int raid_team_from_mon(GameRead *s,SilphManagedRef mon,uint64_t *ids,unsigned max,unsigned *found){
    SilphReadValue idv={0};int r=silph_read_field(s->read,mon,"id_",SILPH_READ_U64,&idv);
    if(r==SILPH_MANAGED_CONTRACT)r=silph_read_field(s->read,mon,"Id",SILPH_READ_U64,&idv);
    if(r==SILPH_MANAGED_CONTRACT)r=silph_read_call(s->read,mon,"get_Id",SILPH_READ_U64,NULL,0,&idv);
    if(!r&&idv.bits&&*found<max){ids[*found]=idv.bits;(*found)++;}
    return r;
}
static int raid_mon_viable(GameRead *s,SilphManagedRef mon,uint64_t *id_out,uint32_t *cp_out){
    if(!id_out||!cp_out)return SILPH_MANAGED_ARGUMENT;*id_out=0;*cp_out=0;
    SilphReadValue egg={0},hp={0},maxhp={0},idv={0},cp={0};
    int r=silph_read_field(s->read,mon,"isEgg_",SILPH_READ_BOOL,&egg);
    if(r==SILPH_MANAGED_CONTRACT)r=silph_read_call(s->read,mon,"get_IsEgg",SILPH_READ_BOOL,NULL,0,&egg);
    if(!r&&egg.bits)return 0;
    r=silph_read_field(s->read,mon,"id_",SILPH_READ_U64,&idv);
    if(r==SILPH_MANAGED_CONTRACT)r=silph_read_call(s->read,mon,"get_Id",SILPH_READ_U64,NULL,0,&idv);
    if(!r&&!idv.bits)return 0;
    r=silph_read_field(s->read,mon,"stamina_",SILPH_READ_I32,&hp);
    if(r==SILPH_MANAGED_CONTRACT)r=silph_read_field(s->read,mon,"maxStamina_",SILPH_READ_I32,&maxhp);
    if(!r&&(!hp.bits||!maxhp.bits))return 0;
    r=silph_read_field(s->read,mon,"cp_",SILPH_READ_I32,&cp);
    if(r==SILPH_MANAGED_CONTRACT)r=silph_read_call(s->read,mon,"get_Cp",SILPH_READ_I32,NULL,0,&cp);
    if(r==SILPH_MANAGED_CONTRACT){r=0;cp.bits=(int32_t)(hp.bits+maxhp.bits);}
    if(!r){*id_out=idv.bits;*cp_out=(uint32_t)cp.bits;return 0;}
    return r;
}
static void raid_team_insert_top(uint64_t *ids,uint32_t *cps,unsigned *found,unsigned max,uint64_t id,uint32_t cp){
    if(!id||!cp||!found||*found>=max)return;
    unsigned slot=*found;
    for(unsigned i=0;i<*found;i++)if(ids[i]==id)return;
    for(unsigned i=0;i<*found;i++)if(cp>cps[i]){slot=i;break;}
    if(slot>=max)return;
    if(*found<max)(*found)++;
    for(unsigned i=*found;i>slot+1;i--){ids[i-1]=ids[i-2];cps[i-1]=cps[i-2];}
    ids[slot]=id;cps[slot]=cp;
}
static int raid_team_from_player_party(GameRead *s,uint64_t *ids,unsigned max,unsigned *found){
    SilphReadValue party={0};int status=silph_read_call(s->read,s->player,"get_RaidBattleTeam",SILPH_READ_REFERENCE,NULL,0,&party);
    if(status==SILPH_MANAGED_CONTRACT)status=silph_read_field(s->read,s->player,"raidBattleTeam_",SILPH_READ_REFERENCE,&party);
    if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,s->player,"GetRaidBattleTeam",SILPH_READ_REFERENCE,NULL,0,&party);
    if(status==SILPH_MANAGED_ARGUMENT)status=SILPH_MANAGED_NOT_READY;
    if(status||!party.reference.serial){int freed=release(s,&party.reference);return status?status:(freed?freed:SILPH_MANAGED_NOT_READY);}
    for(unsigned i=0;!status&&*found<max&&i<3;i++){
        SilphReadArgument arg={.kind=SILPH_READ_I32,.value.bits=i};SilphReadValue mon={0};
        status=silph_read_call(s->read,party.reference,"get_Item",SILPH_READ_REFERENCE,&arg,1,&mon);
        if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,party.reference,"GetPokemon",SILPH_READ_REFERENCE,&arg,1,&mon);
        if(status==SILPH_MANAGED_CONTRACT){status=0;continue;}
        if(status)break;
        status=raid_team_from_mon(s,mon.reference,ids,max,found);
        int freed=release(s,&mon.reference);if(!status)status=freed;
    }
    int freed=release(s,&party.reference);return *found?0:(status?status:freed);
}
static int raid_team_from_bag_dict(GameRead *s,uint64_t *ids,unsigned max,unsigned *found){
    static const char *dict_names[]={"cwxy","cwyb","pokemonById_","pokemons_","_pokemonMap",NULL};
    uint32_t cps[3]={0};unsigned before=*found;
    SilphManagedRef bag_impl={0};void *bag_object=NULL;
    int bag_r=silph_managed_target(s->managed,s->pokemon_bag,&bag_object);
    if(!bag_r)bag_r=silph_managed_retain(s->managed,bag_object,false,&bag_impl);
    SilphManagedRef bag_ref=bag_r? s->pokemon_bag:bag_impl;
    for(unsigned d=0;dict_names[d];d++){
        SilphReadValue dict={0};int r=silph_read_field(s->read,bag_ref,dict_names[d],SILPH_READ_REFERENCE,&dict);
        if(r==SILPH_MANAGED_CONTRACT)r=silph_read_call(s->read,bag_ref,dict_names[d],SILPH_READ_REFERENCE,NULL,0,&dict);
        if(r||!dict.reference.serial){release(s,&dict.reference);continue;}
        SilphReadValue collection={0},iterator={0},current={0},more={0};
        r=silph_read_call(s->read,dict.reference,"get_Values",SILPH_READ_REFERENCE,NULL,0,&collection);
        if(!r&&collection.reference.serial)r=silph_read_call(s->read,collection.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
        for(unsigned i=0;!r&&i<512;i++){
            r=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&more);
            if(r||!more.bits)break;
            r=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
            if(!r){uint64_t id=0;uint32_t cp=0;int ok=raid_mon_viable(s,current.reference,&id,&cp);if(!ok&&id)raid_team_insert_top(ids,cps,found,max,id,cp);}
            int freed=release(s,&current.reference);if(!r)r=freed;
        }
        if(iterator.reference.serial)silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&more);
        SilphManagedRef *refs[]={&current.reference,&iterator.reference,&collection.reference,&dict.reference};
        for(unsigned i=0;i<4;i++){int freed=release(s,refs[i]);if(!r)r=freed;}
        if(*found>=max||*found>before)break;
    }
    release(s,&bag_impl);
    return *found>before?0:SILPH_MANAGED_NOT_READY;
}
int silph_game_read_heal_list(void *memory,uint64_t *ids,int32_t *hp,int32_t *max_hp,unsigned max,unsigned *found){
    GameRead *s=memory;if(!s||!s->opened||!ids||!hp||!max_hp||!max||max>12||!found)return SILPH_MANAGED_ARGUMENT;*found=0;
    static const char *dict_names[]={"cwxy","cwyb","pokemonById_","pokemons_","_pokemonMap",NULL};
    uint32_t cps[12]={0};unsigned seen=0;
    SilphManagedRef bag_impl={0};void *bag_object=NULL;
    int bag_r=silph_managed_target(s->managed,s->pokemon_bag,&bag_object);
    if(!bag_r)bag_r=silph_managed_retain(s->managed,bag_object,false,&bag_impl);
    SilphManagedRef bag_ref=bag_r?s->pokemon_bag:bag_impl;
    for(int d=-1;(d<0||dict_names[d])&&!seen;d++){
        SilphReadValue dict={0},collection={0},iterator={0},current={0},more={0};int r=0;
        if(d<0){r=silph_read_call(s->read,bag_ref,"get_AllPokemon",SILPH_READ_REFERENCE,NULL,0,&collection);
            if(!r&&!collection.reference.serial)r=SILPH_MANAGED_NOT_READY;
            if(r){__android_log_print(ANDROID_LOG_WARN,"SilphRaid","heal list get_AllPokemon r=%d",r);release(s,&collection.reference);continue;}}
        else{
        r=silph_read_field(s->read,bag_ref,dict_names[d],SILPH_READ_REFERENCE,&dict);
        if(r==SILPH_MANAGED_CONTRACT)r=silph_read_call(s->read,bag_ref,dict_names[d],SILPH_READ_REFERENCE,NULL,0,&dict);
        if(r||!dict.reference.serial){release(s,&dict.reference);continue;}
        r=silph_read_call(s->read,dict.reference,"get_Values",SILPH_READ_REFERENCE,NULL,0,&collection);}
        if(!r&&collection.reference.serial)r=silph_read_call(s->read,collection.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
        if(r==SILPH_MANAGED_CONTRACT&&collection.reference.serial)r=silph_read_call(s->read,collection.reference,"GetEnumerator",SILPH_READ_REFERENCE,NULL,0,&iterator);
        if(d<0&&r){__android_log_print(ANDROID_LOG_WARN,"SilphRaid","heal list enumerator r=%d",r);silph_dump_methods(s->read,collection.reference,"AllPokemon");}
        for(unsigned i=0;!r&&i<3000;i++){
            r=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&more);
            if(d<0&&i==0&&(r||!more.bits))__android_log_print(ANDROID_LOG_WARN,"SilphRaid","heal list first MoveNext r=%d more=%llu",r,(unsigned long long)more.bits);
            if(r||!more.bits)break;
            r=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
            if(r==SILPH_MANAGED_CONTRACT)r=silph_read_field(s->read,iterator.reference,"<>2__current",SILPH_READ_REFERENCE,&current);
            if(d<0&&i==0&&r)__android_log_print(ANDROID_LOG_WARN,"SilphRaid","heal list current r=%d",r);
            if(!r){
                SilphReadValue egg={0},idv={0},sv={0},mv={0},cv={0};SilphManagedRef mon=current.reference;
                int e=silph_read_field(s->read,mon,"isEgg_",SILPH_READ_BOOL,&egg);
                if(e==SILPH_MANAGED_CONTRACT)e=silph_read_call(s->read,mon,"get_IsEgg",SILPH_READ_BOOL,NULL,0,&egg);
                if(e==SILPH_MANAGED_CONTRACT){e=0;egg.bits=0;}
                int step=1;
                if(!e){step=2;e=silph_read_field(s->read,mon,"id_",SILPH_READ_U64,&idv);}
                if(!e){step=3;e=silph_read_field(s->read,mon,"stamina_",SILPH_READ_I32,&sv);}
                if(!e){step=4;e=silph_read_field(s->read,mon,"maxStamina_",SILPH_READ_I32,&mv);}
                if(!e){step=5;e=silph_read_field(s->read,mon,"cp_",SILPH_READ_I32,&cv);if(e==SILPH_MANAGED_CONTRACT)e=silph_read_call(s->read,mon,"get_Cp",SILPH_READ_I32,NULL,0,&cv);}
                if(e&&!seen&&i<2){__android_log_print(ANDROID_LOG_WARN,"SilphRaid","heal list read failed step=%d err=%d",step,e);if(i==0)silph_dump_methods(s->read,mon,"HealMon");}
                if(!e){seen++;uint32_t cp=(uint32_t)cv.bits;int32_t h=(int32_t)sv.bits,m=(int32_t)mv.bits;
                    if(!egg.bits&&idv.bits&&m>0&&cp){
                        unsigned slot=*found;for(unsigned k=0;k<*found;k++)if(cp>cps[k]){slot=k;break;}
                        if(slot<max){if(*found<max)(*found)++;
                            for(unsigned k=*found-1;k>slot;k--){ids[k]=ids[k-1];cps[k]=cps[k-1];hp[k]=hp[k-1];max_hp[k]=max_hp[k-1];}
                            ids[slot]=idv.bits;cps[slot]=cp;hp[slot]=h;max_hp[slot]=m;}
                    }}
            }
            int freed=release(s,&current.reference);if(!r)r=freed;
        }
        if(iterator.reference.serial)silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&more);
        SilphManagedRef *refs[]={&current.reference,&iterator.reference,&collection.reference,&dict.reference};
        for(unsigned i=0;i<4;i++){int freed=release(s,refs[i]);if(!r)r=freed;}
    }
    release(s,&bag_impl);
    if(!*found)__android_log_print(ANDROID_LOG_WARN,"SilphRaid","heal list empty seen=%u",seen);
    return *found?0:SILPH_MANAGED_NOT_READY;
}
int silph_game_read_faction_team(void *memory,uint32_t *team){
    GameRead *s=memory;if(!s||!s->opened||!team)return SILPH_MANAGED_ARGUMENT;*team=0;
    SilphReadValue value={0};static const char *getters[]={"get_Team","GetTeam","get_TeamValue",NULL};
    int status=SILPH_MANAGED_CONTRACT;
    for(unsigned i=0;getters[i];i++){
        int r=silph_read_call(s->read,s->player,getters[i],SILPH_READ_ENUM32,NULL,0,&value);
        if(!r){status=0;break;}
        if(r!=SILPH_MANAGED_CONTRACT)status=r;
    }
    if(status==SILPH_MANAGED_CONTRACT){
        static const char *fields[]={"team_","Team","playerTeam_","teamValue_",NULL};
        for(unsigned i=0;fields[i];i++){
            int r=silph_read_field(s->read,s->player,fields[i],SILPH_READ_ENUM32,&value);
            if(!r){status=0;break;}
            if(r!=SILPH_MANAGED_CONTRACT)status=r;
        }
    }
    if(status==SILPH_MANAGED_ARGUMENT)status=SILPH_MANAGED_NOT_READY;
    if(status)return status;
    uint32_t t=(uint32_t)value.bits;
    if(t>=1&&t<=3)*team=t;
    return 0;
}
int silph_game_read_raid_team(void *memory,uint64_t *ids,unsigned max,unsigned *found){
    GameRead *s=memory;if(!s||!s->opened||!ids||!max||!found)return SILPH_MANAGED_ARGUMENT;*found=0;
    int party=raid_team_from_player_party(s,ids,max,found);
    if(!party&&*found)return 0;
    int dict=raid_team_from_bag_dict(s,ids,max,found);
    if(!dict&&*found)return 0;
    static const char *bag_lists[]={"get_AllPokemon","get_PokemonList","get_All","GetAllPokemon","get_Inventory","get_Pokemons","GetPokemons",NULL};
    SilphReadValue all={0};int status=SILPH_MANAGED_CONTRACT;
    for(unsigned m=0;bag_lists[m]&&!all.reference.serial;m++){
        int r=silph_read_call(s->read,s->pokemon_bag,bag_lists[m],SILPH_READ_REFERENCE,NULL,0,&all);
        if(r==SILPH_MANAGED_CONTRACT)r=silph_read_field(s->read,s->pokemon_bag,bag_lists[m],SILPH_READ_REFERENCE,&all);
        if(!r&&all.reference.serial)status=0;
    }
    if(!status&&all.reference.serial){
        SilphReadValue iterator={0},current={0},more={0};
        status=silph_read_call(s->read,all.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
        for(unsigned i=0;!status&&*found<max&&i<256;i++){
            status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&more);
            if(status||!more.bits)break;
            status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
            if(!status)status=raid_team_from_mon(s,current.reference,ids,max,found);
            int freed=release(s,&current.reference);if(!status)status=freed;
        }
        SilphManagedRef *refs[]={&current.reference,&iterator.reference,&all.reference};
        for(unsigned i=0;i<3;i++){int freed=release(s,refs[i]);if(!status)status=freed;}
        if(*found)return 0;
    }
    int freed=release(s,&all.reference);if(status&&status!=SILPH_MANAGED_CONTRACT)return status?status:freed;
    uint32_t count=0;status=scalar(s,s->pokemon_bag,"get_Count",&count);
    if(status==SILPH_MANAGED_CONTRACT){count=6;status=0;}
    if(status)return status;
    uint32_t limit=count>max?max:count;if(limit>12)limit=12;
    for(uint32_t i=0;!status&&*found<max&&i<limit;i++){
        SilphReadArgument arg={.kind=SILPH_READ_I32,.value.bits=i};SilphReadValue mon={0};
        status=silph_read_call(s->read,s->pokemon_bag,"GetPokemonAt",SILPH_READ_REFERENCE,&arg,1,&mon);
        if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,s->pokemon_bag,"get_Item",SILPH_READ_REFERENCE,&arg,1,&mon);
        if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,s->pokemon_bag,"GetPokemon",SILPH_READ_REFERENCE,&arg,1,&mon);
        if(status==SILPH_MANAGED_CONTRACT){status=0;continue;}
        if(status)break;
        status=raid_team_from_mon(s,mon.reference,ids,max,found);
        freed=release(s,&mon.reference);if(!status)status=freed;
    }
    if(*found)__android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid team loaded count=%u first=%llu",(unsigned)*found,(unsigned long long)ids[0]);
    else if(status!=SILPH_MANAGED_NOT_READY&&status!=SILPH_MANAGED_ARGUMENT)
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","raid team load pending err=%d",status);
    return *found?0:SILPH_MANAGED_NOT_READY;
}
static int service(GameRead *s,const char *assembly,const char *space,const char *name,SilphManagedRef *output){
    SilphManagedRef type={0};SilphReadValue value={0};int status=silph_read_type_object(s->read,assembly,space,name,&type);
    SilphReadArgument arg={.kind=SILPH_READ_REFERENCE,.value.reference=type};
    if(!status)status=silph_read_call(s->read,s->container,"TryResolve",SILPH_READ_REFERENCE,&arg,1,&value);
    if(!status&&!value.reference.serial)status=SILPH_MANAGED_CONTRACT;
    if(!status){*output=value.reference;memset(&value.reference,0,sizeof(value.reference));}
    int a=release(s,&type),b=release(s,&value.reference);return status?status:a?a:b;
}
#ifdef SILPH_PILOT_EVENT
int silph_game_read_event_targets(void *memory,SilphManagedRef *behavior,SilphManagedRef *directory){
    GameRead *s=memory;if(!s||!s->opened||!behavior||!directory)return SILPH_MANAGED_ARGUMENT;*behavior=*directory=(SilphManagedRef){0};
    int r=service(s,"holo-game.dll","Niantic.Holoholo.Services.DynamicTappables","IDynamicTappableBehaviorService",behavior);
    void *object=NULL;if(!r)r=silph_managed_target(s->managed,s->directory,&object);if(!r)r=silph_managed_retain(s->managed,object,false,directory);return r;
}
#endif
#ifdef SILPH_PRESENT_SCOPED
int silph_game_read_event_service(void *memory,SilphManagedRef *output){
    GameRead *s=memory;if(!s||!s->opened||!output)return SILPH_MANAGED_ARGUMENT;*output=(SilphManagedRef){0};
    return service(s,"holo-game.dll","Niantic.Holoholo.Services.DynamicTappables","IDynamicTappablesService",output);
}
#endif
int silph_game_read_rpc_handler(void *memory,SilphManagedRef *output){
    GameRead *s=memory;if(!s||!s->opened||!output)return SILPH_MANAGED_ARGUMENT;memset(output,0,sizeof(*output));
    return service(s,"holo-game.dll","Niantic.Holoholo.Rpc","IRpcHandler",output);
}
#ifdef SILPH_PRESENT_SCOPED
int silph_game_read_buddy_service(void *memory,uint32_t kind,SilphManagedRef *out){
    GameRead *s=memory;if(!s||!s->opened||!out||kind>4)return SILPH_MANAGED_ARGUMENT;*out=(SilphManagedRef){0};
    if(kind==4){void *object=NULL;int r=silph_managed_target(s->managed,s->pokemon_bag,&object);return r?r:silph_managed_retain(s->managed,object,false,out);}
    const char *spaces[]={"Niantic.Holoholo.Buddy","Niantic.Holoholo.Buddy","Niantic.Holoholo.Buddy","Niantic.Holoholo.Services"};
    const char *names[]={"IBuddyPokemonService","IBuddyRpcService","IBuddyStatsService","IMegaEvoCostService"};
    return service(s,kind==2||kind==3?"holo-game-interfaces.dll":"holo-game.dll",spaces[kind],names[kind],out);
}
#endif
int silph_game_read_service(void *memory,uint32_t kind,SilphManagedRef *output){
    GameRead *s=memory;if(!s||!s->opened||!output||kind>1)return SILPH_MANAGED_ARGUMENT;memset(output,0,sizeof(*output));
    return kind?service(s,"holo-game.dll","Niantic.Holoholo.Buddy","IBuddyPokemonService",output):service(s,"holo-protos.dll","","IGameMasterData",output);
}
int silph_game_read_rocket_service(void *memory,uint32_t kind,SilphManagedRef *out){
    GameRead *s=memory;if(!s||!s->opened||!out||kind>
#ifdef SILPH_PILOT_ROCKET
        8
#else
        7
#endif
    )return SILPH_MANAGED_ARGUMENT;
    memset(out,0,sizeof(*out));
#ifdef SILPH_PILOT_ROCKET
    if(kind==8)return service(s,"holo-game.dll","Niantic.Holoholo.Combat.Util","ICombatDirectorProperties",out);
#endif
    if(kind==7)return silph_read_field_type(s->read,s->map,"Niantic.Holoholo","IPokemonBag",out);
    if(kind==5)return silph_read_field_type(s->read,s->map,"Niantic.Holoholo.Map","MapContentHandler",out);
    if(kind==6)return service(s,"holo-game.dll","Niantic.Holoholo","ILocationProvider",out);
    const char *spaces[]={"Niantic.Holoholo.Incident","Niantic.Holoholo.NUI","Niantic.Holoholo.Encounter","Niantic.Holoholo.UI","Niantic.Holoholo.Incident"};
    const char *names[]={"IIncidentService","IGuiLayerService","IEncounterState","IItemFragmentAssemblyService","IIncidentRpcService"};
    if(kind==1){
        int r=service(s,"holo-game.dll",spaces[1],names[1],out);
        if((r||!out->serial)&&s->map.serial){
            SilphReadValue v={0};
            if(!silph_read_field(s->read,s->map,"eltg",SILPH_READ_REFERENCE,&v)&&v.reference.serial){*out=v.reference;r=0;}
            else r=silph_read_field_type(s->read,s->map,"Niantic.Holoholo.NUI","IGuiLayerService",out);
        }
        return r;
    }
    return service(s,kind==3?"holo-game-interfaces.dll":"holo-game.dll",spaces[kind],names[kind],out);
}
void silph_game_read_raid_rpc_diag(void *memory){
    GameRead *s=memory;if(!s||!s->opened)return;
    static int dumped;if(dumped)return;
    const char *asms[]={"holo-game-interfaces.dll","holo-game.dll","holo-game-interfaces.dll","holo-game.dll"};
    const char *spaces[]={"Niantic.Holoholo.Raid","Niantic.Holoholo.Raid","Niantic.Holoholo.Gym","Niantic.Holoholo.Gym"};
    const char *names[]={"IRaidRpcService","IRaidRpcService","IGymRootController","IGymRootController"};
    for(unsigned i=0;i<4;i++){
        SilphManagedRef svc={0};
        int r=service(s,asms[i],spaces[i],names[i],&svc);
        if(!r&&svc.serial){
            dumped=1;
            silph_dump_methods(s->read,svc,names[i]);
            release(s,&svc);
        }else if(svc.serial)release(s,&svc);
    }
}
void silph_game_read_gui_diag(void *memory){
    GameRead *s=memory;if(!s||!s->opened)return;
    SilphManagedRef gui={0};
    if(silph_game_read_raid_service(memory,1,&gui)||!gui.serial)return;
    SilphReadValue top={0};
    if(!silph_read_call(s->read,gui,"TopGui",SILPH_READ_REFERENCE,NULL,0,&top)&&top.reference.serial){
        char cls[128]={0};silph_raid_class(s->read,top.reference,cls,sizeof(cls));
        __android_log_print(ANDROID_LOG_INFO,"SilphRaid","TOP_GUI=%s",cls[0]?cls:"?");
        {static int dumped;if(!dumped){dumped=1;silph_dump_methods(s->read,top.reference,"TopGui");}}
        release(s,&top.reference);
    }
    release(s,&gui);
}
int silph_game_read_raid_root_boss(void *memory,uint32_t *boss){
    GameRead *s=memory;if(!s||!s->opened||!boss)return SILPH_MANAGED_ARGUMENT;
    *boss=0;
    SilphManagedRef root={0};
    int r=service(s,"holo-game.dll","Niantic.Holoholo.Gym","IGymRootController",&root);
    if(r||!root.serial)return r?r:SILPH_MANAGED_NOT_READY;
    SilphReadValue has={0};uint32_t has_ri=0;
    if(!silph_read_call(s->read,root,"get_HasRaidInfo",SILPH_READ_BOOL,NULL,0,&has))has_ri=(uint32_t)has.bits;
    SilphReadValue lvl={0};uint32_t lvlv=0;
    if(!silph_read_call(s->read,root,"get_CurrentRaidLevel",SILPH_READ_ENUM32,NULL,0,&lvl))lvlv=(uint32_t)lvl.bits;
    SilphReadValue rinfo={0};
    r=silph_read_call(s->read,root,"get_RaidInfo",SILPH_READ_REFERENCE,NULL,0,&rinfo);
    if(r==SILPH_MANAGED_CONTRACT)r=silph_read_call(s->read,root,"get_RaidInfo",SILPH_READ_BOXED,NULL,0,&rinfo);
    if(!r&&rinfo.reference.serial){
        SilphReadValue bossv={0};
        gym_raid_apply_info(s,rinfo.reference,NULL,NULL,NULL,NULL,&bossv,NULL);
        *boss=(uint32_t)bossv.bits;
        {static int dumped_ri;if(!dumped_ri&&rinfo.reference.serial){dumped_ri=1;silph_dump_methods(s->read,rinfo.reference,"GymRootRaidInfo");}}
        release(s,&rinfo.reference);
    }
    if(!*boss){
        static const char *alt[]={"caam","caan","caao",NULL};
        for(unsigned i=0;!*boss&&alt[i];i++){
            SilphReadValue av={0};
            int ar=silph_read_call(s->read,root,alt[i],SILPH_READ_REFERENCE,NULL,0,&av);
            if(ar==SILPH_MANAGED_CONTRACT)ar=silph_read_call(s->read,root,alt[i],SILPH_READ_BOXED,NULL,0,&av);
            if(!ar&&av.reference.serial){
                SilphReadValue bossv={0};
                gym_raid_apply_info(s,av.reference,NULL,NULL,NULL,NULL,&bossv,NULL);
                if(bossv.bits)*boss=(uint32_t)bossv.bits;
                {static int dumped_a;if(!dumped_a&&av.reference.serial){dumped_a=1;silph_dump_methods(s->read,av.reference,alt[i]);}}
                release(s,&av.reference);
            }
        }
    }
    __android_log_print(ANDROID_LOG_INFO,"SilphRaid","ROOT_BOSS boss=%u hasRaidInfo=%u level=%u err=%d",(unsigned)*boss,(unsigned)has_ri,(unsigned)lvlv,(int)r);
    release(s,&root);
    return r;
}
int silph_game_read_rocket_position(void *memory,double *lat,double *lon){
    GameRead *s=memory;if(!s||!s->opened||!lat||!lon)return SILPH_MANAGED_ARGUMENT;SilphManagedRef provider={0};SilphReadValue valid={0};
    int r=silph_game_read_rocket_service(s,6,&provider);if(!r)r=silph_read_call(s->read,provider,"get_HasValidLocation",SILPH_READ_BOOL,NULL,0,&valid);
    if(!r&&!valid.bits)r=SILPH_MANAGED_NOT_READY;if(!r)r=location(s,provider,lat,lon);int d=release(s,&provider);return r?r:d;
}
static int fort_dictionary(GameRead *s,const char *field,const char *fort,SilphManagedRef *out){
    if(!s||!field||!fort||!fort[0]||!out)return SILPH_MANAGED_ARGUMENT;
    SilphReadValue dictionary={0},values={0},iterator={0},current={0},v={0};
    int status=silph_read_field(s->read,s->directory,field,SILPH_READ_REFERENCE,&dictionary);
    if(status||!dictionary.reference.serial)return status?status:SILPH_MANAGED_NOT_READY;

    SilphManagedRef key_str={0};
    if(!silph_managed_ascii(s->managed,fort,strlen(fort),&key_str)&&key_str.serial){
        SilphReadArgument arg={.kind=SILPH_READ_REFERENCE,.value.reference=key_str};
        SilphReadValue val={0};
        int gr=silph_read_call(s->read,dictionary.reference,"get_Item",SILPH_READ_REFERENCE,&arg,1,&val);
        release(s,&key_str);
        if(!gr&&val.reference.serial){
            *out=val.reference;
            release(s,&dictionary.reference);
            return 0;
        }
        if(val.reference.serial)release(s,&val.reference);
    }

    if(!status)status=silph_read_call(s->read,dictionary.reference,"get_Values",SILPH_READ_REFERENCE,NULL,0,&values);
    if(!status)status=silph_read_call(s->read,values.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
    for(unsigned i=0;!status&&i<512;i++){
        status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&v);if(status||!v.bits)break;
        status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
        char id[161]={0};
        if(!status){
            if(!read_text(s,current.reference,"get_Id",id,sizeof(id))&&id[0]){}
            else gym_fort_id_text(s,current.reference,id,sizeof(id));
        }
        if(!status&&!strcmp(id,fort)){*out=current.reference;memset(&current.reference,0,sizeof(current.reference));break;}
        int freed=release(s,&current.reference);if(!status)status=freed;
    }
    int disposed=iterator.reference.serial?silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&v):0;
    SilphManagedRef *refs[]={&current.reference,&iterator.reference,&values.reference,&dictionary.reference};
    for(unsigned i=0;i<4;i++){int freed=release(s,refs[i]);if(!status)status=freed;}
    if(!status)status=disposed;return status?status:out->serial?0:SILPH_MANAGED_NOT_READY;
}
int silph_game_read_place(void *memory,const char *fort,SilphManagedRef *out){
    GameRead *s=memory;if(!s||!s->opened||!fort||!fort[0]||strnlen(fort,161)>160||!out)return SILPH_MANAGED_ARGUMENT;memset(out,0,sizeof(*out));
    init_directory_fields(s);
    int r=fort_dictionary(s,s->field_stops[0]?s->field_stops:"emdp",fort,out);
    if((r||!out->serial)&&s->field_gyms[0])r=fort_dictionary(s,s->field_gyms,fort,out);
    return r;
}
static int gym_retain_map(GameRead *s,SilphManagedRef gym,SilphManagedRef *out){
    SilphManagedRef resolved={0};int status=gym_resolve(s,gym,&resolved);
    if(status==SILPH_MANAGED_NOT_READY||status==SILPH_MANAGED_CONTRACT)return status;
    if(status)return status;
    SilphReadValue place={0};
    status=silph_read_call(s->read,resolved,"get_MapPlace",SILPH_READ_REFERENCE,NULL,0,&place);
    if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,resolved,"get_ClickTarget",SILPH_READ_REFERENCE,NULL,0,&place);
    if(!status&&place.reference.serial){*out=place.reference;memset(&place.reference,0,sizeof(place.reference));status=release(s,&resolved);return status?status:0;}
    void *object=NULL;status=silph_managed_target(s->managed,resolved,&object);
    if(!status&&object)status=silph_managed_retain(s->managed,object,false,out);
    if(!status&&!out->serial){*out=resolved;memset(&resolved,0,sizeof(resolved));}
    else{int freed=release(s,&resolved);if(!status)status=freed;}
    return status?status:out->serial?0:SILPH_MANAGED_NOT_READY;
}
static int gym_match_list(GameRead *s,SilphManagedRef list,const char *fort,SilphManagedRef *out){
    SilphReadValue count={0};uint32_t n=0;int status=silph_read_call(s->read,list,"get_Count",SILPH_READ_I32,NULL,0,&count);
    if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,list,"get_Length",SILPH_READ_I32,NULL,0,&count);
    if(status)return status;n=(uint32_t)count.bits;if(n>512)n=512;
    for(uint32_t i=0;!status&&i<n;i++){
        SilphReadArgument arg={.kind=SILPH_READ_I32,.value.bits=i};SilphReadValue item={0};
        status=silph_read_call(s->read,list,"get_Item",SILPH_READ_REFERENCE,&arg,1,&item);
        if(status==SILPH_MANAGED_CONTRACT)status=silph_read_call(s->read,list,"get_Item",SILPH_READ_BOXED,&arg,1,&item);
        if(status)break;
        SilphManagedRef gym={0};int r=gym_resolve(s,item.reference,&gym);int drop=release(s,&item.reference);if(!status)status=drop;
        if(r==SILPH_MANAGED_NOT_READY||r==SILPH_MANAGED_CONTRACT){r=0;continue;}if(r){status=r;break;}
        char id[161]={0};if(!status)status=read_text(s,gym,"get_Id",id,sizeof(id));
        if(!status&&!strcmp(id,fort)){status=gym_retain_map(s,gym,out);drop=release(s,&gym);if(!status)status=drop;return status?status:0;}
        drop=release(s,&gym);if(!status)status=drop;
    }
    return status?status:SILPH_MANAGED_NOT_READY;
}
static int gym_retain_map(GameRead *s,SilphManagedRef gym,SilphManagedRef *out);
static bool gym_fort_id_matches(const char *a,const char *b){
    if(!a||!b||!a[0]||!b[0])return false;
#if defined(_WIN32)
    return _stricmp(a,b)==0;
#else
    return strcasecmp(a,b)==0;
#endif
}
static int gym_retain_tap_target(GameRead *s,SilphManagedRef gym,SilphManagedRef *out){
    if(!gym.serial||!out)return SILPH_MANAGED_ARGUMENT;
    int status=gym_retain_map(s,gym,out);
    if(!status&&out->serial)return 0;
    if(out->serial){int freed=release(s,out);if(!status)status=freed;memset(out,0,sizeof(*out));}
    bool map=false;if(!gym_is_map_gym(s,gym,&map)&&map){
        void *object=NULL;status=silph_managed_target(s->managed,gym,&object);
        if(!status&&object)status=silph_managed_retain(s->managed,object,false,out);
        if(!status&&out->serial)return 0;
    }
    return status?status:SILPH_MANAGED_NOT_READY;
}
static int fort_dict_lookup(GameRead *s,const char *field,const char *fort,SilphManagedRef *out){
    SilphReadValue dictionary={0};int status=silph_read_field(s->read,s->directory,field,SILPH_READ_REFERENCE,&dictionary);
    if(status)return status;
    SilphManagedRef key={0};status=silph_managed_ascii(s->managed,fort,(uint32_t)strlen(fort),&key);
    if(status){int freed=release(s,&dictionary.reference);return freed?freed:status;}
    SilphReadArgument arg={.kind=SILPH_READ_REFERENCE,.value.reference=key};SilphReadValue item={0};
    status=silph_read_call(s->read,dictionary.reference,"TryGetValue",SILPH_READ_REFERENCE,&arg,1,&item);
    if(status==SILPH_MANAGED_CONTRACT||!item.reference.serial){
        if(item.reference.serial)release(s,&item.reference);
        status=silph_read_call(s->read,dictionary.reference,"get_Item",SILPH_READ_REFERENCE,&arg,1,&item);
    }
    int fk=release(s,&key),fd=release(s,&dictionary.reference);if(!status)status=fk?fk:fd;
    if(status)return status;
    if(!item.reference.serial)return SILPH_MANAGED_NOT_READY;
    SilphManagedRef gym={0};int gr=gym_resolve(s,item.reference,&gym);
    if(gr==SILPH_MANAGED_EXCEPTION||gr==SILPH_MANAGED_CONTRACT)gr=0;
    if(!gr&&gym.serial)status=gym_retain_tap_target(s,gym,out);
    else if(gr)status=gr;
    int fg=release(s,&gym);int fi=release(s,&item.reference);if(!status)status=fg?fg:fi;
    if(!status&&out->serial){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym map via %s TryGetValue fort=%s",field,fort);return 0;}
    return status?status:SILPH_MANAGED_NOT_READY;
}
static int gym_map_from_list_keys(GameRead *s,SilphManagedRef list,const char *fort,SilphManagedRef *out){
    SilphReadValue keys={0},iterator={0},current={0},value={0};
    int status=silph_read_call(s->read,list,"get_Keys",SILPH_READ_REFERENCE,NULL,0,&keys);
    if(status==SILPH_MANAGED_CONTRACT||!keys.reference.serial)return status?status:SILPH_MANAGED_NOT_READY;
    status=silph_read_call(s->read,keys.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
    for(unsigned i=0;!status&&i<512;i++){
        status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&value);
        if(status||!value.bits)break;
        status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
        char id[161]={0};uint32_t n=0;
        if(!status&&!read_text(s,current.reference,"get_Id",id,sizeof(id))&&id[0]){}
        else if(!status&&!silph_read_utf8(s->read,current.reference,id,sizeof(id),&n)&&id[0]){}
        else if(!status)read_text(s,current.reference,"ToString",id,sizeof(id));
        if(!gym_fort_id_matches(id,fort)){int drop=release(s,&current.reference);if(!status)status=drop;continue;}
        SilphManagedRef gym={0};(void)gym_lookup_from_key(s,current.reference,&gym);
        if(!gym.serial&&id[0])(void)gym_lookup_by_id(s,id,&gym);
        int drop=release(s,&current.reference);if(!status)status=drop;
        if(!gym.serial)continue;
        status=gym_retain_tap_target(s,gym,out);
        drop=release(s,&gym);if(!status)status=drop;
        if(!status&&out->serial)break;
        status=0;
    }
    if(iterator.reference.serial){int disposed=silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&value);if(!status)status=disposed;release(s,&iterator.reference);}
    if(keys.reference.serial)release(s,&keys.reference);
    return status?status:out->serial?0:SILPH_MANAGED_NOT_READY;
}
static int gym_map_from_raid_lists(GameRead *s,const char *fort,SilphManagedRef *out){
    static const char *methods[]={"get_UncompletedRaidGyms","get_ActiveGyms","get_ActiveMapPlaces","get_ActiveMapGyms",NULL};
    int status=0;
    for(unsigned m=0;methods[m]&&!out->serial;m++){
        SilphReadValue list={0};int r=silph_read_call(s->read,s->directory,methods[m],SILPH_READ_REFERENCE,NULL,0,&list);
        if(r==SILPH_MANAGED_CONTRACT||r==SILPH_MANAGED_ARGUMENT)continue;
        if(!r)r=gym_match_list(s,list.reference,fort,out);
        if(!r&&out->serial){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym map via %s fort=%s",methods[m],fort);release(s,&list.reference);return 0;}
        if(out->serial)memset(out,0,sizeof(*out));
        if(!r)r=gym_map_from_list_keys(s,list.reference,fort,out);
        int freed=release(s,&list.reference);if(!status)status=r?r:freed;
        if(!status&&out->serial){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym map via %s keys fort=%s",methods[m],fort);return 0;}
        status=0;
    }
    return status?status:SILPH_MANAGED_NOT_READY;
}
static int gym_map_from_emds(GameRead *s,const char *fort,SilphManagedRef *out){
    SilphReadValue dictionary={0},collection={0},iterator={0},current={0},value={0};
    int status=gym_dictionary_emds(s,&dictionary);if(status||!dictionary.reference.serial)return status?status:SILPH_MANAGED_NOT_READY;
    status=silph_read_call(s->read,dictionary.reference,"get_Values",SILPH_READ_REFERENCE,NULL,0,&collection);
    if(!status&&collection.reference.serial)status=silph_read_call(s->read,collection.reference,"GetEnumerator",SILPH_READ_BOXED,NULL,0,&iterator);
    for(unsigned i=0;!status&&i<512;i++){
        status=silph_read_call(s->read,iterator.reference,"MoveNext",SILPH_READ_BOOL,NULL,0,&value);
        if(status||!value.bits)break;
        status=silph_read_call(s->read,iterator.reference,"get_Current",SILPH_READ_REFERENCE,NULL,0,&current);
        char id[161]={0};SilphManagedRef gym={0};
        if(!status)status=gym_resolve(s,current.reference,&gym);
        if(!status&&gym.serial)status=read_text(s,gym,"get_Id",id,sizeof(id));
        int drop=release(s,&current.reference);if(!status)status=drop;
        if(!gym.serial||!gym_fort_id_matches(id,fort)){if(gym.serial)release(s,&gym);status=0;continue;}
        status=gym_retain_tap_target(s,gym,out);drop=release(s,&gym);if(!status)status=drop;
        if(!status&&out->serial)break;
        status=0;
    }
    if(iterator.reference.serial){int disposed=silph_read_call(s->read,iterator.reference,"Dispose",SILPH_READ_VOID,NULL,0,&value);if(!status)status=disposed;release(s,&iterator.reference);}
    if(collection.reference.serial)release(s,&collection.reference);
    if(dictionary.reference.serial)release(s,&dictionary.reference);
    if(!status&&out->serial){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym map via emds fort=%s",fort);return 0;}
    return status?status:SILPH_MANAGED_NOT_READY;
}
int silph_game_read_raid_gym_target(void *memory,const char *fort,SilphManagedRef *out){
    GameRead *s=memory;if(!s||!s->opened||!fort||!fort[0]||!out)return SILPH_MANAGED_ARGUMENT;memset(out,0,sizeof(*out));
    static const char *methods[]={"get_UncompletedRaidGyms","get_ActiveGyms","get_ActiveMapGyms","GatherNearbyGyms","GatherNearbyMapGyms","GetNearbyGyms",NULL};
    int status=0;for(unsigned m=0;methods[m]&&!out->serial;m++){
        SilphReadValue list={0};int r=silph_read_call(s->read,s->directory,methods[m],SILPH_READ_REFERENCE,NULL,0,&list);
        if(r==SILPH_MANAGED_CONTRACT||r==SILPH_MANAGED_ARGUMENT)continue;
        if(!r)r=gym_match_list(s,list.reference,fort,out);
        int freed=release(s,&list.reference);if(!status)status=r?r:freed;
        if(!status&&out->serial){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym target via %s fort=%s",methods[m],fort);return 0;}
    }
    return status?status:SILPH_MANAGED_NOT_READY;
}
static bool gym_map_err_soft(int status){
    return !status||status==SILPH_MANAGED_NOT_READY||status==SILPH_MANAGED_CONTRACT||status==SILPH_MANAGED_ARGUMENT||status==SILPH_MANAGED_EXCEPTION;
}
int silph_game_read_gym_map(void *memory,const char *fort,SilphManagedRef *out){
    GameRead *s=memory;if(!s||!s->opened||!fort||!fort[0]||!out)return SILPH_MANAGED_ARGUMENT;memset(out,0,sizeof(*out));
    init_directory_fields(s);
    const char *fstop = s->field_stops[0] ? s->field_stops : "emkv";
    const char *fgym  = s->field_gyms[0]  ? s->field_gyms  : "emkw";
    int status=fort_dict_lookup(s,fstop,fort,out);if(!status&&out->serial)return 0;
    if(!gym_map_err_soft(status))return status;memset(out,0,sizeof(*out));status=0;
    status=fort_dict_lookup(s,fgym,fort,out);if(!status&&out->serial)return 0;
    if(!gym_map_err_soft(status))return status;memset(out,0,sizeof(*out));status=0;
    status=gym_map_from_raid_lists(s,fort,out);if(!status&&out->serial)return 0;
    if(!gym_map_err_soft(status))return status;memset(out,0,sizeof(*out));status=0;
    status=gym_map_from_emds(s,fort,out);if(!status&&out->serial)return 0;
    if(!gym_map_err_soft(status))return status;memset(out,0,sizeof(*out));status=0;
    status=silph_game_read_raid_gym_target(s,fort,out);if(!status&&out->serial)return 0;
    if(!gym_map_err_soft(status))return status;memset(out,0,sizeof(*out));status=0;
    SilphManagedRef resolved={0};status=gym_lookup_by_id(s,fort,&resolved);
    if(!status&&resolved.serial){status=gym_retain_tap_target(s,resolved,out);int freed=release(s,&resolved);if(!status)status=freed;
        if(!status&&out->serial){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym map via lookup fort=%s",fort);return 0;}}
    else if(resolved.serial)release(s,&resolved);
    if(status&&status!=SILPH_MANAGED_NOT_READY&&status!=SILPH_MANAGED_CONTRACT)return status;memset(out,0,sizeof(*out));
    status=fort_dictionary(s,fgym,fort,out);if(!status&&out->serial)return 0;
    if(status&&status!=SILPH_MANAGED_NOT_READY&&status!=SILPH_MANAGED_CONTRACT)return status;
    status=fort_dictionary(s,fstop,fort,out);if(!status&&out->serial)return 0;
    if(status&&status!=SILPH_MANAGED_NOT_READY&&status!=SILPH_MANAGED_CONTRACT)return status;memset(out,0,sizeof(*out));
    SilphManagedRef key={0};status=silph_managed_ascii(s->managed,fort,(uint32_t)strlen(fort),&key);if(status)return status;
    SilphReadArgument arg={.kind=SILPH_READ_REFERENCE,.value.reference=key};SilphReadValue gym={0};
    status=silph_read_call(s->read,s->directory,"GetMapGym",SILPH_READ_REFERENCE,&arg,1,&gym);
    if(status==SILPH_MANAGED_CONTRACT||status==SILPH_MANAGED_ARGUMENT)status=silph_read_call(s->read,s->directory,"GetGym",SILPH_READ_REFERENCE,&arg,1,&gym);
    if(status==SILPH_MANAGED_CONTRACT||status==SILPH_MANAGED_ARGUMENT)status=silph_read_call(s->read,s->directory,"TryGetGym",SILPH_READ_REFERENCE,&arg,1,&gym);
    int freed=release(s,&key);if(!status)status=freed;
    if(status==SILPH_MANAGED_CONTRACT||status==SILPH_MANAGED_ARGUMENT)status=0;
    if(status)return status;
    if(!gym.reference.serial)return SILPH_MANAGED_NOT_READY;
    status=gym_retain_map(s,gym.reference,out);freed=release(s,&gym.reference);if(!status)status=freed;
    if(!status&&out->serial)return 0;
    static const char *lists[]={"get_UncompletedRaidGyms","get_ActiveGyms","get_ActiveMapPlaces",NULL};
    for(unsigned m=0;lists[m]&&!out->serial;m++){
        SilphReadValue active={0};int lr=silph_read_call(s->read,s->directory,lists[m],SILPH_READ_REFERENCE,NULL,0,&active);
        if(lr==SILPH_MANAGED_CONTRACT||lr==SILPH_MANAGED_ARGUMENT)continue;
        if(!lr)lr=gym_match_list(s,active.reference,fort,out);
        int freed=release(s,&active.reference);if(!status)status=lr?lr:freed;
        if(!status&&out->serial){__android_log_print(ANDROID_LOG_INFO,"SilphRaid","gym map via %s fort=%s",lists[m],fort);return 0;}
    }
    return status?status:SILPH_MANAGED_NOT_READY;
}
int silph_game_read_gift_service(void *memory,uint32_t kind,SilphManagedRef *output){
    GameRead *s=memory;if(!s||!s->opened||!output||kind>2)return SILPH_MANAGED_ARGUMENT;memset(output,0,sizeof(*output));
    return service(s,kind?"holo-game-interfaces.dll":"holo-game.dll",kind?"Niantic.Holoholo.Social.Gifting":"Niantic.Holoholo.Social",kind==0?"IFriendsRpcService":kind==1?"IGiftingRpcService":"IGiftingService",output);
}
int silph_game_read_map_count(void *memory,uint32_t *output){
    GameRead *s=memory;if(!s||!s->opened||!output)return SILPH_MANAGED_ARGUMENT;SilphReadValue dictionary={0};
    int status=silph_read_field(s->read,s->directory,"emds",SILPH_READ_REFERENCE,&dictionary);if(!status)status=scalar(s,dictionary.reference,"get_Count",output);int freed=release(s,&dictionary.reference);return status?status:freed;
}
#ifdef SILPH_ENABLE_ACTIONS
#include "research-bridge.h"
int silph_game_read_research_service(void *memory,uint32_t kind,SilphManagedRef *out){
    GameRead *s=memory;if(!s||!s->opened||!out||kind>4)return SILPH_MANAGED_ARGUMENT;memset(out,0,sizeof(*out));
    if(kind<2)return service(s,"holo-game.dll","Niantic.Holoholo.Quests",kind?"IQuestRpcService":"IQuestService",out);
    if(kind==3)return service(s,"holo-game-interfaces.dll","Niantic.Holoholo.Quests","IQuestStampCardService",out);
    if(kind==4)return service(s,"holo-game.dll","Niantic.Holoholo.UI.TodayView.EventPass","IEventPassService",out);
    SilphManagedRef type={0};SilphReadValue value={0};int status=silph_research_factory_type(s->read,&type);
    SilphReadArgument arg={.kind=SILPH_READ_REFERENCE,.value.reference=type};
    if(!status)status=silph_read_call(s->read,s->container,"TryResolve",SILPH_READ_REFERENCE,&arg,1,&value);
    if(!status&&!value.reference.serial)status=SILPH_MANAGED_CONTRACT;
    if(!status){*out=value.reference;value.reference=(SilphManagedRef){0};}
    int a=release(s,&type),b=release(s,&value.reference);return status?status:a?a:b;
}
#endif
static int flag_field(GameRead *s,SilphManagedRef pokemon,const char *name,uint32_t flag,SilphTransferInput *data){
    SilphReadValue value;int status=silph_read_field(s->read,pokemon,name,SILPH_READ_BOOL,&value);
    if(!status){data->known_flags|=flag;if(value.bits)data->flags|=flag;}return status;
}
static int flag_call(GameRead *s,SilphManagedRef pokemon,const char *name,uint32_t flag,SilphTransferInput *data){
    SilphReadValue value;int status=silph_read_call(s->read,pokemon,name,SILPH_READ_BOOL,NULL,0,&value);
    if(!status){data->known_flags|=flag;if(value.bits)data->flags|=flag;}return status;
}
static int text_present(GameRead *s,SilphManagedRef pokemon,const char *name,bool *present){
    SilphReadValue text={0},size={0};*present=false;int status=silph_read_field(s->read,pokemon,name,SILPH_READ_REFERENCE,&text);
    if(!status&&text.reference.serial)status=silph_read_call(s->read,text.reference,"get_Length",SILPH_READ_I32,NULL,0,&size);
    if(!status&&size.bits>4096)status=SILPH_MANAGED_CONTRACT;if(!status)*present=size.bits!=0;
    int freed=release(s,&text.reference);return status?status:freed;
}
int silph_game_read_transfer_data(void *memory,uint64_t id,SilphTransferInput *data,SilphManagedRef *bag,SilphManagedRef *pokemon){
    GameRead *s=memory;if(!s||!s->opened||!data||!bag||!pokemon||bag==pokemon)return SILPH_MANAGED_ARGUMENT;
    memset(data,0,sizeof(*data));memset(bag,0,sizeof(*bag));memset(pokemon,0,sizeof(*pokemon));
    int status=silph_game_read_owned(s,id,pokemon);if(!status&&!pokemon->serial)return SILPH_MANAGED_NOT_READY;
    if(!status)status=silph_read_is_type(s->read,*pokemon,"Holoholo.Rpc","PokemonProto");
    SilphReadValue value={0},display={0};SilphManagedRef buddy={0},master={0};
    if(!status)status=silph_read_field(s->read,*pokemon,"id_",SILPH_READ_U64,&value);
    if(!status&&value.bits!=id)status=SILPH_MANAGED_STALE;
    if(!status){data->owned_id=id;data->abi=1;data->valid=SILPH_VALID_ID;data->source=SILPH_SOURCE_WILD;
        data->options_present=data->options_known=data->options=255;}
    if(!status)status=flag_field(s,*pokemon,"isEgg_",SILPH_DATA_EGG,data);
    const char *integers[]={"pokemonId_","individualAttack_","individualDefense_","individualStamina_"};
    int32_t *destinations[]={&data->species,&data->attack,&data->defense,&data->stamina};
    for(unsigned i=0;!status&&i<4;i++){status=silph_read_field(s->read,*pokemon,integers[i],SILPH_READ_I32,&value);if(!status)memcpy(destinations[i],&value.bits,4);}
    if(!status)data->valid|=SILPH_VALID_SPECIES|SILPH_VALID_IV;
    if(!status)status=flag_field(s,*pokemon,"favorite_",SILPH_DATA_FAVORITE,data);
    if(!status)status=flag_field(s,*pokemon,"isFusion_",SILPH_DATA_FUSION,data);
    if(!status)status=flag_field(s,*pokemon,"hatchedFromEgg_",SILPH_DATA_HATCHED,data);
    if(!status)status=flag_call(s,*pokemon,"get_IsShinyPokemon",SILPH_DATA_SHINY,data);
    bool fort=false,station=false,limited=false;
    if(!status)status=text_present(s,*pokemon,"deployedFortId_",&fort);
    if(!status)status=text_present(s,*pokemon,"deployedStationId_",&station);
    if(!status)status=text_present(s,*pokemon,"limitedPokemonIdentifier_",&limited);
    if(!status){data->known_flags|=SILPH_DATA_DEPLOYED|SILPH_DATA_LIMITED;if(fort||station)data->flags|=SILPH_DATA_DEPLOYED;if(limited)data->flags|=SILPH_DATA_LIMITED;}
    if(!status)status=silph_read_field(s->read,*pokemon,"pokemonDisplay_",SILPH_READ_REFERENCE,&display);
    if(!status)status=silph_read_is_type(s->read,display.reference,"Holoholo.Rpc","PokemonDisplayProto");
    if(!status)status=flag_call(s,display.reference,"get_IsAnyBreadPokemon",SILPH_DATA_DYNAMAX,data);
    SilphReadValue card={0},art={0};
    if(!status)status=silph_read_call(s->read,display.reference,"get_HasLocationCard",SILPH_READ_BOOL,NULL,0,&card);
    if(!status)status=silph_read_call(s->read,display.reference,"get_IsNaturalArtPokemon",SILPH_READ_BOOL,NULL,0,&art);
    if(!status){data->known_flags|=SILPH_DATA_BACKGROUND;if(card.bits||art.bits)data->flags|=SILPH_DATA_BACKGROUND;}
    if(!status)status=service(s,"holo-game.dll","Niantic.Holoholo.Buddy","IBuddyPokemonService",&buddy);
    SilphReadArgument arg={.kind=SILPH_READ_U64,.value.bits=id};
    if(!status)status=silph_read_call(s->read,buddy,"GetIsBuddy",SILPH_READ_BOOL,&arg,1,&value);
    if(!status){data->known_flags|=SILPH_DATA_BUDDY;if(value.bits)data->flags|=SILPH_DATA_BUDDY;}
    if(!status)status=service(s,"holo-protos.dll","","IGameMasterData",&master);
    arg=(SilphReadArgument){.kind=SILPH_READ_REFERENCE,.value.reference=master};
    if(!status)status=silph_read_call(s->read,*pokemon,"GetClass",SILPH_READ_ENUM32,&arg,1,&value);
    if(!status){memcpy(&data->pokemon_class,&value.bits,4);data->valid|=SILPH_VALID_CLASS;}
    void *object=NULL;if(!status)status=silph_managed_target(s->managed,s->pokemon_bag,&object);
    if(!status)status=silph_managed_retain(s->managed,object,false,bag);
    int a=release(s,&display.reference),b=release(s,&buddy),c=release(s,&master);if(!status)status=a?a:b?b:c;
    if(status){release(s,bag);release(s,pokemon);memset(data,0,sizeof(*data));}return status;
}
