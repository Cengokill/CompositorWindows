param([Parameter(Mandatory=$true)][string]$Capture,[switch]$NativeOnly)
$ErrorActionPreference='Stop'
$taskWindows=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $taskWindows 'scripts\bootstrap.ps1') -Offline
if([IO.Path]::GetFileName($Capture) -ne $Capture -or $Capture -in @('.','..')){throw 'Capture must be a new directory name'}
$taskDestination=Join-Path $PSScriptRoot $Capture
if(Test-Path -LiteralPath $taskDestination){throw 'New capture directory required'}
New-Item -ItemType Directory -Path $taskDestination | Out-Null
$taskBin=Join-Path $PSScriptRoot $(if($NativeOnly){'native-bin'}else{'bin'})
$taskName=$(if($NativeOnly){'native_project_dialog_tests'}else{'project_open_batch_tests'})
Copy-Item -LiteralPath "$taskBin/build-manifest.json","$taskBin/$taskName.exe" -Destination $taskDestination
Copy-Item -LiteralPath "$PSScriptRoot/BeforeOpenBatchTests.cpp","$PSScriptRoot/ProjectOpenBatchTests.cpp","$PSScriptRoot/NativeProjectDialogTests.cpp" -Destination $taskDestination
$taskCases=$(if($NativeOnly){@('cancel','multiple','this_pc')}else{@('drop_order','drop_mixed_failure','drop_existing','drop_pending_guard','batch_empty','batch_order','batch_existing','batch_mixed_failure','batch_all_fail','batch_pending_guard','batch_busy_guard','batch_error_reentrancy','welcome_fresh_session','welcome_failure_preserves_session','welcome_project_then_image_skipped','package_policy','native_options','native_dialog_cancel')})
$taskResults=@()
foreach($taskCase in $taskCases){
    $env:QT_QPA_PLATFORM=$(if($NativeOnly -or $taskCase -eq 'native_dialog_cancel'){'windows'}else{'offscreen'})
    $taskProcess=Start-Process -FilePath "$taskBin/$taskName.exe" -ArgumentList $taskCase -PassThru -WindowStyle Hidden -RedirectStandardOutput "$taskDestination/$taskCase.out.log" -RedirectStandardError "$taskDestination/$taskCase.err.log"
    if(-not $taskProcess.WaitForExit(30000)){Stop-Process -Id $taskProcess.Id -Force;$taskCode=124}else{$taskProcess.Refresh();$taskCode=$taskProcess.ExitCode}
    $taskResults += [ordered]@{case=$taskCase;exit_code=$taskCode;status=$(if($taskCode -eq 0){'passed'}elseif($taskCode -eq 124){'timeout'}else{'failed'});environment=@{QT_QPA_PLATFORM=$env:QT_QPA_PLATFORM}}
}
$taskResults | ConvertTo-Json -Depth 4 | Set-Content "$taskDestination/results.json" -Encoding utf8
$taskResults | Format-Table
