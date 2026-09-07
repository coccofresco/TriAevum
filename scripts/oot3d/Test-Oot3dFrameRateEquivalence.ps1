[CmdletBinding()]
param(
    [string]$Executable = "I:\oot3dre_work\build-mass-cpp-9fd2f56\oot3d_native_game.exe",
    [string]$A32ProcessManifest = "I:\oot3dre_work\build-mass-cpp-9fd2f56\oot3d_native_process_manifest.json",
    [string]$Checkpoint = "I:\oot3dre_work\native_game\checkpoints\timing_gameplay_candidate_13000.oot3dsav",
    [string]$OutputDirectory = "I:\oot3dre_work\native_game\frame_rate_equivalence",
    [ValidateRange(3, 10001)]
    [int]$PresentationGuestRefreshes = 61,
    [ValidateRange(2, 10000)]
    [int]$SimulationGuestRefreshes = 60,
    [ValidateRange(0.0, 1.0)]
    [double]$AnimationFrameTolerance = 0.001
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (($PresentationGuestRefreshes % 2) -ne 1) {
    throw "PresentationGuestRefreshes must be odd so 30 Hz batching reaches the same CTR refresh boundary"
}
if (($SimulationGuestRefreshes % 2) -ne 0) {
    throw "SimulationGuestRefreshes must be even so 30 and 60 Hz updates cover the same native time"
}
foreach ($path in @($Executable, $A32ProcessManifest, $Checkpoint)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required frame-rate equivalence input is missing: $path"
    }
}

$launcher = Join-Path $PSScriptRoot "Invoke-Oot3dNativeGame.ps1"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function Invoke-FrameRateCase {
    param(
        [string]$Name,
        [int]$Frames,
        [double]$FixedDeltaSeconds,
        [int]$SimulationRate,
        [string]$PresentationRate
    )

    $output = Join-Path $OutputDirectory "$Name.json"
    & $launcher -SkipBuild -Executable $Executable `
        -A32ProcessManifest $A32ProcessManifest -LoadState $Checkpoint `
        -DisableAudio -Frames $Frames `
        -FixedDeltaSeconds $FixedDeltaSeconds `
        -SimulationRate $SimulationRate `
        -PresentationRate $PresentationRate -Output $output | Out-Host
    return Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
}

function Assert-Equal {
    param($Left, $Right, [string]$Role)
    if (($Left | ConvertTo-Json -Compress -Depth 8) -ne
        ($Right | ConvertTo-Json -Compress -Depth 8)) {
        throw "$Role mismatch: '$Left' != '$Right'"
    }
}

$present30Frames = [int](($PresentationGuestRefreshes + 1) / 2)
$present30 = Invoke-FrameRateCase -Name "simulation30_presentation30" `
    -Frames $present30Frames -FixedDeltaSeconds (1.0 / 30.0) `
    -SimulationRate 30 -PresentationRate "30"
$present60 = Invoke-FrameRateCase -Name "simulation30_presentation60" `
    -Frames $PresentationGuestRefreshes -FixedDeltaSeconds (1.0 / 60.0) `
    -SimulationRate 30 -PresentationRate "60"

Assert-Equal $present30.vblanks $PresentationGuestRefreshes `
    "30 Hz presentation VBlank count"
Assert-Equal $present60.vblanks $PresentationGuestRefreshes `
    "60 Hz presentation VBlank count"
Assert-Equal $present30.frame_rate.guest_refreshes_dropped 0 `
    "30 Hz presentation dropped refresh count"
Assert-Equal $present60.frame_rate.guest_refreshes_dropped 0 `
    "60 Hz presentation dropped refresh count"
Assert-Equal $present30.process_state_fingerprint `
    $present60.process_state_fingerprint `
    "presentation-independent guest state fingerprint"
Assert-Equal $present30.player_timing.state $present60.player_timing.state `
    "presentation-independent Player state"

$simulation30 = Invoke-FrameRateCase -Name "simulation30_60ticks" `
    -Frames $SimulationGuestRefreshes -FixedDeltaSeconds (1.0 / 60.0) `
    -SimulationRate 30 -PresentationRate "60"
$simulation60 = Invoke-FrameRateCase -Name "simulation60_60ticks" `
    -Frames $SimulationGuestRefreshes -FixedDeltaSeconds (1.0 / 60.0) `
    -SimulationRate 60 -PresentationRate "60"

$state30 = $simulation30.player_timing.state
$state60 = $simulation60.player_timing.state
foreach ($field in @(
        "action_function", "animation_resource", "animation_mode",
        "background_check_flags", "shape_yaw", "actor_speed",
        "player_speed", "world_position", "velocity")) {
    Assert-Equal $state30.$field $state60.$field `
        "30/60 Hz Player field '$field'"
}
$animationFrameDelta = [math]::Abs(
    [double]$state30.animation_frame - [double]$state60.animation_frame)
if ($animationFrameDelta -gt $AnimationFrameTolerance) {
    throw "30/60 Hz animation-frame delta $animationFrameDelta exceeds $AnimationFrameTolerance"
}

[pscustomobject]@{
    presentation_guest_refreshes = $PresentationGuestRefreshes
    presentation_state_fingerprint = $present30.process_state_fingerprint
    simulation_guest_refreshes = $SimulationGuestRefreshes
    simulation30_updates = $simulation30.player_timing.update_entries_observed
    simulation60_updates = $simulation60.player_timing.update_entries_observed
    animation_frame_delta = $animationFrameDelta
    animation_frame_tolerance = $AnimationFrameTolerance
    player_state_equivalent = $true
}
