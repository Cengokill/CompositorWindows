param([string]$BuildDirectory='')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $BuildDirectory){$BuildDirectory=Join-Path $root 'build/debug/Debug'}
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$out=Join-Path $PSScriptRoot 'results'
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $out
try {
    & cl /nologo /std:c++20 /EHsc /W4 /O2 /MDd /Zi /utf-8 "/I$root/src" "$root/src/filters/PixelFilters.cpp" "$PSScriptRoot/filter_checks.cpp" "$BuildDirectory/compositor_core.lib" "$BuildDirectory/compositor_graphics.lib" ole32.lib /Fe:filter_checks.exe 2>&1 | Tee-Object build.log
    if($LASTEXITCODE){throw 'Filter compilation failed'}
    & ./filter_checks.exe 2>&1 | Tee-Object functional.log
    if($LASTEXITCODE){throw 'Filter functional checks failed'}
    & ./filter_checks.exe --benchmark 2>&1 | Tee-Object benchmark.log
    if($LASTEXITCODE){throw 'Filter benchmark failed'}
    # A separate real failing parity invocation, retained as failure evidence.
    # Do not convert the known source assertion conflict into a successful test.
    & ./filter_checks.exe --source-parity 2>&1 | Tee-Object source-parity.log
    $parityExit=$LASTEXITCODE
    @{functional='pass';source_parity=if($parityExit){'fail'}else{'pass'};source_parity_exit=$parityExit;mac_reference='not_run';baseline='a19db9011282399785dc18efcfded904627bdcc2'} | ConvertTo-Json | Set-Content -Encoding utf8 results.json
    Write-Host "Functional checks passed. Source parity exit: $parityExit (see source-parity.log)."
} finally {Pop-Location}
