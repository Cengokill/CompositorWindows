param([switch]$SkipBuild,[switch]$PortableOnly,[string]$Version='0.1.0')
$ErrorActionPreference='Stop'
$packageRoot=Split-Path $PSScriptRoot -Parent
if($Version -notmatch '^\d+\.\d+\.\d+$'){throw 'Version must be a numeric semantic version'}
. (Join-Path $PSScriptRoot 'bootstrap.ps1') -Offline
& (Join-Path $PSScriptRoot 'bootstrap-packaging.ps1') -Offline -PortableOnly:$PortableOnly
Push-Location $packageRoot
try {
 if(-not $SkipBuild){
  & cmake --preset windows-x64-release; if($LASTEXITCODE){throw 'Release configure failed'}
  & cmake --build --preset windows-x64-release --parallel 4; if($LASTEXITCODE){throw 'Release build failed'}
 }
 $stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
 $output=Join-Path $packageRoot "dist\CompositorWindows-$Version-dev-$stamp"
 if(Test-Path -LiteralPath $output){throw 'Package output already exists'}
 $portable=Join-Path $output 'CompositorWindows'
 $payload=Join-Path $portable "versions\$Version"
 New-Item -ItemType Directory -Path $payload -Force | Out-Null
 $release=Join-Path $packageRoot 'build\release\Release'
 foreach($exe in @('CompositorLauncher.exe','CompositorUpdater.exe')){Copy-Item -LiteralPath (Join-Path $release $exe) -Destination $portable}
 Copy-Item -LiteralPath (Join-Path $release 'Compositor.exe') -Destination $payload
 $deploy=Join-Path $packageRoot 'dependencies\qt\bin\windeployqt.exe'
 & $deploy --release --no-translations --no-opengl-sw --no-compiler-runtime --dir $payload (Join-Path $payload 'Compositor.exe') *> (Join-Path $output 'qt-deploy.log')
 if($LASTEXITCODE){throw 'Qt application deployment failed'}
 & $deploy --release --no-translations --no-opengl-sw --no-compiler-runtime --dir $portable (Join-Path $portable 'CompositorLauncher.exe') *> (Join-Path $output 'launcher-deploy.log')
 if($LASTEXITCODE){throw 'Qt launcher deployment failed'}
 # Application-local redistributable DLLs avoid an elevated VC runtime installer.
 $crt=Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
 if(-not (Test-Path -LiteralPath $crt)){throw 'MSVC application-local redistributable directory missing'}
 Get-ChildItem -LiteralPath $crt -Filter '*.dll' | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $portable;Copy-Item -LiteralPath $_.FullName -Destination $payload}
 $imaging=Join-Path $packageRoot 'dependencies\imaging'
 $lock=Get-Content -LiteralPath (Join-Path $imaging 'lock.json') -Raw | ConvertFrom-Json
 foreach($binary in $lock.binaries){
  $binarySource=Join-Path $imaging $binary.path
  if((Get-FileHash -LiteralPath $binarySource -Algorithm SHA256).Hash -ine $binary.sha256){throw "Imaging dependency checksum mismatch: $($binary.path)"}
  Copy-Item -LiteralPath $binarySource -Destination $payload
 }
 New-Item -ItemType Directory -Path (Join-Path $payload 'models'),(Join-Path $payload 'shaders'),(Join-Path $payload 'licenses'),(Join-Path $payload 'sources') | Out-Null
 $modelSource=Join-Path $imaging 'model\birefnet-lite.onnx'
 if((Get-FileHash -LiteralPath $modelSource -Algorithm SHA256).Hash -ine $lock.model.onnx_sha256){throw 'Foreground model checksum mismatch'}
 Copy-Item -LiteralPath $modelSource -Destination (Join-Path $payload 'models')
 Copy-Item -LiteralPath (Join-Path $packageRoot 'shaders\BrushCoverage.hlsl') -Destination (Join-Path $payload 'shaders')
 Copy-Item -LiteralPath (Join-Path $packageRoot 'LICENSE') -Destination (Join-Path $payload 'licenses\Compositor-MIT.txt')
 Copy-Item -Path (Join-Path $imaging 'notices\*') -Destination (Join-Path $payload 'licenses') -Recurse
 Copy-Item -Path (Join-Path $packageRoot 'dependencies\packaging\notices\*') -Destination (Join-Path $payload 'licenses') -Recurse
 Copy-Item -LiteralPath (Join-Path $packageRoot 'dependencies\qt\sbom') -Destination (Join-Path $payload 'licenses\Qt-SBOM') -Recurse
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'PACKAGE-NOTICES.txt') -Destination (Join-Path $payload 'licenses\README.txt')
 $qtLock=Get-Content -LiteralPath (Join-Path $packageRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
 $qtSource=Join-Path $packageRoot $qtLock.qt.source.path
 if((Get-FileHash -LiteralPath $qtSource -Algorithm SHA256).Hash -ine $qtLock.qt.source.sha256){throw 'Qt corresponding source checksum mismatch'}
 Copy-Item -LiteralPath $qtSource -Destination (Join-Path $payload 'sources')
 foreach($repo in $lock.repositories){
  $archive=Join-Path $imaging "$($repo.name)-$($repo.version)-source.tar"
  if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $repo.source_archive_sha256){throw "Corresponding source mismatch: $($repo.name)"}
  Copy-Item -LiteralPath $archive -Destination (Join-Path $payload 'sources')
 }
 $source=Join-Path $output 'application-source'
 New-Item -ItemType Directory -Path $source | Out-Null
 # Copy source without traversing isolated build trees or reparse points.
 function Copy-ApplicationSource([string]$from,[string]$to){
  New-Item -ItemType Directory -Path $to -Force | Out-Null
  foreach($entry in Get-ChildItem -LiteralPath $from -Force){
   if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
   if($entry.PSIsContainer){if($entry.Name -notin @('build','ui-build','__pycache__','.git')){Copy-ApplicationSource $entry.FullName (Join-Path $to $entry.Name)}}
   elseif($entry.Extension -notin @('.exe','.dll','.obj','.pdb','.ilk','.lib','.pch','.exp','.idb','.ifc','.res')){Copy-Item -LiteralPath $entry.FullName -Destination (Join-Path $to $entry.Name)}
  }
 }
 foreach($dir in @('src','shaders','assets','scripts','docs','tests')){Copy-ApplicationSource (Join-Path $packageRoot $dir) (Join-Path $source $dir)}
 foreach($file in @('CMakeLists.txt','CMakePresets.json','dependencies.lock.json','LICENSE','README.md','KNOWN-ISSUES.md','PARITY.md','PROGRESS.md','VALIDATION.md')){Copy-Item -LiteralPath (Join-Path $packageRoot $file) -Destination $source}
 Copy-Item -LiteralPath (Join-Path $packageRoot 'docs\source-package.md') -Destination (Join-Path $source 'VERIFICATION-INPUTS.md')
 foreach($dir in @('imaging','packaging')){New-Item -ItemType Directory -Path (Join-Path $source "dependencies\$dir") -Force | Out-Null;Copy-Item -LiteralPath (Join-Path $packageRoot "dependencies\$dir\lock.json") -Destination (Join-Path $source "dependencies\$dir")}
 Copy-Item -LiteralPath (Join-Path $imaging 'model-requirements.hashes.txt') -Destination (Join-Path $source 'dependencies\imaging')
 Compress-Archive -Path (Join-Path $source '*') -DestinationPath (Join-Path $payload 'sources\CompositorWindows-source.zip') -CompressionLevel Optimal
 Copy-Item -LiteralPath (Join-Path $packageRoot 'KNOWN-ISSUES.md') -Destination (Join-Path $payload 'KNOWN-ISSUES.md')
 Copy-Item -LiteralPath (Join-Path $packageRoot 'docs\user-guide.md') -Destination (Join-Path $payload 'USER-GUIDE.md')
 & (Join-Path $PSScriptRoot 'update-initialize.ps1') -Root $portable -Version $Version -AllowTestKey
 [IO.File]::WriteAllText((Join-Path $portable 'README.txt'),"Compositor Windows $Version development build`r`nRun CompositorLauncher.exe. All image codecs, the offline foreground model and runtime DLLs are bundled.`r`nEditing and shortcut instructions: versions\$Version\USER-GUIDE.md.`r`nThis unsigned development build has unresolved parity requirements. Read versions\$Version\KNOWN-ISSUES.md.`r`nUpdate receipts use an explicitly local test key; there is no production update feed.`r`nLicenses and corresponding sources are in versions\$Version\licenses and sources.`r`n")
 $files=@(Get-ChildItem -LiteralPath $portable -Recurse -File | ForEach-Object {[ordered]@{path=[IO.Path]::GetRelativePath($portable,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})
 $revision=(& git -c "safe.directory=$($packageRoot.Replace('\','/'))" rev-parse HEAD).Trim()
 $sourceStatus=@(& git -c "safe.directory=$($packageRoot.Replace('\','/'))" status --porcelain)
 if($LASTEXITCODE){throw 'Cannot record package source status'}
 $sourceDirty=$sourceStatus.Count -gt 0
 [ordered]@{schema=1;version=$Version;channel='development';createdUtc=[DateTime]::UtcNow.ToString('o');sourceRevision=$revision;sourceDirty=$sourceDirty;sourceSnapshot="versions/$Version/sources/CompositorWindows-source.zip";upstream='a19db9011282399785dc18efcfded904627bdcc2';signed=$false;files=$files} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'package-manifest.json') -Encoding utf8
 Compress-Archive -LiteralPath $portable -DestinationPath (Join-Path $output "CompositorWindows-$Version-portable.zip") -CompressionLevel Optimal
 if(-not $PortableOnly){
  $makensis=Join-Path $packageRoot 'dependencies\packaging\nsis-3.12\makensis.exe'
  if(-not (Test-Path -LiteralPath $makensis)){throw 'Locked NSIS 3.12 missing; see docs/packaging.md'}
  # Root files are owned by the installer. Version payloads are removed only by verified receipts.
  $owned=@(Get-ChildItem -LiteralPath $portable -File | ForEach-Object {'  Delete "$INSTDIR\'+$_.Name+'"'})
  $owned | Set-Content -LiteralPath (Join-Path $output 'uninstall-root.nsh') -Encoding utf8
  & $makensis "/DPACKAGE_DIR=$portable" "/DOUTPUT_DIR=$output" "/DAPP_VERSION=$Version" (Join-Path $PSScriptRoot 'installer.nsi') *> (Join-Path $output 'installer-build.log')
  if($LASTEXITCODE){throw "Installer compilation failed: $output\installer-build.log"}
 }
 Get-ChildItem -LiteralPath $output -File | Where-Object {$_.Extension -in @('.exe','.zip')} | ForEach-Object {"$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"} | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding utf8
 Write-Output "PACKAGE=$output"
} finally {Pop-Location}
