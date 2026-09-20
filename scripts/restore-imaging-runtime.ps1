param([Parameter(Mandatory=$true)][string]$RuntimeDirectory)
$ErrorActionPreference='Stop'
$restoreWorkspace=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$restoreRoot=[IO.Path]::GetFullPath((Join-Path $restoreWorkspace 'dependencies\imaging'))
$restorePayload=(Get-Item -LiteralPath $RuntimeDirectory).FullName
if(-not (Test-Path -LiteralPath $restorePayload -PathType Container)){throw 'RuntimeDirectory must be the extracted application payload directory.'}
$restoreLock=Get-Content -LiteralPath (Join-Path $restoreRoot 'lock.json') -Raw | ConvertFrom-Json
if(@($restoreLock.binaries).Count -ne 4){throw 'Expected the four runtimes in the current imaging lock.'}
$restoreId=[Guid]::NewGuid().ToString('N')

function Assert-RestoreTarget([string]$Target){
    $absolute=[IO.Path]::GetFullPath($Target)
    if(-not $absolute.StartsWith($restoreRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
        throw "Runtime target escapes dependencies/imaging: $Target"
    }
    for($ancestor=$absolute;$ancestor -and $ancestor.Length -ge $restoreRoot.Length;$ancestor=Split-Path $ancestor -Parent){
        if(Test-Path -LiteralPath $ancestor){
            $item=Get-Item -LiteralPath $ancestor -Force
            if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Runtime target crosses a reparse point: $ancestor"}
        }
    }
}
function Runtime-Hash([string]$Path){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}

$restorePlan=@()
$restoreNames=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
# Complete validation precedes any directory creation, staging or destination copy.
foreach($binary in $restoreLock.binaries){
    if([IO.Path]::IsPathRooted($binary.path)){throw 'Runtime lock target must be workspace relative.'}
    $name=[IO.Path]::GetFileName($binary.path)
    if(-not $restoreNames.Add($name)){throw 'Runtime lock contains duplicate payload basenames.'}
    $source=Join-Path $restorePayload $name
    $target=[IO.Path]::GetFullPath((Join-Path $restoreRoot $binary.path))
    Assert-RestoreTarget $target
    if(-not (Test-Path -LiteralPath $source -PathType Leaf)){throw "Locked runtime missing from payload: $source"}
    $expected=([string]$binary.sha256).ToLowerInvariant()
    if($expected -notmatch '^[0-9a-f]{64}$' -or (Runtime-Hash $source) -ne $expected){throw "Locked runtime checksum mismatch: $source"}
    $original=$null
    if(Test-Path -LiteralPath $target){
        if(-not (Test-Path -LiteralPath $target -PathType Leaf)){throw "Runtime destination is not a file: $target"}
        $original=Runtime-Hash $target
    }
    $restorePlan += [pscustomobject]@{source=$source;target=$target;expected=$expected;original=$original;
        stage=$target+'.restore-'+$restoreId+'.tmp';backup=$null;installed=$false;unchanged=($original -eq $expected)}
}

try {
    # Stage and reverify all changed files before moving any previous runtime.
    foreach($entry in $restorePlan){
        if($entry.unchanged){continue}
        Assert-RestoreTarget $entry.stage
        New-Item -ItemType Directory -Path (Split-Path $entry.target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $entry.source -Destination $entry.stage
        if((Runtime-Hash $entry.stage) -ne $entry.expected){throw "Staged runtime checksum mismatch: $($entry.source)"}
    }
    foreach($entry in $restorePlan){
        if($entry.unchanged){continue}
        Assert-RestoreTarget $entry.target
        $current=if(Test-Path -LiteralPath $entry.target){Runtime-Hash $entry.target}else{$null}
        if($current -ne $entry.original){throw "Runtime changed while preparing restore: $($entry.target)"}
        if($entry.original){
            $entry.backup=$entry.target+'.before-runtime-'+$entry.original+'-'+$restoreId+'.bak'
            Assert-RestoreTarget $entry.backup
            Move-Item -LiteralPath $entry.target -Destination $entry.backup
        }
        Move-Item -LiteralPath $entry.stage -Destination $entry.target
        $entry.installed=$true
    }
    foreach($entry in $restorePlan){if((Runtime-Hash $entry.target) -ne $entry.expected){throw "Installed runtime verification failed: $($entry.target)"}}
} catch {
    $failure=$_
    $rollbackErrors=[Collections.Generic.List[string]]::new()
    for($index=$restorePlan.Count-1;$index -ge 0;--$index){
        $entry=$restorePlan[$index]
        if($entry.unchanged){continue}
        try {
            Assert-RestoreTarget $entry.target
            if($entry.installed -and (Test-Path -LiteralPath $entry.target)){
                # Preserve installed or concurrently changed bytes; do not delete originals.
                $retained=$entry.target+'.failed-restore-'+(Runtime-Hash $entry.target)+'-'+$restoreId+'.bak'
                Assert-RestoreTarget $retained
                Move-Item -LiteralPath $entry.target -Destination $retained
            }
            if($entry.backup -and (Test-Path -LiteralPath $entry.backup)){
                if(Test-Path -LiteralPath $entry.target){throw "Cannot restore original over a concurrently created file: $($entry.target); original retained at $($entry.backup)"}
                Move-Item -LiteralPath $entry.backup -Destination $entry.target
            }
        } catch {$rollbackErrors.Add($_.Exception.Message)}
    }
    if($rollbackErrors.Count){throw ($failure.Exception.Message+'; rollback requires attention: '+($rollbackErrors -join '; '))}
    throw $failure
}
$restorePlan | ForEach-Object {[ordered]@{target=$_.target;sha256=$_.expected;status=$(if($_.unchanged){'already_verified'}else{'restored'});preserved_previous_file=$_.backup}}
