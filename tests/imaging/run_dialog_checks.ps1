$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$out=Join-Path $root 'evidence/imaging';$qt=Join-Path $root 'dependencies/qt'
Push-Location $out
try{
    $includes=@("/I$root/src/imaging","/I$root/dependencies/imaging/onnxruntime-win-x64-1.30.0/include","/I$qt/include","/I$qt/include/QtWidgets","/I$qt/include/QtGui","/I$qt/include/QtCore","/I$qt/include/QtConcurrent")
    & cl /nologo /std:c++20 /Zc:__cplusplus /permissive- /EHsc /W4 /O2 /MD /utf-8 /DUNICODE /D_UNICODE @includes "$root/src/imaging/wic_codec.cpp" "$root/src/imaging/subject_matte.cpp" "$root/src/imaging/onnx_subject_provider.cpp" "$root/src/imaging/SubjectDialog.cpp" "$PSScriptRoot/subject_dialog_checks.cpp" "$root/dependencies/imaging/onnxruntime-win-x64-1.30.0/lib/onnxruntime.lib" "$qt/lib/Qt6Widgets.lib" "$qt/lib/Qt6Gui.lib" "$qt/lib/Qt6Core.lib" "$qt/lib/Qt6Concurrent.lib" windowscodecs.lib ole32.lib oleaut32.lib propsys.lib bcrypt.lib /Fe:subject_dialog_checks.exe 2>&1 | Tee-Object build-subject-dialog.log
    if($LASTEXITCODE){throw 'Subject dialog build failed'}
    $env:PATH="$qt/bin;"+$env:PATH;$env:QT_PLUGIN_PATH=Join-Path $qt 'plugins';$env:QT_QPA_PLATFORM='offscreen'
    & ./subject_dialog_checks.exe "$root/dependencies/imaging/model/birefnet-lite.onnx" "$out/astronaut.png" 2>&1 | Tee-Object test-subject-dialog.log
    if($LASTEXITCODE){throw 'Subject dialog checks failed'}
}finally{Pop-Location}

