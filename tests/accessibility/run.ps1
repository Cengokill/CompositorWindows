param([Parameter(Mandatory=$true)][string]$EvidenceDirectory)
$ErrorActionPreference='Stop'
$taskWindows=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
$taskDestination=[IO.Path]::GetFullPath($EvidenceDirectory)
if(Test-Path -LiteralPath $taskDestination){throw 'Choose a new evidence directory; captures are never overwritten.'}
New-Item -ItemType Directory -Path $taskDestination | Out-Null
$env:QT_QPA_PLATFORM='windows'
$taskExecutable=Join-Path $PSScriptRoot 'bin\accessibility_witness.exe'
Copy-Item -LiteralPath "$PSScriptRoot/bin/build-manifest.json" -Destination "$taskDestination/build-manifest.json"
if(Test-Path -LiteralPath "$PSScriptRoot/bin/build-mode.json"){Copy-Item -LiteralPath "$PSScriptRoot/bin/build-mode.json" -Destination "$taskDestination/build-mode.json"}
$taskRun=Start-Process -FilePath $taskExecutable -ArgumentList @('"'+(Join-Path $taskDestination 'uia.json')+'"') -PassThru -WindowStyle Hidden -RedirectStandardOutput "$taskDestination/stdout.log" -RedirectStandardError "$taskDestination/stderr.log"
if(-not $taskRun.WaitForExit(120000)){
    # Only the exact process launched above can be stopped by this witness.
    Stop-Process -Id $taskRun.Id -Force
    [ordered]@{status='timeout';process_id=$taskRun.Id;seconds=120} | ConvertTo-Json | Set-Content "$taskDestination/process.json" -Encoding utf8
    throw 'Owned UIA witness exceeded its frozen 120-second limit.'
}
$taskRun.Refresh()
[ordered]@{status='completed';process_id=$taskRun.Id;exit_code=$taskRun.ExitCode;executable_sha256=(Get-FileHash -LiteralPath $taskExecutable).Hash.ToLowerInvariant()} | ConvertTo-Json | Set-Content "$taskDestination/process.json" -Encoding utf8
Write-Host "UIA witness exit $($taskRun.ExitCode); evidence $taskDestination"
exit $taskRun.ExitCode
