package dev.rootcatch.nativeengine;

public final class NativeProgressWatch {
    public static final int WAIT=0,CHANNEL_STALLED=1,PROGRESS_STALLED=2;
    private long lastSnapshot,lastReply,lastCheck,activeWait,executed;
    public NativeProgressWatch(long now){lastSnapshot=lastReply=lastCheck=now;}
    public synchronized void snapshot(long now){lastSnapshot=now;activeWait=0;}
    public synchronized void reply(long now,long executed){lastReply=now;this.executed=executed;}
    public synchronized int check(long now,boolean active){return check(now,active,65_000);}
    public synchronized int check(long now,boolean active,long stallMs){
        long elapsed=Math.max(0,Math.min(2500,now-lastCheck));lastCheck=now;
        if(!active){activeWait=0;return WAIT;}
        activeWait+=elapsed;
        long limit=stallMs<15_000?65_000:stallMs;
        if(now-lastSnapshot<limit||activeWait<limit)return WAIT;
        return now-lastReply<=5000?PROGRESS_STALLED:CHANNEL_STALLED;
    }
    public synchronized String details(long now){return "snapshotAgeMs="+(now-lastSnapshot)+" replyAgeMs="+(now-lastReply)+" foregroundWaitMs="+activeWait+" executed="+executed;}
}
