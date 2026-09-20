param([Parameter(Mandatory)][string]$PackageDirectory,[switch]$Install)
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
