$ErrorActionPreference='Stop'
$taskWindows=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
$taskBuild=Join-Path $PSScriptRoot 'build'
& cmake -S $PSScriptRoot -B $taskBuild -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_SYSTEM_VERSION=10.0.26100.0' "-DCMAKE_PREFIX_PATH=$taskWindows/dependencies/qt"
if($LASTEXITCODE){throw 'Command registry configure failed'}
& cmake --build $taskBuild --config Release
if($LASTEXITCODE){throw 'Command registry build failed'}
& ctest --test-dir $taskBuild -C Release --output-on-failure --output-junit (Join-Path $PSScriptRoot 'results.xml')
if($LASTEXITCODE){throw 'Command registry tests failed'}
