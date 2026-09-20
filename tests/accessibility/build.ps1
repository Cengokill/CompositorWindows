param([string]$BuildDirectory='build\release',[switch]$SourceOverrides,[switch]$DescriptionProbe)
$ErrorActionPreference='Stop'
$taskWindows=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
$taskBuild=Join-Path $taskWindows $BuildDirectory
[xml]$taskProject=Get-Content (Join-Path $taskBuild 'remembered_filter_tests.vcxproj')
$taskRelease=$taskProject.Project.ItemDefinitionGroup | Where-Object {$_.Condition -eq "'`$(Configuration)|`$(Platform)'=='Release|x64'"}
$taskLibs=@($taskRelease.Link.AdditionalDependencies.Split(';') | Where-Object {$_ -notmatch '%'} | ForEach-Object {if($_.StartsWith('Release\')){Join-Path $taskBuild $_}else{$_}})
$taskDirs=@($taskRelease.Link.AdditionalLibraryDirectories.Split(';') | Where-Object {$_ -notmatch '\$|%'} | ForEach-Object {"/LIBPATH:$_"})
$taskOutput=Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force $taskOutput | Out-Null
$taskInclude=@("/I$taskWindows/src","/I$taskWindows/dependencies/qt/include")
foreach($taskModule in @('QtCore','QtGui','QtWidgets','QtTest','QtConcurrent')){$taskInclude+="/I$taskWindows/dependencies/qt/include/$taskModule"}
$taskSources=@("$PSScriptRoot/AccessibilityWitness.cpp")
if($DescriptionProbe){$taskSources=@("$PSScriptRoot/DescriptionWitness.cpp")}
if($SourceOverrides){
    # Explicit objects replace only these three archive members in this isolated executable.
    $taskSources+=@("$taskWindows/src/ui/CanvasAccessibility.cpp","$taskWindows/src/ui/NativeCanvas.cpp","$taskWindows/src/ui/LayerPanel.cpp")
    if(-not (Select-String -LiteralPath "$taskWindows/src/ui/NativeCanvas.cpp" -SimpleMatch 'installCanvasAccessibility()' -Quiet)){throw 'Root must integrate the production constructor call before this probe.'}
}
& cl /nologo /std:c++20 /Zc:__cplusplus /EHsc /O2 /MD /W4 /WX /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DQT_WIDGETS_LIB /DQT_GUI_LIB /DQT_CORE_LIB /DQT_TESTLIB_LIB $taskInclude $taskSources "/Fo$taskOutput/" "/Fe$taskOutput/accessibility_witness.exe" /link $taskLibs $taskDirs uiautomationcore.lib ole32.lib oleaut32.lib
if($LASTEXITCODE){throw 'Accessibility witness compilation failed'}
# Use the same pinned DLL deployment as Release28. Never rely on PATH overriding System32.
& cmake "-DIMAGING_ROOT=$taskWindows/dependencies/imaging" "-DRUNTIME_DESTINATION=$taskOutput" -P "$taskWindows/scripts/deploy-imaging-runtime.cmake"
if($LASTEXITCODE){throw 'Pinned runtime deployment failed'}
$taskInputs=@("$PSScriptRoot/AccessibilityWitness.cpp","$PSScriptRoot/build.ps1","$taskOutput/accessibility_witness.exe")+$taskSources
$taskInputs+=Get-ChildItem -LiteralPath "$taskBuild/Release" -Filter 'compositor_*.lib' | ForEach-Object FullName
$taskInputs+=Get-ChildItem -LiteralPath "$taskWindows/src" -Recurse -File | ForEach-Object FullName
$taskManifest=@($taskInputs | ForEach-Object {[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}})
$taskManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$taskOutput/build-manifest.json" -Encoding utf8
[ordered]@{source_overrides=[bool]$SourceOverrides;compiled_sources=$taskSources;link_project="$taskBuild/remembered_filter_tests.vcxproj"} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$taskOutput/build-mode.json" -Encoding utf8
Write-Host 'Built only the isolated UIA witness; no GUI was launched.'
