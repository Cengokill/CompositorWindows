param([switch]$Offline,[switch]$PortableOnly)
$ErrorActionPreference='Stop'
$packagingTaskRoot=Split-Path $PSScriptRoot -Parent
$packagingTaskLock=Get-Content -LiteralPath (Join-Path $packagingTaskRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
function Get-LockedPackagingAsset([string]$Url,[string]$Path,[string]$Hash){
    if(-not (Test-Path -LiteralPath $Path)){
        if($Offline){throw "Offline packaging asset missing: $Path. Run scripts/bootstrap-packaging.ps1 once with network access."}
        New-Item -ItemType Directory -Path (Split-Path $Path -Parent) -Force | Out-Null
        $partial=$Path+'.partial-'+[Guid]::NewGuid().ToString('N')
        Invoke-WebRequest -Uri $Url -OutFile $partial
        if((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ine $Hash){throw "Downloaded packaging asset hash mismatch; retained $partial"}
        Move-Item -LiteralPath $partial -Destination $Path
    }
    if((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ine $Hash){throw "Packaging asset hash mismatch: $Path"}
}
$packagingTaskQt=$packagingTaskLock.qt.source
Get-LockedPackagingAsset $packagingTaskQt.url (Join-Path $packagingTaskRoot $packagingTaskQt.path) $packagingTaskQt.sha256
if(-not $PortableOnly){
    if(-not(Get-Command dotnet -ErrorAction SilentlyContinue)){throw 'MSI packaging requires a .NET runtime (6 or later); application users do not need .NET.'}
    $packagingTaskTools=Join-Path $packagingTaskRoot 'dependencies\packaging'
    $packagingTaskWix=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'msi\toolchain.json') -Raw | ConvertFrom-Json
    foreach($packagingTaskAsset in $packagingTaskWix.assets){
        $packagingTaskArchive=Join-Path $packagingTaskTools $packagingTaskAsset.archive
        Get-LockedPackagingAsset $packagingTaskAsset.url $packagingTaskArchive $packagingTaskAsset.sha256
        $packagingTaskExtract=Join-Path $packagingTaskTools $packagingTaskAsset.directory
        if(-not(Test-Path -LiteralPath (Join-Path $packagingTaskExtract $packagingTaskAsset.entry))){
            [IO.Compression.ZipFile]::ExtractToDirectory($packagingTaskArchive,$packagingTaskExtract)
        }
    }
    $packagingTaskCompiler=Join-Path (Join-Path $packagingTaskTools $packagingTaskWix.assets[0].directory) $packagingTaskWix.assets[0].entry
    $packagingTaskVersion=& dotnet exec --roll-forward Major $packagingTaskCompiler --version
    if($LASTEXITCODE -or $packagingTaskVersion -notlike "$($packagingTaskWix.version)+*"){throw 'The locked WiX compiler could not run'}
    Get-LockedPackagingAsset $packagingTaskWix.license.url (Join-Path $packagingTaskTools $packagingTaskWix.license.path) $packagingTaskWix.license.sha256
}
Write-Output 'Locked packaging source and installer assets are ready.'
