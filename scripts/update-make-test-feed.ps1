[CmdletBinding(DefaultParameterSetName='Feed')]
param([Parameter(Mandatory)][string]$PayloadDirectory,[Parameter(Mandatory,ParameterSetName='Feed')][string]$FeedDirectory,[Parameter(Mandatory,ParameterSetName='Receipt')][string]$ReceiptPath,[Parameter(Mandatory)][string]$Version,[string]$Channel='test')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$source=[IO.Path]::GetFullPath($PayloadDirectory)
$feed=if($PSCmdlet.ParameterSetName -eq 'Feed'){[IO.Path]::GetFullPath($FeedDirectory)}else{''}
$outputPath=if($feed){Join-Path $feed 'feed.json'}else{[IO.Path]::GetFullPath($ReceiptPath)}
if(Test-Path -LiteralPath $outputPath){throw 'Test metadata already exists; use a fresh directory'}
if(-not (Test-Path -LiteralPath (Join-Path $source 'Compositor.exe'))){throw 'Payload requires Compositor.exe'}
$files=@()
$pending=[Collections.Generic.Stack[string]]::new();$pending.Push($source)
while($pending.Count){$folder=$pending.Pop();foreach($entry in Get-ChildItem -LiteralPath $folder -Force){
    if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Payload contains a reparse point'}
    if($entry.PSIsContainer){$pending.Push($entry.FullName);continue}
    $relative=[IO.Path]::GetRelativePath($source,$entry.FullName).Replace('\','/')
    if($relative -ieq 'update-receipt.json'){continue}
    if($feed){
        $target=Join-Path (Join-Path $feed 'payload') $relative
        New-Item -ItemType Directory -Force (Split-Path $target -Parent) | Out-Null
        Copy-Item -LiteralPath $entry.FullName -Destination $target
    }
    $files+=@{path=$relative;size=$entry.Length;sha256=(Get-FileHash -LiteralPath $entry.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}}
$payload=@{schema=1;product='compositor-windows';channel=$Channel;version=$Version;files=@($files|Sort-Object path)}|ConvertTo-Json -Depth 6 -Compress
$bytes=[Text.UTF8Encoding]::new($false).GetBytes($payload)
$rsa=[Security.Cryptography.RSA]::Create();$rsa.FromXmlString([IO.File]::ReadAllText((Join-Path $root 'tests/update/fixtures/TEST-ONLY-private-key.xml')))
try{$signature=$rsa.SignData($bytes,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)}finally{$rsa.Dispose()}
$envelope=@{keyId='compositor-local-update-test-rsa3072-v1';payload=[Convert]::ToBase64String($bytes);signature=[Convert]::ToBase64String($signature)}|ConvertTo-Json -Compress
[IO.File]::WriteAllText($outputPath,$envelope,[Text.UTF8Encoding]::new($false))
