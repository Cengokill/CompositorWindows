param([string]$EvidenceDirectory)
$ErrorActionPreference='Stop'
$taskWindowsRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $EvidenceDirectory){$EvidenceDirectory=Join-Path $taskWindowsRoot 'evidence/graphics/distortion-01'}
New-Item -ItemType Directory -Force $EvidenceDirectory | Out-Null
$taskEvidence=(Resolve-Path $EvidenceDirectory).Path
& 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$taskSources=@('src/editing_transform/TransformGeometry.cpp','src/editing_transform/Distortion.cpp','tests/editing_transform/DistortionTests.cpp','src/core/Document.cpp')
$taskObjects=@()
foreach($taskRelative in $taskSources){
    $taskName=[IO.Path]::GetFileNameWithoutExtension($taskRelative)
    $taskObject=Join-Path $taskEvidence ($taskName+'.obj')
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /O2 /MD /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /c "/I$(Join-Path $taskWindowsRoot 'src')" (Join-Path $taskWindowsRoot $taskRelative) "/Fo$taskObject" 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence ($taskName+'.build.log'))
    if($LASTEXITCODE){throw "Distortion compilation failed: $taskRelative"}
    $taskObjects+=$taskObject
}
& link.exe /nologo $taskObjects ole32.lib "/OUT:$(Join-Path $taskEvidence 'DistortionTests.exe')" 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'distortion.link.log')
if($LASTEXITCODE){throw 'Distortion link failed'}
& (Join-Path $taskEvidence 'DistortionTests.exe') 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'distortion.test.log')
$taskExit=$LASTEXITCODE
[pscustomobject]@{exitCode=$taskExit;macDifferential=$false;compiler=(Get-Command cl.exe).Source;windowsSdkVersion=$env:WindowsSDKVersion} | ConvertTo-Json | Out-File (Join-Path $taskEvidence 'results.json') -Encoding utf8
if($taskExit){throw 'Distortion tests failed; see preserved logs'}
