package dev.rootcatch.ugphone;

import java.util.Locale;

public final class KecleonMovement {
    public static final long STEP_MS=350;
    static final double RADIUS_METERS=8;
    private static final String POGO="com.nianticlabs.pokemongo";
    private static final String TEST_PROVIDER="silkraid";

    public static double[][] points(double latitude,double longitude){
        if(!Double.isFinite(latitude)||!Double.isFinite(longitude)||Math.abs(latitude)>89||Math.abs(longitude)>180)
            throw new IllegalArgumentException("Invalid movement coordinates");
        double delta=Math.toDegrees(RADIUS_METERS/6371008.8),offset=delta/Math.cos(Math.toRadians(latitude)),east=longitude+offset,west=longitude-offset;
        if(east>180)east-=360;
        if(west< -180)west+=360;
        return new double[][]{{latitude,longitude},{latitude,east},{latitude+delta,longitude},
                {latitude,west},{latitude-delta,longitude},{latitude,longitude}};
    }

    /** Single shell line: tries GPS JoyStick, PGTools host broadcast, then cmd location test provider. */
    public static String command(double latitude,double longitude){
        if(!Double.isFinite(latitude)||!Double.isFinite(longitude)||Math.abs(latitude)>90||Math.abs(longitude)>180)
            throw new IllegalArgumentException("Invalid teleport coordinates");
        String coord=String.format(Locale.ROOT,"%.8f,%.8f",latitude,longitude);
        String joy=String.format(Locale.ROOT,
            "am start-foreground-service -n com.mbcfgkmhkdjx.cunpcbaavarqntr/.service.OverlayService -a theappninjas.gpsjoystick.TELEPORT --ef lat %.8f --ef lng %.8f"
            +" || am start-foreground-service -n com.theappninjas.gpsjoystick/.TeleportService -a theappninjas.gpsjoystick.TELEPORT --ef lat %.8f --ef lng %.8f"
            +" || am start-foreground-service -a theappninjas.gpsjoystick.TELEPORT --ef lat %.8f --ef lng %.8f",
            latitude,longitude,latitude,longitude,latitude,longitude);
        String pgTools=String.format(Locale.ROOT,
            "am broadcast -a net.pgtools.auto.host.BroadcastReceiver --es action TELEPORT --es coordinates %s"
            +" ; am broadcast -a net.pgtools.auto.host.BroadcastReceiver --es type TELEPORT --ef lat %.8f --ef lng %.8f",
            coord,latitude,longitude);
        String testLoc=String.format(Locale.ROOT,
            "appops set %s android:mock_location allow 2>/dev/null; appops set net.pgtools.auto android:mock_location allow 2>/dev/null;"
            +" cmd location providers add-test-provider %s 2>/dev/null;"
            +" cmd location providers set-test-provider-enabled %s true 2>/dev/null;"
            +" cmd location providers set-test-provider-location %s --location %s",
            POGO,TEST_PROVIDER,TEST_PROVIDER,TEST_PROVIDER,coord);
        return String.format(Locale.ROOT,
            "(%s && echo JOYSTICK_OK) || (%s && echo PGTOOLS_OK) || (%s && echo LOCATION_OK) || echo TELEPORT_FAIL",
            joy,pgTools,testLoc);
    }

    public static boolean teleportSucceeded(String output){
        if(output==null||output.isEmpty())return false;
        String lower=output.toLowerCase(Locale.ROOT);
        if(lower.contains("teleport_fail"))return false;
        return lower.contains("joystick_ok")||lower.contains("pgtools_ok")||lower.contains("location_ok")
            ||lower.contains("starting service: intent { act=theappninjas.gpsjoystick.teleport")
            ||lower.contains("broadcast completed");
    }

    private KecleonMovement(){ }
}
