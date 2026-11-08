package dev.silphmod.nativecheck;

import android.content.Context;
import android.content.SharedPreferences;
import dev.rootcatch.nativeengine.*;
import dev.rootcatch.automation.PokemonSelection;
import dev.rootcatch.ugphone.KecleonFeed;
import java.util.ArrayList;

final class NativeSettings {
    final SharedPreferences prefs;
    NativeSettings(Context context){
        prefs=context.getSharedPreferences("native-settings",0);
        if(!prefs.contains("berries"))prefs.edit().putBoolean("berries",true).putString("berriesOrder","701,705,703").apply();
        if(!prefs.contains("raidAnchor"))prefs.edit()
            .putString("raidAnchor",DEFAULT_RAID_ANCHOR)
            .putInt("raidRadius",5000)
            .putInt("raidTierMask",127)
            .putInt("raidPassItem",0)
            .putInt("raidMinPlayers",0)
            .putBoolean("raidGlobalHot",true)
            .putBoolean("catchBoss",true)
            .putBoolean("recommendedTeam",true)
            .putBoolean("fixLocation",true)
            .putBoolean("raidAutoClean",true)
            .putInt("raidsMax",100)
            .putInt("delay",150)
            .putBoolean("berries",true)
            .putString("berriesOrder","701,705,703")
            .putInt("quality",4)
            .putBoolean("curve",true)
            .apply();
        SharedPreferences.Editor migrate=prefs.edit();boolean changed=false;
        String curAnchor=prefs.getString("raidAnchor","");
        if("35.729000,139.718600".equals(curAnchor)||"-23.585672,-46.657279".equals(curAnchor)||"40.770460,-73.979120".equals(curAnchor)||"19.429225,-99.134272".equals(curAnchor)||"40.766696,-73.972402".equals(curAnchor)||"40.771740,-73.973212".equals(curAnchor)||"19.432260,-99.137243".equals(curAnchor)||"19.42726,-99.16756".equals(curAnchor)||"37.511499,127.101065".equals(curAnchor)||"41.671948,-0.885460".equals(curAnchor)||"42.790918,-4.845848".equals(curAnchor)||"34.656963,135.512312".equals(curAnchor)){migrate.putString("raidAnchor",DEFAULT_RAID_ANCHOR);changed=true;}
        if(prefs.getInt("raidTierMask",127)==1){migrate.putInt("raidTierMask",127);changed=true;}
        if(!prefs.getBoolean("raidGlobalHot",false)){migrate.putBoolean("raidGlobalHot",true);changed=true;}
        if(prefs.getInt("raidRadius",500)<5000){migrate.putInt("raidRadius",5000);changed=true;}
        if(!prefs.getBoolean("fixLocation",false)){migrate.putBoolean("fixLocation",true);changed=true;}
        if(changed)migrate.apply();
    }
    private static final String DEFAULT_RAID_ANCHOR="34.693738,135.502165";
    private long clock;
    private boolean timing;
    long elapsed(int mode){return prefs.getLong("stats"+mode+".elapsed",0);}
    void elapsedPulse(int mode,boolean active){long now=android.os.SystemClock.elapsedRealtime();if(clock==0){clock=now;timing=active;return;}long delta=now-clock;if(delta>=1000||!active){if(timing&&delta>0&&delta<=10000){long before=elapsed(mode);prefs.edit().putLong("stats"+mode+".elapsed",before>Long.MAX_VALUE-delta?Long.MAX_VALUE:before+delta).apply();}clock=now;}timing=active;}
    NativeSessionState.Totals totals(int mode){String p="stats"+mode+".";return new NativeSessionState.Totals(prefs.getLong(p+"caught",0),prefs.getLong(p+"fled",0),prefs.getLong(p+"spun",0),prefs.getLong(p+"discarded",0),prefs.getLong(p+"transferred",0),prefs.getLong(p+"encounters",0),prefs.getLong(p+"shinies",0),prefs.getLong(p+"xp",0),prefs.getLong(p+"stardust",0));}
    void save(int mode,NativeSessionState.Totals t){String p="stats"+mode+".";prefs.edit().putLong(p+"caught",t.caught).putLong(p+"fled",t.fled).putLong(p+"spun",t.spun).putLong(p+"discarded",t.discarded).putLong(p+"transferred",t.transferred).putLong(p+"encounters",t.encounters).putLong(p+"shinies",t.shinies).putLong(p+"xp",t.xp).putLong(p+"stardust",t.stardust).apply();}
    String berryNames(){return berryNames("berriesOrder","701,705,703");}
    void saveBerries(String names){ArrayList<String> ids=new ArrayList<>();for(String name:dev.rootcatch.automation.BerryOptions.parse(names))ids.add(Integer.toString(ItemCatalog.ENTRIES.stream().filter(e->e.name().equals(name)).findFirst().orElseThrow().id()));prefs.edit().putString("berriesOrder",String.join(",",ids)).putBoolean("berries",!ids.isEmpty()).apply();}
    private String berryNames(String key,String fallback){ArrayList<String> names=new ArrayList<>();for(int id:integers(prefs.getString(key,fallback)))names.add(ItemCatalog.ENTRIES.stream().filter(e->e.id()==id).findFirst().orElseThrow().name());return String.join(",",names);}
    NativeSessionWire.Config recordConfig(){
        NativeSessionWire.Config c=new NativeSessionWire.Config();
        c.mode=NativeSessionWire.RAID;c.delayMs=150;c.durationMs=0;c.maxEncounters=1;
        c.flags=NativeSessionWire.RECORD_FLAGS;
        double[] anchor=KecleonFeed.parseCoordinates(prefs.getString("raidAnchor",DEFAULT_RAID_ANCHOR));
        c.raidAnchorLat=anchor[0];c.raidAnchorLon=anchor[1];c.raidRadiusM=prefs.getInt("raidRadius",500);
        c.raidTierMask=127;
        return c;
    }
    NativeSessionWire.Config config(int mode,boolean readonly){
        NativeSessionWire.Config c=new NativeSessionWire.Config();c.mode=NativeSessionWire.RAID;c.delayMs=prefs.getInt("delay",150);c.durationMs=readonly?300000:0;
        if(readonly){if(prefs.getBoolean("fixLocation",false))throw new IllegalArgumentException("Turn off Read-only check to use Fix error 12");c.flags=NativeSessionWire.READ_ONLY;return c;}
        c.flags=NativeSessionWire.SCENES;
        if(prefs.getBoolean("catchBoss",true))c.flags|=NativeSessionWire.CATCH;
        if(prefs.getBoolean("recommendedTeam",true))c.flags|=NativeSessionWire.RECOMMENDED_TEAM;
        if(prefs.getBoolean("curve",true))c.flags|=NativeSessionWire.CURVE;
        if(prefs.getBoolean("hideTrainerName",true))c.flags|=NativeSessionWire.TRAINER_NAME;
        if(prefs.getBoolean("fixLocation",true))c.flags|=NativeSessionWire.FIX_LOCATION;
        if(prefs.getBoolean("berries",true)){c.flags|=NativeSessionWire.BERRIES;c.berries=integers(prefs.getString("berriesOrder","701,705,703"));}
        c.quality=prefs.getInt("quality",4);
        double[] anchor=KecleonFeed.parseCoordinates(prefs.getString("raidAnchor",DEFAULT_RAID_ANCHOR));
        c.raidAnchorLat=anchor[0];c.raidAnchorLon=anchor[1];
        c.raidRadiusM=prefs.getInt("raidRadius",500);
        c.raidTierMask=prefs.getInt("raidTierMask",127);
        c.raidPassItem=0x10000|passMask();
        c.raidMinPlayers=packedMinPlayers();
        c.raidGlobalHot=(prefs.getBoolean("raidGlobalHot",false)?1:0)|(prefs.getBoolean("raidHotLobby",true)?2:0)|(Math.max(1,Math.min(3,prefs.getInt("raidTeam",1)))<<4);
        c.potionOrder=RocketCareOrder.parse(prefs.getString("raidPotionOrder",""),false);
        c.reviveOrder=RocketCareOrder.parse(prefs.getString("raidReviveOrder",""),true);
        c.filters=prefs.getInt("raidFilters",0);
        int[] bosses=parseBosses();
        c.raidBossSpecies=(bosses.length>0)?bosses[0]:0;
        c.raidBossCount=bosses.length;
        for(int i=0;i<bosses.length&&i<8;i++)c.raidBossSpeciesList[i]=bosses[i];
        NativeSessionState.Totals t=totals(mode);
        c.maxEncounters=remaining(prefs.getInt("raidsMax",100),t.spun,"Raid");
        return c;
    }
    int[] parseBosses(){
        String raw=prefs.getString("raidBossSpeciesList","").trim();
        if(raw.isEmpty())return new int[0];
        java.util.List<Integer> list=PokemonSelection.parse(raw);
        int[] out=new int[Math.min(list.size(),8)];
        for(int i=0;i<out.length;i++)out[i]=list.get(i);
        return out;
    }
    static final String[] MIN_PLAYER_KEYS={"raidMin1","raidMin3","raidMin5","raidMin7"};
    static final int[] MIN_PLAYER_DEFAULTS={3,6,10,16};
    int minPlayers(int slot){return Math.max(0,Math.min(20,prefs.getInt(MIN_PLAYER_KEYS[slot],MIN_PLAYER_DEFAULTS[slot])));}
    int packedMinPlayers(){int v=NativeSessionWire.MIN_PLAYERS_PACKED;for(int i=0;i<4;i++)v|=minPlayers(i)<<(i*8);return v;}
    static int[] integers(String s){if(s==null||s.trim().isEmpty())return new int[0];String[] values=s.split(",");int[] out=new int[values.length];for(int i=0;i<values.length;i++)out[i]=Integer.parseInt(values[i].trim());return out;}
    private static int remaining(int maximum,long current,String name){if(maximum<1||maximum>50000)throw new IllegalArgumentException(name+" limit must be 1–50000");if(current>=maximum)throw new IllegalArgumentException(name+" limit reached; clear statistics or increase limit");return(int)(maximum-current);}
    int passMask(){
        int m=prefs.getInt("raidPassMask",0);
        if(m>=1&&m<=7)return m;
        int legacy=prefs.getInt("raidPassItem",0);
        return legacy==1400||legacy==1401?1:legacy==1402?2:3;
    }
}
