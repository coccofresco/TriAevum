param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$SkinnedBindingManifest = "",
    [string]$AnimationLikeAudit = "",
    [string]$Output = "",
    [string]$CsvOutput = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($SkinnedBindingManifest)) {
    $SkinnedBindingManifest = Join-Path $WorkRoot "skinned_animation_binding\oot3d_skinned_animation_binding_manifest.json"
}
if ([string]::IsNullOrWhiteSpace($AnimationLikeAudit)) {
    $AnimationLikeAudit = Join-Path $WorkRoot "animation_like_audit\oot3d_actor_animation_like_payload_audit.json"
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $WorkRoot "character_conversion\zelda_ganon_character_comparative_audit.json"
}
if ([string]::IsNullOrWhiteSpace($CsvOutput)) {
    $CsvOutput = Join-Path $WorkRoot "character_conversion\zelda_ganon_character_comparative_targets.csv"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Zelda/Ganon comparative audit verification failed: $Message"
    }
}

function Get-JsonValue([object]$Object, [string]$Name) {
    if ($null -eq $Object) {
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

function Counter-Json([object]$Object) {
    if ($null -eq $Object) {
        return "{}"
    }
    return ($Object | ConvertTo-Json -Compress -Depth 8)
}

function Archive-PayloadCount([object]$AnimationLike, [string]$PayloadType, [string]$ArchivePath) {
    $counts = Get-JsonValue (Get-JsonValue $AnimationLike "archive_payload_counts") $PayloadType
    $value = Get-JsonValue $counts $ArchivePath
    if ($null -eq $value) {
        return 0
    }
    return [int]$value
}

function Target-Group([object]$Target) {
    $archive = [string]$Target.archive_path
    $cmb = [string]$Target.target_cmb_name
    if ($archive -match '^zelda_zl[0-9]' -or $archive -eq "zelda_horse_zelda.zar" -or $cmb -match 'zelda') {
        return "zelda"
    }
    if ($archive -match 'ganon|gnd|fantomHG' -or $cmb -match 'ganon|gnd|phantom') {
        return "ganon"
    }
    return ""
}

function Numeric-Summary([int[]]$Values) {
    if ($Values.Count -eq 0) {
        return [ordered]@{ min = 0; max = 0; total = 0 }
    }
    $measure = $Values | Measure-Object -Minimum -Maximum -Sum
    return [ordered]@{
        min = [int]$measure.Minimum
        max = [int]$measure.Maximum
        total = [int]$measure.Sum
    }
}

Require-Path $SkinnedBindingManifest "Skinned animation binding manifest"
Require-Path $AnimationLikeAudit "Animation-like payload audit"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $CsvOutput) | Out-Null

$binding = Get-Content -LiteralPath $SkinnedBindingManifest -Raw | ConvertFrom-Json
$animationLike = Get-Content -LiteralPath $AnimationLikeAudit -Raw | ConvertFrom-Json

$rows = New-Object System.Collections.Generic.List[object]
foreach ($target in @($binding.targets)) {
    $group = Target-Group $target
    if ([string]::IsNullOrWhiteSpace($group)) {
        continue
    }
    $animations = @($target.animations)
    $frameSlots = @($animations | ForEach-Object { [int]$_.frame_slot_count })
    $trackCounts = @($animations | ForEach-Object { [int]$_.counts.track_count })
    $channelCounts = @($animations | ForEach-Object { [int]$_.counts.channel_count })
    $keyedCounts = @($animations | ForEach-Object { [int]$_.counts.keyed_channel_count })
    $constCounts = @($animations | ForEach-Object { [int]$_.counts.const_channel_count })
    $keyframeCounts = @($animations | ForEach-Object { [int]$_.counts.keyframe_count })
    $bindCounts = $target.bind_pose.counts
    $archive = [string]$target.archive_path
    $row = [pscustomobject]@{
        target_group = $group
        archive_path = $archive
        target_cmb_name = [string]$target.target_cmb_name
        model_name = [string]$target.model_name
        bone_count = [int]$target.bone_count
        animation_count = [int]$target.animation_count
        frame_slot_min = (Numeric-Summary $frameSlots).min
        frame_slot_max = (Numeric-Summary $frameSlots).max
        track_count_min = (Numeric-Summary $trackCounts).min
        track_count_max = (Numeric-Summary $trackCounts).max
        channel_count_min = (Numeric-Summary $channelCounts).min
        channel_count_max = (Numeric-Summary $channelCounts).max
        keyed_channel_total = (Numeric-Summary $keyedCounts).total
        const_channel_total = (Numeric-Summary $constCounts).total
        keyframe_total = (Numeric-Summary $keyframeCounts).total
        support_status_counts = Counter-Json $target.support_status_counts
        target_resolution_status_counts = Counter-Json $target.target_resolution_status_counts
        mesh_count = [int]$bindCounts.mesh_count
        skinned_primitive_count = [int]$bindCounts.skinned_primitive_count
        mode_1_primitive_count = [int]$bindCounts.mode_1_primitive_count
        mode_2_primitive_count = [int]$bindCounts.mode_2_primitive_count
        anb_payload_count = Archive-PayloadCount $animationLike "anb" $archive
        faceb_payload_count = Archive-PayloadCount $animationLike "faceb" $archive
        sample_csab_names = (($animations | Select-Object -First 8 | ForEach-Object { [string]$_.csab_name }) -join "; ")
    }
    $rows.Add($row)
}

$rowArray = @($rows.ToArray() | Sort-Object target_group, archive_path, target_cmb_name)
$groupSummaries = @{}
foreach ($group in @("zelda", "ganon")) {
    $groupRows = @($rowArray | Where-Object { $_.target_group -eq $group })
    $groupSummaries[$group] = [ordered]@{
        target_count = $groupRows.Count
        archive_count = @($groupRows | Select-Object -ExpandProperty archive_path -Unique).Count
        animation_count = [int](($groupRows | Measure-Object animation_count -Sum).Sum)
        bone_count_min = [int](($groupRows | Measure-Object bone_count -Minimum).Minimum)
        bone_count_max = [int](($groupRows | Measure-Object bone_count -Maximum).Maximum)
        frame_slot_max = [int](($groupRows | Measure-Object frame_slot_max -Maximum).Maximum)
        anb_payload_count = [int](($groupRows | Measure-Object anb_payload_count -Sum).Sum)
        faceb_payload_count = [int](($groupRows | Measure-Object faceb_payload_count -Sum).Sum)
    }
}

$audit = [ordered]@{
    format = "oot3d_zelda_ganon_character_comparative_audit_v1"
    skinned_binding_manifest = (Resolve-Path $SkinnedBindingManifest).Path
    animation_like_audit = (Resolve-Path $AnimationLikeAudit).Path
    target_count = $rowArray.Count
    group_summaries = $groupSummaries
    link_child_reference = [ordered]@{
        archive_path = "zelda_link_child_new.zar"
        target_cmb_name = "child/model/childlink_v2.cmb"
        animation_count = 582
        bone_count = 25
        anb_payload_count = 561
        faceb_payload_count = 582
    }
    comparative_notes = @(
        "Zelda and Ganon/Ganondorf comparison targets are CSAB/CMB skinned-animation targets, not ANB-bearing Link ultra archives.",
        "Their archives provide broader skinned CSAB skeleton and skinning-mode comparison cases, while ANB remains Link-ultra specific in the current extraction audit."
    )
    targets = $rowArray
}

$audit | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Output -Encoding UTF8
$rowArray | Export-Csv -LiteralPath $CsvOutput -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($audit.format -eq "oot3d_zelda_ganon_character_comparative_audit_v1") "unexpected audit format"
    Assert-Condition ([int]$audit.target_count -eq 15) "expected 15 Zelda/Ganon comparative skinned targets"
    Assert-Condition ([int]$groupSummaries.zelda.target_count -eq 4) "expected 4 Zelda comparative targets"
    Assert-Condition ([int]$groupSummaries.ganon.target_count -eq 11) "expected 11 Ganon/Ganondorf comparative targets"
    Assert-Condition ([int]$groupSummaries.zelda.animation_count -eq 112) "expected 112 Zelda CSAB animations"
    Assert-Condition ([int]$groupSummaries.ganon.animation_count -eq 150) "expected 150 Ganon/Ganondorf CSAB animations"
    Assert-Condition ([int]$groupSummaries.zelda.anb_payload_count -eq 0) "expected no Zelda ANB payloads in selected archives"
    Assert-Condition ([int]$groupSummaries.ganon.anb_payload_count -eq 0) "expected no Ganon/Ganondorf ANB payloads in selected archives"
    Assert-Condition ([int]$groupSummaries.zelda.faceb_payload_count -eq 0) "expected no Zelda FACEB payloads in selected archives"
    Assert-Condition ([int]$groupSummaries.ganon.faceb_payload_count -eq 0) "expected no Ganon/Ganondorf FACEB payloads in selected archives"
}

Write-Host "OOT3D Zelda/Ganon comparative audit: $Output"
Write-Host "OOT3D Zelda/Ganon comparative target CSV: $CsvOutput"
Write-Host "targets=$($audit.target_count) zeldaTargets=$($groupSummaries.zelda.target_count) ganonTargets=$($groupSummaries.ganon.target_count)"
