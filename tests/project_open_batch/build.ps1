param([switch]$Before)
$ErrorActionPreference='Stop'
$taskWindows=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
$taskBuild=Join-Path $taskWindows 'build\release'
[xml]$taskProject=Get-Content (Join-Path $taskBuild 'remembered_filter_tests.vcxproj')
$taskRelease=$taskProject.Project.ItemDefinitionGroup | Where-Object {$_.Condition -eq "'`$(Configuration)|`$(Platform)'=='Release|x64'"}
$taskLibs=@($taskRelease.Link.AdditionalDependencies.Split(';') | Where-Object {$_ -notmatch '%'} | ForEach-Object {if($_.StartsWith('Release\')){Join-Path $taskBuild $_}else{$_}})
$taskDirs=@($taskRelease.Link.AdditionalLibraryDirectories.Split(';') | Where-Object {$_ -notmatch '\$|%'} | ForEach-Object {"/LIBPATH:$_"})
$taskOutput=Join-Path $PSScriptRoot $(if($Before){'before-bin'}else{'bin'})
New-Item -ItemType Directory -Force $taskOutput | Out-Null
$taskHeaderRoot=$(if($Before){Join-Path $PSScriptRoot 'baseline-headers'}else{Join-Path $taskWindows 'src'})
$taskInclude=@("/I$taskHeaderRoot","/I$taskWindows/dependencies/qt/include")
foreach($taskModule in @('QtCore','QtGui','QtWidgets','QtTest','QtConcurrent')){$taskInclude+="/I$taskWindows/dependencies/qt/include/$taskModule"}
$taskSources=@($(if($Before){"$PSScriptRoot/BeforeOpenBatchTests.cpp"}else{"$PSScriptRoot/ProjectOpenBatchTests.cpp"}))
& cl /nologo /std:c++20 /Zc:__cplusplus /EHsc /O2 /MD /W4 /WX /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DQT_WIDGETS_LIB /DQT_GUI_LIB /DQT_CORE_LIB /DQT_TESTLIB_LIB $taskInclude $taskSources "/Fo$taskOutput/" "/Fe$taskOutput/project_open_batch_tests.exe" /link $taskLibs $taskDirs
if($LASTEXITCODE){throw 'Project batch witness compilation failed'}
& cmake "-DIMAGING_ROOT=$taskWindows/dependencies/imaging" "-DRUNTIME_DESTINATION=$taskOutput" -P "$taskWindows/scripts/deploy-imaging-runtime.cmake"
if($LASTEXITCODE){throw 'Runtime deployment failed'}
$taskInputs=@($taskSources)+@("$PSScriptRoot/build.ps1","$taskOutput/project_open_batch_tests.exe")
$taskInputs+=Get-ChildItem -LiteralPath "$taskBuild/Release" -Filter 'compositor_*.lib' | ForEach-Object FullName
$taskInputs+=Get-ChildItem -LiteralPath $taskHeaderRoot -Recurse -File | ForEach-Object FullName
@($taskInputs | ForEach-Object {[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant()}}) | ConvertTo-Json -Depth 5 | Set-Content "$taskOutput/build-manifest.json" -Encoding utf8
[ordered]@{baseline_headers=$(if($Before){'c7cdaed9f42ab3d54154dc071cad90972e6168c5'}else{$null});compiled_sources=$taskSources} | ConvertTo-Json | Set-Content "$taskOutput/build-mode.json" -Encoding utf8
