$ErrorActionPreference='Stop'
$taskWindows=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
$taskBuild=Join-Path $taskWindows 'build\release'
[xml]$taskProject=Get-Content (Join-Path $taskBuild 'layer_panel_tests.vcxproj')
$taskRelease=$taskProject.Project.ItemDefinitionGroup | Where-Object {$_.Condition -eq "'`$(Configuration)|`$(Platform)'=='Release|x64'"}
$taskLibs=$taskRelease.Link.AdditionalDependencies.Split(';') | ForEach-Object {if($_.StartsWith('Release\')){Join-Path $taskBuild $_}else{$_}}
$taskDirs=$taskRelease.Link.AdditionalLibraryDirectories.Split(';') | Where-Object {$_ -notmatch '\$|%'} | ForEach-Object {"/LIBPATH:$_"}
& cl /nologo /std:c++20 /Zc:__cplusplus /EHsc /O2 /MD /W4 /WX /utf-8 /DQT_WIDGETS_LIB /DQT_GUI_LIB /DQT_CORE_LIB /DNOMINMAX /DWIN32_LEAN_AND_MEAN "/I$taskWindows/src" "/I$taskWindows/dependencies/qt/include" "/I$taskWindows/dependencies/qt/include/QtCore" "/I$taskWindows/dependencies/qt/include/QtGui" "/I$taskWindows/dependencies/qt/include/QtWidgets" "/I$taskWindows/dependencies/qt/include/QtTest" "$PSScriptRoot/SelectionFollowupTests.cpp" "/Fo$PSScriptRoot/SelectionFollowupTests-before.obj" "/Fe$PSScriptRoot/selection_followup_before.exe" /link $taskLibs $taskDirs
if($LASTEXITCODE){throw 'Before event probe compilation failed'}
$env:QT_QPA_PLATFORM='offscreen'
$env:PATH="$taskWindows/dependencies/imaging/install/bin;$taskWindows/dependencies/imaging/onnxruntime-win-x64-1.30.0/lib;$env:PATH"
foreach($taskCase in 'undo_redo_draft','escape_mode_reset','expand_range','wand_replace'){
 & "$PSScriptRoot/selection_followup_before.exe" $taskCase
 Write-Output "EXIT $taskCase $LASTEXITCODE"
}
