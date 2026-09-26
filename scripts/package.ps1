param([switch]$SkipBuild,[switch]$PortableOnly,[string]$Version='0.1.4')
$ErrorActionPreference='Stop'
$packageRoot=Split-Path $PSScriptRoot -Parent
if($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'){throw 'Version must be a numeric semantic version'}
$versionParts=$Version.Split('.')
if([long]$versionParts[0] -gt 255 -or [long]$versionParts[1] -gt 255 -or [long]$versionParts[2] -gt 65535){throw 'MSI version must fit 255.255.65535'}
. (Join-Path $PSScriptRoot 'bootstrap.ps1') -Offline
& (Join-Path $PSScriptRoot 'bootstrap-packaging.ps1') -Offline -PortableOnly:$PortableOnly
Push-Location $packageRoot
try {
 if(-not $SkipBuild){
  & cmake --preset windows-x64-release; if($LASTEXITCODE){throw 'Release configure failed'}
  & cmake --build --preset windows-x64-release --target Compositor --parallel 4; if($LASTEXITCODE){throw 'Release build failed'}
 }
 $stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
 $output=Join-Path $packageRoot "dist\CompositorWindows-$Version-preview-$stamp"
 if(Test-Path -LiteralPath $output){throw 'Package output already exists'}
 $portable=Join-Path $output 'CompositorWindows'
 $payload=$portable
 New-Item -ItemType Directory -Path $payload | Out-Null
 $release=Join-Path $packageRoot 'build\release\Release'
 Copy-Item -LiteralPath (Join-Path $release 'Compositor.exe') -Destination $payload
 $deploy=Join-Path $packageRoot 'dependencies\qt\bin\windeployqt.exe'
 & $deploy --release --no-translations --no-opengl-sw --no-compiler-runtime --dir $payload (Join-Path $payload 'Compositor.exe') *> (Join-Path $output 'qt-deploy.log')
 if($LASTEXITCODE){throw 'Qt application deployment failed'}
 # Application-local redistributable DLLs avoid an elevated VC runtime installer.
 $crt=Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
 if(-not (Test-Path -LiteralPath $crt)){throw 'MSVC application-local redistributable directory missing'}
 Get-ChildItem -LiteralPath $crt -Filter '*.dll' | Copy-Item -Destination $payload
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
 Copy-Item -LiteralPath (Join-Path $packageRoot 'dependencies\packaging\notices\Qt') -Destination (Join-Path $payload 'licenses\Qt') -Recurse
 Copy-Item -LiteralPath (Join-Path $packageRoot 'dependencies\qt\sbom') -Destination (Join-Path $payload 'licenses\Qt-SBOM') -Recurse
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'msi\PACKAGE-NOTICES.txt') -Destination (Join-Path $payload 'licenses\README.txt')
 $wixLicense=Join-Path $packageRoot 'dependencies\packaging\WIX-LICENSE.txt'
 if(Test-Path -LiteralPath $wixLicense){Copy-Item -LiteralPath $wixLicense -Destination (Join-Path $payload 'licenses')}
 $qtLock=Get-Content -LiteralPath (Join-Path $packageRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
 $qtSource=Join-Path $packageRoot $qtLock.qt.source.path
 if((Get-FileHash -LiteralPath $qtSource -Algorithm SHA256).Hash -ine $qtLock.qt.source.sha256){throw 'Qt corresponding source checksum mismatch'}
 Copy-Item -LiteralPath $qtSource -Destination (Join-Path $payload 'sources')
 foreach($repo in $lock.repositories){
  $archive=Join-Path $imaging "$($repo.name)-$($repo.version)-source.tar"
  if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $repo.source_archive_sha256){throw "Corresponding source mismatch: $($repo.name)"}
  Copy-Item -LiteralPath $archive -Destination (Join-Path $payload 'sources')
 }
 Copy-Item -LiteralPath (Join-Path $packageRoot 'assets/fonts/Inter-LICENSE.txt') -Destination (Join-Path $payload 'licenses/Inter-LICENSE.txt')
 $source=Join-Path $output 'application-source'
 New-Item -ItemType Directory -Path $source | Out-Null
 function Copy-ApplicationSource([string]$from,[string]$to,[string[]]$extensions){
  foreach($entry in Get-ChildItem -LiteralPath $from -Force){
   if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
   if($entry.PSIsContainer){
    if($entry.Name -notin @('build','ui-build','CMakeFiles','__pycache__','.git','.venv','node_modules')){Copy-ApplicationSource $entry.FullName (Join-Path $to $entry.Name) $extensions}
   }elseif($entry.Extension.ToLowerInvariant() -in $extensions -or $entry.Name -in @('LICENSE','COPYING','NOTICE')){
    New-Item -ItemType Directory -Path $to -Force | Out-Null
    Copy-Item -LiteralPath $entry.FullName -Destination (Join-Path $to $entry.Name)
   }
  }
 }
 foreach($dir in @('src','shaders')){Copy-ApplicationSource (Join-Path $packageRoot $dir) (Join-Path $source $dir) @('.cpp','.c','.h','.hpp','.hlsl','.rc','.manifest','.in')}
 Copy-ApplicationSource (Join-Path $packageRoot 'assets') (Join-Path $source 'assets') @('.ico','.svg','.png','.txt','.ttf')
 $testSources=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 $includeDirectories=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 [void]$includeDirectories.Add((Join-Path $packageRoot 'src'))
 [void]$includeDirectories.Add($packageRoot)
 $projects=@(Get-ChildItem -LiteralPath (Join-Path $packageRoot 'build\release') -Filter '*.vcxproj')
 if(-not $projects.Count){throw 'Configure the Release build before packaging its matching test sources'}
 foreach($project in $projects){
  [xml]$projectXml=Get-Content -LiteralPath $project.FullName
  foreach($node in $projectXml.SelectNodes('//*[local-name()="ClCompile"][@Include]')){
   $path=[IO.Path]::GetFullPath($node.Include,$project.DirectoryName)
   if($path.StartsWith((Join-Path $packageRoot 'tests')+'\',[StringComparison]::OrdinalIgnoreCase)){[void]$testSources.Add($path)}
  }
  foreach($node in $projectXml.SelectNodes('//*[local-name()="AdditionalIncludeDirectories"]')){
   foreach($path in $node.InnerText.Split(';')){
    if($path.StartsWith($packageRoot+'\',[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $path -PathType Container)){[void]$includeDirectories.Add($path)}
   }
  }
 }
 $cmakeQueue=[Collections.Generic.Queue[string]]::new(); $cmakeQueue.Enqueue((Join-Path $packageRoot 'CMakeLists.txt'))
 $cmakeSeen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 while($cmakeQueue.Count){
  $cmakeFile=$cmakeQueue.Dequeue(); if(-not $cmakeSeen.Add($cmakeFile)){continue}
  foreach($match in [regex]::Matches((Get-Content -LiteralPath $cmakeFile -Raw),'tests/[A-Za-z0-9_./-]+\.(?:cmake|ps1|py)')){
   $path=Join-Path $packageRoot $match.Value
   if(Test-Path -LiteralPath $path -PathType Leaf){[void]$testSources.Add($path);if($path.EndsWith('.cmake')){$cmakeQueue.Enqueue($path)}}
  }
 }
 [void]$testSources.Add((Join-Path $packageRoot 'tests\packaging\run_checks.ps1'))
 $includeQueue=[Collections.Generic.Queue[string]]::new()
 foreach($path in $testSources){$includeQueue.Enqueue($path)}
 while($includeQueue.Count){
  $path=$includeQueue.Dequeue()
  foreach($match in [regex]::Matches((Get-Content -LiteralPath $path -Raw),'(?m)^\s*#\s*include\s*"([^"]+)"')){
   foreach($directory in @((Split-Path $path -Parent))+@($includeDirectories)){
    $included=[IO.Path]::GetFullPath((Join-Path $directory $match.Groups[1].Value))
    if(Test-Path -LiteralPath $included -PathType Leaf){
     if($included.StartsWith((Join-Path $packageRoot 'tests')+'\',[StringComparison]::OrdinalIgnoreCase) -and $testSources.Add($included)){$includeQueue.Enqueue($included)}
     break
    }
   }
  }
 }
 foreach($path in $testSources){
  if($path -match '[\\/](?:[^\\/]*_autogen|CMakeFiles|CompilerId[^\\/]*)[\\/]'){throw "Generated source entered the package: $path"}
  $destination=Join-Path $source ([IO.Path]::GetRelativePath($packageRoot,$path))
  New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
  Copy-Item -LiteralPath $path -Destination $destination
 }
 New-Item -ItemType Directory -Path (Join-Path $source 'scripts'),(Join-Path $source 'docs') | Out-Null
 foreach($script in @('bootstrap.ps1','bootstrap-imaging.ps1','bootstrap-packaging.ps1','deploy-imaging-runtime.cmake','restore-imaging-runtime.ps1','package.ps1')){
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination (Join-Path $source 'scripts')
 }
 Copy-ApplicationSource (Join-Path $PSScriptRoot 'msi') (Join-Path $source 'scripts\msi') @('.ps1','.wxs','.json','.txt')
 foreach($doc in @('README.md','source-package.md','packaging.md','user-guide.md','release-notes.md','architecture.md','project-format-v7.md')){
  $docPath=Join-Path $packageRoot "docs\$doc"
  if(Test-Path -LiteralPath $docPath){Copy-Item -LiteralPath $docPath -Destination (Join-Path $source 'docs')}
 }
 foreach($file in @('CMakeLists.txt','CMakePresets.json','dependencies.lock.json','LICENSE','README.md','CONTRIBUTING.md','AGENTS.md','PROGRESS.md','VALIDATION.md','KNOWN-ISSUES.md')){Copy-Item -LiteralPath (Join-Path $packageRoot $file) -Destination $source}
 Copy-ApplicationSource (Join-Path $packageRoot 'reference') (Join-Path $source 'reference') @('.py','.swift','.md')
 Copy-ApplicationSource (Join-Path $packageRoot 'docs/images') (Join-Path $source 'docs/images') @('.png')
 New-Item -ItemType Directory -Path (Join-Path $source 'demo') -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $packageRoot 'demo/README.md') -Destination (Join-Path $source 'demo/README.md')
 foreach($fixture in @('tests/display_profile/fixtures/linear-rgb.icc','tests/display_profile/fixtures/manifest.json','tests/imaging/quality_criteria.json')){
  $destination=Join-Path $source $fixture
  New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
  Copy-Item -LiteralPath (Join-Path $packageRoot $fixture) -Destination $destination
 }
 foreach($dir in @('imaging','packaging')){
  New-Item -ItemType Directory -Path (Join-Path $source "dependencies\$dir") -Force | Out-Null
  Copy-Item -LiteralPath (Join-Path $packageRoot "dependencies\$dir\lock.json") -Destination (Join-Path $source "dependencies\$dir")
 }
 Copy-Item -LiteralPath (Join-Path $imaging 'model-requirements.hashes.txt') -Destination (Join-Path $source 'dependencies\imaging')
 Copy-Item -LiteralPath (Join-Path $packageRoot 'dependencies\packaging\notices') -Destination (Join-Path $source 'dependencies\packaging\notices') -Recurse
 $sourceArchive=Join-Path $output "CompositorWindows-$Version-source.zip"
 Compress-Archive -Path (Join-Path $source '*') -DestinationPath $sourceArchive -CompressionLevel Optimal
 Copy-Item -LiteralPath $sourceArchive -Destination (Join-Path $payload 'sources\CompositorWindows-source.zip')
 Copy-Item -LiteralPath (Join-Path $packageRoot 'KNOWN-ISSUES.md') -Destination (Join-Path $payload 'KNOWN-ISSUES.md')
 Copy-Item -LiteralPath (Join-Path $packageRoot 'docs\user-guide.md') -Destination (Join-Path $payload 'USER-GUIDE.md')
 Copy-Item -LiteralPath (Join-Path $packageRoot 'docs\release-notes.md') -Destination (Join-Path $payload 'RELEASE-NOTES.md')
 $encoding=[Text.UTF8Encoding]::new($false)
 $policy=[ordered]@{schema=1;channel='community-preview';automaticUpdates=$false;upstream='a19db9011282399785dc18efcfded904627bdcc2'}
 [IO.File]::WriteAllText((Join-Path $payload 'release-policy.json'),($policy|ConvertTo-Json),$encoding)
 $readme=@"
Compositor Windows $Version preview

Run Compositor.exe. Windows 11 x64 is required. Codecs, the offline foreground model and runtime DLLs are included.
Read RELEASE-NOTES.md, USER-GUIDE.md and KNOWN-ISSUES.md. Save projects outside the application directory.
Updates are manual: install a newer MSI or extract a newer portable download into a new directory.
This build is unsigned. Windows may show a publisher warning. Keep Windows security enabled and obtain releases from the published project links.
Independent port of Compositor by Robbie Tilton: https://github.com/robbietilton/Compositor
Baseline a19db9011282399785dc18efcfded904627bdcc2 (1.0.4); no upstream endorsement or complete Mac parity is claimed.
Licenses and corresponding sources are in licenses and sources.
"@
 [IO.File]::WriteAllText((Join-Path $portable 'README.txt'),$readme,$encoding)
 $files=@(Get-ChildItem -LiteralPath $portable -Recurse -File | Sort-Object FullName | ForEach-Object {[ordered]@{path=[IO.Path]::GetRelativePath($portable,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})
 $revision=(& git -c "safe.directory=$($packageRoot.Replace('\','/'))" rev-parse HEAD).Trim()
 if($LASTEXITCODE){throw 'Cannot record package source revision'}
 $sourcePaths=@(Get-ChildItem -LiteralPath $source -Recurse -File | ForEach-Object {[IO.Path]::GetRelativePath($source,$_.FullName).Replace('\','/')} | Sort-Object)
 $tracked=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 & git -c "safe.directory=$($packageRoot.Replace('\','/'))" -c core.quotepath=false ls-tree -r --name-only HEAD | ForEach-Object {[void]$tracked.Add($_)}
 if($LASTEXITCODE){throw 'Cannot enumerate committed source files'}
 $sourceDirty=@($sourcePaths | Where-Object {-not $tracked.Contains($_)}).Count -gt 0
 for($start=0;$start -lt $sourcePaths.Count;$start+=100){
  $batch=$sourcePaths[$start..([Math]::Min($start+99,$sourcePaths.Count-1))]
  $sourceStatus=@(& git -c "safe.directory=$($packageRoot.Replace('\','/'))" status --porcelain -- @batch)
  if($LASTEXITCODE){throw 'Cannot record package source status'}
  if($sourceStatus.Count){$sourceDirty=$true}
 }
 [ordered]@{schema=2;version=$Version;channel='community-preview';createdUtc=[DateTime]::UtcNow.ToString('o');sourceRevision=$revision;sourceDirty=$sourceDirty;sourcePaths=$sourcePaths;sourceSnapshot=[IO.Path]::GetFileName($sourceArchive);sourceSha256=(Get-FileHash -LiteralPath $sourceArchive).Hash.ToLowerInvariant();upstream=$policy.upstream;signed=$false;automaticUpdates=$false;entryPoint='Compositor.exe';files=$files} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'package-manifest.json') -Encoding utf8
 Compress-Archive -LiteralPath $portable -DestinationPath (Join-Path $output "CompositorWindows-$Version-portable.zip") -CompressionLevel Optimal
 $demo=Join-Path $packageRoot 'demo'
 if(Test-Path -LiteralPath $demo){
  $demoStage=Join-Path $output 'demo'
  New-Item -ItemType Directory -Path $demoStage | Out-Null
  foreach($entry in Get-ChildItem -LiteralPath $demo){
   if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
   if($entry.PSIsContainer -and $entry.Extension -eq '.comp'){
    if(@(Get-ChildItem -LiteralPath $entry.FullName -Recurse -Force | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count){throw 'A demo project contains a reparse point'}
    Copy-Item -LiteralPath $entry.FullName -Destination $demoStage -Recurse
   }elseif(-not $entry.PSIsContainer -and $entry.Extension -in @('.png','.jpg','.jpeg','.md','.py')){
    Copy-Item -LiteralPath $entry.FullName -Destination $demoStage
   }
  }
  Compress-Archive -LiteralPath $demoStage -DestinationPath (Join-Path $output "CompositorWindows-$Version-demo.zip") -CompressionLevel Optimal
 }
 if(-not $PortableOnly){& (Join-Path $PSScriptRoot 'msi\build-msi.ps1') -PackageDirectory $portable -OutputDirectory $output -Version $Version}
 Get-ChildItem -LiteralPath $output -File | Where-Object {$_.Extension -in @('.msi','.zip')} | ForEach-Object {"$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"} | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding utf8
 Write-Output "PACKAGE=$output"
} finally {Pop-Location}

