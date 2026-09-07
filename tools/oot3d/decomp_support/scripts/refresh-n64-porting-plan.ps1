param(
    [string]$Python = "python",
    [int]$GlobalMaxOot3d = 100,
    [int]$FocusedMaxOot3d = 80,
    [int]$TopN64PerOot3d = 6,
    [int]$BatchPackets = 5,
    [int]$PointerPackets = 6,
    [int]$LaneSources = 8,
    [int]$LaneSourceMaxHits = 24,
    [switch]$IncludeNamed,
    [switch]$NoSymbolOverlay,
    [switch]$CopyN64Source,
    [switch]$SkipLaneSources
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Push-Location $repoRoot
try {
    $overlayArgs = @("scripts\build_symbol_overlay.py")
    & $Python @overlayArgs

    & $Python "scripts\extract_function_pointer_refs.py"

    $common = @()
    if ($IncludeNamed) {
        $common += "--include-named"
    }
    if ($NoSymbolOverlay) {
        $common += "--no-symbol-overlay"
    }

    $globalArgs = @(
        "scripts\rank_n64_port_candidates.py",
        "--max-oot3d", "$GlobalMaxOot3d",
        "--top-n64-per-oot3d", "$TopN64PerOot3d"
    ) + $common
    & $Python @globalArgs

    $focusedArgs = @(
        "scripts\rank_n64_port_candidates.py",
        "--domain", "pause|item|inventory|equip|slot|message",
        "--include-small-constants",
        "--max-oot3d", "$FocusedMaxOot3d",
        "--top-n64-per-oot3d", "$TopN64PerOot3d",
        "--out-json", "analysis\n64_port_candidate_rank_pause_item.json",
        "--out-md", "analysis\n64_port_candidate_rank_pause_item.md",
        "--out-csv", "analysis\n64_port_candidate_rank_pause_item.csv"
    ) + $common
    & $Python @focusedArgs

    $plannerArgs = @(
        "scripts\plan_n64_batch_porting.py",
        "--emit-batch-packets", "$BatchPackets"
    )
    if ($IncludeNamed) {
        $plannerArgs += "--include-named"
    }
    if ($NoSymbolOverlay) {
        $plannerArgs += "--no-symbol-overlay"
    }
    if ($CopyN64Source) {
        $plannerArgs += "--copy-n64-source"
    }
    & $Python @plannerArgs

    & $Python "scripts\classify_n64_importability.py"

    if (-not $SkipLaneSources) {
        & $Python "scripts\extract_n64_lane_sources.py" `
            "--top-lanes" "$LaneSources" `
            "--max-hits-per-lane" "$LaneSourceMaxHits"
    }

    & $Python "scripts\plan_pointer_state_batches.py" "--emit-packets" "$PointerPackets"

    Write-Host "Fast N64 porting refresh complete. No Ghidra export was run."
}
finally {
    Pop-Location
}
