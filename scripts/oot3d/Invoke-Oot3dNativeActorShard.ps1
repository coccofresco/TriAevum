param(
    [string[]]$Source = @("E:\ppssppvr\oot3d_decomp\work\extract\romfs\actor\zelda_link_child_new.zar"),
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$ReadinessManifest = "",
    [string]$ActorSourceRoot = "E:\ppssppvr\oot3d_decomp\work\extract\romfs\actor",
    [string]$Catalog = "",
    [string]$CodeBin = "E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin",
    [string]$NativeAbiCatalog = "",
    [string[]]$RoomCompilationUnit = @(),
    [switch]$IncludeReadinessSkinnedArchives,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$toolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
$outputRoot = Join-Path $WorkRoot "playable_actors"
$archive = Join-Path $outputRoot "oot3d-actors-native.o2r"
$manifest = Join-Path $outputRoot "oot3d_native_actor_shard.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$resolvedSources = [System.Collections.Generic.List[string]]::new()
$seenSources = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($path in $Source) {
    $resolved = (Resolve-Path -LiteralPath $path -ErrorAction Stop).Path
    if ($seenSources.Add($resolved)) { $resolvedSources.Add($resolved) }
}
if ($IncludeReadinessSkinnedArchives) {
    if ([string]::IsNullOrWhiteSpace($ReadinessManifest)) {
        $ReadinessManifest = Join-Path $WorkRoot "kokiri_actor_asset_readiness\oot3d_kokiri_actor_asset_readiness_audit.json"
    }
    if (-not (Test-Path -LiteralPath $ReadinessManifest -PathType Leaf)) {
        throw "actor readiness manifest not found: $ReadinessManifest"
    }
    $readiness = Get-Content -LiteralPath $ReadinessManifest -Raw | ConvertFrom-Json
    if ($readiness.format -ne "oot3d_kokiri_actor_asset_readiness_v1") {
        throw "unsupported actor readiness manifest format: $($readiness.format)"
    }
    foreach ($record in $readiness.object_records) {
        if ([string]$record.asset_readiness_status -notmatch "skinned_animation_bindings_ready") { continue }
        $archiveName = [string]$record.archive_path
        if ([string]::IsNullOrWhiteSpace($archiveName)) {
            throw "ready skinned actor record has no archive_path: $($record.object_name)"
        }
        $resolved = (Resolve-Path -LiteralPath (Join-Path $ActorSourceRoot $archiveName) -ErrorAction Stop).Path
        if ($seenSources.Add($resolved)) { $resolvedSources.Add($resolved) }
    }
}
if ($resolvedSources.Count -eq 0) { throw "no native actor sources selected" }
if ([string]::IsNullOrWhiteSpace($Catalog)) {
    $Catalog = Join-Path $WorkRoot "playable_catalog\oot3d_asset_catalog.json"
}
if ([string]::IsNullOrWhiteSpace($NativeAbiCatalog)) {
    $operationalInputs = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "tools\oot3d\operational_inputs.json") | ConvertFrom-Json
    $evidenceRoot = ([string]$operationalInputs.variables.zelda3dRecompEvidence).Replace('${repoRoot}', $repoRoot)
    $NativeAbiCatalog = Join-Path $evidenceRoot "native_abi_catalog.json"
}

Push-Location $toolRoot
try {
    $env:PYTHONPATH = Join-Path $toolRoot "src"
    $arguments = @("-m", "oot3d_asset_tool.native_actor_shard")
    foreach ($path in $resolvedSources) { $arguments += @("--source", $path) }
    foreach ($path in $RoomCompilationUnit) {
        $arguments += @("--room-compilation-unit", (Resolve-Path -LiteralPath $path -ErrorAction Stop).Path)
    }
    if ($RoomCompilationUnit.Count -ne 0) {
        $arguments += @("--actor-source-root", (Resolve-Path -LiteralPath $ActorSourceRoot -ErrorAction Stop).Path)
    }
    $arguments += @("--output", $archive, "--manifest-output", $manifest)
    if ($IncludeReadinessSkinnedArchives) {
        $arguments += @("--code-bin", (Resolve-Path -LiteralPath $CodeBin -ErrorAction Stop).Path)
        $arguments += @("--native-abi-catalog", (Resolve-Path -LiteralPath $NativeAbiCatalog -ErrorAction Stop).Path)
    }
    if ($Verify) { $arguments += "--verify" }
    & python @arguments
    if ($LASTEXITCODE -ne 0) { throw "native actor shard failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$shard = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if ($Verify) {
    if (-not (Test-Path -LiteralPath $Catalog -PathType Leaf)) {
        throw "OOT3D asset catalog not found: $Catalog"
    }
    $catalogJson = Get-Content -LiteralPath $Catalog -Raw | ConvertFrom-Json
    $catalogIdentities = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($record in $catalogJson.records) {
        [void]$catalogIdentities.Add([string]$record.source_identity)
    }
    $missingModels = [System.Collections.Generic.List[string]]::new()
    foreach ($archiveRecord in $shard.records) {
        foreach ($file in $archiveRecord.files) {
            if ([string]$file.type -ne "cmb") { continue }
            $identity = "cmb:$($archiveRecord.source_container)!$($file.name)"
            if (-not $catalogIdentities.Contains($identity)) { $missingModels.Add($identity) }
        }
    }
    if ($missingModels.Count -ne 0) {
        throw "native actor shard contains uncatalogued CMB identities: $($missingModels -join ', ')"
    }
}

Write-Host "OOT3D native actor shard: $archive"
Write-Host "OOT3D native actor manifest: $manifest"
Write-Host "OOT3D native actor archive count: $($shard.archive_count)"
Write-Host "OOT3D native object-bank binding count: $($shard.object_bank_bindings.Count)"
