param([Parameter(Mandatory=$true)][string]$Capture)
$ErrorActionPreference='Stop'
$taskWindows=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
$taskDestination=Join-Path $PSScriptRoot $Capture
if(Test-Path -LiteralPath $taskDestination){throw 'New capture directory required'}
New-Item -ItemType Directory -Path $taskDestination | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/bin/build-manifest.json" -Destination "$taskDestination/build-manifest.json"
Copy-Item -LiteralPath "$PSScriptRoot/bin/build-mode.json" -Destination "$taskDestination/build-mode.json"
Copy-Item -LiteralPath "$PSScriptRoot/bin/project_reopen_tests.exe" -Destination "$taskDestination/project_reopen_tests.exe"
$env:QT_QPA_PLATFORM='offscreen'
$taskResults=@()
foreach($taskCase in @('same_active','other_tab','case_unicode_dot_alias','junction_alias','distinct_directories','pending_gradient','busy_guard','failed_load')){
    $taskProcess=Start-Process -FilePath "$PSScriptRoot/bin/project_reopen_tests.exe" -ArgumentList @($taskCase) -PassThru -WindowStyle Hidden -RedirectStandardOutput "$taskDestination/$taskCase.out.log" -RedirectStandardError "$taskDestination/$taskCase.err.log"
    if(-not $taskProcess.WaitForExit(30000)){Stop-Process -Id $taskProcess.Id -Force;$taskCode=124}else{$taskProcess.Refresh();$taskCode=$taskProcess.ExitCode}
    $taskResults += [ordered]@{case=$taskCase;exit_code=$taskCode;status=$(if($taskCode -eq 0){'passed'}elseif($taskCode -eq 124){'timeout'}else{'failed'})}
}
$taskResults | ConvertTo-Json -Depth 3 | Set-Content "$taskDestination/results.json" -Encoding utf8
$taskResults | Format-Table
