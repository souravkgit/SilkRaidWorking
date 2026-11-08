package dev.silphmod.nativecheck;

import android.Manifest;
import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.os.*;
import android.text.InputType;
import android.view.View;
import android.widget.*;
import dev.rootcatch.automation.ThrowQuality;
import dev.rootcatch.nativeengine.*;
import dev.rootcatch.ugphone.KecleonFeed;
import java.util.*;

public final class MainActivity extends Activity {
    private static final int MODE=1;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private NativeSettings settings;
    private ModeUi ui;
    private TextView status,details,elapsed;
    private final TextView[] metrics=new TextView[6];
    private Button start,export,clear;
    private String inputError="";
    private final ArrayList<View> edits=new ArrayList<>();
    private final Runnable refresh=new Runnable(){public void run(){render();handler.postDelayed(this,750);}};
    @Override public void onCreate(Bundle state){
        super.onCreate(state);settings=new NativeSettings(this);
        getWindow().setStatusBarColor(ModeUi.BG);getWindow().setNavigationBarColor(ModeUi.BG);
        show();
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED)requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},10);
    }
    @Override protected void onResume(){super.onResume();handler.removeCallbacks(refresh);handler.post(refresh);}
    @Override protected void onPause(){handler.removeCallbacks(refresh);super.onPause();}
    private void show(){
        edits.clear();inputError="";ui=new ModeUi(this);status=null;start=null;
        ui.title("SilkRaid",BuildConfig.VERSION_NAME);
        status=ui.text(CheckService.view.status,17,ModeUi.ACCENT);ui.page.addView(status);
        elapsed=ui.text("00:00:00",16,ModeUi.MUTED);ui.page.addView(elapsed);
        details=ui.text("",13,ModeUi.MUTED);ui.page.addView(details);
        String[] labels={"Raids completed","Boss catches","Shinies","Items deleted","XP","Stardust"};
        for(int row=0;row<3;row++){LinearLayout line=ui.row();ui.page.addView(line);for(int col=0;col<2;col++)metrics[row*2+col]=ui.metric(line,labels[row*2+col]);}
        clear=ui.button(ui.page,"Clear statistics",false);clear.setOnClickListener(v->{if(CheckService.view.running)return;
            inputError="";settings.save(MODE,new NativeSessionState.Totals());settings.prefs.edit().putLong("stats"+MODE+".elapsed",0).apply();render();});ui.space(ui.page,16);
        LinearLayout area=ui.card("Search area");
        area.addView(ui.text("Teleport to the anchor, then scan gyms within the radius.",14,ModeUi.MUTED));
        setting(area,"Anchor coordinate",settings.prefs.getString("raidAnchor","34.693738,135.502165")).setOnClickListener(v->coordDialog());
        setting(area,"Radius (meters)",Integer.toString(settings.prefs.getInt("raidRadius",500))).setOnClickListener(v->intDialog("raidRadius","Radius (meters)",1,5000,500));
        check(area,"Prefer hot gyms worldwide","raidGlobalHot",false);
        LinearLayout raid=ui.card("Raid options");
        setting(raid,"Raid tiers",tierSummary()).setOnClickListener(v->tierDialog());
        setting(raid,"Raid passes",passLabel(settings.passMask())).setOnClickListener(v->passDialog());
        check(raid,"Hot gym selection (skip gyms with less than half the minimum players)","raidHotLobby",true);
        setting(raid,"Team (new accounts)",TEAM_LABELS[Math.max(1,Math.min(3,settings.prefs.getInt("raidTeam",1)))-1]).setOnClickListener(v->teamDialog());
        String[] minTiers={"1★","3★","5★","7★"};
        for(int i=0;i<4;i++){int slot=i;setting(raid,"Minimum lobby players · "+minTiers[i]+" raid",settings.minPlayers(i)+" players").setOnClickListener(v->intDialog(NativeSettings.MIN_PLAYER_KEYS[slot],"Minimum players · "+minTiers[slot]+" raid (leave lobby if fewer)",0,20,NativeSettings.MIN_PLAYER_DEFAULTS[slot]));}
        toggle(raid,"Catch raid boss","catchBoss",true);
        setting(raid,"Raid boss (only these)","Only raid the selected boss species").setOnClickListener(v->new PokemonPickerDialog(this,settings.prefs.getString("raidBossSpeciesList","")).show(s->{settings.prefs.edit().putString("raidBossSpeciesList",s).apply();show();}));
        check(raid,"Use recommended team","recommendedTeam",true);
        LinearLayout record=ui.card("Learn from your raid");
        record.addView(ui.text("Opens Pokémon GO with Fix error 12, teleports to the anchor, and records logcat while you play one raid manually. Stop the service when finished, then export logs.",14,ModeUi.MUTED));
        Button recordRaid=ui.button(record,"Record manual raid",true);edits.add(recordRaid);recordRaid.setOnClickListener(v->startRecord());
        LinearLayout battle=ui.card("Battle & travel");
        check(battle,"Fix error 12","fixLocation",true);
        toggle(battle,"Curve ball","curve",true);
        CompoundButton berries=toggle(battle,"Use berries","berries",true);
        LinearLayout berryOptions=ui.column();battle.addView(berryOptions);
        setting(berryOptions,"Berry priority",settings.berryNames().replace(","," · ")).setOnClickListener(v->new SettingsDialogs(this).berriesDialog(settings.berryNames(),value->{settings.saveBerries(value);show();}));
        berries.setOnCheckedChangeListener((v,on)->{settings.prefs.edit().putBoolean("berries",on).apply();berryOptions.setVisibility(on?View.VISIBLE:View.GONE);});
        berryOptions.setVisibility(berries.isChecked()?View.VISIBLE:View.GONE);
        check(battle,"Hide trainer name","hideTrainerName",true);
        int q=Math.max(1,Math.min(4,settings.prefs.getInt("quality",4)));
        setting(battle,"Throw quality",ThrowQuality.values()[q-1].label).setOnClickListener(v->new SettingsDialogs(this).throwsDialog(ThrowQuality.values()[q-1],value->{settings.prefs.edit().putInt("quality",value.ordinal()+1).apply();show();}));
        LinearLayout care=ui.card("Team care");
        setting(care,"Potion priority",medicineSummary(false)).setOnClickListener(v->new SettingsDialogs(this).medicineDialog("Potion priority",settings.prefs.getString("raidPotionOrder",""),false,value->{settings.prefs.edit().putString("raidPotionOrder",value).apply();show();}));
        setting(care,"Revive priority",medicineSummary(true)).setOnClickListener(v->new SettingsDialogs(this).medicineDialog("Revive priority",settings.prefs.getString("raidReviveOrder",""),true,value->{settings.prefs.edit().putString("raidReviveOrder",value).apply();show();}));
        Button engine=ui.button(ui.page,"Engine settings",false);edits.add(engine);engine.setOnClickListener(v->engineDialog());
        serviceControls();ui.show();render();
    }
    private void startRecord(){
        if(CheckService.view.running){startService(new Intent(this,CheckService.class).setAction(CheckService.STOP));return;}
        inputError="";openGame();
        startForegroundService(new Intent(this,CheckService.class).setAction(CheckService.START).putExtra("mode",MODE).putExtra(NativeManualRecord.EXTRA,true));
        Toast.makeText(this,"Recording started. Use Stop in the notification or return here when done.",Toast.LENGTH_LONG).show();
    }
    private void serviceControls(){
        start=ui.button(ui.controls,"Start service",true);start.setOnClickListener(v->{if(CheckService.view.running){startService(new Intent(this,CheckService.class).setAction(CheckService.STOP));return;}
            try{inputError="";settings.config(MODE,false);openGame();
                startForegroundService(new Intent(this,CheckService.class).setAction(CheckService.START).putExtra("mode",MODE));finish();}
            catch(Exception error){inputError=error.getMessage();status.setText(inputError);Toast.makeText(this,inputError,Toast.LENGTH_LONG).show();}
        });
        export=ui.button(ui.controls,"Export logs",false);export.setOnClickListener(v->export());
    }
    private void render(){
        if(status==null)return;CheckService.View value=CheckService.view;var t=settings.totals(MODE);
        set(status,value.running||inputError.isEmpty()?value.status:inputError);set(details,value.running?value.details:"");
        long[] values={t.spun,t.caught,t.shinies,t.discarded,t.xp,t.stardust};
        long seconds=settings.elapsed(MODE)/1000;set(elapsed,String.format(Locale.US,"%02d:%02d:%02d",seconds/3600,seconds/60%60,seconds%60));
        for(int i=0;i<6;i++)set(metrics[i],String.format(Locale.US,"%,d",values[i]));
        set(start,value.running?"Stop service":"Start service");clear.setEnabled(!value.running);
        if(value.running&&value.details.startsWith("Manual raid"))set(start,"Stop recording");
        for(View edit:edits)edit.setEnabled(!value.running);
    }
    private static void set(TextView view,String value){if(!view.getText().toString().equals(value))view.setText(value);}
    private void check(LinearLayout parent,String title,String key,boolean fallback){CheckBox b=ui.check(parent,title,settings.prefs.getBoolean(key,fallback));edits.add(b);b.setOnCheckedChangeListener((v,on)->settings.prefs.edit().putBoolean(key,on).apply());}
    private CompoundButton toggle(LinearLayout parent,String title,String key,boolean fallback){
        Switch b=ui.toggle(parent,title,settings.prefs.getBoolean(key,fallback));edits.add(b);b.setOnCheckedChangeListener((v,on)->settings.prefs.edit().putBoolean(key,on).apply());return b;
    }
    private Button setting(LinearLayout parent,String label,String value){Button b=ui.button(parent,label+"\n"+value,false);edits.add(b);return b;}
    private String tierSummary(){
        int mask=settings.prefs.getInt("raidTierMask",127);if(mask==127)return "All tiers";
        String[] names={"1★","2★","3★","4★","5★","Mega","Elite"};ArrayList<String> out=new ArrayList<>();
        for(int i=0;i<names.length;i++)if((mask&(1<<i))!=0)out.add(names[i]);return String.join(", ",out);
    }
    private static String passLabel(int mask){ArrayList<String> names=new ArrayList<>();if((mask&1)!=0)names.add("Free");if((mask&2)!=0)names.add("Premium");if((mask&4)!=0)names.add("Remote");return String.join(" · ",names);}
    private String medicineSummary(boolean revives){int[] ids=RocketCareOrder.parse(settings.prefs.getString(revives?"raidReviveOrder":"raidPotionOrder",""),revives);if(ids.length==0)return "Automatic";ArrayList<String> names=new ArrayList<>();for(int id:ids)for(ItemCatalog.Entry item:ItemCatalog.ENTRIES)if(item.id()==id)names.add(item.label());return String.join(" · ",names);}
    private void export(){export.setEnabled(false);export.setText("Exporting…");new Thread(()->{String result;try{CheckLog.export(this);result="Saved to Downloads/SilkRaid";}catch(Exception error){result=error.getMessage();}String message=result;runOnUiThread(()->{export.setEnabled(true);export.setText("Export logs");Toast.makeText(this,message==null?"Export failed":message,Toast.LENGTH_LONG).show();});},"silkraid-export").start();}
    private void coordDialog(){
        Dialog dialog=new Dialog(this,R.style.SettingsDialogTheme);LinearLayout page=ui.column();page.setPadding(ui.dp(22),ui.dp(20),ui.dp(22),ui.dp(20));page.setBackgroundColor(ModeUi.BG);
        page.addView(ui.text("Anchor coordinate",23,ModeUi.TEXT));EditText edit=new EditText(this);edit.setSingleLine(true);edit.setTextColor(ModeUi.TEXT);edit.setText(settings.prefs.getString("raidAnchor","34.693738,135.502165"));page.addView(edit);
        ui.button(page,"Save",true).setOnClickListener(v->{try{KecleonFeed.parseCoordinates(edit.getText().toString().trim());settings.prefs.edit().putString("raidAnchor",edit.getText().toString().trim()).apply();dialog.dismiss();show();}catch(Exception error){Toast.makeText(this,"Invalid coordinate",Toast.LENGTH_SHORT).show();}});
        ui.button(page,"Cancel",false).setOnClickListener(v->dialog.dismiss());dialog.setContentView(page);dialog.show();
    }
    private void intDialog(String key,String title,int min,int max,int fallback){
        Dialog dialog=new Dialog(this,R.style.SettingsDialogTheme);LinearLayout page=ui.column();page.setPadding(ui.dp(22),ui.dp(20),ui.dp(22),ui.dp(20));page.setBackgroundColor(ModeUi.BG);
        page.addView(ui.text(title,23,ModeUi.TEXT));EditText edit=new EditText(this);edit.setInputType(InputType.TYPE_CLASS_NUMBER);edit.setTextColor(ModeUi.TEXT);edit.setText(Integer.toString(settings.prefs.getInt(key,fallback)));page.addView(edit);
        ui.button(page,"Save",true).setOnClickListener(v->{try{int n=Integer.parseInt(edit.getText().toString());if(n<min||n>max)throw new IllegalArgumentException();settings.prefs.edit().putInt(key,n).apply();dialog.dismiss();show();}catch(Exception error){Toast.makeText(this,"Invalid value",Toast.LENGTH_SHORT).show();}});
        ui.button(page,"Cancel",false).setOnClickListener(v->dialog.dismiss());dialog.setContentView(page);dialog.show();
    }
    private void tierDialog(){
        Dialog dialog=new Dialog(this,R.style.SettingsDialogTheme);LinearLayout page=ui.column();page.setPadding(ui.dp(22),ui.dp(20),ui.dp(22),ui.dp(20));page.setBackgroundColor(ModeUi.BG);
        page.addView(ui.text("Raid tiers",23,ModeUi.TEXT));int mask=settings.prefs.getInt("raidTierMask",127);String[] names={"1★","2★","3★","4★","5★","Mega","Elite"};CheckBox[] boxes=new CheckBox[names.length];
        for(int i=0;i<names.length;i++){boxes[i]=ui.check(page,names[i],(mask&(1<<i))!=0);edits.add(boxes[i]);}
        ui.button(page,"Save",true).setOnClickListener(v->{int next=0;for(int i=0;i<boxes.length;i++)if(boxes[i].isChecked())next|=(1<<i);if(next==0){Toast.makeText(this,"Select at least one tier",Toast.LENGTH_SHORT).show();return;}settings.prefs.edit().putInt("raidTierMask",next).apply();dialog.dismiss();show();});
        ui.button(page,"Cancel",false).setOnClickListener(v->dialog.dismiss());dialog.setContentView(page);dialog.show();
    }
    private static final String[] TEAM_LABELS={"Team Mystic (Blue) · Blanche · Articuno","Team Valor (Red) · Candela · Moltres","Team Instinct (Yellow) · Spark · Zapdos"};
    private void teamDialog(){
        Dialog dialog=new Dialog(this,R.style.SettingsDialogTheme);LinearLayout page=ui.column();page.setPadding(ui.dp(22),ui.dp(20),ui.dp(22),ui.dp(20));page.setBackgroundColor(ModeUi.BG);
        page.addView(ui.text("Team",23,ModeUi.TEXT));page.addView(ui.text("Picked automatically when a level 5+ account has no team yet",14,ModeUi.TEXT));int current=settings.prefs.getInt("raidTeam",1);
        for(int i=0;i<3;i++){int id=i+1;RadioButton b=new RadioButton(this);b.setText(TEAM_LABELS[i]);b.setTextColor(ModeUi.TEXT);b.setChecked(id==current);b.setOnClickListener(v->settings.prefs.edit().putInt("raidTeam",id).apply());page.addView(b);}
        ui.button(page,"Done",true).setOnClickListener(v->{dialog.dismiss();show();});dialog.setContentView(page);dialog.show();
    }
    private void passDialog(){
        Dialog dialog=new Dialog(this,R.style.SettingsDialogTheme);LinearLayout page=ui.column();page.setPadding(ui.dp(22),ui.dp(20),ui.dp(22),ui.dp(20));page.setBackgroundColor(ModeUi.BG);
        page.addView(ui.text("Raid passes",23,ModeUi.TEXT));
        page.addView(ui.text("Only the selected passes are used. If none of them can be used, the bot stops. With only the free pass selected it stops after the free raid.",14,ModeUi.TEXT));
        int mask=settings.passMask();String[] labels={"Daily free raid pass","Premium battle pass","Remote raid pass (not supported yet)"};CheckBox[] boxes=new CheckBox[labels.length];
        for(int i=0;i<labels.length;i++){CheckBox b=new CheckBox(this);b.setText(labels[i]);b.setTextColor(ModeUi.TEXT);b.setChecked((mask&(1<<i))!=0);boxes[i]=b;page.addView(b);}
        ui.button(page,"Save",true).setOnClickListener(v->{int next=0;for(int i=0;i<boxes.length;i++)if(boxes[i].isChecked())next|=(1<<i);if(next==0){Toast.makeText(this,"Select at least one pass",Toast.LENGTH_SHORT).show();return;}settings.prefs.edit().putInt("raidPassMask",next).apply();dialog.dismiss();show();});
        dialog.setContentView(page);dialog.show();
    }
    private void engineDialog(){
        Dialog dialog=new Dialog(this,R.style.SettingsDialogTheme);LinearLayout page=ui.column();page.setPadding(ui.dp(22),ui.dp(20),ui.dp(22),ui.dp(20));page.setBackgroundColor(ModeUi.BG);
        page.addView(ui.text("Engine settings",23,ModeUi.TEXT));LinkedHashMap<String,EditText> inputs=new LinkedHashMap<>();
        input(page,inputs,"delay","Action interval (ms)",150);input(page,inputs,"raidsMax","Maximum raids",100);
        ui.button(page,"Save",true).setOnClickListener(v->{try{var editor=settings.prefs.edit();for(var pair:inputs.entrySet()){int n=Integer.parseInt(pair.getValue().getText().toString());if(pair.getKey().equals("delay")?(n<100||n>3000):(n<1||n>50000))throw new IllegalArgumentException("Invalid value");editor.putInt(pair.getKey(),n);}editor.apply();inputError="";dialog.dismiss();}catch(Exception error){Toast.makeText(this,"Invalid value",Toast.LENGTH_SHORT).show();}});
        ui.button(page,"Cancel",false).setOnClickListener(v->dialog.dismiss());dialog.setContentView(page);dialog.show();
    }
    private void input(LinearLayout page,Map<String,EditText> inputs,String key,String title,int fallback){page.addView(ui.text(title,14,ModeUi.MUTED));EditText edit=new EditText(this);edit.setSingleLine(true);edit.setInputType(InputType.TYPE_CLASS_NUMBER);edit.setTextColor(ModeUi.TEXT);edit.setText(Integer.toString(settings.prefs.getInt(key,fallback)));page.addView(edit);inputs.put(key,edit);}
    private void openGame(){
        Intent launch=getPackageManager().getLaunchIntentForPackage("com.nianticlabs.pokemongo");
        if(launch==null)return;launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);startActivity(launch);
    }
}
