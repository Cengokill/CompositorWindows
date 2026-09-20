param([string]$Python='C:\Users\blurr\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',[switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $SkipBuild){& (Join-Path $root 'scripts/update-build.ps1')}
$bin=Join-Path $PSScriptRoot 'bin'
$helper=Join-Path $bin 'CompositorUpdater.exe'
$run=Join-Path $PSScriptRoot ('results/run-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Force $run | Out-Null
$log=Join-Path $run 'commands.log';$passed=[Collections.Generic.List[string]]::new()
function Assert($Value,[string]$Message){if(-not $Value){throw $Message}}
function Pass([string]$Name){$passed.Add($Name);Write-Host "PASS update.$Name"}
function Helper([string[]]$Arguments,[int]$Expected=0){
    $output=@(& $helper @Arguments 2>&1);$code=$LASTEXITCODE
    [IO.File]::AppendAllLines($log,[string[]](@(('> '+($Arguments -join ' ')),('exit='+$code))+@($output|ForEach-Object {$_.ToString()})))
    Assert ($code -eq $Expected) "Helper exit $code expected $Expected for $($Arguments -join ' '): $output"
    return ($output -join "`n")
}
function Current([string]$Install){return (Get-Content -LiteralPath (Join-Path $Install 'state/active.json') -Raw|ConvertFrom-Json)}
function Make-Install([string]$Name){
    $install=Join-Path $run $Name;$version=Join-Path $install 'versions/0.1.0'
    New-Item -ItemType Directory -Force $version | Out-Null
    Copy-Item -LiteralPath (Join-Path $bin 'FixtureApp.exe') -Destination (Join-Path $version 'Compositor.exe')
    foreach($name in 'CompositorLauncher.exe','CompositorUpdater.exe','Qt6Core.dll'){Copy-Item -LiteralPath (Join-Path $bin $name) -Destination $install}
    & (Join-Path $root 'scripts/update-initialize.ps1') -Root $install -AllowTestKey
    return $install
}
function Make-Feed([string]$Name,[string]$Version,[switch]$Unhealthy,[string]$Channel='test'){
    $payload=Join-Path $run ('payload-'+$Name.Replace('/','-'));New-Item -ItemType Directory -Force (Join-Path $payload 'data') | Out-Null
    Copy-Item -LiteralPath (Join-Path $bin $(if($Unhealthy){'UnhealthyApp.exe'}else{'FixtureApp.exe'})) -Destination (Join-Path $payload 'Compositor.exe')
    $data=[byte[]]::new(2*1024*1024);for($i=0;$i -lt $data.Length;$i+=4096){$data[$i]=[byte](($i/4096)%251)}
    [IO.File]::WriteAllBytes((Join-Path $payload 'data/large.bin'),$data)
    [IO.File]::WriteAllText((Join-Path $payload 'data/日本語.txt'),'Unicode payload')
    $feed=Join-Path $run $Name
    & (Join-Path $root 'scripts/update-make-test-feed.ps1') -PayloadDirectory $payload -FeedDirectory $feed -Version $Version -Channel $Channel
    return $feed
}
function Resign([string]$Feed,[scriptblock]$Change){
    $path=Join-Path $Feed 'feed.json';$envelope=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
    $content=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($envelope.payload))|ConvertFrom-Json
    & $Change $content
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($content|ConvertTo-Json -Depth 8 -Compress))
    $rsa=[Security.Cryptography.RSA]::Create();$rsa.FromXmlString([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures/TEST-ONLY-private-key.xml')))
    try{$signature=$rsa.SignData($bytes,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)}finally{$rsa.Dispose()}
    $envelope.payload=[Convert]::ToBase64String($bytes);$envelope.signature=[Convert]::ToBase64String($signature)
    [IO.File]::WriteAllText($path,($envelope|ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
}
$install=Make-Install 'Install with 日本語 space'
$outside=Join-Path $run 'User Projects/守られた Sample.comp';$inside=Join-Path $install 'Unrelated Project.comp'
New-Item -ItemType Directory -Force $outside,$inside | Out-Null
foreach($project in $outside,$inside){[IO.File]::WriteAllText((Join-Path $project 'manifest.json'),'user project bytes must survive');[IO.File]::WriteAllBytes((Join-Path $project 'image.png'),[byte[]](1,2,3,4,5))}
$projectHashes=@{};foreach($project in $outside,$inside){foreach($file in Get-ChildItem -LiteralPath $project){$projectHashes[$file.FullName]=(Get-FileHash -LiteralPath $file.FullName).Hash}}
$env:COMPOSITOR_TEST_HEALTH_LOG=Join-Path $run 'health.log'
$feed=Make-Feed 'valid' '0.2.0'
$null=Helper @('check','--root',$install,'--test-feed',$feed) 2
Pass 'test_key_requires_opt_in'
$check=Helper @('check','--root',$install,'--test-feed',$feed,'--allow-test-key')|ConvertFrom-Json
Assert $check.available 'Signed release should be available';Pass 'signed_metadata_check'
$null=Helper @('install','--root',$install,'--test-feed',$feed,'--allow-test-key')
Assert ((Current $install).current -eq '0.2.0') 'Valid update did not activate'
Assert (Test-Path -LiteralPath (Join-Path $install 'versions/0.1.0/Compositor.exe')) 'Prior version missing'
Assert ((Get-Content -LiteralPath $env:COMPOSITOR_TEST_HEALTH_LOG).Count -eq 1) 'Real payload health process did not run'
Pass 'install_verified_payload_and_health'
$check=Helper @('check','--root',$install,'--test-feed',$feed,'--allow-test-key')|ConvertFrom-Json
Assert (-not $check.available) 'Same version should be unavailable';Pass 'same_version_unavailable'
$lower=Make-Feed 'downgrade' '0.1.0';$null=Helper @('check','--root',$install,'--test-feed',$lower,'--allow-test-key') 2
$wrong=Make-Feed 'channel' '0.3.0' -Channel 'stable';$null=Helper @('check','--root',$install,'--test-feed',$wrong,'--allow-test-key') 2
$invalid=Make-Feed 'version' '00.3.0';$null=Helper @('check','--root',$install,'--test-feed',$invalid,'--allow-test-key') 2
Pass 'strict_version_and_channel'
$bad=Make-Feed 'signature' '0.3.0';$badFile=Join-Path $bad 'feed.json';$signed=Get-Content -LiteralPath $badFile -Raw|ConvertFrom-Json;$signature=[Convert]::FromBase64String($signed.signature);$signature[0]=$signature[0] -bxor 1;$signed.signature=[Convert]::ToBase64String($signature);[IO.File]::WriteAllText($badFile,($signed|ConvertTo-Json -Compress))
$null=Helper @('install','--root',$install,'--test-feed',$bad,'--allow-test-key') 2
Assert ((Current $install).current -eq '0.2.0') 'Signature rejection changed active version';Pass 'signature_rejection'
$bad=Make-Feed 'tamper' '0.3.0';[IO.File]::AppendAllText((Join-Path $bad 'payload/Compositor.exe'),'tamper')
$null=Helper @('install','--root',$install,'--test-feed',$bad,'--allow-test-key') 2
Assert ((Get-Content -LiteralPath $env:COMPOSITOR_TEST_HEALTH_LOG).Count -eq 1) 'Unverified payload executed';Pass 'payload_hash_rejection_before_execution'
$bad=Make-Feed 'traversal' '0.3.0';Resign $bad {param($m)$m.files[1].path='../Unrelated Project.comp/manifest.json'}
$null=Helper @('install','--root',$install,'--test-feed',$bad,'--allow-test-key') 2
$bad=Make-Feed 'reserved' '0.3.0';Resign $bad {param($m)$m.files[1].path='NUL.txt'}
$null=Helper @('check','--root',$install,'--test-feed',$bad,'--allow-test-key') 2
$bad=Make-Feed 'duplicate' '0.3.0';Resign $bad {param($m)$m.files[1].path='compositor.EXE'}
$null=Helper @('check','--root',$install,'--test-feed',$bad,'--allow-test-key') 2
Pass 'signed_unsafe_paths_and_duplicates_rejected'
$cancel=Make-Feed 'cancel' '0.3.0';$null=Helper @('install','--root',$install,'--test-feed',$cancel,'--allow-test-key','--cancel-after-bytes','65536') 2
Assert ((Current $install).current -eq '0.2.0' -and -not (Test-Path -LiteralPath (Join-Path $install 'versions/0.3.0'))) 'Download cancellation activated incomplete payload';Pass 'mid_download_cancel'
$unhealthy=Make-Feed 'unhealthy' '0.3.0' -Unhealthy
$null=Helper @('install','--root',$install,'--test-feed',$unhealthy,'--allow-test-key') 2
Assert ((Current $install).current -eq '0.2.0' -and -not (Current $install).pending) 'Unhealthy update failed to roll back';Pass 'real_health_failure_rollback'
$next=Make-Feed 'rename-failure' '0.4.0';$null=Helper @('install','--root',$install,'--test-feed',$next,'--allow-test-key','--fault','after-version') 2
Assert ((Current $install).current -eq '0.2.0') 'Pre-activation failure changed pointer'
$null=Helper @('install','--root',$install,'--test-feed',$next,'--allow-test-key')
Assert ((Current $install).current -eq '0.4.0') 'Verified retained version could not retry';Pass 'rename_failure_retains_previous_and_retry'
$crash=Make-Feed 'crash' '0.5.0';$null=Helper @('install','--root',$install,'--test-feed',$crash,'--allow-test-key','--fault','crash-after-activation') 86
Assert ((Current $install).pending -and (Current $install).current -eq '0.5.0') 'Crash fixture did not leave pending activation'
$record=Join-Path $run 'forwarded.txt';$expected=@($outside,'a value with spaces','quote"inside','trailing\','日本語 Ω','')
& (Join-Path $install 'CompositorLauncher.exe') '--record' $record @expected
for($i=0;$i -lt 100 -and -not (Test-Path -LiteralPath $record);$i++){Start-Sleep -Milliseconds 50}
Assert (Test-Path -LiteralPath $record) 'Launcher did not run previous application'
$received=[IO.File]::ReadAllLines($record,[Text.Encoding]::Unicode)
Assert (($received -join '|') -ceq ($expected -join '|')) 'Launcher corrupted Unicode/quoted/trailing-backslash arguments'
Assert ((Current $install).current -eq '0.4.0' -and -not (Current $install).pending) 'Launcher did not recover interrupted activation';Pass 'crash_recovery_and_unicode_argument_forwarding'
$locked=Make-Feed 'locked-pointer' '0.6.0';$held=[IO.File]::Open((Join-Path $install 'state/active.json'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try{$null=Helper @('install','--root',$install,'--test-feed',$locked,'--allow-test-key') 2}finally{$held.Dispose()}
Assert ((Current $install).current -eq '0.4.0') 'Filesystem install failure changed version';Pass 'real_pointer_replacement_failure'
$alias=Join-Path $run 'install-junction';New-Item -ItemType Junction -Path $alias -Target $install | Out-Null
$null=Helper @('check','--root',$alias,'--test-feed',$feed,'--allow-test-key') 2
Pass 'reparse_install_root_rejected'
$httpGood=Make-Feed 'http/valid' '0.7.0';$httpBad=Make-Feed 'http/interrupt' '0.8.0'
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
$server=Start-Process -FilePath $Python -ArgumentList @((Join-Path $PSScriptRoot 'serve_feed.py'),'--port',"$port") -WorkingDirectory $run -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'http.log') -RedirectStandardError (Join-Path $run 'http-errors.log')
try{
    $ready=$false;for($i=0;$i -lt 100;$i++){try{$client=[Net.Sockets.TcpClient]::new();$client.Connect('127.0.0.1',$port);$client.Dispose();$ready=$true;break}catch{Start-Sleep -Milliseconds 50}}
    Assert $ready 'Local HTTP test feed did not start'
    $null=Helper @('install','--root',$install,'--test-feed',"http://127.0.0.1:$port/http/valid/",'--allow-test-key')
    Assert ((Current $install).current -eq '0.7.0') 'Loopback HTTP install failed';Pass 'loopback_http_install'
    $null=Helper @('install','--root',$install,'--test-feed',"http://127.0.0.1:$port/http/interrupt/",'--allow-test-key') 2
    Assert ((Current $install).current -eq '0.7.0' -and -not (Test-Path -LiteralPath (Join-Path $install 'versions/0.8.0'))) 'Interrupted HTTP payload activated';Pass 'real_http_disconnect_preserves_previous'
}finally{if(-not $server.HasExited){Stop-Process -Id $server.Id}}
foreach($file in $projectHashes.Keys){Assert ((Get-FileHash -LiteralPath $file).Hash -eq $projectHashes[$file]) "Project changed: $file"}
Pass 'inside_and_outside_project_bytes_preserved'
$userInVersion=Join-Path $install 'versions/0.7.0/Personal Work.comp';New-Item -ItemType Directory -Force $userInVersion | Out-Null
[IO.File]::WriteAllText((Join-Path $userInVersion 'manifest.json'),'personal project inside version directory')
$modified=Join-Path $install 'versions/0.7.0/data/日本語.txt';[IO.File]::WriteAllText($modified,'user modified this originally packaged file')
$unknown=Join-Path $install 'versions/0.90.0';New-Item -ItemType Directory -Force $unknown | Out-Null
Copy-Item -LiteralPath (Join-Path $bin 'FixtureApp.exe') -Destination (Join-Path $unknown 'Compositor.exe')
$versionAlias=Join-Path $install 'versions/0.91.0';New-Item -ItemType Junction -Path $versionAlias -Target $outside | Out-Null
$null=Helper @('--uninstall-payloads','--root',$install) 2
$uninstalled=Helper @('--uninstall-payloads','--root',$install,'--allow-test-key')|ConvertFrom-Json
Assert ($uninstalled.removedFiles -ge 10) 'Verified updater payloads were not removed'
Assert (-not (Test-Path -LiteralPath (Join-Path $install 'versions/0.1.0/Compositor.exe'))) 'Signed baseline executable was not removed'
Assert (-not (Test-Path -LiteralPath (Join-Path $install 'versions/0.7.0/Compositor.exe'))) 'Signed updated executable was not removed'
Assert (Test-Path -LiteralPath (Join-Path $install 'CompositorLauncher.exe')) 'Helper removed stable installer-owned launcher'
Assert ([IO.File]::ReadAllText($modified) -eq 'user modified this originally packaged file') 'Uninstaller deleted modified payload path'
Assert ([IO.File]::ReadAllText((Join-Path $userInVersion 'manifest.json')) -eq 'personal project inside version directory') 'Uninstaller deleted project within version directory'
Assert (Test-Path -LiteralPath (Join-Path $unknown 'Compositor.exe')) 'Uninstaller deleted untrusted version payload'
Pass 'verified_payload_uninstall_preserves_user_changes'
foreach($file in $projectHashes.Keys){Assert ((Get-FileHash -LiteralPath $file).Hash -eq $projectHashes[$file]) "Project changed during uninstall: $file"}
Pass 'uninstall_preserves_projects_and_reparse_targets'
@{status='pass';cases=$passed.ToArray();case_count=$passed.Count;production_signing='not_available';clean_machine='not_run';install=$install;test_key_sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'fixtures/TEST-ONLY-private-key.xml')).Hash;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $run 'results.json') -Encoding utf8
Write-Host "Updater test evidence: $run"


