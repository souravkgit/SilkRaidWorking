package dev.silphmod.nativecheck;

import dev.rootcatch.root.RootShell;
import dev.rootcatch.ugphone.KecleonFeed;
import dev.rootcatch.ugphone.KecleonMovement;
import java.util.Locale;
import java.util.function.Supplier;

final class NativeManualRecord implements AutoCloseable {
    static final String EXTRA="recordManual";
    private final CheckLog log;
    private final NativeSettings settings;
    private final RootShell shell=new RootShell();
    private volatile boolean closed;
    private Thread sampler;
    private int gamePid;
    private Supplier<String> nativeSample;

    NativeManualRecord(CheckLog log,NativeSettings settings){this.log=log;this.settings=settings;}

    void teleportToAnchor() throws Exception {
        double[] anchor=KecleonFeed.parseCoordinates(settings.prefs.getString("raidAnchor","34.693738,135.502165"));
        RootShell.Result value=shell.execute(KecleonMovement.command(anchor[0],anchor[1]),5000);
        String lower=value.output.toLowerCase(Locale.ROOT);
        if(value.code!=0||lower.contains("error")||lower.contains("exception"))throw new java.io.IOException("GPS move to anchor failed: "+value.output);
        log.write("record_anchor","latitude="+anchor[0]+" longitude="+anchor[1]);
    }

    void start(int pid,Supplier<String> nativeSample){
        if(closed||pid<=1)return;
        gamePid=pid;this.nativeSample=nativeSample;
        log.write("record_begin","pid="+pid);
        sampler=new Thread(()->{
            while(!closed&&gamePid>0){
                try{sampleOnce();}catch(Exception error){log.write("record_sample_error",error.toString());}
                try{Thread.sleep(8000);}catch(InterruptedException ignored){break;}
            }
        },"manual-raid-record");sampler.setDaemon(true);sampler.start();
    }

    void sampleOnce() throws Exception {
        if(closed||gamePid<=1)return;
        String logcat=shell.execute("logcat -d -v brief --pid="+gamePid+" -s SilphRaid:V SilphRuntime:I Unity:W Niantic:I -t 200 2>/dev/null",5000).output;
        if(logcat.isEmpty())logcat=shell.execute("logcat -d -v brief --pid="+gamePid+" -t 150 2>/dev/null",4000).output;
        if(logcat.length()>12000)logcat=logcat.substring(logcat.length()-12000);
        log.write("record_logcat",logcat);
        if(nativeSample!=null){
            String nativeStatus=nativeSample.get();
            if(nativeStatus!=null&&!nativeStatus.isEmpty())log.write("record_native",nativeStatus);
        }
    }

    void finish(){
        if(closed)return;
        closed=true;
        if(sampler!=null)sampler.interrupt();
        try{sampleOnce();}catch(Exception error){log.write("record_final_error",error.toString());}
        try{
            if(gamePid>0){
                String tail=shell.execute("logcat -d -v threadtime --pid="+gamePid+" -s SilphRaid:V SilphRuntime:I Unity:V Niantic:V | tail -c 300000",10000).output;
                log.write("record_logcat_final",tail);
            }
        }catch(Exception error){log.write("record_logcat_final_error",error.toString());}
        settings.prefs.edit().putBoolean("lastRecordReady",true).apply();
        log.write("record_complete","manual raid capture finished — export logs to reuse this trace");
    }

    @Override public void close(){finish();shell.close();}
}
