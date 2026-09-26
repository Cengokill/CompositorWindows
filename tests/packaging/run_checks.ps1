param([Parameter(Mandatory)][string]$PackageDirectory,[switch]$Install,[string]$UpgradeInstaller)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$package=[IO.Path]::GetFullPath($PackageDirectory)
$portable=Join-Path $package 'CompositorWindows'
$run=Join-Path $root ('evidence\packaging\run-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $run | Out-Null
$checks=[Collections.Generic.List[object]]::new()
function Record([string]$name,[bool]$passed,[object]$details){$checks.Add([ordered]@{name=$name;passed=$passed;details=$details});$checks|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $run 'checks.json') -Encoding utf8;if(-not $passed){throw "Package check failed: $name"}}
function RunApp([string]$exe,[string[]]$arguments,[string]$name){
 $info=[Diagnostics.ProcessStartInfo]::new($exe);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($arg in $arguments){$info.ArgumentList.Add($arg)}
 $info.Environment['PATH']="$env:SystemRoot\System32;$env:SystemRoot"
 foreach($key in @('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','PYTHONPATH','PYTHONHOME','VIRTUAL_ENV')){$info.Environment.Remove($key)|Out-Null}
 $timer=[Diagnostics.Stopwatch]::StartNew();$process=[Diagnostics.Process]::Start($info)
 $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
 if(-not $process.WaitForExit(180000)){$process.Kill();throw "$name timed out"}
 $stdout.Result | Set-Content -LiteralPath (Join-Path $run "$name.stdout.txt")
 $stderr.Result | Set-Content -LiteralPath (Join-Path $run "$name.stderr.txt")
 return [ordered]@{exitCode=$process.ExitCode;milliseconds=$timer.ElapsedMilliseconds}
}
$manifest=Get-Content -LiteralPath (Join-Path $package 'package-manifest.json') -Raw|ConvertFrom-Json
$bad=@($manifest.files|Where-Object {(Get-FileHash -LiteralPath (Join-Path $portable $_.path) -Algorithm SHA256).Hash -ine $_.sha256})
Record 'package_sha256' ($bad.Count -eq 0) @{files=$manifest.files.Count;bad=$bad}
if($manifest.schema -eq 2){
 $application=Join-Path $portable $manifest.entryPoint
 $policy=Get-Content -LiteralPath (Join-Path $portable 'release-policy.json') -Raw | ConvertFrom-Json
 $developmentFiles=@('CompositorLauncher.exe','CompositorUpdater.exe','install.json','state','update-source.json','update-receipt.json')
 $unexpected=@($developmentFiles | Where-Object {Test-Path -LiteralPath (Join-Path $portable $_)})
 Record 'preview_manual_update_distribution' ($policy.automaticUpdates -eq $false -and $manifest.automaticUpdates -eq $false -and $unexpected.Count -eq 0) @{unexpected=$unexpected;policy=$policy}
 $sourceArchive=Join-Path $package $manifest.sourceSnapshot
 Record 'matching_application_source' ((Get-FileHash -LiteralPath $sourceArchive).Hash -ieq $manifest.sourceSha256 -and (Get-FileHash -LiteralPath (Join-Path $portable 'sources\CompositorWindows-source.zip')).Hash -ieq $manifest.sourceSha256) @{sha256=$manifest.sourceSha256}
 $health=RunApp $application @('--update-health-check') 'portable-health'
 Record 'portable_clean_path_health' ($health.exitCode -eq 0) $health
 $native=Join-Path $run 'native'
 $ui=RunApp $application @('--warp','--ui-test','--evidence',$native) 'portable-native'
 Record 'portable_native_workflow' ($ui.exitCode -eq 0) $ui
 $project=Join-Path $native 'Project 実証 test.comp'
 if(-not(Test-Path -LiteralPath $project)){throw 'Native test did not produce the Unicode project fixture'}
 $render=Join-Path $run 'portable-reopen.png'
 $reopened=RunApp $application @('--render-project',$project,'--output',$render) 'portable-reopen'
 Record 'portable_unicode_project_reopen_export' ($reopened.exitCode -eq 0 -and (Test-Path -LiteralPath $render)) $reopened
 if($Install){
  $verb='HKCU:\Software\Classes\Directory\shell\CompositorWindowsPreview'
  $registration='HKCU:\Software\CompositorWindows\CommunityPreview'
  if((Test-Path -LiteralPath $verb) -or (Test-Path -LiteralPath $registration)){throw 'An existing community preview installation is registered; refusing to change it for testing'}
  $installRoot=Join-Path $run 'standard-user-install'
  $setup=Join-Path $package "CompositorWindows-$($manifest.version)-x64.msi"
  $installedMsi=$null
  try {
   $installed=RunApp "$env:SystemRoot\System32\msiexec.exe" @('/i',$setup,'/qn','/norestart',"INSTALLFOLDER=$installRoot",'/l*v',(Join-Path $run 'msi-install.log')) 'msi-install'
   if($installed.exitCode -in @(0,3010)){$installedMsi=$setup}
   Record 'msi_per_user_install' ($installed.exitCode -eq 0 -and (Get-ItemProperty -LiteralPath $registration).InstallLocation.TrimEnd('\') -eq $installRoot) $installed
   $installedBad=@($manifest.files | Where-Object {(Get-FileHash -LiteralPath (Join-Path $installRoot $_.path)).Hash -ine $_.sha256})
   Record 'msi_matches_portable_payload' ($installedBad.Count -eq 0) @{files=$manifest.files.Count;bad=$installedBad}
   $verbCommand=(Get-ItemProperty -LiteralPath ($verb+'\command')).'(default)'
   Record 'msi_project_folder_verb' ($verbCommand -eq ('"'+$installRoot+'\Compositor.exe" "%1"')) @{command=$verbCommand;explorerMenuInteraction='not verified'}
   $sentinel=Join-Path $installRoot 'My preserved project.comp'
   Copy-Item -LiteralPath $project -Destination $sentinel -Recurse
   $projectHashes=@(Get-ChildItem -LiteralPath $sentinel -Recurse -File | ForEach-Object {[ordered]@{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
   $installedHealth=RunApp (Join-Path $installRoot 'Compositor.exe') @('--update-health-check') 'installed-health'
   Record 'msi_installed_runtime' ($installedHealth.exitCode -eq 0) $installedHealth
   $installedUi=RunApp (Join-Path $installRoot 'Compositor.exe') @('--warp','--ui-test','--evidence',(Join-Path $run 'installed-native')) 'installed-native'
   Record 'msi_installed_native_workflow' ($installedUi.exitCode -eq 0) $installedUi
   $repair=RunApp "$env:SystemRoot\System32\msiexec.exe" @('/fa',$setup,'/qn','/norestart','/l*v',(Join-Path $run 'msi-repair.log')) 'msi-repair'
   $repairBad=@($projectHashes | Where-Object {-not(Test-Path -LiteralPath $_.path) -or (Get-FileHash -LiteralPath $_.path).Hash -ne $_.sha256})
   Record 'msi_repair_preserves_project' ($repair.exitCode -eq 0 -and $repairBad.Count -eq 0) @{process=$repair;projectFiles=$projectHashes.Count;changed=$repairBad}
   if($UpgradeInstaller){
    $upgradePath=[IO.Path]::GetFullPath($UpgradeInstaller)
    $upgraded=RunApp "$env:SystemRoot\System32\msiexec.exe" @('/i',$upgradePath,'/qn','/norestart','/l*v',(Join-Path $run 'msi-upgrade.log')) 'msi-upgrade'
    if($upgraded.exitCode -in @(0,3010)){$installedMsi=$upgradePath}
    $upgradeBad=@($projectHashes | Where-Object {-not(Test-Path -LiteralPath $_.path) -or (Get-FileHash -LiteralPath $_.path).Hash -ne $_.sha256})
    $preservedLocation=(Get-ItemProperty -LiteralPath $registration).InstallLocation.TrimEnd('\') -eq $installRoot
    Record 'msi_upgrade_preserves_project' ($upgraded.exitCode -eq 0 -and $upgradeBad.Count -eq 0 -and $preservedLocation) @{process=$upgraded;projectFiles=$projectHashes.Count;changed=$upgradeBad;customLocationPreserved=$preservedLocation}
    $upgradedHealth=RunApp (Join-Path $installRoot 'Compositor.exe') @('--update-health-check') 'upgraded-health'
    Record 'msi_upgraded_runtime' ($upgradedHealth.exitCode -eq 0) $upgradedHealth
    $downgrade=RunApp "$env:SystemRoot\System32\msiexec.exe" @('/i',$setup,'/qn','/norestart',"INSTALLFOLDER=$installRoot",'/l*v',(Join-Path $run 'msi-downgrade.log')) 'msi-downgrade'
    Record 'msi_rejects_downgrade' ($downgrade.exitCode -eq 1603) $downgrade
   }
   $removed=RunApp "$env:SystemRoot\System32\msiexec.exe" @('/x',$installedMsi,'/qn','/norestart','/l*v',(Join-Path $run 'msi-uninstall.log')) 'msi-uninstall'
   if($removed.exitCode -eq 0){$installedMsi=$null}
   $unchanged=@($projectHashes | Where-Object {-not(Test-Path -LiteralPath $_.path) -or (Get-FileHash -LiteralPath $_.path).Hash -ne $_.sha256})
   Record 'msi_uninstall_preserves_project' ($removed.exitCode -eq 0 -and $unchanged.Count -eq 0 -and -not(Test-Path -LiteralPath (Join-Path $installRoot 'Compositor.exe')) -and -not(Test-Path -LiteralPath $verb)) @{process=$removed;projectFiles=$projectHashes.Count;changed=$unchanged}
  }finally{
   if($installedMsi){$cleanup=RunApp "$env:SystemRoot\System32\msiexec.exe" @('/x',$installedMsi,'/qn','/norestart','/l*v',(Join-Path $run 'msi-failed-run-cleanup.log')) 'msi-failed-run-cleanup';if($cleanup.exitCode -ne 0){Write-Warning "Test installation remains at $installRoot; cleanup returned $($cleanup.exitCode)"}}
  }
 }
 [ordered]@{timestamp=[DateTime]::UtcNow.ToString('o');machine=$env:COMPUTERNAME;checks=$checks;cleanVM=$false;humanAcceptance=$false;developmentPathsRemoved=$true;msiUpgradeExercised=[bool]$UpgradeInstaller} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $run 'report.json') -Encoding utf8
 Write-Output "EVIDENCE=$run"
 return
}
$health=RunApp (Join-Path $portable "versions\$($manifest.version)\Compositor.exe") @('--update-health-check') 'portable-health'
Record 'portable_clean_path_health' ($health.exitCode -eq 0) $health
$native=Join-Path $run 'native'
$ui=RunApp (Join-Path $portable "versions\$($manifest.version)\Compositor.exe") @('--warp','--ui-test','--evidence',$native) 'portable-native'
Record 'portable_native_workflow' ($ui.exitCode -eq 0) $ui
$project=Join-Path $native 'Project 実証 test.comp'
if(-not (Test-Path -LiteralPath $project)){throw 'Native test did not produce the Unicode project fixture'}
$render=Join-Path $run 'launcher-render.png'
$launch=RunApp (Join-Path $portable 'CompositorLauncher.exe') @('--render-project',$project,'--output',$render) 'portable-launcher'
$deadline=[DateTime]::UtcNow.AddSeconds(40)
while(-not(Test-Path -LiteralPath $render) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 100}
Record 'launcher_unicode_project_forwarding' ($launch.exitCode -eq 0 -and (Test-Path -LiteralPath $render)) $launch
$feed=Join-Path $run 'feed'
$upgradeRoot=Join-Path $run 'upgrade-install'
Copy-Item -LiteralPath $portable -Destination $upgradeRoot -Recurse
$sentinel=Join-Path $upgradeRoot 'My preserved project.comp';Copy-Item -LiteralPath $project -Destination $sentinel -Recurse
$projectHashes=@(Get-ChildItem -LiteralPath $sentinel -Recurse -File|ForEach-Object {[ordered]@{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
& (Join-Path $root 'scripts\update-make-test-feed.ps1') -PayloadDirectory (Join-Path $portable "versions\$($manifest.version)") -FeedDirectory $feed -Version '0.1.1'
$update=RunApp (Join-Path $upgradeRoot 'CompositorUpdater.exe') @('install','--root',$upgradeRoot,'--test-feed',$feed,'--allow-test-key') 'real-payload-update'
$active=Get-Content -LiteralPath (Join-Path $upgradeRoot 'state\active.json') -Raw|ConvertFrom-Json
Record 'real_payload_update_health' ($update.exitCode -eq 0 -and $active.current -eq '0.1.1' -and -not $active.pending) @{process=$update;state=$active}
$uninstall=RunApp (Join-Path $upgradeRoot 'CompositorUpdater.exe') @('--uninstall-payloads','--root',$upgradeRoot,'--allow-test-key') 'real-payload-uninstall'
$unchanged=@($projectHashes|Where-Object {-not(Test-Path -LiteralPath $_.path) -or (Get-FileHash -LiteralPath $_.path).Hash -ne $_.sha256})
Record 'verified_uninstall_keeps_projects' ($uninstall.exitCode -eq 0 -and $unchanged.Count -eq 0 -and -not(Test-Path -LiteralPath (Join-Path $upgradeRoot 'versions\0.1.1\Compositor.exe'))) @{process=$uninstall;projectFiles=$projectHashes.Count;changed=$unchanged}
if($Install){
 $registry='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\CompositorWindows'
 $verb='HKCU:\Software\Classes\Directory\shell\CompositorWindows'
 if((Test-Path $registry) -or (Test-Path $verb)){throw 'A real registered installation exists; refusing to overwrite it for testing'}
 $installRoot=Join-Path $run 'standard-user-install'
 $setup=Join-Path $package "CompositorWindows-$($manifest.version)-setup.exe"
 $installed=RunApp $setup @('/S',"/D=$installRoot") 'installer'
 $registered=Get-ItemProperty -LiteralPath $registry
 Record 'per_user_installer' ($installed.exitCode -eq 0 -and $registered.InstallLocation -eq $installRoot) $installed
 $verbCommand=(Get-ItemProperty -LiteralPath ($verb+'\command')).'(default)'
 Record 'directory_project_shell_verb' ($verbCommand -eq ('"'+$installRoot+'\CompositorLauncher.exe" "%1"')) @{command=$verbCommand;appliesTo=(Get-ItemProperty -LiteralPath $verb).AppliesTo;explorerMenuInteraction='not yet verified'}
 Copy-Item -LiteralPath $project -Destination (Join-Path $installRoot 'keep.comp') -Recurse
 $installedHealth=RunApp (Join-Path $installRoot "versions\$($manifest.version)\Compositor.exe") @('--update-health-check') 'installed-health'
 Record 'installed_offline_runtime' ($installedHealth.exitCode -eq 0) $installedHealth
 $removed=RunApp (Join-Path $installRoot 'Uninstall.exe') @('/S',"_?=$installRoot") 'installer-uninstall'
 Record 'per_user_uninstall_preserves_project' ($removed.exitCode -eq 0 -and (Test-Path -LiteralPath (Join-Path $installRoot 'keep.comp')) -and -not(Test-Path $registry) -and -not(Test-Path $verb)) $removed
}
[ordered]@{timestamp=[DateTime]::UtcNow.ToString('o');machine=$env:COMPUTERNAME;checks=$checks;cleanVM=$false;humanAcceptance=$false;developmentPathsRemoved=$true}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $run 'report.json') -Encoding utf8
Write-Output "EVIDENCE=$run"
