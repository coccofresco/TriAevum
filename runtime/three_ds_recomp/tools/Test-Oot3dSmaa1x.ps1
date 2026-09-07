[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateRange(6, 240)]
    [int]$Frames = 8,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 45
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rendererRoot =
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$harness = Join-Path $rendererRoot `
    "tools\Invoke-Oot3dRendererDev.ps1"
$artifactRoot = Join-Path $rendererRoot ".renderer-dev\artifacts"
$diagnosticsPath = Join-Path $artifactRoot `
    "vulkan-diagnostics.json"
$screenshotPath = Join-Path $artifactRoot "native-game.bmp"
$environmentNames = @(
    "OOT3D_VULKAN_VALIDATION",
    "OOT3D_GRAPHICS_FXAA",
    "OOT3D_GRAPHICS_SMAA",
    "OOT3D_GRAPHICS_MSAA",
    "OOT3D_GRAPHICS_TAA",
    "OOT3D_GRAPHICS_NIS",
    "OOT3D_GRAPHICS_FSR",
    "OOT3D_GRAPHICS_DLSS",
    "OOT3D_GRAPHICS_CACAO",
    "OOT3D_GRAPHICS_CACAO_QUALITY",
    "OOT3D_GRAPHICS_HIZ",
    "OOT3D_GRAPHICS_SSSR"
)
$savedEnvironment = @{}
foreach ($name in $environmentNames) {
    $item = Get-Item "Env:$name" -ErrorAction SilentlyContinue
    $savedEnvironment[$name] =
        if ($null -eq $item) { $null } else { $item.Value }
    Remove-Item "Env:$name" -ErrorAction SilentlyContinue
}

function Read-RunArtifacts {
    if (-not (
            Test-Path -LiteralPath $diagnosticsPath -PathType Leaf
        ) -or -not (
            Test-Path -LiteralPath $screenshotPath -PathType Leaf
        )) {
        throw "SMAA gate did not produce complete artifacts"
    }
    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $framesFound = @($diagnostics.frames)
    if ($framesFound.Count -ne $Frames) {
        throw "SMAA gate produced $($framesFound.Count) of $Frames frames"
    }
    return @{
        Frames = $framesFound
        Screenshot = [IO.File]::ReadAllBytes($screenshotPath)
    }
}

function Assert-CleanValidation {
    param([object[]]$FramesFound, [string]$Case)
    $vulkanErrors = [uint64](
        $FramesFound |
            Measure-Object -Property `
                vulkan_validation_error_count -Maximum
    ).Maximum
    $nriErrors = [uint64](
        $FramesFound |
            Measure-Object -Property `
                nri_validation_error_count -Maximum
    ).Maximum
    if ($vulkanErrors -ne 0 -or $nriErrors -ne 0) {
        throw "$Case reported validation errors: " +
            "Vulkan=$vulkanErrors, NRI=$nriErrors"
    }
    $draws = [uint64](
        $FramesFound |
            Measure-Object -Property native_pica_draw_count -Sum
    ).Sum
    $ownedDraws = [uint64](
        $FramesFound |
            Measure-Object -Property nri_pica_owned_draw_count -Sum
    ).Sum
    if ($draws -eq 0 -or $draws -ne $ownedDraws) {
        throw "$Case broke native PICA/NRI draw parity"
    }
}

try {
    $env:OOT3D_VULKAN_VALIDATION = "1"
    $env:OOT3D_GRAPHICS_CACAO = "1"
    $env:OOT3D_GRAPHICS_CACAO_QUALITY = "1"

    & $harness -SkipBuild:$SkipBuild `
        -Frames $Frames -MaxSeconds $MaxSeconds | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "SMAA reference run exited with code $LASTEXITCODE"
    }
    $reference = Read-RunArtifacts
    Assert-CleanValidation $reference.Frames "SMAA reference"

    $env:OOT3D_GRAPHICS_SMAA = "1"
    & $harness -SkipBuild `
        -Frames $Frames -MaxSeconds $MaxSeconds | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "SMAA run exited with code $LASTEXITCODE"
    }
    $smaa = Read-RunArtifacts
    Assert-CleanValidation $smaa.Frames "SMAA"

    $smaaFrames = @(
        $smaa.Frames |
            Where-Object { $_.smaa_1x_pass_count -gt 0 }
    )
    if ($smaaFrames.Count -eq 0) {
        throw "SMAA did not execute on the playable scene"
    }
    foreach ($frame in $smaaFrames) {
        if ($frame.smaa_edge_pass_count -ne
                $frame.smaa_1x_pass_count -or
            $frame.smaa_blend_weight_pass_count -ne
                $frame.smaa_1x_pass_count -or
            $frame.smaa_neighborhood_pass_count -ne
                $frame.smaa_1x_pass_count) {
            throw "SMAA did not execute all three stages"
        }
        if (-not $frame.smaa_outputs_nri_owned -or
            -not $frame.smaa_lookups_nri_owned -or
            -not $frame.smaa_compute_nri_owned -or
            -not $frame.smaa_barriers_nri_owned -or
            -not $frame.smaa_lookup_upload_nri_owned) {
            throw "SMAA escaped the NRI ownership contract"
        }
        if ($frame.spatial_aa_mode -ne 2) {
            throw "SMAA scanout diagnostics lost the selected mode"
        }
    }
    if (@(
            $smaa.Frames |
                Where-Object {
                    $_.scene_composite_pass_count -gt 0 -and
                    (-not $_.scene_composite_output_nri_owned -or
                     -not $_.scene_composite_compute_nri_owned -or
                     -not $_.scene_composite_barriers_nri_owned)
                }
        ).Count -ne 0) {
        throw "The AO-before-SMAA composite escaped NRI ownership"
    }
    $compositeCount = [uint64](
        $smaa.Frames |
            Measure-Object -Property scene_composite_pass_count -Sum
    ).Sum
    if ($compositeCount -eq 0) {
        throw "SMAA did not consume the advanced scene composite"
    }

    if ($reference.Screenshot.Length -ne $smaa.Screenshot.Length) {
        throw "Reference and SMAA screenshots have different dimensions"
    }
    $changedComponents = 0
    $maximumDelta = 0
    for ($index = 54;
         $index -lt $smaa.Screenshot.Length; ++$index) {
        $delta = [Math]::Abs(
            [int]$reference.Screenshot[$index] -
            [int]$smaa.Screenshot[$index])
        if ($delta -gt 0) {
            ++$changedComponents
            $maximumDelta = [Math]::Max($maximumDelta, $delta)
        }
    }
    if ($changedComponents -lt 128 -or $maximumDelta -eq 0) {
        throw "SMAA produced no measurable framebuffer change"
    }

    Write-Host (
        ("SMAA 1x gate passed: {0} pass frames, " +
         "{1} changed framebuffer components, max delta {2}") -f
        $smaaFrames.Count, $changedComponents, $maximumDelta)
} finally {
    foreach ($name in $environmentNames) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
        if ($null -ne $savedEnvironment[$name]) {
            Set-Item "Env:$name" $savedEnvironment[$name]
        }
    }
}
