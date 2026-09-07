[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Path,
    [ValidateRange(2, 65536)]
    [int]$MinimumUniqueColors = 32,
    [ValidateRange(1, 255)]
    [int]$MinimumChannelRange = 24,
    [ValidateRange(0.0, 1.0)]
    [double]$MinimumChromaticFraction = 0.01
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$module = Join-Path $PSScriptRoot "Oot3dFramebufferArtifact.psm1"
Import-Module $module -Force
$framebuffer = Read-Oot3dFramebufferBmp -Path $Path
$metrics = Get-Oot3dFramebufferMetrics -Framebuffer $framebuffer
if ($metrics.UniqueColors -lt $MinimumUniqueColors -or
    $metrics.ChannelRange -lt $MinimumChannelRange -or
    $metrics.ChromaticFraction -lt $MinimumChromaticFraction) {
    throw "Framebuffer artifact is visually collapsed: " +
        "colors=$($metrics.UniqueColors), " +
        "channelRange=$($metrics.ChannelRange), " +
        "chromaticFraction=" +
        "$([Math]::Round($metrics.ChromaticFraction, 4))"
}

$metrics
