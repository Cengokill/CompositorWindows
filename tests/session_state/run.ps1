param([switch]$Build)
$ErrorActionPreference='Stop'
$taskWindows=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
$taskBuild=Join-Path $taskWindows 'build\release'
if($Build){
    & cmake --build $taskBuild --config Release --target session_state_tests
    if($LASTEXITCODE){throw 'Session state build failed'}
}
& ctest --test-dir $taskBuild -C Release -R '^session_state\.' --output-on-failure --output-junit (Join-Path $PSScriptRoot 'results.xml')
if($LASTEXITCODE){throw 'Session state checks failed'}
