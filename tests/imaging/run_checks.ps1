$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$out=Join-Path $root 'evidence/imaging'
$manifest=Get-Content (Join-Path $out 'fixture-manifest.json') -Raw | ConvertFrom-Json
foreach($fixture in $manifest.required){
    $path=Join-Path $out $fixture.file
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Required fixture missing: $path"}
    if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $fixture.sha256){throw "Fixture hash changed: $path"}
}
Push-Location $out
try {
    & cl /nologo /std:c++20 /EHsc /W4 /O2 /MD /utf-8 /DUNICODE /D_UNICODE "/I$root/src/imaging" "$root/src/imaging/wic_codec.cpp" "$root/src/imaging/project_png.cpp" "$root/src/imaging/subject_matte.cpp" "$PSScriptRoot/imaging_checks.cpp" windowscodecs.lib ole32.lib oleaut32.lib propsys.lib /Fe:imaging_checks.exe 2>&1 | Tee-Object build-core.log
    if($LASTEXITCODE){throw 'Imaging compilation failed'}
    & ./imaging_checks.exe $out 2>&1 | Tee-Object test-core.log
    if($LASTEXITCODE){throw 'Imaging checks failed'}
} finally {Pop-Location}
