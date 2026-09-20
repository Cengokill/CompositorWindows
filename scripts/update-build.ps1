$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$out=Join-Path $root 'tests/update/bin'
New-Item -ItemType Directory -Force $out | Out-Null
$qt=Join-Path $root 'dependencies/qt'
Push-Location $out
try {
    $flags=@('/nologo','/std:c++20','/EHsc','/W4','/WX','/O2','/MD','/utf-8','/Zc:__cplusplus','/permissive-',"/I$qt/include","/I$qt/include/QtCore","/I$root/src")
    & cl @flags /c "$root/src/update/Updater.cpp" /Fo:Updater.obj 2>&1 | Tee-Object build-core.log
    if($LASTEXITCODE){throw 'Updater core compilation failed'}
    & cl @flags "$root/src/update/UpdaterMain.cpp" Updater.obj "$qt/lib/Qt6Core.lib" bcrypt.lib winhttp.lib /Fe:CompositorUpdater.exe 2>&1 | Tee-Object build-helper.log
    if($LASTEXITCODE){throw 'Updater helper compilation failed'}
    & cl @flags "$root/src/update/LauncherMain.cpp" Updater.obj "$qt/lib/Qt6Core.lib" bcrypt.lib winhttp.lib shell32.lib user32.lib /Fe:CompositorLauncher.exe /link /SUBSYSTEM:WINDOWS 2>&1 | Tee-Object build-launcher.log
    if($LASTEXITCODE){throw 'Updater launcher compilation failed'}
    & cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT "$root/tests/update/FixtureApp.cpp" /Fe:FixtureApp.exe 2>&1 | Tee-Object build-fixture.log
    if($LASTEXITCODE){throw 'Updater fixture compilation failed'}
    & cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /DHEALTH_RESULT=7 "$root/tests/update/FixtureApp.cpp" /Fe:UnhealthyApp.exe 2>&1 | Tee-Object build-unhealthy.log
    if($LASTEXITCODE){throw 'Updater failing fixture compilation failed'}
    Copy-Item -LiteralPath (Join-Path $qt 'bin/Qt6Core.dll') -Destination $out -Force
} finally {Pop-Location}
