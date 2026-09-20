param(
    [string]$VsRoot='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools',
    [string]$Python='C:\Users\blurr\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
    [switch]$SkipModel
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$dependency=Join-Path $root 'dependencies/imaging'
$evidence=Join-Path $root 'evidence/imaging'
$lock=Get-Content (Join-Path $dependency 'lock.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force $dependency,$evidence | Out-Null
function Assert-Hash([string]$Path,[string]$Expected){
    if((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ine $Expected){throw "Pinned artifact hash mismatch: $Path"}
}
function Fetch-Asset($Asset){
    $path=[IO.Path]::GetFullPath((Join-Path $root $Asset.path))
    if(-not $path.StartsWith([IO.Path]::GetFullPath($root)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Asset path escapes Windows workspace'}
    New-Item -ItemType Directory -Force (Split-Path $path -Parent) | Out-Null
    if(-not (Test-Path -LiteralPath $path)){Invoke-WebRequest $Asset.url -OutFile $path}
    Assert-Hash $path $Asset.sha256
}
foreach($asset in $lock.assets){if(-not ($SkipModel -and $asset.group -eq 'model')){Fetch-Asset $asset}}
foreach($repository in $lock.repositories){
    $path=Join-Path $dependency $repository.name
    if(-not (Test-Path -LiteralPath $path)){& git clone --depth 1 --branch $repository.tag $repository.url $path;if($LASTEXITCODE){throw 'Dependency source clone failed'}}
    $sha=& git -c "safe.directory=$($path.Replace('\','/'))" -C $path rev-parse HEAD
    if($sha.Trim() -ne $repository.commit){throw "Dependency commit mismatch: $path"}
    # Full corresponding source is retained for LGPL distribution/rebuild materials.
    $archive=Join-Path $dependency ($repository.name+'-'+$repository.version+'-source.tar')
    & git -c "safe.directory=$($path.Replace('\','/'))" -C $path archive --format=tar "-o$archive" HEAD
    if($LASTEXITCODE){throw 'Source archive failed'}
    Assert-Hash $archive $repository.source_archive_sha256
}
& (Join-Path $VsRoot 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$cmake=Join-Path $VsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja=Join-Path $VsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$prefix=Join-Path $dependency 'install'
function Configure-Build([string]$Name,[string[]]$Options){
    $source=Join-Path $dependency $Name;$build=Join-Path $dependency ($Name+'-ninja')
    & $cmake -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$prefix" "-DCMAKE_PREFIX_PATH=$prefix" -DBUILD_SHARED_LIBS=ON @Options
    if($LASTEXITCODE){throw "$Name configuration failed"}
    & $cmake --build $build --parallel 8;if($LASTEXITCODE){throw "$Name build failed"}
    & $cmake --install $build;if($LASTEXITCODE){throw "$Name install failed"}
}
Configure-Build 'libde265' @('-DENABLE_DECODER=OFF','-DENABLE_ENCODER=OFF','-DENABLE_SDL=OFF')
Configure-Build 'libheif' @('-DENABLE_PLUGIN_LOADING=OFF','-DWITH_LIBDE265=ON','-DWITH_LIBDE265_PLUGIN=OFF','-DWITH_X265=OFF','-DWITH_X264=OFF','-DWITH_OpenH264_DECODER=OFF','-DWITH_AOM_DECODER=OFF','-DWITH_AOM_ENCODER=OFF','-DWITH_LIBSHARPYUV=OFF','-DWITH_GDK_PIXBUF=OFF','-DWITH_EXAMPLES=OFF','-DBUILD_TESTING=OFF','-DBUILD_DOCUMENTATION=OFF')
if(-not $SkipModel){
    Expand-Archive (Join-Path $dependency 'onnxruntime-win-x64-1.30.0.zip') -DestinationPath $dependency -Force
    $venv=Join-Path $dependency 'model-venv'
    if(-not (Test-Path (Join-Path $venv 'Scripts/python.exe'))){& $Python -m venv $venv;if($LASTEXITCODE){throw 'Python 3.12 environment creation failed'}}
    $pythonExe=Join-Path $venv 'Scripts/python.exe'
    & $pythonExe -m pip install --extra-index-url https://download.pytorch.org/whl/cpu --require-hashes -r (Join-Path $dependency 'model-requirements.hashes.txt')
    if($LASTEXITCODE){throw 'Pinned conversion dependency installation failed'}
    & $pythonExe (Join-Path $root 'tests/imaging/make_fixtures.py');if($LASTEXITCODE){throw 'Fixture creation failed'}
    $model=Join-Path $dependency 'model/birefnet-lite.onnx'
    if(-not (Test-Path $model)){& $pythonExe (Join-Path $root 'tests/imaging/convert_birefnet.py');if($LASTEXITCODE){throw 'Model conversion/verification failed'}}
    Assert-Hash $model $lock.model.onnx_sha256
}
Write-Output 'Pinned imaging dependencies are available locally.'

