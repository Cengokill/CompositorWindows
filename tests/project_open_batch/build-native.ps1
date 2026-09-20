$ErrorActionPreference='Stop'
$taskWindows=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
$taskOutput=Join-Path $PSScriptRoot 'native-bin'
New-Item -ItemType Directory -Force $taskOutput | Out-Null
$taskInclude=@("/I$taskWindows/src","/I$taskWindows/dependencies/qt/include")
foreach($taskModule in @('QtCore','QtGui','QtWidgets')){$taskInclude+="/I$taskWindows/dependencies/qt/include/$taskModule"}
& cl /nologo /std:c++20 /Zc:__cplusplus /EHsc /O2 /MD /W4 /WX /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DQT_WIDGETS_LIB /DQT_GUI_LIB /DQT_CORE_LIB $taskInclude "$taskWindows/src/ui/ProjectOpenDialog.cpp" "$PSScriptRoot/NativeProjectDialogTests.cpp" "/Fo$taskOutput/" "/Fe$taskOutput/native_project_dialog_tests.exe" /link "/LIBPATH:$taskWindows/dependencies/qt/lib" Qt6Widgets.lib Qt6Gui.lib Qt6Core.lib ole32.lib shell32.lib user32.lib uuid.lib
if($LASTEXITCODE){throw 'Native project picker witness compilation failed'}
$taskInputs=@("$taskWindows/src/ui/ProjectOpenDialog.cpp","$taskWindows/src/ui/ProjectOpenDialog.h","$PSScriptRoot/NativeProjectDialogTests.cpp","$PSScriptRoot/build-native.ps1","$taskOutput/native_project_dialog_tests.exe")
@($taskInputs | ForEach-Object {[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant()}}) | ConvertTo-Json -Depth 5 | Set-Content "$taskOutput/build-manifest.json" -Encoding utf8
