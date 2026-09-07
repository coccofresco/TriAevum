Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-Oot3dReflectionRunMetrics {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [ValidateSet("HiZ", "FidelityFxSssr")]
        [string]$Provider,
        [Parameter(Mandatory)]
        [ValidateRange(1, 10000)]
        [int]$ExpectedFrames,
        [Parameter(Mandatory)]
        [ValidatePattern("^[0-9a-f]{16}$")]
        [string]$TextureHash,
        [ValidateRange(0.1, 1000.0)]
        [double]$MaximumReflectionMilliseconds = 50.0
    )

    $diagnostics =
        Get-Content -LiteralPath $Path -Raw |
        ConvertFrom-Json
    $frameDiagnostics = @($diagnostics.frames)
    if ($frameDiagnostics.Count -ne $ExpectedFrames) {
        throw "$Provider produced $($frameDiagnostics.Count) of " +
            "$ExpectedFrames diagnostic frames"
    }
    if (@(
            $frameDiagnostics |
                Where-Object {
                    -not $_.vulkan_validation_enabled -or
                    -not $_.nri_validation_enabled
                }
        ).Count -ne 0) {
        throw "$Provider did not keep Vulkan/NRI validation enabled"
    }
    $vulkanErrors = [uint64](
        $frameDiagnostics |
            Measure-Object -Property `
                vulkan_validation_error_count -Maximum
    ).Maximum
    $nriErrors = [uint64](
        $frameDiagnostics |
            Measure-Object -Property `
                nri_validation_error_count -Maximum
    ).Maximum
    if ($vulkanErrors -ne 0 -or $nriErrors -ne 0) {
        throw "$Provider reported validation errors: " +
            "Vulkan=$vulkanErrors, NRI=$nriErrors"
    }
    $draws = [uint64](
        $frameDiagnostics |
            Measure-Object -Property native_pica_draw_count -Sum
    ).Sum
    $nriDraws = [uint64](
        $frameDiagnostics |
            Measure-Object -Property nri_pica_owned_draw_count -Sum
    ).Sum
    if ($draws -eq 0 -or $draws -ne $nriDraws) {
        throw "$Provider broke native PICA/NRI draw parity"
    }
    $hiZCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property hiz_reflection_count -Sum
    ).Sum
    $sssrCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property fidelityfx_sssr_count -Sum
    ).Sum
    if ($Provider -eq "HiZ") {
        if ($hiZCount -eq 0 -or $sssrCount -ne 0) {
            throw "Hi-Z comparison run selected the wrong provider"
        }
    } elseif ($sssrCount -eq 0) {
        throw "FidelityFX comparison run did not dispatch SSSR"
    }

    $profiledDraws = [uint64](
        $frameDiagnostics |
            Measure-Object -Property `
                reflection_material_profiled_draw_count -Sum
    ).Sum
    $profiledTextureDraws = 0UL
    $textureIdentityKeys =
        [Collections.Generic.HashSet[string]]::new(
            [StringComparer]::Ordinal)
    foreach ($frame in $frameDiagnostics) {
        if ([uint64]$frame.reflection_texture_usage_overflow_count -ne 0) {
            throw "$Provider reflection texture inventory overflowed"
        }
        foreach ($usage in @($frame.reflection_texture_usages)) {
            [void]$textureIdentityKeys.Add(
                "$($usage.texture_hash):$($usage.width)x" +
                "$($usage.height):$($usage.mapper_slot)")
            if ([string]$usage.texture_hash -eq $TextureHash -and
                [bool]$usage.profiled) {
                $profiledTextureDraws +=
                    [uint64]$usage.profiled_draw_count
            }
        }
    }
    if ($profiledDraws -eq 0 -or
        $profiledDraws -ne $profiledTextureDraws) {
        throw "$Provider did not attribute every profiled draw to " +
            $TextureHash
    }

    $timings = @(
        $frameDiagnostics |
            ForEach-Object { $_.gpu.reflection_ms } |
            Where-Object { $null -ne $_ } |
            ForEach-Object { [double]$_ }
    )
    $minimumTimingCount = [Math]::Max(1, $ExpectedFrames - 3)
    if ($timings.Count -lt $minimumTimingCount -or
        @(
            $timings |
                Where-Object {
                    -not [double]::IsFinite($_) -or
                    $_ -lt 0.0 -or
                    $_ -gt $MaximumReflectionMilliseconds
                }
        ).Count -ne 0) {
        throw "$Provider reflection GPU timings are missing or unstable"
    }
    $timingMeasure = $timings | Measure-Object -Average -Maximum
    [pscustomobject]@{
        Provider = $Provider
        Frames = $frameDiagnostics.Count
        DrawCount = $draws
        HiZCount = $hiZCount
        FidelityFxSssrCount = $sssrCount
        ProfiledDrawCount = $profiledDraws
        TextureIdentityCount = $textureIdentityKeys.Count
        TimingCount = $timings.Count
        ReflectionMeanMilliseconds =
            [double]$timingMeasure.Average
        ReflectionMaximumMilliseconds =
            [double]$timingMeasure.Maximum
        VulkanErrors = $vulkanErrors
        NriErrors = $nriErrors
    }
}

Export-ModuleMember -Function Get-Oot3dReflectionRunMetrics
