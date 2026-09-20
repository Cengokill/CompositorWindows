$ErrorActionPreference='Stop'
$taskWorkspace=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$taskRun=Join-Path $PSScriptRoot ('run-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,6))
New-Item -ItemType Directory -Path $taskRun | Out-Null
$taskLockPath=Join-Path $taskWorkspace 'dependencies/imaging/lock.json'
$taskLock=Get-Content -LiteralPath $taskLockPath -Raw | ConvertFrom-Json
$taskOriginalHashes=@($taskLock.binaries | ForEach-Object {[ordered]@{path=$_.path;sha256=(Get-FileHash -LiteralPath (Join-Path "$taskWorkspace/dependencies/imaging" $_.path)).Hash}})
$taskPayload=Join-Path $taskRun 'payload'
New-Item -ItemType Directory -Path $taskPayload | Out-Null
foreach($taskBinary in $taskLock.binaries){Copy-Item -LiteralPath (Join-Path "$taskWorkspace/dependencies/imaging" $taskBinary.path) -Destination (Join-Path $taskPayload ([IO.Path]::GetFileName($taskBinary.path)))}
function New-Fixture([string]$Name){
    $location=Join-Path $taskRun $Name
    New-Item -ItemType Directory -Path "$location/scripts","$location/dependencies/imaging/install/bin" -Force | Out-Null
    Copy-Item -LiteralPath "$taskWorkspace/scripts/restore-imaging-runtime.ps1" -Destination "$location/scripts"
    Copy-Item -LiteralPath $taskLockPath -Destination "$location/dependencies/imaging/lock.json"
    [IO.File]::WriteAllText("$location/dependencies/imaging/install/bin/heif.dll",'preserve previous local build bytes')
    return $location
}
$taskPositive=New-Fixture 'positive-workspace'
$taskOld="$taskPositive/dependencies/imaging/install/bin/heif.dll"
$taskOldHash=(Get-FileHash -LiteralPath $taskOld).Hash
$taskResult=@(& "$taskPositive/scripts/restore-imaging-runtime.ps1" -RuntimeDirectory $taskPayload)
if($taskResult.Count -ne 4){throw 'Positive restore did not report four entries'}
foreach($taskBinary in $taskLock.binaries){if((Get-FileHash -LiteralPath (Join-Path "$taskPositive/dependencies/imaging" $taskBinary.path)).Hash -ine $taskBinary.sha256){throw 'Positive installed hash mismatch'}}
$taskBackup=@(Get-ChildItem -LiteralPath "$taskPositive/dependencies/imaging/install/bin" -Filter '*.before-runtime-*.bak')
if($taskBackup.Count -ne 1 -or (Get-FileHash -LiteralPath $taskBackup[0].FullName).Hash -ne $taskOldHash -or $taskBackup[0].Name -notlike ('*'+$taskOldHash.ToLowerInvariant()+'*')){throw 'Previous file was not preserved with checksum in its unique backup name'}
$taskRepeat=@(& "$taskPositive/scripts/restore-imaging-runtime.ps1" -RuntimeDirectory $taskPayload)
if(@($taskRepeat | Where-Object {$_.status -ne 'already_verified'}).Count){throw 'Idempotent repeat rewrote a verified runtime'}

$taskCorrupt=New-Fixture 'corrupt-workspace'
$taskBadPayload=Join-Path $taskRun 'corrupt-payload'
New-Item -ItemType Directory -Path $taskBadPayload | Out-Null
foreach($taskBinary in $taskLock.binaries){Copy-Item -LiteralPath (Join-Path $taskPayload ([IO.Path]::GetFileName($taskBinary.path))) -Destination $taskBadPayload}
[IO.File]::WriteAllText((Join-Path $taskBadPayload ([IO.Path]::GetFileName($taskLock.binaries[-1].path))),'corrupt fourth runtime')
$taskRejected=$false;$taskError=''
try{& "$taskCorrupt/scripts/restore-imaging-runtime.ps1" -RuntimeDirectory $taskBadPayload | Out-Null}catch{$taskRejected=$true;$taskError=$_.Exception.Message}
if(-not $taskRejected -or $taskError -notlike '*checksum mismatch*'){throw 'Corrupt fourth runtime was not rejected'}
if((Get-FileHash -LiteralPath "$taskCorrupt/dependencies/imaging/install/bin/heif.dll").Hash -ne $taskOldHash){throw 'Corrupt source changed previous destination bytes'}
$taskExisting=@(Get-ChildItem -LiteralPath "$taskCorrupt/dependencies/imaging" -Recurse -File)
if($taskExisting.Count -ne 2){throw 'Corrupt input created a stage, backup or additional runtime before validating all four'}
foreach($taskOriginal in $taskOriginalHashes){if((Get-FileHash -LiteralPath (Join-Path "$taskWorkspace/dependencies/imaging" $taskOriginal.path)).Hash -ne $taskOriginal.sha256){throw 'Live workspace runtime changed during isolated tests'}}
[ordered]@{schema=1;status='passed';positive_four_hashes=$true;previous_bytes_preserved=$true;idempotent_repeat=$true;corrupt_fourth_rejected_before_copy=$true;live_originals_unchanged=$true;corrupt_error=$taskError;helper_sha256=(Get-FileHash -LiteralPath "$taskWorkspace/scripts/restore-imaging-runtime.ps1").Hash.ToLowerInvariant();lock_sha256=(Get-FileHash -LiteralPath $taskLockPath).Hash.ToLowerInvariant();restore_result=$taskResult;original_runtime_hashes=$taskOriginalHashes} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$taskRun/result.json" -Encoding utf8
Write-Output "RESTORE_VERIFICATION=$taskRun/result.json"
