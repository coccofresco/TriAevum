param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$SceneRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [int]$SampleLimit = 100,
    [switch]$NoRecords,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($SceneRoot)) {
    $SceneRoot = Join-Path $RomFs "scene"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Invoke-Oot3dTool([string[]]$Arguments) {
    Write-Host "python -m oot3d_asset_tool $($Arguments -join ' ')"
    & python -m oot3d_asset_tool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "oot3d_asset_tool failed with exit code $LASTEXITCODE"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D ZSI scene metadata audit verification failed: $Message"
    }
}

function Get-JsonValue($Object, [string]$Name) {
    if ($null -eq $Object) {
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

Require-Path $ToolRoot "Tool root"
Require-Path $SceneRoot "OOT3D scene directory"

$outputRoot = Join-Path $WorkRoot "zsi_scene_metadata_audit"
$auditOutput = Join-Path $outputRoot "oot3d_zsi_scene_metadata_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_zsi_scene_metadata_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $auditArgs = @(
        "audit-zsi-scene-metadata",
        $SceneRoot,
        "--output",
        $auditOutput,
        "--sample-limit",
        ([string]$SampleLimit)
    )
    if ($NoRecords) {
        $auditArgs += "--no-records"
    }
    Invoke-Oot3dTool -Arguments $auditArgs
}
finally {
    Pop-Location
}

$audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    scene_root = (Resolve-Path $SceneRoot).Path
    output_root = $outputRoot
    audit = $auditOutput
    zsi_file_count = $audit.zsi_file_count
    role_counts = $audit.role_counts
    scene_stem_count = $audit.scene_stem_count
    room_stem_count = $audit.room_stem_count
    room_count_total = $audit.room_count_total
    scene_file_multiplicity_counts = $audit.scene_file_multiplicity_counts
    room_count_counts = $audit.room_count_counts
    room_counts_by_scene = $audit.room_counts_by_scene
    setup_file_count = $audit.setup_file_count
    setup_total = $audit.setup_total
    setup_count_counts = $audit.setup_count_counts
    setup_count_by_role = $audit.setup_count_by_role
    command_total = $audit.command_total
    command_id_counts = $audit.command_id_counts
    command_parameter_counts = $audit.command_parameter_counts
    command_sequence_counts = $audit.command_sequence_counts
    embedded_cmb_count_counts = $audit.embedded_cmb_count_counts
    collision_candidate_count_counts = $audit.collision_candidate_count_counts
    collision_file_count = $audit.collision_file_count
    collision_candidate_total = $audit.collision_candidate_total
    camera_position_vector_total = $audit.camera_position_vector_total
    water_box_total = $audit.water_box_total
    bgcam_total = $audit.bgcam_total
    collision_vertex_total = $audit.collision_vertex_total
    collision_raw_polygon_total = $audit.collision_raw_polygon_total
    collision_effective_polygon_total = $audit.collision_effective_polygon_total
    collision_surface_type_total = $audit.collision_surface_type_total
    parse_error_count = $audit.parse_error_count
    sample_record_count = $audit.sample_records.Count
    record_count = $audit.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.zsi_file_count -eq 724) "expected 724 scene ZSI files"
    Assert-Condition ($summary.role_counts.scene -eq 114) "expected 114 scene-level ZSI files"
    Assert-Condition ($summary.role_counts.room -eq 610) "expected 610 room ZSI files"
    Assert-Condition ($summary.scene_stem_count -eq 102) "expected 102 scene stems"
    Assert-Condition ($summary.room_stem_count -eq 102) "expected 102 room stems"
    Assert-Condition ($summary.room_count_total -eq 610) "expected 610 total room files"
    Assert-Condition ((Get-JsonValue $summary.scene_file_multiplicity_counts "1") -eq 90) "expected 90 single scene files"
    Assert-Condition ((Get-JsonValue $summary.scene_file_multiplicity_counts "2") -eq 12) "expected 12 duplicated scene stems"
    Assert-Condition ($summary.room_counts_by_scene.jyasinzou -eq 58) "expected jyasinzou to have 58 room files"
    Assert-Condition ($summary.room_counts_by_scene.hidan -eq 54) "expected hidan to have 54 room files"
    Assert-Condition ($summary.room_counts_by_scene.bmori1 -eq 46) "expected bmori1 to have 46 room files"
    Assert-Condition ($summary.room_counts_by_scene.hakadan -eq 46) "expected hakadan to have 46 room files"
    Assert-Condition ($summary.room_counts_by_scene.mizusin -eq 46) "expected mizusin to have 46 room files"

    Assert-Condition ($summary.setup_file_count -eq 724) "expected every scene/room ZSI to expose native setup commands"
    Assert-Condition ($summary.setup_total -eq 1149) "expected 254 scene and 895 room setup streams"
    Assert-Condition ((Get-JsonValue $summary.setup_count_counts "1") -eq 576) "expected 576 files with 1 setup"
    Assert-Condition ((Get-JsonValue $summary.setup_count_counts "2") -eq 71) "expected 71 files with 2 setups"
    Assert-Condition ((Get-JsonValue $summary.setup_count_counts "3") -eq 19) "expected 19 files with 3 setups"
    Assert-Condition ((Get-JsonValue $summary.setup_count_counts "4") -eq 31) "expected 31 files with 4 setups"
    Assert-Condition ((Get-JsonValue $summary.setup_count_counts "5") -eq 2) "expected 2 files with 5 setups"
    Assert-Condition ((Get-JsonValue $summary.setup_count_counts "6") -eq 8) "expected 8 files with 6 setups"
    Assert-Condition ((Get-JsonValue $summary.setup_count_counts "7") -eq 2) "expected 2 files with 7 setups"
    Assert-Condition ((Get-JsonValue $summary.setup_count_counts "9") -eq 2) "expected 2 files with 9 setups"
    Assert-Condition ((Get-JsonValue $summary.setup_count_counts "11") -eq 2) "expected 2 files with 11 setups"
    Assert-Condition ((Get-JsonValue $summary.setup_count_counts "12") -eq 5) "expected 5 files with 12 setups"
    Assert-Condition ((Get-JsonValue $summary.setup_count_counts "13") -eq 6) "expected 6 files with 13 setups"
    Assert-Condition ((Get-JsonValue $summary.setup_count_by_role "scene:1") -eq 79) "expected 79 one-setup scene files"
    Assert-Condition ((Get-JsonValue $summary.setup_count_by_role "room:1") -eq 497) "expected 497 one-setup room files"

    Assert-Condition ($summary.command_total -eq 9563) "expected 9563 total native setup commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x15") -eq 175) "expected 175 0x15 commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x04") -eq 254) "expected 254 0x04 commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x0e") -eq 153) "expected 153 0x0e commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x19") -eq 254) "expected 254 0x19 commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x03") -eq 254) "expected 254 0x03 commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x06") -eq 254) "expected 254 0x06 commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x07") -eq 220) "expected 220 0x07 commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x00") -eq 254) "expected 254 0x00 commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x11") -eq 254) "expected 254 0x11 commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x13") -eq 236) "expected 236 0x13 commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x0f") -eq 254) "expected 254 0x0f commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x08") -eq 895) "expected 895 room behavior commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x0a") -eq 895) "expected 895 room mesh commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x0b") -eq 866) "expected 866 room object-list commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x10") -eq 895) "expected 895 room time commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x12") -eq 895) "expected 895 room sky-control commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x14") -eq 1149) "expected one end marker per setup"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x16") -eq 398) "expected 398 room audio/environment commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x17") -eq 112) "expected 112 0x17 commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x0d") -eq 66) "expected 66 0x0d commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x01") -eq 800) "expected 800 actor-list commands"

    Assert-Condition ((Get-JsonValue $summary.embedded_cmb_count_counts "0") -eq 114) "expected 114 ZSI files without embedded CMBs"
    Assert-Condition ((Get-JsonValue $summary.embedded_cmb_count_counts "1") -eq 610) "expected 610 ZSI files with one embedded CMB"
    Assert-Condition ((Get-JsonValue $summary.collision_candidate_count_counts "0") -eq 610) "expected room ZSI files without scene collision headers"
    Assert-Condition ((Get-JsonValue $summary.collision_candidate_count_counts "1") -eq 114) "expected every scene ZSI to expose one collision header"
    Assert-Condition ($summary.collision_file_count -eq 114) "expected 114 files with collision candidates"
    Assert-Condition ($summary.collision_candidate_total -eq 114) "expected 114 total collision candidates"
    Assert-Condition ($summary.camera_position_vector_total -eq 714) "expected 714 camera position vectors"
    Assert-Condition ($summary.water_box_total -eq 186) "expected 186 water boxes"
    Assert-Condition ($summary.bgcam_total -eq 581) "expected 581 bg camera entries"
    Assert-Condition ($summary.collision_vertex_total -eq 118424) "expected 118424 collision vertices"
    Assert-Condition ($summary.collision_raw_polygon_total -eq 170175) "expected 170175 raw collision polygons"
    Assert-Condition ($summary.collision_effective_polygon_total -eq 170175) "expected 170175 effective collision polygons"
    Assert-Condition ($summary.collision_surface_type_total -eq 3277) "expected 3277 collision surface types"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 ZSI parse errors"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D ZSI scene metadata audit: $auditOutput"
Write-Host "OOT3D ZSI scene metadata summary: $summaryOutput"
