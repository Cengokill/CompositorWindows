param([switch]$SourceOverride)
$ErrorActionPreference='Stop'
$taskWindows=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
$taskBuild=Join-Path $taskWindows 'build\release'
[xml]$taskProject=Get-Content (Join-Path $taskBuild 'remembered_filter_tests.vcxproj')
$taskRelease=$taskProject.Project.ItemDefinitionGroup | Where-Object {$_.Condition -eq "'`$(Configuration)|`$(Platform)'=='Release|x64'"}
$taskLibs=@($taskRelease.Link.AdditionalDependencies.Split(';') | Where-Object {$_ -notmatch '%'} | ForEach-Object {if($_.StartsWith('Release\')){Join-Path $taskBuild $_}else{$_}})
$taskDirs=@($taskRelease.Link.AdditionalLibraryDirectories.Split(';') | Where-Object {$_ -notmatch '\$|%'} | ForEach-Object {"/LIBPATH:$_"})
$taskOutput=Join-Path $PSScriptRoot 'bin';New-Item -ItemType Directory -Force $taskOutput | Out-Null
$taskInclude=@("/I$taskWindows/src","/I$taskWindows/dependencies/qt/include")
foreach($taskModule in @('QtCore','QtGui','QtWidgets','QtTest','QtConcurrent')){$taskInclude+="/I$taskWindows/dependencies/qt/include/$taskModule"}
$taskSources=@("$PSScriptRoot/ProjectReopenTests.cpp")
if($SourceOverride){$taskSources+="$taskWindows/src/ui/FileActions.cpp"}
& cl /nologo /std:c++20 /Zc:__cplusplus /EHsc /O2 /MD /W4 /WX /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DQT_WIDGETS_LIB /DQT_GUI_LIB /DQT_CORE_LIB /DQT_TESTLIB_LIB $taskInclude $taskSources "/Fo$taskOutput/" "/Fe$taskOutput/project_reopen_tests.exe" /link $taskLibs $taskDirs
if($LASTEXITCODE){throw 'Project reopen witness compilation failed'}
& cmake "-DIMAGING_ROOT=$taskWindows/dependencies/imaging" "-DRUNTIME_DESTINATION=$taskOutput" -P "$taskWindows/scripts/deploy-imaging-runtime.cmake"
if($LASTEXITCODE){throw 'Runtime deployment failed'}
$taskInputs=@("$PSScriptRoot/ProjectReopenTests.cpp","$PSScriptRoot/build.ps1","$taskOutput/project_reopen_tests.exe")
$taskInputs+=Get-ChildItem -LiteralPath "$taskBuild/Release" -Filter 'compositor_*.lib' | ForEach-Object FullName
$taskInputs+=Get-ChildItem -LiteralPath "$taskWindows/src" -Recurse -File | ForEach-Object FullName
@($taskInputs | ForEach-Object {[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant()}}) | ConvertTo-Json -Depth 5 | Set-Content "$taskOutput/build-manifest.json" -Encoding utf8
[ordered]@{file_actions_override=[bool]$SourceOverride;compiled_sources=$taskSources} | ConvertTo-Json | Set-Content "$taskOutput/build-mode.json" -Encoding utf8
