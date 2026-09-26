param([switch]$SkipBuild,[string]$Executable)
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
if(!$Executable){
    $Executable=Join-Path $root 'build/release/Release/foreground_quality_tests.exe'
    if(!$SkipBuild){
        . (Join-Path $root 'scripts/bootstrap.ps1') -Offline
        Push-Location $root
        try{
            & cmake --preset windows-x64-release
            if($LASTEXITCODE){throw 'Native quality configuration failed'}
            & cmake --build --preset windows-x64-release --target foreground_quality_tests --parallel 4
            if($LASTEXITCODE){throw 'Native quality build failed'}
        }finally{Pop-Location}
    }
}
$Executable=(Resolve-Path -LiteralPath $Executable).Path
if(!(Test-Path -LiteralPath $Executable -PathType Leaf)){throw 'Native quality executable is missing'}
$run=Join-Path $evidence ('run-'+(Get-Date -AsUTC -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory $run|Out-Null
$model=Join-Path $root 'dependencies/imaging/model/birefnet-lite.onnx'
$provenance=[ordered]@{started_utc=(Get-Date -AsUTC -Format o);criteria_sha256=$expectedCriteria;manifest_sha256=$expectedManifest;model_sha256=(Get-FileHash $model).Hash.ToLower();executable=$Executable;executable_sha256=(Get-FileHash -LiteralPath $Executable).Hash.ToLower();cpu=$env:PROCESSOR_IDENTIFIER;logical_processors=[Environment]::ProcessorCount;source_hashes=@{}}
foreach($file in @('src/imaging/onnx_subject_provider.cpp','src/imaging/subject_input.cpp','src/imaging/subject_matte.cpp','src/imaging/wic_codec.cpp','src/imaging/project_png.cpp','tests/imaging/quality_native.cpp','tests/imaging/quality_report.py')){$provenance.source_hashes[$file]=(Get-FileHash (Join-Path $root $file)).Hash.ToLower()}
$provenance|ConvertTo-Json -Depth 6|Set-Content -Encoding utf8 "$run/provenance.json"
foreach($case in $manifest.cases){
    $out=Join-Path $run $case.id
    New-Item -ItemType Directory $out|Out-Null
    & $Executable $model (Join-Path $evidence $case.input_file) $out 2>&1|Tee-Object "$out/run.log"
    if($LASTEXITCODE){throw "Native inference failed: $($case.id)"}
}
& "$root/dependencies/imaging/model-venv/Scripts/python.exe" "$PSScriptRoot/quality_report.py" $run
if($LASTEXITCODE){throw 'Quality report generation failed'}
Write-Output "Quality report: $run/report.html"

