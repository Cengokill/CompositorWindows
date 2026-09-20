$ErrorActionPreference = 'Stop'
$taskWindows = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
$taskBuild = Join-Path $PSScriptRoot 'build'
$taskQt = Join-Path $taskWindows 'dependencies\qt'
& cmake -S $PSScriptRoot -B $taskBuild -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$taskQt" '-DCMAKE_SYSTEM_VERSION=10.0.26100.0'
if ($LASTEXITCODE) { throw 'Cache configure failed' }
& cmake --build $taskBuild --config Debug
if ($LASTEXITCODE) { throw 'Cache build failed' }
& ctest --test-dir $taskBuild -C Debug --output-on-failure --output-junit (Join-Path $PSScriptRoot 'results.xml')
if ($LASTEXITCODE) { throw 'Cache tests failed' }
