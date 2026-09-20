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
    $packagingTaskNsisRoot=Join-Path $packagingTaskRoot 'dependencies\packaging'
    $packagingTaskNsis=(Get-Content -LiteralPath (Join-Path $packagingTaskNsisRoot 'lock.json') -Raw | ConvertFrom-Json).nsis
    $packagingTaskArchive=Join-Path $packagingTaskNsisRoot $packagingTaskNsis.archive
    Get-LockedPackagingAsset $packagingTaskNsis.url $packagingTaskArchive $packagingTaskNsis.sha256
    $packagingTaskCompiler=Join-Path $packagingTaskNsisRoot "nsis-$($packagingTaskNsis.version)\makensis.exe"
    if(-not (Test-Path -LiteralPath $packagingTaskCompiler)){
        Expand-Archive -LiteralPath $packagingTaskArchive -DestinationPath $packagingTaskNsisRoot
    }
    if(-not (Test-Path -LiteralPath $packagingTaskCompiler)){throw 'NSIS archive did not contain the locked compiler path'}
    $packagingTaskLicense=Join-Path (Split-Path $packagingTaskCompiler -Parent) 'COPYING'
    if(-not (Test-Path -LiteralPath $packagingTaskLicense)){throw 'NSIS license is missing from its verified archive'}
    New-Item -ItemType Directory -Path (Join-Path $packagingTaskNsisRoot 'notices') -Force | Out-Null
    Copy-Item -LiteralPath $packagingTaskLicense -Destination (Join-Path $packagingTaskNsisRoot $packagingTaskNsis.license_file)
}
Write-Output 'Locked packaging source and installer assets are ready.'
