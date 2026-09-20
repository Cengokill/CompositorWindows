param([Parameter(Mandatory)][string]$Root,[string]$Version='0.1.0',[switch]$AllowTestKey)
$ErrorActionPreference='Stop'
if(-not $AllowTestKey){throw 'Development installation receipts require -AllowTestKey'}
if($Version -notmatch '^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$'){throw 'Invalid initial version'}
$installRoot=[IO.Path]::GetFullPath($Root)
if(-not (Test-Path -LiteralPath (Join-Path $installRoot "versions/$Version/Compositor.exe") -PathType Leaf)){throw 'Place the initial application payload in versions/<version> first'}
if(Test-Path -LiteralPath (Join-Path $installRoot 'install.json')){throw 'Installation is already initialized; refusing to reset its state'}
$versionDirectory=Join-Path $installRoot "versions/$Version"
$cursor=Get-Item -LiteralPath $versionDirectory
while($null -ne $cursor){if($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Install root cannot contain reparse points'};$cursor=$cursor.Parent}
New-Item -ItemType Directory -Force (Join-Path $installRoot 'state') | Out-Null
if((Get-Item -LiteralPath (Join-Path $installRoot 'state')).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'State directory cannot be a reparse point'}
& (Join-Path $PSScriptRoot 'update-make-test-feed.ps1') -PayloadDirectory $versionDirectory -ReceiptPath (Join-Path $versionDirectory 'update-receipt.json') -Version $Version
$encoding=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $installRoot 'install.json'),(@{schema=1;product='compositor-windows'}|ConvertTo-Json -Compress),$encoding)
[IO.File]::WriteAllText((Join-Path $installRoot 'state/active.json'),(@{schema=1;product='compositor-windows';channel='test';current=$Version;previous='';pending=$false}|ConvertTo-Json -Compress),$encoding)
