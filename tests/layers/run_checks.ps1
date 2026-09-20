param([string]$BuildDirectory='')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $BuildDirectory){$BuildDirectory=Join-Path $root 'build/debug/Debug'}
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$out=Join-Path $PSScriptRoot 'results'
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $out
try {
    & cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MDd /Zi /utf-8 "/I$root/src" "$root/src/layers/LayerOperations.cpp" "$PSScriptRoot/layer_checks.cpp" "$BuildDirectory/compositor_core.lib" "$BuildDirectory/compositor_graphics.lib" ole32.lib /Fe:layer_checks.exe 2>&1 | Tee-Object build.log
    if($LASTEXITCODE){throw 'Layer compilation failed'}
    & ./layer_checks.exe 2>&1 | Tee-Object functional.log
    if($LASTEXITCODE){throw 'Layer functional checks failed'}
} finally {Pop-Location}
