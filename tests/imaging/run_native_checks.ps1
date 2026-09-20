$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$out=Join-Path $root 'evidence/imaging'
Push-Location $out
try {
    $flags=@('/nologo','/std:c++20','/EHsc','/W4','/O2','/MD','/utf-8','/DUNICODE','/D_UNICODE',"/I$root/src/imaging")
    $system=@('windowscodecs.lib','ole32.lib','oleaut32.lib','propsys.lib')
    & cl @flags "/I$root/dependencies/imaging/install/include" "$root/src/imaging/wic_codec.cpp" "$root/src/imaging/heif_codec.cpp" "$PSScriptRoot/heif_checks.cpp" "$root/dependencies/imaging/install/lib/heif.lib" @system /Fe:heif_checks.exe 2>&1 | Tee-Object build-heif.log
    if($LASTEXITCODE){throw 'HEIC build failed'}
    Copy-Item "$root/dependencies/imaging/install/bin/*.dll" .
    $cases=@('examples/example.heic','tests/data/with-alpha-512x512.heic','tests/data/rainbow-451x461.heic','tests/data/clap_cropped.heic')
    foreach($case in $cases){$name=[IO.Path]::GetFileNameWithoutExtension($case);& ./heif_checks.exe "$root/dependencies/imaging/libheif/$case" "$out/$name-heic.png" 2>&1 | Tee-Object "$name-heic.log";if($LASTEXITCODE){throw "HEIC case failed: $case"}}
    & dumpbin /dependents ./heif_checks.exe > heif-dependencies.log
    & cl @flags "/I$root/dependencies/imaging/onnxruntime-win-x64-1.30.0/include" "$root/src/imaging/wic_codec.cpp" "$root/src/imaging/subject_matte.cpp" "$root/src/imaging/onnx_subject_provider.cpp" "$PSScriptRoot/native_model_checks.cpp" "$root/dependencies/imaging/onnxruntime-win-x64-1.30.0/lib/onnxruntime.lib" @system bcrypt.lib /Fe:native_model_checks.exe 2>&1 | Tee-Object build-model-native.log
    if($LASTEXITCODE){throw 'Native model build failed'}
    Copy-Item "$root/dependencies/imaging/onnxruntime-win-x64-1.30.0/lib/*.dll" .
    foreach($case in @('astronaut','chelsea')){& ./native_model_checks.exe "$root/dependencies/imaging/model/birefnet-lite.onnx" "$out/$case.png" "$out/$case-native-advanced.png" 2>&1 | Tee-Object "$case-native.log";if($LASTEXITCODE){throw "Native model case failed: $case"}}
    & cl @flags "/I$root/dependencies/imaging/onnxruntime-win-x64-1.30.0/include" "$root/src/imaging/subject_matte.cpp" "$root/src/imaging/onnx_subject_provider.cpp" "$PSScriptRoot/model_health_checks.cpp" "$root/dependencies/imaging/onnxruntime-win-x64-1.30.0/lib/onnxruntime.lib" bcrypt.lib /Fe:model_health_checks.exe 2>&1 | Tee-Object build-model-health.log
    if($LASTEXITCODE){throw 'Native health build failed'}
    & ./model_health_checks.exe "$root/dependencies/imaging/model/birefnet-lite.onnx" 2>&1 | Tee-Object model-health-check.log
    if($LASTEXITCODE){throw 'Native health/semantic separation failed'}
}finally{Pop-Location}
