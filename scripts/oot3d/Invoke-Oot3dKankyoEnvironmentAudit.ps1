param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$KankyoRoot = "",
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
if ([string]::IsNullOrWhiteSpace($KankyoRoot)) {
    $KankyoRoot = Join-Path $RomFs "kankyo"
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
        throw "OOT3D kankyo environment audit verification failed: $Message"
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
Require-Path $KankyoRoot "OOT3D kankyo directory"

$outputRoot = Join-Path $WorkRoot "kankyo_environment_audit"
$auditOutput = Join-Path $outputRoot "oot3d_kankyo_environment_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_kankyo_environment_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $auditArgs = @(
        "audit-kankyo-environment-assets",
        $KankyoRoot,
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
    kankyo_root = (Resolve-Path $KankyoRoot).Path
    output_root = $outputRoot
    audit = $auditOutput
    archive_count = $audit.archive_count
    archive_size_total = $audit.archive_size_total
    archive_file_count_total = $audit.archive_file_count_total
    archive_file_count_counts = $audit.archive_file_count_counts
    archive_parse_error_count = $audit.archive_parse_error_count
    embedded_type_counts = $audit.embedded_type_counts
    cmb_counts = $audit.cmb_counts
    cmb_support_counts = $audit.cmb_support_counts
    cmb_bone_count_counts = $audit.cmb_bone_count_counts
    cmb_mesh_count_counts = $audit.cmb_mesh_count_counts
    cmb_material_count_counts = $audit.cmb_material_count_counts
    cmb_texture_count_counts = $audit.cmb_texture_count_counts
    cmb_skeleton_header_counts = $audit.cmb_skeleton_header_counts
    cmb_skinning_mode_primitive_counts = $audit.cmb_skinning_mode_primitive_counts
    cmb_primitive_bone_count_counts = $audit.cmb_primitive_bone_count_counts
    cmb_skinning_mode_bone_count_counts = $audit.cmb_skinning_mode_bone_count_counts
    cmb_triangle_total = $audit.cmb_triangle_total
    cmb_vertex_total = $audit.cmb_vertex_total
    cmb_primitive_total = $audit.cmb_primitive_total
    cmb_shape_total = $audit.cmb_shape_total
    environment_model_group_counts = $audit.environment_model_group_counts
    material_alpha_test_counts = $audit.material_alpha_test_counts
    material_blend_mode_counts = $audit.material_blend_mode_counts
    material_depth_state_counts = $audit.material_depth_state_counts
    cmab_count = $audit.cmab_count
    cmab_size_summary = $audit.cmab_size_summary
    cmab_magic_counts = $audit.cmab_magic_counts
    cmab_declared_size_match_counts = $audit.cmab_declared_size_match_counts
    cmab_header_version_counts = $audit.cmab_header_version_counts
    cmab_header_field_counts = $audit.cmab_header_field_counts
    cmab_layout_status_counts = $audit.cmab_layout_status_counts
    cmab_mmad_count_counts = $audit.cmab_mmad_count_counts
    cmab_mads_record_count_counts = $audit.cmab_mads_record_count_counts
    cmab_frame_count_candidate_counts = $audit.cmab_frame_count_candidate_counts
    cmab_loop_mode_candidate_counts = $audit.cmab_loop_mode_candidate_counts
    ctxb_count = $audit.ctxb_count
    ctxb_size_summary = $audit.ctxb_size_summary
    ctxb_magic_counts = $audit.ctxb_magic_counts
    tbd_count = $audit.tbd_count
    tbd_size_summary = $audit.tbd_size_summary
    tbd_magic_counts = $audit.tbd_magic_counts
    tbd_version_counts = $audit.tbd_version_counts
    tbd_declared_size_match_counts = $audit.tbd_declared_size_match_counts
    tbd_entry_count_counts = $audit.tbd_entry_count_counts
    sample_record_count = $audit.sample_records.Count
    record_count = $audit.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.archive_count -eq 6) "expected 6 kankyo ZAR archives"
    Assert-Condition ($summary.archive_size_total -eq 1793296) "expected kankyo archive total size 1793296"
    Assert-Condition ($summary.archive_file_count_total -eq 107) "expected 107 embedded kankyo files"
    Assert-Condition ($summary.archive_parse_error_count -eq 0) "expected 0 archive parse errors"
    Assert-Condition ((Get-JsonValue $summary.archive_file_count_counts "12") -eq 3) "expected three archives with 12 embedded files"
    Assert-Condition ((Get-JsonValue $summary.archive_file_count_counts "18") -eq 1) "expected one archive with 18 embedded files"
    Assert-Condition ((Get-JsonValue $summary.archive_file_count_counts "51") -eq 1) "expected one archive with 51 embedded files"
    Assert-Condition ((Get-JsonValue $summary.archive_file_count_counts "2") -eq 1) "expected one archive with 2 embedded files"

    Assert-Condition ($summary.embedded_type_counts.cmb -eq 76) "expected 76 embedded CMB files"
    Assert-Condition ($summary.embedded_type_counts.cmab -eq 11) "expected 11 embedded CMAB files"
    Assert-Condition ($summary.embedded_type_counts.ctxb -eq 18) "expected 18 embedded CTXB files"
    Assert-Condition ($summary.embedded_type_counts.tbd -eq 2) "expected 2 embedded TBD files"

    Assert-Condition ($summary.cmb_counts.discovered -eq 76) "expected 76 discovered kankyo CMB models"
    Assert-Condition ($summary.cmb_counts.parsed -eq 76) "expected 76 parsed kankyo CMB models"
    Assert-Condition ($summary.cmb_counts.parse_errors -eq 0) "expected 0 CMB parse errors"
    Assert-Condition ($summary.cmb_support_counts.static_cmb_export_supported -eq 76) "expected every kankyo CMB to be static-export supported"
    Assert-Condition ((Get-JsonValue $summary.cmb_bone_count_counts "1") -eq 76) "expected every kankyo CMB to have one bone"
    Assert-Condition ((Get-JsonValue $summary.cmb_mesh_count_counts "1") -eq 76) "expected every kankyo CMB to have one mesh"
    Assert-Condition ((Get-JsonValue $summary.cmb_material_count_counts "1") -eq 76) "expected every kankyo CMB to have one material"
    Assert-Condition ((Get-JsonValue $summary.cmb_texture_count_counts "0") -eq 38) "expected 38 kankyo CMBs without embedded textures"
    Assert-Condition ((Get-JsonValue $summary.cmb_texture_count_counts "1") -eq 38) "expected 38 kankyo CMBs with one embedded texture"
    Assert-Condition ((Get-JsonValue $summary.cmb_skeleton_header_counts "2") -eq 76) "expected every kankyo SKL header word_0c to be 2"
    Assert-Condition ((Get-JsonValue $summary.cmb_skinning_mode_primitive_counts "0") -eq 76) "expected 76 rigid mode-0 primitives"
    Assert-Condition ((Get-JsonValue $summary.cmb_primitive_bone_count_counts "1") -eq 76) "expected every kankyo primitive to reference one bone"
    Assert-Condition ((Get-JsonValue $summary.cmb_skinning_mode_bone_count_counts "mode=0;bones=1") -eq 76) "expected every kankyo primitive to be mode 0 with one bone"
    Assert-Condition ($summary.cmb_triangle_total -eq 9216) "expected 9216 kankyo CMB triangles"
    Assert-Condition ($summary.cmb_vertex_total -eq 6290) "expected 6290 kankyo CMB vertices"
    Assert-Condition ($summary.cmb_primitive_total -eq 76) "expected 76 kankyo primitives"
    Assert-Condition ($summary.cmb_shape_total -eq 76) "expected 76 kankyo shapes"
    Assert-Condition ($summary.environment_model_group_counts.kumo_cloud -eq 36) "expected 36 cloud models"
    Assert-Condition ($summary.environment_model_group_counts.tenkyu_sky_dome -eq 32) "expected 32 sky-dome models"
    Assert-Condition ($summary.environment_model_group_counts.sun -eq 6) "expected 6 sun models"
    Assert-Condition ($summary.environment_model_group_counts.star -eq 2) "expected 2 star models"
    Assert-Condition ((Get-JsonValue $summary.material_alpha_test_counts "False") -eq 76) "expected no kankyo alpha-test materials"
    Assert-Condition ((Get-JsonValue $summary.material_blend_mode_counts "1") -eq 69) "expected 69 blend-mode 1 materials"
    Assert-Condition ((Get-JsonValue $summary.material_blend_mode_counts "0") -eq 7) "expected 7 blend-mode 0 materials"
    Assert-Condition ((Get-JsonValue $summary.material_depth_state_counts "test=False;write=False") -eq 70) "expected 70 no-depth-write/test materials"
    Assert-Condition ((Get-JsonValue $summary.material_depth_state_counts "test=True;write=False") -eq 6) "expected 6 depth-test/no-write materials"

    Assert-Condition ($summary.cmab_count -eq 11) "expected 11 kankyo CMAB files"
    Assert-Condition ($summary.cmab_size_summary.total -eq 1856) "expected CMAB total byte size 1856"
    Assert-Condition ($summary.cmab_size_summary.min -eq 128) "expected CMAB min size 128"
    Assert-Condition ($summary.cmab_size_summary.max -eq 192) "expected CMAB max size 192"
    Assert-Condition ($summary.cmab_size_summary.unique_size_count -eq 2) "expected 2 unique CMAB sizes"
    Assert-Condition ($summary.cmab_magic_counts.'ascii:cmab' -eq 11) "expected every kankyo CMAB to use cmab magic"
    Assert-Condition ($summary.cmab_declared_size_match_counts.matches -eq 11) "expected every kankyo CMAB declared size to match"
    Assert-Condition ((Get-JsonValue $summary.cmab_header_version_counts "1") -eq 11) "expected every kankyo CMAB header version to be 1"
    Assert-Condition ((Get-JsonValue $summary.cmab_layout_status_counts "markers_consistent") -eq 11) "expected every kankyo CMAB layout marker audit to pass"
    Assert-Condition ((Get-JsonValue $summary.cmab_mmad_count_counts "2") -eq 7) "expected seven two-MMAD kankyo CMAB payloads"
    Assert-Condition ((Get-JsonValue $summary.cmab_mmad_count_counts "1") -eq 4) "expected four one-MMAD kankyo CMAB payloads"
    Assert-Condition ((Get-JsonValue $summary.cmab_frame_count_candidate_counts "900") -eq 9) "expected nine kankyo CMAB payloads with 900-frame candidate"
    Assert-Condition ((Get-JsonValue $summary.cmab_frame_count_candidate_counts "600") -eq 2) "expected two kankyo CMAB payloads with 600-frame candidate"
    Assert-Condition ((Get-JsonValue $summary.cmab_loop_mode_candidate_counts "1") -eq 10) "expected ten looping kankyo CMAB payloads"
    Assert-Condition ((Get-JsonValue $summary.cmab_loop_mode_candidate_counts "0") -eq 1) "expected one non-looping kankyo CMAB payload"

    Assert-Condition ($summary.ctxb_count -eq 18) "expected 18 kankyo CTXB files"
    Assert-Condition ($summary.ctxb_size_summary.total -eq 197904) "expected CTXB total byte size 197904"
    Assert-Condition ($summary.ctxb_size_summary.min -eq 8264) "expected CTXB min size 8264"
    Assert-Condition ($summary.ctxb_size_summary.max -eq 32840) "expected CTXB max size 32840"
    Assert-Condition ($summary.ctxb_magic_counts.'ascii:ctxb' -eq 18) "expected every kankyo CTXB to use ctxb magic"

    Assert-Condition ($summary.tbd_count -eq 2) "expected 2 kankyo TBD files"
    Assert-Condition ($summary.tbd_size_summary.total -eq 648) "expected TBD total byte size 648"
    Assert-Condition ($summary.tbd_magic_counts.'hex:74626400' -eq 2) "expected every TBD to use tbd00 magic"
    Assert-Condition ((Get-JsonValue $summary.tbd_version_counts "1") -eq 2) "expected every TBD version to be 1"
    Assert-Condition ($summary.tbd_declared_size_match_counts.matches -eq 2) "expected every TBD declared size to match"
    Assert-Condition ((Get-JsonValue $summary.tbd_entry_count_counts "5") -eq 1) "expected one TBD with 5 entries"
    Assert-Condition ((Get-JsonValue $summary.tbd_entry_count_counts "2") -eq 1) "expected one TBD with 2 entries"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D kankyo environment audit: $auditOutput"
Write-Host "OOT3D kankyo environment summary: $summaryOutput"
