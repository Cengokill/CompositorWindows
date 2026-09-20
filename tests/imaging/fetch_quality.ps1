$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$evidence=Join-Path $root 'evidence/imaging/quality-v1'
$licensePath=Join-Path $evidence 'license-manifest.json'
if((Get-FileHash $licensePath).Hash.ToLower() -ne 'dd3780a33daa50196813d03e9219ab50778d7397416bba1913eaa5637d5f80eb'){throw 'Pinned permission manifest changed'}
foreach($item in (Get-Content $licensePath -Raw|ConvertFrom-Json).files){
    $path=Join-Path $evidence $item.path
    if(!(Test-Path $path) -or (Get-FileHash $path).Hash.ToLower() -ne $item.sha256){throw "Required source permission record absent or changed: $path"}
}
$sources=Get-Content "$evidence/sources.json" -Raw|ConvertFrom-Json
$criteria=Get-Content "$PSScriptRoot/quality_criteria.json" -Raw|ConvertFrom-Json
# Permission records are checked in. Fetch only immutable-hash input photos used by this corpus.
foreach($id in ($criteria.cases.source|Select-Object -Unique)){
    $source=@($sources|Where-Object id -eq $id)
    if($source.Count -ne 1){throw "Missing or ambiguous source record: $id"}
    $source=$source[0]
    $path=Join-Path $evidence $source.file
    if(Test-Path -LiteralPath $path){if((Get-FileHash $path).Hash.ToLower() -ne $source.sha256){throw "Existing input hash differs: $path"};continue}
    New-Item -ItemType Directory -Force (Split-Path $path -Parent)|Out-Null
    $temp=$path+'.download'
    Invoke-WebRequest $source.url -OutFile $temp
    if((Get-FileHash $temp).Hash.ToLower() -ne $source.sha256){throw "Downloaded input differs from pinned bytes: $id"}
    Move-Item -LiteralPath $temp -Destination $path
}
& "$root/dependencies/imaging/model-venv/Scripts/python.exe" "$PSScriptRoot/prepare_quality.py"
if($LASTEXITCODE){throw 'Frozen image preparation failed'}
