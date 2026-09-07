[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$ReferencePath,
    [Parameter(Mandatory)]
    [string]$CandidatePath,
    [ValidateRange(0, 255)]
    [int]$ChangedPixelThreshold = 2,
    [ValidateRange(1024, 10000000)]
    [int]$MaximumSampledPixels = 240000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$module = Join-Path $PSScriptRoot "Oot3dFramebufferArtifact.psm1"
Import-Module $module -Force
$reference = Read-Oot3dFramebufferBmp -Path $ReferencePath
$candidate = Read-Oot3dFramebufferBmp -Path $CandidatePath
Compare-Oot3dFramebufferBmp `
    -Reference $reference `
    -Candidate $candidate `
    -ChangedPixelThreshold $ChangedPixelThreshold `
    -MaximumSampledPixels $MaximumSampledPixels
