param(
    [string]$Python = "python",
    [switch]$SkipExtractN64PortUnits,
    [switch]$SkipAnimationAbi,
    [switch]$SkipMaterializeWorkorders,
    [switch]$SkipCompileProbes,
    [switch]$SkipProbeCompare,
    [switch]$SkipProbeSymbolScan,
    [switch]$SkipSplitWorkorders,
    [switch]$SkipSplitCandidateProbes,
    [switch]$SkipTargetSplitSuggestions,
    [switch]$SkipMetrics
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Push-Location $repoRoot
try {
    if (-not $SkipExtractN64PortUnits) {
        & $Python "scripts\extract_n64_port_units.py"
    }

    & $Python "scripts\analyze_n64_conversion_patterns.py"
    & $Python "scripts\analyze_direct_source_conversion.py"

    if (-not $SkipAnimationAbi) {
        & $Python "scripts\analyze_animation_change_abi.py"
        & $Python "scripts\build_animation_change_port_queue.py"
    }

    & $Python "scripts\build_direct_conversion_recipes.py"
    & $Python "scripts\mine_direct_conversion_structures.py"
    & $Python "scripts\mine_direct_shape_anchors.py"
    & $Python "scripts\build_direct_conversion_workorders.py"
    if (-not $SkipMaterializeWorkorders) {
        & $Python "scripts\materialize_direct_conversion_workorders.py" "--candidates-only"
        & $Python "scripts\materialize_direct_conversion_workorders.py" "--templates-only" "--out-dir" "analysis\direct_template_materialized"
    }
    if (-not $SkipCompileProbes) {
        & $Python "scripts\probe_direct_packet_compilability.py"
        & $Python "scripts\probe_direct_packet_compilability.py" "--materialized-dir" "analysis\direct_template_materialized" "--build-dir" "build\direct_template_compile_probe" "--out-json" "analysis\direct_template_compile_probe.json" "--out-csv" "analysis\direct_template_compile_probe.csv" "--out-md" "analysis\direct_template_compile_probe.md"
    }
    if (-not $SkipProbeCompare) {
        & $Python "scripts\compare_direct_packet_probe_matches.py"
        & $Python "scripts\compare_direct_packet_probe_matches.py" "--probe-json" "analysis\direct_template_compile_probe.json" "--dump-dir" "build\direct_template_compile_probe\dumps" "--out-json" "analysis\direct_template_probe_match.json" "--out-csv" "analysis\direct_template_probe_match.csv" "--out-md" "analysis\direct_template_probe_match.md"
    }
    if (-not $SkipProbeSymbolScan) {
        & $Python "scripts\scan_direct_packet_probe_symbols.py"
    }
    if (-not $SkipSplitWorkorders) {
        & $Python "scripts\build_direct_split_workorders.py"
    }
    if (-not $SkipSplitCandidateProbes) {
        & $Python "scripts\probe_direct_split_candidate_sources.py"
    }
    if (-not $SkipTargetSplitSuggestions) {
        & $Python "scripts\build_direct_target_split_suggestions.py"
    }

    & $Python "scripts\audit_c_reconstruction_frontier.py"

    if (-not $SkipMetrics) {
        & $Python "scripts\report_porting_metrics.py"
    }

    Write-Host "Direct conversion analysis refreshed. No Ghidra export was run."
}
finally {
    Pop-Location
}
