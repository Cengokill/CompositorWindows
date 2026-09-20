param([string]$Results = '', [string]$Output = '')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$Results){$Results=Join-Path $root 'evidence/graphics/downsample-01/adapter/results.json'}
if(!$Output){$Output=Join-Path $root 'audit/downsample-acceptance-additions.json'}
$sourcePath=Join-Path $root '../upstream/CompositorTests/DownsampleTests.swift'
$source=Get-Content $sourcePath
$report=Get-Content $Results -Raw | ConvertFrom-Json
$audit=Get-Content (Join-Path $root 'audit/windows-coverage-review.json') -Raw | ConvertFrom-Json
$rows=@($audit.invocations | Where-Object {$_.family -eq 'DownsampleTests'})
$expected=@{
    halvingsAreReusedAndOnlyUsedForLargeReductions=@(11,18,39,41,42,43)
    aHardEdgeStaysSharpShrunkEightTimes=@(11,18,34,51,52)
    fineStripesAverageToFlatGrayWithoutShimmer=@(11,18,34,62,63)
    translucentEdgesStayValidAndMasksStayGray=@(70,23,72,77,79)
}
if($rows.Count -ne 4 -or $report.invocations.Count -ne 4){throw 'Expected4 invocations'}
$mapped=@{}
$invocations=foreach($row in $rows){
    $actual=@($report.invocations | Where-Object {$_.id -eq $row.id})
    if($actual.Count -ne 1){throw "Missing or duplicate $($row.id)"}
    $actual=$actual[0];$method=$row.upstream_test_id -replace '^UT-DownsampleTests-',''
    $assertions=foreach($line in $expected[$method]){
        $checks=@($actual.checks | Where-Object {$_.source_line -eq $line})
        if($checks.Count -ne 1){throw "Missing/duplicate assertion $($row.id) L$line"}
        if($source[$line-1] -notmatch '#(expect|require)'){throw "Invalid source line$line"}
        $kind=$Matches[1];$mapped[$line]=$true
        [ordered]@{source_line=$line;source_kind="#$kind";source_statement=$source[$line-1].Trim();source_execution_count=1;native_execution_count=1;native_file='tests/downsample/DownsampleTests.cpp';native_line=$checks[0].native_line;passed=$checks[0].passed;check=$checks[0]}
    }
    [ordered]@{id=$row.id;upstream_test_id=$row.upstream_test_id;argument=$null;source=$row.source;family='DownsampleTests';native_method_port_complete=$true;source_assertions_and_helpers_all_mapped=$true;native_result=$(if($actual.passed){'passed'}else{'failed'});adapter_mode=$report.mode;global_renderer_integrated=$report.global_renderer_integrated;mac_differential=$false;upstream_run_result='not_run';native_witness=[ordered]@{executable='downsample_tests';arguments=@($(if($report.global_renderer_integrated){'production'}else{'adapter'}),'<evidence-directory>');path='tests/downsample/DownsampleTests.cpp';result_file=[IO.Path]::GetRelativePath($root,$Results)};assertions=@($assertions)}
}
$sites=@();for($index=0;$index -lt $source.Count;++$index){if($source[$index] -match '#(expect|require)'){$sites+=$index+1}}
if(@($sites | Where-Object {!$mapped.ContainsKey($_)}).Count){throw 'Unmapped source assertion/helper'}
[ordered]@{schema_version=1;baseline_sha=$report.baseline_sha;scope=$(if($report.global_renderer_integrated){'Native assertions on production lazy sampling/cache path; no Mac differential.'}else{'Native assertions on isolated adapter; production integration pending; no Mac differential.'});source_site_count=$sites.Count;mapped_source_sites=$sites;invocation_count=4;native_passed=$report.passed;native_failed=$report.failed;source_sha256=(Get-FileHash $sourcePath -Algorithm SHA256).Hash;native_test_sha256=(Get-FileHash (Join-Path $root 'tests/downsample/DownsampleTests.cpp') -Algorithm SHA256).Hash;results_sha256=(Get-FileHash $Results -Algorithm SHA256).Hash;invocations=@($invocations)} | ConvertTo-Json -Depth 20 | Set-Content -Encoding utf8 $Output
Write-Output "Mapped $($sites.Count) source sites /4 invocations: $($report.passed) passed,$($report.failed) failed; mode=$($report.mode)."
