param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$sceneShardScript = Join-Path $PSScriptRoot "Invoke-Oot3dNativeSceneShard.ps1"

$shards = @(
    @{ Name = "oot3d-scenes-overworld"; Scenes = @("spot04") },
    @{ Name = "oot3d-scenes-indoors"; Scenes = @("link") }
)

foreach ($shard in $shards) {
    $arguments = @{
        WorkRoot = $WorkRoot
        ShardName = $shard.Name
        Scene = $shard.Scenes
    }
    if ($Verify) {
        $arguments.Verify = $true
    }
    & $sceneShardScript @arguments
}

Write-Host "OOT3D Route A scene shards generated: $($shards.Name -join ', ')"
