param([string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
$taskWindowsRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $EvidenceDirectory) { $EvidenceDirectory = Join-Path $taskWindowsRoot 'evidence/graphics/native' }
New-Item -ItemType Directory -Force $EvidenceDirectory | Out-Null
$taskEvidence = (Resolve-Path $EvidenceDirectory).Path
& 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
[pscustomobject]@{
    utcTime=[DateTime]::UtcNow.ToString('o'); compiler=(Get-Command cl.exe).Source;
    vcToolsVersion=$env:VCToolsVersion; windowsSdkVersion=$env:WindowsSDKVersion;
    targetArchitecture=$env:VSCMD_ARG_TGT_ARCH; baseline='a19db9011282399785dc18efcfded904627bdcc2';
    macDifferential=$false; gray8Tolerance=1; densityAbsoluteTolerance=0.0003
} | ConvertTo-Json | Out-File (Join-Path $taskEvidence 'build-settings.json') -Encoding utf8
$taskObjects = @()
foreach ($taskSource in Get-ChildItem (Join-Path $taskWindowsRoot 'src/graphics/upstream') -Filter '*.c') {
    $taskObject = Join-Path $taskEvidence ($taskSource.BaseName + '.obj')
    & cl.exe /nologo /std:c11 /TC /W4 /WX /O2 /MD /D_USE_MATH_DEFINES /c $taskSource.FullName "/Fo$taskObject" 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence ($taskSource.BaseName + '.build.log'))
    if ($LASTEXITCODE) { throw "C compilation failed: $($taskSource.Name)" }
    $taskObjects += $taskObject
}
$taskCppSources = @('src/graphics/PixelAlgorithms.cpp','src/graphics/BrushCoverage.cpp','src/graphics/D3D11BrushCoverage.cpp','tests/graphics/PixelAlgorithmsTests.cpp','tests/graphics/BrushCoverageTests.cpp')
foreach ($taskRelative in $taskCppSources) {
    $taskSourcePath=Join-Path $taskWindowsRoot $taskRelative
    $taskName=[IO.Path]::GetFileNameWithoutExtension($taskRelative)
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /O2 /MD /utf-8 /c "/I$(Join-Path $taskWindowsRoot 'src')" $taskSourcePath "/Fo$(Join-Path $taskEvidence ($taskName + '.obj'))" 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence ($taskName + '.build.log'))
    if ($LASTEXITCODE) { throw "C++ compilation failed: $taskRelative" }
}
& link.exe /nologo $taskObjects (Join-Path $taskEvidence 'PixelAlgorithms.obj') (Join-Path $taskEvidence 'PixelAlgorithmsTests.obj') "/OUT:$(Join-Path $taskEvidence 'PixelAlgorithmsTests.exe')" 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'pixels.link.log')
if ($LASTEXITCODE) { throw 'Pixel tests link failed' }
& link.exe /nologo (Join-Path $taskEvidence 'BrushCoverage.obj') (Join-Path $taskEvidence 'D3D11BrushCoverage.obj') (Join-Path $taskEvidence 'BrushCoverageTests.obj') d3d11.lib d3dcompiler.lib dxgi.lib "/OUT:$(Join-Path $taskEvidence 'BrushCoverageTests.exe')" 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'brush.link.log')
if ($LASTEXITCODE) { throw 'Brush tests link failed' }
& (Join-Path $taskEvidence 'PixelAlgorithmsTests.exe') 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'pixels.test.log')
$taskPixelsExit=$LASTEXITCODE
& (Join-Path $taskEvidence 'BrushCoverageTests.exe') (Join-Path $taskWindowsRoot 'shaders/BrushCoverage.hlsl') $taskEvidence 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'brush.test.log')
$taskBrushExit=$LASTEXITCODE
[pscustomobject]@{pixelsExit=$taskPixelsExit;brushExit=$taskBrushExit;macDifferential=$false} | ConvertTo-Json | Out-File (Join-Path $taskEvidence 'results.json') -Encoding utf8
if ($taskPixelsExit -or $taskBrushExit) { throw 'Native graphics tests failed; see preserved logs.' }
