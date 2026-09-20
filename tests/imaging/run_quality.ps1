param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$evidence=Join-Path $root 'evidence/imaging/quality-v1'
$licensePath=Join-Path $evidence 'license-manifest.json'
if((Get-FileHash $licensePath).Hash.ToLower() -ne 'dd3780a33daa50196813d03e9219ab50778d7397416bba1913eaa5637d5f80eb'){throw 'Pinned permission manifest changed'}
foreach($item in (Get-Content $licensePath -Raw|ConvertFrom-Json).files){
    $path=Join-Path $evidence $item.path
    if(!(Test-Path $path) -or (Get-FileHash $path).Hash.ToLower() -ne $item.sha256){throw "Required source permission record absent or changed: $path"}
}
$criteria=Join-Path $PSScriptRoot 'quality_criteria.json'
$expectedCriteria='d974c354c07979c7f02c8538a55a2e3e626df627f1e999fc33cc2478129b7eb7'
$expectedManifest='73272fd6962f2a061e70a42697b6842970dbadb99b5da0c0eb1edfc18f4faa75'
if((Get-FileHash $criteria).Hash.ToLower() -ne $expectedCriteria){throw 'Frozen quality criteria changed'}
$manifestPath=Join-Path $evidence 'prepared-manifest.json'
if((Get-FileHash $manifestPath).Hash.ToLower() -ne $expectedManifest){throw 'Frozen required fixture manifest changed'}
$manifest=Get-Content $manifestPath -Raw|ConvertFrom-Json
if($manifest.cases.Count -ne 8){throw 'All eight required quality cases must be present'}
foreach($case in $manifest.cases){
    foreach($pair in @(@($case.source_file,$case.source_sha256),@($case.input_file,$case.input_sha256))){
        $path=Join-Path $evidence $pair[0]
        if(!(Test-Path -LiteralPath $path) -or (Get-FileHash $path).Hash.ToLower() -ne $pair[1]){throw "Required fixture absent or changed: $path"}
    }
    if($case.reference_file){$path=Join-Path $evidence $case.reference_file;if(!(Test-Path $path) -or (Get-FileHash $path).Hash.ToLower() -ne $case.reference_sha256){throw 'Required independent reference absent or changed'}}
}
$bin=Join-Path $evidence 'bin'
New-Item -ItemType Directory -Force $bin|Out-Null
if(!$SkipBuild){
    & 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    Push-Location $bin
    try{
        & cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MD /utf-8 "/I$root/src/imaging" "/I$root/dependencies/imaging/onnxruntime-win-x64-1.30.0/include" "$root/src/imaging/wic_codec.cpp" "$root/src/imaging/project_png.cpp" "$root/src/imaging/subject_matte.cpp" "$root/src/imaging/onnx_subject_provider.cpp" "$PSScriptRoot/quality_native.cpp" "$root/dependencies/imaging/onnxruntime-win-x64-1.30.0/lib/onnxruntime.lib" windowscodecs.lib ole32.lib oleaut32.lib propsys.lib bcrypt.lib /Fe:quality_native.exe 2>&1|Tee-Object build.log
        if($LASTEXITCODE){throw 'Native quality build failed'}
        Copy-Item "$root/dependencies/imaging/onnxruntime-win-x64-1.30.0/lib/*.dll" .
    }finally{Pop-Location}
}
$run=Join-Path $evidence ('run-'+(Get-Date -AsUTC -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory $run|Out-Null
$model=Join-Path $root 'dependencies/imaging/model/birefnet-lite.onnx'
$provenance=[ordered]@{started_utc=(Get-Date -AsUTC -Format o);criteria_sha256=$expectedCriteria;manifest_sha256=$expectedManifest;model_sha256=(Get-FileHash $model).Hash.ToLower();executable_sha256=(Get-FileHash "$bin/quality_native.exe").Hash.ToLower();cpu=$env:PROCESSOR_IDENTIFIER;logical_processors=[Environment]::ProcessorCount;source_hashes=@{}}
foreach($file in @('src/imaging/onnx_subject_provider.cpp','src/imaging/subject_matte.cpp','src/imaging/wic_codec.cpp','src/imaging/project_png.cpp','tests/imaging/quality_native.cpp','tests/imaging/quality_report.py')){$provenance.source_hashes[$file]=(Get-FileHash (Join-Path $root $file)).Hash.ToLower()}
$provenance|ConvertTo-Json -Depth 6|Set-Content -Encoding utf8 "$run/provenance.json"
foreach($case in $manifest.cases){
    $out=Join-Path $run $case.id
    New-Item -ItemType Directory $out|Out-Null
    & "$bin/quality_native.exe" $model (Join-Path $evidence $case.input_file) $out 2>&1|Tee-Object "$out/run.log"
    if($LASTEXITCODE){throw "Native inference failed: $($case.id)"}
}
& "$root/dependencies/imaging/model-venv/Scripts/python.exe" "$PSScriptRoot/quality_report.py" $run
if($LASTEXITCODE){throw 'Quality report generation failed'}
Write-Output "Quality report: $run/report.html"

