param([string]$EvidenceDirectory,[switch]$Benchmark,[switch]$WarpBenchmarkOnly)
$ErrorActionPreference='Stop'
$taskWindowsRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $EvidenceDirectory){$EvidenceDirectory=Join-Path $taskWindowsRoot 'evidence/graphics/session'}
New-Item -ItemType Directory -Force $EvidenceDirectory | Out-Null
$taskEvidence=(Resolve-Path $EvidenceDirectory).Path
& 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$taskSources=@('src/graphics/BrushCoverage.cpp','src/graphics/D3D11BrushCoverage.cpp','src/graphics/BrushSession.cpp','src/graphics/StackRenderer.cpp','tests/graphics/BrushSessionTests.cpp','src/core/Document.cpp')
$taskObjects=@()
foreach($taskRelative in $taskSources){
    $taskName=[IO.Path]::GetFileNameWithoutExtension($taskRelative)
    $taskObject=Join-Path $taskEvidence ($taskName+'.obj')
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /O2 /MD /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /c "/I$(Join-Path $taskWindowsRoot 'src')" (Join-Path $taskWindowsRoot $taskRelative) "/Fo$taskObject" 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence ($taskName+'.build.log'))
    if($LASTEXITCODE){throw "Session compilation failed: $taskRelative"}
    $taskObjects+=$taskObject
}
& link.exe /nologo $taskObjects d3d11.lib d3dcompiler.lib dxgi.lib ole32.lib "/OUT:$(Join-Path $taskEvidence 'BrushSessionTests.exe')" 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'session.link.log')
if($LASTEXITCODE){throw 'Brush session tests link failed'}
$taskArgs=@((Join-Path $taskWindowsRoot 'shaders/BrushCoverage.hlsl'))
if($WarpBenchmarkOnly){$taskArgs+='--benchmark-warp'}elseif($Benchmark){$taskArgs+='--benchmark'}
& (Join-Path $taskEvidence 'BrushSessionTests.exe') @taskArgs 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'session.test.log')
$taskExit=$LASTEXITCODE
[pscustomobject]@{exitCode=$taskExit;benchmark=[bool]$Benchmark;macDifferential=$false;compiler=(Get-Command cl.exe).Source;windowsSdkVersion=$env:WindowsSDKVersion} | ConvertTo-Json | Out-File (Join-Path $taskEvidence 'results.json') -Encoding utf8
if($taskExit){throw 'Brush session tests failed; see preserved logs'}
