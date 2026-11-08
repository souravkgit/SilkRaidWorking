param([string]$Ndk=$env:SILPH_NDK,[string]$ShadowHook=$env:SILPH_SHADOWHOOK)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$Ndk){
    if($env:ANDROID_NDK_HOME){$Ndk=$env:ANDROID_NDK_HOME}
    elseif($env:ANDROID_HOME){$Ndk=Join-Path $env:ANDROID_HOME 'ndk/28.2.13676358'}
    else{$Ndk='/opt/homebrew/share/android-ndk'}
}
$hostName=if($IsLinux){'linux-x86_64'}elseif($IsMacOS){'darwin-x86_64'}else{'windows-x86_64'}
$compiler=Join-Path $Ndk ('toolchains/llvm/prebuilt/'+$hostName+'/bin/clang'+$(if($hostName -eq 'windows-x86_64'){'.exe'}else{''}))
if(!(Test-Path -LiteralPath $compiler)){throw "Android NDK clang not found at $compiler"}
$revision='854c775c2c3676e57a0f383597ebf420b5204161'
if(!$ShadowHook){
    $ShadowHook=Join-Path $project '.dependencies/shadowhook'
    if(!(Test-Path -LiteralPath $ShadowHook)){
        & git clone --filter=blob:none --no-checkout https://github.com/bytedance/android-inline-hook.git $ShadowHook
        if($LASTEXITCODE -ne 0){throw 'Cannot obtain hook backend'}
        & git -C $ShadowHook checkout --detach $revision
        if($LASTEXITCODE -ne 0){throw 'Cannot select hook backend revision'}
    }
}
if((& git -C $ShadowHook rev-parse HEAD) -ne $revision){throw 'Unexpected hook backend revision'}
$source=Join-Path $project 'native'
$output=Join-Path $project 'app/src/main/jniLibs/arm64-v8a'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$flags=@('--target=aarch64-linux-android29','-std=c11','-O2','-Wall','-Wextra','-Werror','-fvisibility=hidden','-fstack-protector-strong','-D_FORTIFY_SOURCE=2','-Wl,-z,relro,-z,now,-z,noexecstack,-z,max-page-size=16384','-Wl,--no-undefined')
function Compile([string[]]$Arguments){& $compiler @Arguments;if($LASTEXITCODE -ne 0){throw 'Native compilation failed'}}
$hook=Join-Path $ShadowHook 'shadowhook/src/main/cpp'
$includes=@($hook,(Join-Path $hook 'include'),(Join-Path $hook 'arch/arm64'),(Join-Path $hook 'common'),(Join-Path $hook 'third_party/xdl'),(Join-Path $hook 'third_party/bsd'),(Join-Path $hook 'third_party/lss')) | ForEach-Object {'-I'+$_}
$hookSources=@(Get-ChildItem -LiteralPath $hook -Filter '*.c')+@(Get-ChildItem -LiteralPath (Join-Path $hook 'arch/arm64') -File | Where-Object Extension -In '.c','.S')+@(Get-ChildItem -LiteralPath (Join-Path $hook 'common') -Filter '*.c')+@(Get-ChildItem -Path (Join-Path $hook 'third_party/*/*.c'))
$hookFlags=@($flags | Where-Object {$_ -ne '-fvisibility=hidden'})
Compile ($hookFlags+$includes+@('-Wno-unused-command-line-argument','-fPIC','-shared','-Wl,-soname,libshadowhook.so','-Wl,-rpath,$ORIGIN','-Wl,--undefined-version',('-Wl,--version-script='+ (Join-Path $hook 'shadowhook.map.txt')))+@($hookSources.FullName)+@('-llog','-ldl','-o',(Join-Path $output 'libshadowhook.so')))
Compile ($flags+@('-fPIC','-shared','-Wl,-soname,libshadowhook_nothing.so',(Join-Path $hook 'nothing/sh_nothing.c'),'-o',(Join-Path $output 'libshadowhook_nothing.so')))
Compile ($flags+@('-fPIE','-pie',(Join-Path $source 'standalone/probe.c'),(Join-Path $source 'standalone/remote.c'),(Join-Path $source 'standalone/elf-symbol.c'),'-ldl','-o',(Join-Path $output 'libsilph-native-probe.so')))
$control=@('control-client.c','control.c','scheduler.c','session-wire.c','kecleon-wire.c') | ForEach-Object {Join-Path $source $_}
Compile ($flags+@('-DSILPH_EVENT_WIRE=1','-fPIE','-pie')+$control+@('-lm','-llog','-o',(Join-Path $output 'libsilph-control.so')))
$common=@('unity-pilot.c','managed.c','managed-read.c','game-read.c','scheduler.c','control.c','promise.c','managed-promise.c','farmer-actions.c','transfer-policy.c','rpc-actions.c','kecleon-actions.c','maintenance-actions.c','session-wire.c','session-link.c','kecleon-wire.c','kecleon-plan.c','visual-tools.c','research-policy.c','research-actions.c','location-tools.c','code-memory.c','presentation-tools.c','buddy-actions.c','recovery-policy.c','readback.c','core.c') | ForEach-Object {Join-Path $source $_}
$defines=@('-DSILPH_PILOT_FRAME=1','-DSILPH_PRESENT_UNWIND=1','-fexceptions','-DSILPH_PILOT_GAME_READ=1','-DSILPH_PILOT_EXTERNAL=1','-DSILPH_PILOT_ACTIONS=1','-DSILPH_ENABLE_ACTIONS=1','-DSILPH_PILOT_CONFIGURED=1','-DSILPH_PILOT_SERVICE=1','-DSILPH_PILOT_RAID=1','-DSILPH_PILOT_COMBINED=1')
$common+=Join-Path $source 'startup-thread.c'
$common+=Join-Path $source 'callback-memory.c'
$common+=Join-Path $source 'gift-actions.c'
$common+=Join-Path $source 'pass-actions.c'
$modeSources=@((Join-Path $source 'farmer-view.c'),(Join-Path $source 'raid-policy.c'),(Join-Path $source 'raid-session.c'),(Join-Path $source 'raid-flow.c'))
Compile ($flags+$defines+@('-fPIC','-shared','-Wl,-soname,libsilph-native-observer.so','-pthread',('-I'+(Join-Path $hook 'include')))+$common+$modeSources+@(('-L'+$output),'-lshadowhook','-ldl','-lm','-llog','-Wl,-rpath,$ORIGIN','-o',(Join-Path $output 'libsilph-raid-check.so')))
Compile ($flags+$defines+@('-fPIE','-pie',(Join-Path $source 'pilot-reader.c'),(Join-Path $source 'standalone/remote.c'),(Join-Path $source 'standalone/elf-symbol.c'),'-ldl','-o',(Join-Path $output 'libsilph-raid-status.so')))
Get-ChildItem -LiteralPath $output -File | Select-Object Name,Length
