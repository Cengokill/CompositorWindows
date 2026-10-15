#Requires -Version 7.0
param([ValidateRange(1,64)][int]$Parallel = 4)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
if (-not $IsWindows) { throw 'Release/native viewport validation requires Windows.' }
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskBuild = Join-Path $taskRoot 'build/release'
New-Item -ItemType Directory -Force $taskBuild | Out-Null
# Prevent two invocations of this command from sharing the build tree.
$taskLock = [IO.File]::Open((Join-Path $taskBuild 'validation.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
$taskEnvironment = @{}
foreach ($taskName in @('PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM','QT_SCALE_FACTOR')) {
    $taskEnvironment[$taskName] = [Environment]::GetEnvironmentVariable($taskName, 'Process')
}
Push-Location $taskRoot
try {
    $taskEvidence = Join-Path $taskBuild ('validation/' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $taskEvidence | Out-Null
    Write-Host "Validation logs: $taskEvidence"
    & git rev-parse HEAD | Out-File (Join-Path $taskEvidence 'revision.txt')
    & git status --short | Out-File (Join-Path $taskEvidence 'working-tree.txt')
    [Environment]::OSVersion.VersionString | Out-File (Join-Path $taskEvidence 'windows.txt')

    & cmake --preset windows-x64-release 2>&1 | Tee-Object (Join-Path $taskEvidence 'configure.log')
    if ($LASTEXITCODE -ne 0) { throw 'Configure failed; no build or tests were started.' }
    # Build ALL_BUILD normally. Never enumerate vcxproj files: dashboard
    # projects start CTest and must not run while binaries are being linked.
    & cmake --build --preset windows-x64-release --parallel $Parallel 2>&1 | Tee-Object (Join-Path $taskEvidence 'build.log')
    if ($LASTEXITCODE -ne 0) { throw 'Build failed; no tests were started.' }

    & ctest --preset windows-x64-release --parallel 1 --output-on-failure 2>&1 | Tee-Object (Join-Path $taskEvidence 'ctest.log')
    $taskCtestExit = $LASTEXITCODE
    # This native executable is deliberately separate from the ordinary suite:
    # it requires an interactive 100% display and verifies physical DPR 2.
    $env:PATH = (Join-Path $taskRoot 'dependencies/qt/bin') + ';' + $env:PATH
    $env:QT_PLUGIN_PATH = Join-Path $taskRoot 'dependencies/qt/plugins'
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_SCALE_FACTOR = '2'
    & (Join-Path $taskBuild 'Release/native_viewport_tests.exe') (Join-Path $taskEvidence 'native.png') 2>&1 | Tee-Object (Join-Path $taskEvidence 'native-viewport.log')
    $taskNativeExit = $LASTEXITCODE
    [pscustomobject]@{ctestExit=$taskCtestExit; nativeViewportExit=$taskNativeExit; manualVerification=$false} |
        ConvertTo-Json | Out-File (Join-Path $taskEvidence 'results.json') -Encoding utf8
    if ($taskCtestExit -ne 0 -or $taskNativeExit -ne 0) {
        throw "Validation failed: CTest=$taskCtestExit; native viewport=$taskNativeExit. See $taskEvidence"
    }
} finally {
    foreach ($taskName in $taskEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($taskName, $taskEnvironment[$taskName], 'Process')
    }
    Pop-Location
    $taskLock.Dispose()
}
