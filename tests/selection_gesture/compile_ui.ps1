$ErrorActionPreference='Stop'
$taskWindows=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
$taskObjects=Join-Path $PSScriptRoot 'ui-objects'
New-Item -ItemType Directory -Path $taskObjects -Force | Out-Null
$taskSources='SelectionActions','KeyboardActions','NativeCanvas','ProjectActions','MainWindow','CommandActions','TransformActions','FileActions' | ForEach-Object {Join-Path $taskWindows "src/ui/$_.cpp"}
& cl /nologo /std:c++20 /Zc:__cplusplus /EHsc /O2 /MD /W4 /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN "/I$taskWindows/src" "/I$taskWindows/dependencies/qt/include" "/I$taskWindows/dependencies/qt/include/QtCore" "/I$taskWindows/dependencies/qt/include/QtGui" "/I$taskWindows/dependencies/qt/include/QtWidgets" "/I$taskWindows/dependencies/qt/include/QtTest" "/I$taskWindows/dependencies/qt/include/QtConcurrent" /c $taskSources "/Fo$taskObjects/"
if($LASTEXITCODE){throw 'Selection UI syntax compilation failed'}
