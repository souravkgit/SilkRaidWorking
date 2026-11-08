package dev.rootcatch.nativeengine;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.util.zip.CRC32;

public final class NativeSessionWire {
    public static final int CONFIG_BYTES=1024,SNAPSHOT_BYTES=256,RAID=1;
    public static final int CATCH=1,SPIN=2,CLEAN=4,TRANSFER=8,BERRIES=16,CURVE=32,READ_ONLY=64,TRAINER_NAME=128,IV_NAMES=256,FIX_LOCATION=512,SCENES=1024,RECOMMENDED_TEAM=65536;
    public static final int RECORD_FLAGS=READ_ONLY|FIX_LOCATION|SCENES;
    public static final int MIN_PLAYERS_PACKED=0x40000000;
    private static final long CONFIG_MAGIC=0x3147464348504c53L,SNAPSHOT_MAGIC=0x3154415348504c53L;
    private NativeSessionWire() { }
    public static final class Config {
        public long ticks,sessionHi,sessionLo;
        public int pid,mode=RAID,flags=0,quality=4,delayMs=150,keep;
        public int maxCatches,maxSpins,maxEncounters,filters,durationMs;
        public int[] berries=new int[0];
        public int[][] limits=new int[0][2];
        public byte[] species=new byte[256];
        public int[] potionOrder=new int[0],reviveOrder=new int[0];
        public double raidAnchorLat,raidAnchorLon,raidRadiusM=500;
        public int raidTierMask=127,raidPassItem,raidMinPlayers,raidGlobalHot,raidBossSpecies,raidBossCount;
        public int[] raidBossSpeciesList=new int[8];
        public byte[] encode() {
            validate();ByteBuffer b=ByteBuffer.allocate(CONFIG_BYTES).order(ByteOrder.LITTLE_ENDIAN);
            b.putLong(CONFIG_MAGIC).putInt(1).putInt(CONFIG_BYTES).putInt(pid).putInt(mode).putLong(ticks).putLong(sessionHi).putLong(sessionLo);
            b.putInt(flags).putInt(quality).putInt(delayMs).putInt(keep).putInt(maxCatches).putInt(maxSpins).putInt(maxEncounters).putInt(berries.length);
            for(int i=0;i<berries.length;i++)b.putInt(80+i*4,berries[i]);b.putInt(100,limits.length);
            for(int i=0;i<limits.length;i++){b.putInt(104+i*8,limits[i][0]);b.putInt(108+i*8,limits[i][1]);}
            b.position(616);b.put(species);b.putInt(872,filters).putInt(876,durationMs);
            b.putDouble(880,raidAnchorLat);b.putDouble(888,raidAnchorLon);b.putDouble(896,raidRadiusM);
            b.putInt(904,raidTierMask);b.putInt(908,raidPassItem);b.putInt(912,raidMinPlayers);b.putInt(916,raidGlobalHot);
            b.put(920,(byte)potionOrder.length);for(int i=0;i<potionOrder.length;i++)b.put(921+i,(byte)potionOrder[i]);
            b.put(925,(byte)reviveOrder.length);for(int i=0;i<reviveOrder.length;i++)b.put(926+i,(byte)reviveOrder[i]);
            b.putInt(928,raidBossSpecies);for(int i=0;i<raidBossSpeciesList.length;i++)b.putInt(932+i*4,raidBossSpeciesList[i]);b.putInt(964,raidBossCount);
            checksum(b,1020);return b.array();
        }
        private static boolean validMinPlayers(int v){if((v&MIN_PLAYERS_PACKED)==0)return v>=0&&v<=20;if((v&~(MIN_PLAYERS_PACKED|0x3f3f3f3f))!=0)return false;for(int i=0;i<4;i++)if(((v>>(i*8))&0x3f)>20)return false;return true;}
        public String command(){return "engine "+hex(encode());}
        private void validate() {
            require(potionOrder!=null&&reviveOrder!=null);
            require(Arrays.equals(potionOrder,RocketCareOrder.parse(RocketCareOrder.encode(potionOrder),false)));
            require(Arrays.equals(reviveOrder,RocketCareOrder.parse(RocketCareOrder.encode(reviveOrder),true)));
            require((potionOrder.length==0&&reviveOrder.length==0)||(mode==RAID&&(flags&READ_ONLY)==0));
            require(pid>1&&ticks>0&&(sessionHi!=0||sessionLo!=0)&&mode==RAID&&flags>=0&&(flags&~131071)==0);
            require(quality>=1&&quality<=4&&delayMs>=100&&delayMs<=3000&&maxCatches==0&&maxSpins==0&&maxEncounters>=1&&maxEncounters<=50000);
            require(filters>=0&&filters<=127&&durationMs>=0&&durationMs<=86400000&&(durationMs==0||durationMs>=1000));
            require(berries!=null&&berries.length<=5&&limits!=null&&limits.length==0&&species!=null&&species.length==256&&(species[0]&1)==0);
            if((flags&BERRIES)!=0)require(berries.length>0);else require(berries.length==0);
            for(int i=0;i<berries.length;i++){int n=berries[i];require(n==701||n==703||n==705||n==706||n==708);for(int j=0;j<i;j++)require(n!=berries[j]);}
            require(Double.isFinite(raidAnchorLat)&&Double.isFinite(raidAnchorLon)&&Math.abs(raidAnchorLat)<=90&&Math.abs(raidAnchorLon)<=180);
            require(Double.isFinite(raidRadiusM)&&raidRadiusM>0&&raidRadiusM<=5000);
            require(raidPassItem==0||raidPassItem==1400||raidPassItem==1401||raidPassItem==1402||((raidPassItem>>16)==1&&(raidPassItem&0xffff)>=1&&(raidPassItem&0xffff)<=7));
            require(validMinPlayers(raidMinPlayers)&&(raidGlobalHot&~0x33)==0);
            require(raidBossSpecies>=0&&raidBossSpecies<=1025);
            require(raidBossSpeciesList!=null&&raidBossSpeciesList.length==8&&raidBossCount>=0&&raidBossCount<=8);
            for(int i=0;i<raidBossSpeciesList.length;i++){require(raidBossSpeciesList[i]>=0&&raidBossSpeciesList[i]<=1025);if(i>=raidBossCount)require(raidBossSpeciesList[i]==0);}
            if((flags&READ_ONLY)!=0){
                if(flags==RECORD_FLAGS)require(maxEncounters==1&&filters==0);
                else require(flags==READ_ONLY&&maxEncounters==0&&filters==0);
            }
        }
    }
    public static Config decodeConfig(byte[] data){
        ByteBuffer b=checked(data,CONFIG_BYTES,CONFIG_MAGIC,1020);
        Config c=new Config();c.pid=b.getInt(16);c.mode=b.getInt(20);c.ticks=b.getLong(24);c.sessionHi=b.getLong(32);c.sessionLo=b.getLong(40);
        c.flags=b.getInt(48);c.quality=b.getInt(52);c.delayMs=b.getInt(56);c.keep=b.getInt(60);c.maxCatches=b.getInt(64);c.maxSpins=b.getInt(68);c.maxEncounters=b.getInt(72);
        int count=b.getInt(76);require(count>=0&&count<=5);c.berries=new int[count];for(int i=0;i<5;i++){int n=b.getInt(80+i*4);if(i<count)c.berries[i]=n;else require(n==0);}
        count=b.getInt(100);require(count==0);c.limits=new int[0][0];c.species=Arrays.copyOfRange(data,616,872);c.filters=b.getInt(872);c.durationMs=b.getInt(876);
        c.raidAnchorLat=b.getDouble(880);c.raidAnchorLon=b.getDouble(888);c.raidRadiusM=b.getDouble(896);
        c.raidTierMask=b.getInt(904);c.raidPassItem=b.getInt(908);c.raidMinPlayers=b.getInt(912);c.raidGlobalHot=b.getInt(916);
        int pn=data[920]&255,rn=data[925]&255;require((pn==0||pn==4)&&(rn==0||rn==2));c.potionOrder=new int[pn];c.reviveOrder=new int[rn];
        for(int i=0;i<4;i++){int id=data[921+i]&255;if(i<pn)c.potionOrder[i]=id;else require(id==0);}
        for(int i=0;i<2;i++){int id=data[926+i]&255;if(i<rn)c.reviveOrder[i]=id;else require(id==0);}
        c.raidBossSpecies=b.getInt(928);for(int i=0;i<c.raidBossSpeciesList.length;i++)c.raidBossSpeciesList[i]=b.getInt(932+i*4);c.raidBossCount=b.getInt(964);
        c.validate();return c;
    }
    public static Snapshot decodeSnapshot(byte[] data){ByteBuffer b=checked(data,SNAPSHOT_BYTES,SNAPSHOT_MAGIC,252);return new Snapshot(b);}
    public static final class Snapshot {
        public final int mode,state,error,reason,flags,pid,candidateState,rewardsAvailable,visualAvailable,visualFailed,researchStatus,locationStatus;
        public final long ivReads,nameUpdates;
        public final long ticks,sessionHi,sessionLo,sequence,accountHi,accountLo;
        public final long caught,fled,spun,discarded,transferred,encounters,shinies,berries,throwsCount,xp,stardust,encounterId,capturedId,candidateRequest;
        public final double latitude,longitude;
        private Snapshot(ByteBuffer b){
            mode=b.getInt(16);state=b.getInt(20);error=b.getInt(24);reason=b.getInt(28);flags=b.getInt(32);pid=b.getInt(36);
            ticks=b.getLong(40);sessionHi=b.getLong(48);sessionLo=b.getLong(56);sequence=b.getLong(64);accountHi=b.getLong(72);accountLo=b.getLong(80);
            caught=b.getLong(88);fled=b.getLong(96);spun=b.getLong(104);discarded=b.getLong(112);transferred=b.getLong(120);encounters=b.getLong(128);
            shinies=b.getLong(136);berries=b.getLong(144);throwsCount=b.getLong(152);xp=b.getLong(160);stardust=b.getLong(168);encounterId=b.getLong(176);capturedId=b.getLong(184);
            candidateRequest=b.getLong(192);latitude=b.getDouble(200);longitude=b.getDouble(208);candidateState=b.getInt(216);rewardsAvailable=b.getInt(220);
            visualAvailable=b.getInt(224);visualFailed=b.getInt(228);ivReads=b.getLong(232);nameUpdates=b.getLong(240);
            researchStatus=b.getInt(248)&65535;locationStatus=b.getInt(248)>>>16;
            require(mode==RAID&&pid>1&&ticks>0&&(sessionHi!=0||sessionLo!=0));
        }
        public void bind(Config config){require(pid==config.pid&&ticks==config.ticks&&sessionHi==config.sessionHi&&sessionLo==config.sessionLo&&mode==config.mode);}
    }
    private static ByteBuffer checked(byte[] data,int size,long magic,int checksumAt){
        require(data!=null&&data.length==size);ByteBuffer b=ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN);
        require(b.getLong(0)==magic&&b.getInt(8)==1&&b.getInt(12)==size);CRC32 crc=new CRC32();crc.update(data,0,checksumAt);require(b.getInt(checksumAt)==(int)crc.getValue());return b;
    }
    private static void checksum(ByteBuffer b,int offset){CRC32 crc=new CRC32();crc.update(b.array(),0,offset);b.putInt(offset,(int)crc.getValue());}
    public static String hex(byte[] data){StringBuilder out=new StringBuilder(data.length*2);for(byte value:data)out.append(String.format("%02x",value));return out.toString();}
    public static byte[] unhex(String text){require(text!=null&&(text.length()&1)==0&&text.length()<=4096);byte[] out=new byte[text.length()/2];for(int i=0;i<out.length;i++){int a=Character.digit(text.charAt(i*2),16),b=Character.digit(text.charAt(i*2+1),16);require(a>=0&&b>=0);out[i]=(byte)((a<<4)|b);}return out;}
    static void require(boolean ok){if(!ok)throw new IllegalArgumentException("Invalid native wire value");}
}
