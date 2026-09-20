param([string]$Results = '', [string]$Output = '')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$Results) { $Results = Join-Path $root 'evidence/graphics/tiled-parity-01/results.json' }
if (!$Output) { $Output = Join-Path $root 'audit/tiled-acceptance-additions.json' }
$audit = Get-Content (Join-Path $root 'audit/windows-coverage-review.json') -Raw | ConvertFrom-Json
$run = Get-Content $Results -Raw | ConvertFrom-Json
$source = Get-Content (Join-Path $root '../upstream/CompositorTests/TiledLayerTests.swift')
$native = 'tests/tiled_parity/TiledLayerParityTests.cpp'
$specs = @{
    tiledLayersDrawLikeOneImage = @{12=3;21=3;27=3;33=6;89=1;101=1;112=1}
    maskStrokesDrawLikeOneMask = @{12=1;21=1;37=2;43=2;49=1;33=3;136=1}
    paintingAtTheLayersEdgeDoesNotChangeIt = @{12=1;21=1;159=6;167=1;171=2;177=2;182=2;187=2}
    translucentStrokesDrawLikeOneImage = @{12=3;21=3;27=1;33=3;212=1}
    paintingAScaledDownLayerDoesNotShiftItsPixels = @{12=1;21=1;222=1;242=3;244=3;284=1;285=1;286=1}
    paintingAScaledDownLayersMaskDoesNotShiftItsPixels = @{12=1;21=1;222=1;229=1;242=3;244=3;292=1;293=1;294=1}
}
$rows = @($audit.invocations | Where-Object { $_.family -eq 'TiledLayerTests' })
if ($rows.Count -ne 12 -or $run.invocations.Count -ne 12) { throw 'Expected exactly12 stable invocations.' }
$mappedLines = @{}
$additions = foreach ($row in $rows) {
    $actual = @($run.invocations | Where-Object { $_.id -eq $row.id })
    if ($actual.Count -ne 1) { throw "Missing or duplicate invocation $($row.id)" }
    $actual = $actual[0]
    $expectedArguments = @($row.argument)
    $actualArguments = @($actual.argument)
    if ($expectedArguments.Count -ne $actualArguments.Count) { throw "Argument count mismatch $($row.id)" }
    for ($argumentIndex=0; $argumentIndex -lt $expectedArguments.Count; ++$argumentIndex) {
        if ($expectedArguments[$argumentIndex] -ne $actualArguments[$argumentIndex]) { throw "Argument mismatch $($row.id)" }
    }
    $method = $row.upstream_test_id -replace '^UT-TiledLayerTests-',''
    $assertions = foreach ($line in ($specs[$method].Keys | Sort-Object)) {
        $checks = @($actual.checks | Where-Object { $_.source_line -eq $line })
        $expectedCount = $specs[$method][$line]
        if ($checks.Count -lt $expectedCount) { throw "Omitted source assertion $($row.id) L$line ($($checks.Count)/$expectedCount)" }
        $text = $source[$line-1].Trim()
        if ($text -notmatch '#(expect|require)') { throw "Invalid source assertion L$line" }
        $kind = $Matches[1]
        # Expectations have exact multiplicity; extra native image reads are allowed.
        if ($kind -eq 'expect' -and $checks.Count -ne $expectedCount) { throw "Expectation count mismatch $($row.id) L$line" }
        $mappedLines[$line] = $true
        [ordered]@{
            source_line = $line
            source_kind = "#$kind"
            source_statement = $text
            expected_source_execution_count = $expectedCount
            actual_native_execution_count = $checks.Count
            native_file = $native
            native_lines = @($checks.native_line | Sort-Object -Unique)
            native_check_names = @($checks.name | Sort-Object -Unique)
            passed = (@($checks | Where-Object { !$_.passed }).Count -eq 0)
            measurements = @($checks | Where-Object { $_.PSObject.Properties.Name -contains 'maximum' })
        }
    }
    [ordered]@{
        id = $row.id
        upstream_test_id = $row.upstream_test_id
        argument = $row.argument
        source = $row.source
        family = 'TiledLayerTests'
        portability = 'native_contract_portable'
        native_method_port_complete = $true
        source_assertions_and_helpers_all_mapped = $true
        native_result = $(if ($actual.passed) { 'passed' } else { 'failed' })
        eligible_for_native_contract_acceptance = [bool]$actual.passed
        upstream_run_result = 'not_run'
        mac_differential = $false
        native_witness = [ordered]@{ path=$native; executable='tiled_parity_tests'; result_file=[IO.Path]::GetRelativePath($root,$Results).Replace('\','/') }
        assertions = @($assertions)
        additional_native_checks = @($actual.checks | Where-Object { $_.source_line -eq 0 })
    }
}
$sites = @()
for ($index=0; $index -lt $source.Count; ++$index) {
    if ($source[$index] -match '#(expect|require)') { $sites += $index+1 }
}
$missing = @($sites | Where-Object { !$mappedLines.ContainsKey($_) })
if ($missing.Count) { throw "Unmapped source sites: $missing" }
$sourceFile = Join-Path $root '../upstream/CompositorTests/TiledLayerTests.swift'
$report = [ordered]@{
    schema_version = 1
    baseline_sha = $run.baseline_sha
    coverage_scope = 'Complete native logical assertion contracts only; no Swift execution or Mac differential.'
    source_site_count = $sites.Count
    mapped_source_sites = $sites
    method_count = $specs.Count
    invocation_count = $additions.Count
    native_passed = $run.passed
    native_failed = $run.failed
    thresholds = [ordered]@{unrotated=2;rotated=12;edge_all_pixels=2;canvas_outside_brush=3}
    source_sha256 = (Get-FileHash $sourceFile -Algorithm SHA256).Hash
    native_test_sha256 = (Get-FileHash (Join-Path $root $native) -Algorithm SHA256).Hash
    results_sha256 = (Get-FileHash $Results -Algorithm SHA256).Hash
    native_source_file = $native
    evidence_directory = [IO.Path]::GetRelativePath($root,(Split-Path $Results)).Replace('\','/')
    helper_count_note = 'Expectation multiplicities match exactly. Native preconditions may execute extra times for cache warmup and active-layer lookup; each source requirement has at least its original runtime multiplicity.'
    invocations = @($additions)
}
$report | ConvertTo-Json -Depth 30 | Set-Content -Encoding utf8 $Output
Write-Output "Mapped $($sites.Count)/$($sites.Count) source sites across $($additions.Count) invocations: $($run.passed) passed, $($run.failed) failed."
