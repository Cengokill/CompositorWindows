param(
    [Parameter(Mandatory)][string]$Baseline,
    [Parameter(Mandatory)][string]$Candidate,
    [Parameter(Mandatory)][string]$ComposaCheckout,
    [Parameter(Mandatory)][string]$Output,
    [ValidateRange(1, 200)][int]$Trials = 30,
    [switch]$Representative
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$Baseline = (Resolve-Path -LiteralPath $Baseline).Path
$Candidate = (Resolve-Path -LiteralPath $Candidate).Path
$ComposaCheckout = (Resolve-Path -LiteralPath $ComposaCheckout).Path
$Output = (New-Item -ItemType Directory -Path $Output -Force).FullName
$expectedCommit = '86529d3b5355d28ddc28ec3db2e4de1d4d00f63b'
if ((git -C $ComposaCheckout rev-parse HEAD) -ne $expectedCommit) {
    throw "Check out Composa commit $expectedCommit before running this benchmark."
}
$testDir = Join-Path $ComposaCheckout 'tests/Composa.App.Tests'
$testProject = Join-Path $testDir 'Composa.App.Tests.csproj'
$driver = Join-Path $testDir 'ComposaZoomBenchmark.cs'
if (Test-Path -LiteralPath $driver) {
    $sourceHash = (Get-FileHash -LiteralPath "$repo/tests/graphics/ComposaZoomBenchmark.cs").Hash
    if ((Get-FileHash -LiteralPath $driver).Hash -ne $sourceHash) {
        throw 'The Composa checkout already contains a different benchmark driver.'
    }
} else {
    Copy-Item -LiteralPath "$repo/tests/graphics/ComposaZoomBenchmark.cs" -Destination $driver
}
dotnet build $testProject -c Release *> "$Output/composa-build.log"
if ($LASTEXITCODE -ne 0) { throw "Composa test build failed. See $Output/composa-build.log." }

$env:PATH = "$repo/dependencies/qt/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$repo/dependencies/qt/plugins"
$env:QT_QPA_PLATFORM = 'windows'

$cases = [System.Collections.Generic.List[object]]::new()
if ($Representative) {
    $cases.Add(@(1024,1,0,0,1,30,1,0))
    $cases.Add(@(1024,5,0,0,5,30,1,0))
    $cases.Add(@(4096,5,0,0,1,30,1,0))
    $cases.Add(@(4096,5,0,0,5,30,-1,0))
    $cases.Add(@(4096,5,20,0,1,30,1,0))
    $cases.Add(@(8192,50,0,0,1,30,1,0))
} else {
    foreach ($edge in @(1024,4096,8192)) {
        foreach ($visible in @(1,5,20,50)) {
            foreach ($notches in @(1,5,20)) {
                $intervals = if ($notches -eq 1) { @(30) } else { @(10,30,100) }
                foreach ($interval in $intervals) {
                    foreach ($direction in @(1,-1)) {
                        foreach ($cold in @(0,1)) {
                            $cases.Add(@($edge,$visible,0,0,$notches,$interval,$direction,$cold))
                        }
                    }
                }
            }
        }
    }
    $cases.Add(@(4096,5,20,0,1,30,1,0))
}
$cases.Add(@(1024,5,0,1,1,30,1,0))

function Invoke-Benchmark([string]$Name, [string]$Executable, [string[]]$Arguments, [int]$TimeoutMs) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments -WorkingDirectory $Output -PassThru `
        -RedirectStandardOutput "$Output/$Name.out" -RedirectStandardError "$Output/$Name.err"
    try {
        if (!$process.WaitForExit($TimeoutMs)) {
            $process.Kill($true)
            Write-Warning "$Name exceeded its timeout."
        } elseif ($process.ExitCode -ne 0) {
            Write-Warning "$Name exited with code $($process.ExitCode)."
        }
    } finally {
        $process.Dispose()
    }
}

$dotnet = (Get-Command dotnet).Source
foreach ($case in $cases) {
    $edge,$visible,$hidden,$lens,$notches,$interval,$direction,$cold = $case
    $name = $case -join '_'
    Write-Output "Zoom case $name"
    $native = @($edge,$visible,$hidden,$lens,$notches,$interval,$direction,$Trials,$cold)
    Invoke-Benchmark "baseline-$name" $Baseline $native 900000
    Invoke-Benchmark "candidate-$name" $Candidate $native 900000
    if (!$lens) {
        $env:ZOOM_CASE = @($edge,$visible,$hidden,$notches,$interval,$direction,$Trials,$cold,1) -join ' '
        Invoke-Benchmark "composa-$name" $dotnet @('test',"`"$testProject`"",'-c','Release','--no-build',
            '--filter','FullyQualifiedName~ComposaZoomBenchmark','--logger','console;verbosity=detailed') 900000
    }
}
