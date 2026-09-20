param(
    [string]$Executable = '',
    [string]$EvidenceDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$Executable) { $Executable = Join-Path $root 'build/release/Release/tiled_parity_tests.exe' }
if (!$EvidenceDirectory) { $EvidenceDirectory = Join-Path $root 'evidence/graphics/tiled-parity-current' }
New-Item -ItemType Directory -Force $EvidenceDirectory | Out-Null
$env:PATH = @((Join-Path $root 'dependencies/qt/bin'), (Join-Path $root 'dependencies/imaging/install/bin'), (Join-Path $root 'dependencies/imaging/onnxruntime-win-x64-1.30.0/lib'), $env:PATH) -join ';'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $root 'dependencies/qt/plugins'
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_SCALE_FACTOR = '1'
# Visible native windows are required by the final two source cases.
# Never launch this runner hidden or substitute offscreen rendering.
& $Executable $EvidenceDirectory 2>&1 | Tee-Object (Join-Path $EvidenceDirectory 'run.log')
exit $LASTEXITCODE
