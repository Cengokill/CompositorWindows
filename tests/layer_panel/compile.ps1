$ErrorActionPreference='Stop'
$taskWindows=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
& cl /nologo /std:c++20 /Zc:__cplusplus /EHsc /O2 /W4 /WX /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN "/I$taskWindows/src" "/I$taskWindows/dependencies/qt/include" "/I$taskWindows/dependencies/qt/include/QtCore" "/I$taskWindows/dependencies/qt/include/QtGui" "/I$taskWindows/dependencies/qt/include/QtWidgets" "/I$taskWindows/dependencies/qt/include/QtTest" /c "$taskWindows/src/ui/LayerPanel.cpp" "$PSScriptRoot/LayerPanelSourceTests.cpp" "/Fo$PSScriptRoot/"
if($LASTEXITCODE){throw 'LayerPanel compilation failed'}
