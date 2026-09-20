param([string]$EvidenceDirectory)
$ErrorActionPreference='Stop'
$taskWindowsRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $EvidenceDirectory){$EvidenceDirectory=Join-Path $taskWindowsRoot 'evidence/graphics/retouch-02'}
New-Item -ItemType Directory -Force $EvidenceDirectory | Out-Null
$taskEvidence=(Resolve-Path $EvidenceDirectory).Path
& 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$taskSources=@('src/retouch/RetouchSession.cpp','src/graphics/BrushCoverage.cpp','src/graphics/D3D11BrushCoverage.cpp','src/graphics/BrushSession.cpp','src/graphics/PixelAlgorithms.cpp','tests/retouch/RetouchSessionTests.cpp','src/core/Document.cpp')
$taskObjects=@()
foreach($taskRelative in $taskSources){
    $taskName=[IO.Path]::GetFileNameWithoutExtension($taskRelative)
    $taskObject=Join-Path $taskEvidence ($taskName+'.obj')
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /O2 /MD /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /c "/I$(Join-Path $taskWindowsRoot 'src')" (Join-Path $taskWindowsRoot $taskRelative) "/Fo$taskObject" 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence ($taskName+'.build.log'))
    if($LASTEXITCODE){throw "Retouch compilation failed: $taskRelative"}
    $taskObjects+=$taskObject
}
foreach($taskModule in @('AdjustPixels','BrushPixels','ContentFill','HealPixels','LensPixels','LevelsPixels','NoisePixels','WandPixels')){
    $taskObject=Join-Path $taskEvidence ($taskModule+'.obj')
    & cl.exe /nologo /std:c11 /TC /W4 /WX /O2 /MD /D_USE_MATH_DEFINES /c (Join-Path $taskWindowsRoot ('src/graphics/upstream/'+$taskModule+'.c')) "/Fo$taskObject" 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence ($taskModule+'.build.log'))
    if($LASTEXITCODE){throw "C compilation failed: $taskModule"}
    $taskObjects+=$taskObject
}
& link.exe /nologo $taskObjects d3d11.lib d3dcompiler.lib dxgi.lib ole32.lib "/OUT:$(Join-Path $taskEvidence 'RetouchSessionTests.exe')" 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'retouch.link.log')
if($LASTEXITCODE){throw 'Retouch link failed'}
& (Join-Path $taskEvidence 'RetouchSessionTests.exe') 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'retouch.cpu.test.log')
$taskCpuExit=$LASTEXITCODE
& (Join-Path $taskEvidence 'RetouchSessionTests.exe') (Join-Path $taskWindowsRoot 'shaders/BrushCoverage.hlsl') 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'retouch.warp.test.log')
$taskWarpExit=$LASTEXITCODE
[pscustomobject]@{cpuExit=$taskCpuExit;warpExit=$taskWarpExit;macDifferential=$false;compiler=(Get-Command cl.exe).Source;windowsSdkVersion=$env:WindowsSDKVersion} | ConvertTo-Json | Out-File (Join-Path $taskEvidence 'results.json') -Encoding utf8
if($taskCpuExit -or $taskWarpExit){throw 'Retouch tests failed; see preserved logs'}
