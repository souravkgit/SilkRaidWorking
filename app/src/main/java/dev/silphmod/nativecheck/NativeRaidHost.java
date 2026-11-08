package dev.silphmod.nativecheck;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.SystemClock;
import dev.rootcatch.nativeengine.*;
import dev.rootcatch.root.RootShell;
import dev.rootcatch.ugphone.KecleonFeed;
import dev.rootcatch.ugphone.KecleonMovement;
import java.io.IOException;
import java.util.Locale;
import org.json.JSONObject;

final class NativeRaidHost implements AutoCloseable {
    private static final int GPS_RETRIES=3;
    private static final long SETTLE_MS=2000,GYM_SETTLE_MS=2500;
    private final SharedPreferences settings;
    private final NativeSessionController controller;
    private final CheckLog log;
    private final java.util.function.BooleanSupplier active;
    private final RootShell joystick=new RootShell();
    private volatile boolean closed;
    private NativeKecleonWire.Event event;
    private long commandSequence,lastTap;
    NativeRaidHost(Context context,NativeSessionController controller,CheckLog log,java.util.function.BooleanSupplier active){
        this.controller=controller;this.log=log;this.active=active;settings=new NativeSettings(context).prefs;
    }
    static void beginSession(Context context){}
    synchronized void accept(NativeKecleonWire.Event value){
        if(closed)return;event=value;
        try{process(value);}catch(Exception error){log.write("raid_host_error",error.toString());controller.requestStop("Raid controller: "+error.getMessage());}
    }
    synchronized void tick(){}
    private synchronized void send(NativeKecleonWire.Event e,int kind,int move,boolean accepted,double lat,double lon,String key){
        if(closed||event==null||event.request!=e.request)return;
        controller.enqueue(NativeKecleonWire.command(e,++commandSequence,kind,move,accepted,lat,lon,key));
        log.write("raid_command","sequence="+commandSequence+" request="+e.request+" kind="+kind+" move="+move+" accepted="+accepted);
    }
    private void process(NativeKecleonWire.Event e) throws Exception {
        if(closed)return;log.write("raid_phase","request="+e.request+" phase="+e.phase+" move="+e.move+" reason="+e.reason+" lat="+e.latitude+" lon="+e.longitude);
        if(e.phase==1&&e.move==2){if(SystemClock.elapsedRealtime()-lastTap>3000){lastTap=SystemClock.elapsedRealtime();screenTap(0.5,0.893);}return;}
        if(e.phase==5&&e.move==2){if(SystemClock.elapsedRealtime()-lastTap>3000){lastTap=SystemClock.elapsedRealtime();screenTap(0.5,0.875);}return;}
        if(e.phase==1){
            double[] anchor=KecleonFeed.parseCoordinates(settings.getString("raidAnchor","34.693738,135.502165"));
            move(direct(anchor[0],anchor[1]),e,"anchor");
            send(e,1,0,false,anchor[0],anchor[1],"raid-anchor");return;
        }
        if(e.phase==3){
            if(e.fort!=null&&!e.fort.isEmpty()&&Math.abs(e.latitude)>0.001&&Math.abs(e.longitude)>0.001){
                move(KecleonMovement.points(e.latitude,e.longitude),e,"scan_gym");
                settleAfterMove(SETTLE_MS);
            }else{
                double[] anchor=KecleonFeed.parseCoordinates(settings.getString("raidAnchor","34.693738,135.502165"));
                move(direct(anchor[0],anchor[1]),e,"anchor");
                send(e,1,0,false,anchor[0],anchor[1],"raid-anchor");
            }
            return;
        }
        if(e.phase==6&&e.move!=2&&e.fort!=null&&!e.fort.isEmpty()){
            move(direct(e.latitude,e.longitude),e,"gym_join");
            settleAfterMove(GYM_SETTLE_MS);
            send(e,2,e.move,true,0,0,"");
            return;
        }
        if(e.phase==2||e.phase==6){
            if(e.phase==6&&e.move==2){
                double[] anchor=KecleonFeed.parseCoordinates(settings.getString("raidAnchor","34.693738,135.502165"));
                send(e,1,0,false,anchor[0],anchor[1],"anchor");
                move(direct(anchor[0],anchor[1]),e,"capture_anchor");
                log.write("raid_capture_anchor",KecleonFeed.formatCoordinates(anchor[0],anchor[1]));
            }else move(KecleonMovement.points(e.latitude,e.longitude),e,e.phase==2?"candidate":"nudge");
            send(e,2,e.move,true,0,0,"");
        }else if(e.phase==4){
            move(KecleonMovement.points(e.latitude,e.longitude),e,"gym");
            send(e,3,0,true,0,0,"");
        }else if(e.phase==7){
            send(e,4,0,false,0,0,"");
        }
    }
    private void move(double[][] points,NativeKecleonWire.Event e,String reason) throws Exception {
        for(int i=0;i<points.length;i++){
            long waitUntil=SystemClock.elapsedRealtime()+15_000;
            while(!closed&&!active.getAsBoolean()&&SystemClock.elapsedRealtime()<waitUntil)Thread.sleep(50);
            if(!closed&&!active.getAsBoolean())throw new IOException("PoGO must be in foreground for raid GPS moves");
            if(closed)throw new InterruptedException("Stopped");
            IOException last=null;
            for(int attempt=0;attempt<GPS_RETRIES;attempt++){
                RootShell.Result value=joystick.execute(KecleonMovement.command(points[i][0],points[i][1]),5000);
                String lower=value.output.toLowerCase(Locale.ROOT);
                if(value.code==0&&KecleonMovement.teleportSucceeded(value.output)&&!lower.contains("exception")){
                    log.write("raid_move",new JSONObject().put("request",e.request).put("reason",reason).put("step",i).put("attempt",attempt+1)
                            .put("latitude",points[i][0]).put("longitude",points[i][1]).put("output",value.output.trim()).toString());
                    last=null;break;
                }
                last=new IOException("GPS Joystick teleport rejected (attempt "+(attempt+1)+"): "+value.output.trim());
                log.write("raid_move_retry",last.getMessage());
                Thread.sleep(400);
            }
            if(last!=null)throw last;
            if(i+1<points.length)Thread.sleep(KecleonMovement.STEP_MS);
        }
    }
    private int screenW,screenH;
    private void screenTap(double fx,double fy) throws Exception {
        if(screenW<=0){
            RootShell.Result size=joystick.execute("wm size",5000);
            java.util.regex.Matcher m=java.util.regex.Pattern.compile("(\\d+)x(\\d+)").matcher(size.output);
            String last=null;while(m.find())last=m.group(0);
            if(last!=null){String[] p=last.split("x");screenW=Integer.parseInt(p[0]);screenH=Integer.parseInt(p[1]);}else{screenW=1080;screenH=1920;}
        }
        int x=(int)(screenW*fx),y=(int)(screenH*fy);
        RootShell.Result r=joystick.execute("input tap "+x+" "+y,5000);
        log.write("raid_screen_tap","x="+x+" y="+y+" code="+r.code);
    }
    private static double[][] direct(double latitude,double longitude){return new double[][]{{latitude,longitude}};}
    private void settleAfterMove(long ms) throws InterruptedException {
        long until=SystemClock.elapsedRealtime()+ms;
        while(!closed&&SystemClock.elapsedRealtime()<until)Thread.sleep(100);
    }
    @Override public void close(){closed=true;joystick.close();}
}
