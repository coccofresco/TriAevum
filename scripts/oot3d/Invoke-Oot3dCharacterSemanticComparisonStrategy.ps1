param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$SkinnedBindingManifest = "",
    [string]$AnimationLikeAudit = "",
    [string]$CmabAudit = "",
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
if ([string]::IsNullOrWhiteSpace($CmabAudit)) {
    $CmabAudit = Join-Path $WorkRoot "cmab_audit\oot3d_cmab_payload_audit.json"
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $WorkRoot "character_conversion\character_semantic_comparison_strategy.json"
}
if ([string]::IsNullOrWhiteSpace($CsvOutput)) {
    $CsvOutput = Join-Path $WorkRoot "character_conversion\character_semantic_comparison_strategy.csv"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D character semantic comparison strategy verification failed: $Message"
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

function Archive-CmabCount([object]$Cmab, [string]$ArchivePath) {
    $value = Get-JsonValue $Cmab.archive_cmab_counts $ArchivePath
    if ($null -eq $value) {
        return 0
    }
    return [int]$value
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

function Target-Strategy([string]$Archive, [string]$Cmb, [string]$Model, [int]$AnimationCount, [int]$BoneCount, [int]$AnbCount, [int]$FacebCount, [int]$CmabCount) {
    $nameLower = "$Cmb $Model".ToLowerInvariant()
    if ($Archive -eq "zelda_link_child_new.zar") {
        return [ordered]@{
            priority = 0
            group = "link_child_primary"
            scope = "active_core"
            anb_role = "csab_target_for_link_child_anb"
            reason = "Primary child Link CMB/CSAB/FACEB/CMAB target; the ANB semantic player is ultimately judged against this skeleton and CSAB family."
        }
    }
    if ($Archive -eq "zelda_link_boy_new.zar") {
        return [ordered]@{
            priority = 0
            group = "link_adult_primary"
            scope = "active_core"
            anb_role = "csab_target_for_link_adult_anb"
            reason = "Primary adult Link CMB/CSAB/FACEB/CMAB target; highest-value cross-form check for ANB channel semantics and N64 PlayerAnimation aliases."
        }
    }
    if ($Archive -eq "zelda_link_child_ultra.zar") {
        return [ordered]@{
            priority = 0
            group = "link_child_anb_payloads"
            scope = "active_core"
            anb_role = "direct_anb_payload_source"
            reason = "Child Link ANB raw payload source; no CMB target, but every payload is required for ANB semantic reconstruction."
        }
    }
    if ($Archive -eq "zelda_link_boy_ultra.zar") {
        return [ordered]@{
            priority = 0
            group = "link_adult_anb_payloads"
            scope = "active_core"
            anb_role = "duplicate_anb_payload_source"
            reason = "Adult Link ANB duplicate payload source; byte-identical payloads make it the strongest cross-form consensus check."
        }
    }
    if ($Archive -eq "zelda_link_opening.zar") {
        return [ordered]@{
            priority = 1
            group = "link_opening_sidecar"
            scope = "active_supporting"
            anb_role = "no_anb_link_face_material_control"
            reason = "Link-family opening target with CSAB/FACEB/CMAB but no ANB; useful for face/material sidecar semantics and a Link negative control."
        }
    }
    if ($Archive -eq "zelda_horse.zar" -or $Archive -eq "zelda_horse_normal.zar" -or $Archive -eq "zelda_horse_ganon.zar" -or $Archive -eq "zelda_horse_zelda.zar") {
        return [ordered]@{
            priority = 4
            group = "horse_quadruped_controls"
            scope = "active_supporting"
            anb_role = "subtract_from_anb_semantics"
            reason = "Quadruped and riding-related CSAB/CMB controls; useful for root/translation/pose outliers, but not ANB-bearing evidence."
        }
    }
    if ($nameLower -match "zelda|childzelda|sheik|saria|nabooru|ruto|darunia|impa|mido|kokiri|malon|talon|ingo") {
        return [ordered]@{
            priority = 2
            group = "major_humanoid_npc_controls"
            scope = "active_supporting"
            anb_role = "subtract_from_anb_semantics"
            reason = "Humanoid NPC/sage CSAB/CMB/CMAB comparison target; valuable for general character format semantics, but excluded from ANB-channel inference."
        }
    }
    if ($nameLower -match "ganon|gnd|phantom|stalfos|ironknack|twinrova|goma|dodongo|valbasi") {
        return [ordered]@{
            priority = 3
            group = "boss_enemy_outlier_controls"
            scope = "active_supporting"
            anb_role = "subtract_from_anb_semantics"
            reason = "Enemy/boss or high-bone-count outlier; useful for stress-testing CSAB/CMB skeleton semantics, not for ANB consensus."
        }
    }
    if ($AnimationCount -ge 20 -or $BoneCount -ge 25 -or $CmabCount -gt 0) {
        return [ordered]@{
            priority = 6
            group = "general_character_pool"
            scope = "defer_general_pool"
            anb_role = "not_anb_evidence"
            reason = "Potentially useful skinned character pool item, deferred until Link and high-signal humanoid/outlier controls stop moving the frontier."
        }
    }
    return [ordered]@{
        priority = 9
        group = "low_signal_skinned_pool"
        scope = "defer_general_pool"
        anb_role = "not_anb_evidence"
        reason = "Skinned CSAB/CMB target with low current strategic value; kept in the pool but subtracted from active comparison work."
    }
}

function Anb-Consensus-Policy([string]$AnbRole) {
    if ($AnbRole -in @("direct_anb_payload_source", "duplicate_anb_payload_source", "csab_target_for_link_child_anb", "csab_target_for_link_adult_anb")) {
        return "eligible_for_link_anb_reconstruction"
    }
    if ($AnbRole -eq "no_anb_link_face_material_control") {
        return "negative_control_for_link_anb"
    }
    return "excluded_from_anb_consensus"
}

function Comparison-Decision([string]$Scope, [string]$AnbRole) {
    if ($Scope -eq "active_core") {
        return "add_primary"
    }
    if ($Scope -eq "active_supporting") {
        if ($AnbRole -eq "no_anb_link_face_material_control") {
            return "add_link_negative_control"
        }
        return "add_supporting_subtract_from_anb"
    }
    if ($Scope -eq "defer_bind_pose_only_pool") {
        return "subtract_defer_bind_pose_only"
    }
    if ($Scope -eq "subtract_material_only_pool") {
        return "subtract_material_only"
    }
    return "defer_general_pool"
}

function Semantic-Axes([string[]]$FormatRoles, [string]$Scope, [string]$Group) {
    $axes = New-Object System.Collections.Generic.List[string]
    if ($FormatRoles -contains "anb") { $axes.Add("anb_raw_ir") }
    if ($FormatRoles -contains "csab") { $axes.Add("csab_tracks") }
    if ($FormatRoles -contains "cmb") { $axes.Add("cmb_skeleton_skinning") }
    if ($FormatRoles -contains "bind_pose") { $axes.Add("bind_pose_only_skinning") }
    if ($FormatRoles -contains "faceb") { $axes.Add("faceb_sidecar") }
    if ($FormatRoles -contains "cmab") { $axes.Add("cmab_material") }
    if ($Group -match "^link_") { $axes.Add("link_cross_form_priority") }
    if ($Scope -in @("active_supporting", "defer_bind_pose_only_pool", "subtract_material_only_pool")) {
        $axes.Add("negative_control_or_subtraction")
    }
    return (($axes.ToArray() | Select-Object -Unique) -join ";")
}

Require-Path $SkinnedBindingManifest "Skinned animation binding manifest"
Require-Path $AnimationLikeAudit "Animation-like payload audit"
Require-Path $CmabAudit "CMAB audit"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $CsvOutput) | Out-Null

$binding = Get-Content -LiteralPath $SkinnedBindingManifest -Raw | ConvertFrom-Json
$animationLike = Get-Content -LiteralPath $AnimationLikeAudit -Raw | ConvertFrom-Json
$cmab = Get-Content -LiteralPath $CmabAudit -Raw | ConvertFrom-Json

$rows = New-Object System.Collections.Generic.List[object]
foreach ($target in @($binding.targets)) {
    $archive = [string]$target.archive_path
    $animations = @($target.animations)
    $frameSlots = @($animations | ForEach-Object { [int]$_.frame_slot_count })
    $channelCounts = @($animations | ForEach-Object { [int]$_.counts.channel_count })
    $keyedCounts = @($animations | ForEach-Object { [int]$_.counts.keyed_channel_count })
    $constCounts = @($animations | ForEach-Object { [int]$_.counts.const_channel_count })
    $keyframeCounts = @($animations | ForEach-Object { [int]$_.counts.keyframe_count })
    $bindCounts = $target.bind_pose.counts
    $anbCount = Archive-PayloadCount $animationLike "anb" $archive
    $facebCount = Archive-PayloadCount $animationLike "faceb" $archive
    $cmabCount = Archive-CmabCount $cmab $archive
    $strategy = Target-Strategy $archive ([string]$target.target_cmb_name) ([string]$target.model_name) ([int]$target.animation_count) ([int]$target.bone_count) $anbCount $facebCount $cmabCount
    $formatRoles = @("cmb", "csab")
    if ($anbCount -gt 0) { $formatRoles += "anb" }
    if ($facebCount -gt 0) { $formatRoles += "faceb" }
    if ($cmabCount -gt 0) { $formatRoles += "cmab" }
    $comparisonDecision = Comparison-Decision ([string]$strategy.scope) ([string]$strategy.anb_role)
    $anbConsensusPolicy = Anb-Consensus-Policy ([string]$strategy.anb_role)
    $rows.Add([pscustomobject]@{
        priority = [int]$strategy.priority
        comparison_group = [string]$strategy.group
        comparison_scope = [string]$strategy.scope
        comparison_decision = $comparisonDecision
        anb_semantic_role = [string]$strategy.anb_role
        anb_consensus_policy = $anbConsensusPolicy
        archive_path = $archive
        target_cmb_name = [string]$target.target_cmb_name
        model_name = [string]$target.model_name
        bone_count = [int]$target.bone_count
        csab_animation_count = [int]$target.animation_count
        frame_slot_max = (Numeric-Summary $frameSlots).max
        channel_count_max = (Numeric-Summary $channelCounts).max
        keyed_channel_total = (Numeric-Summary $keyedCounts).total
        const_channel_total = (Numeric-Summary $constCounts).total
        keyframe_total = (Numeric-Summary $keyframeCounts).total
        cmb_mesh_count = [int]$bindCounts.mesh_count
        cmb_skinned_primitive_count = [int]$bindCounts.skinned_primitive_count
        cmb_mode_1_primitive_count = [int]$bindCounts.mode_1_primitive_count
        cmb_mode_2_primitive_count = [int]$bindCounts.mode_2_primitive_count
        anb_payload_count = $anbCount
        faceb_payload_count = $facebCount
        cmab_payload_count = $cmabCount
        format_roles = ($formatRoles -join ";")
        semantic_axes = Semantic-Axes $formatRoles ([string]$strategy.scope) ([string]$strategy.group)
        support_status_counts_json = Counter-Json $target.support_status_counts
        target_resolution_status_counts_json = Counter-Json $target.target_resolution_status_counts
        strategic_reason = [string]$strategy.reason
        sample_csab_names = (($animations | Select-Object -First 8 | ForEach-Object { [string]$_.csab_name }) -join "; ")
    })
}

foreach ($archive in @("zelda_link_child_ultra.zar", "zelda_link_boy_ultra.zar")) {
    $anbCount = Archive-PayloadCount $animationLike "anb" $archive
    $facebCount = Archive-PayloadCount $animationLike "faceb" $archive
    $cmabCount = Archive-CmabCount $cmab $archive
    $strategy = Target-Strategy $archive "" "" 0 0 $anbCount $facebCount $cmabCount
    $formatRoles = @("anb")
    $comparisonDecision = Comparison-Decision ([string]$strategy.scope) ([string]$strategy.anb_role)
    $anbConsensusPolicy = Anb-Consensus-Policy ([string]$strategy.anb_role)
    $rows.Add([pscustomobject]@{
        priority = [int]$strategy.priority
        comparison_group = [string]$strategy.group
        comparison_scope = [string]$strategy.scope
        comparison_decision = $comparisonDecision
        anb_semantic_role = [string]$strategy.anb_role
        anb_consensus_policy = $anbConsensusPolicy
        archive_path = $archive
        target_cmb_name = ""
        model_name = ""
        bone_count = 0
        csab_animation_count = 0
        frame_slot_max = 0
        channel_count_max = 0
        keyed_channel_total = 0
        const_channel_total = 0
        keyframe_total = 0
        cmb_mesh_count = 0
        cmb_skinned_primitive_count = 0
        cmb_mode_1_primitive_count = 0
        cmb_mode_2_primitive_count = 0
        anb_payload_count = $anbCount
        faceb_payload_count = $facebCount
        cmab_payload_count = $cmabCount
        format_roles = ($formatRoles -join ";")
        semantic_axes = Semantic-Axes $formatRoles ([string]$strategy.scope) ([string]$strategy.group)
        support_status_counts_json = "{}"
        target_resolution_status_counts_json = "{}"
        strategic_reason = [string]$strategy.reason
        sample_csab_names = ""
    })
}

foreach ($target in @($binding.unused_bind_pose_targets)) {
    $archive = [string]$target.archive_path
    $bindCounts = $target.counts
    $anbCount = Archive-PayloadCount $animationLike "anb" $archive
    $facebCount = Archive-PayloadCount $animationLike "faceb" $archive
    $cmabCount = Archive-CmabCount $cmab $archive
    $formatRoles = @("cmb", "bind_pose")
    if ($anbCount -gt 0) { $formatRoles += "anb" }
    if ($facebCount -gt 0) { $formatRoles += "faceb" }
    if ($cmabCount -gt 0) { $formatRoles += "cmab" }
    $group = "bind_pose_only_skinned_pool"
    $scope = "defer_bind_pose_only_pool"
    $anbRole = "not_anb_evidence"
    $rows.Add([pscustomobject]@{
        priority = 7
        comparison_group = $group
        comparison_scope = $scope
        comparison_decision = Comparison-Decision $scope $anbRole
        anb_semantic_role = $anbRole
        anb_consensus_policy = Anb-Consensus-Policy $anbRole
        archive_path = $archive
        target_cmb_name = [string]$target.target_cmb_name
        model_name = [string]$target.model_name
        bone_count = [int]$target.bone_count
        csab_animation_count = 0
        frame_slot_max = 0
        channel_count_max = 0
        keyed_channel_total = 0
        const_channel_total = 0
        keyframe_total = 0
        cmb_mesh_count = [int]$bindCounts.mesh_count
        cmb_skinned_primitive_count = [int]$bindCounts.skinned_primitive_count
        cmb_mode_1_primitive_count = [int]$bindCounts.mode_1_primitive_count
        cmb_mode_2_primitive_count = [int]$bindCounts.mode_2_primitive_count
        anb_payload_count = $anbCount
        faceb_payload_count = $facebCount
        cmab_payload_count = $cmabCount
        format_roles = ($formatRoles -join ";")
        semantic_axes = Semantic-Axes $formatRoles $scope $group
        support_status_counts_json = "{}"
        target_resolution_status_counts_json = "{}"
        strategic_reason = "Skinned CMB bind-pose target without a resolved CSAB track target; useful for CMB/skinning coverage, excluded from ANB and CSAB animation consensus."
        sample_csab_names = ""
    })
}

$knownCmbArchives = @{}
foreach ($target in @($binding.targets)) {
    $knownCmbArchives[[string]$target.archive_path] = $true
}
foreach ($target in @($binding.unused_bind_pose_targets)) {
    $knownCmbArchives[[string]$target.archive_path] = $true
}
foreach ($property in @($cmab.archive_cmab_counts.PSObject.Properties)) {
    $archive = [string]$property.Name
    if ($knownCmbArchives.ContainsKey($archive)) {
        continue
    }
    $group = "cmab_only_material_pool"
    $scope = "subtract_material_only_pool"
    $anbRole = "not_anb_evidence"
    $formatRoles = @("cmab")
    $rows.Add([pscustomobject]@{
        priority = 8
        comparison_group = $group
        comparison_scope = $scope
        comparison_decision = Comparison-Decision $scope $anbRole
        anb_semantic_role = $anbRole
        anb_consensus_policy = Anb-Consensus-Policy $anbRole
        archive_path = $archive
        target_cmb_name = ""
        model_name = ""
        bone_count = 0
        csab_animation_count = 0
        frame_slot_max = 0
        channel_count_max = 0
        keyed_channel_total = 0
        const_channel_total = 0
        keyframe_total = 0
        cmb_mesh_count = 0
        cmb_skinned_primitive_count = 0
        cmb_mode_1_primitive_count = 0
        cmb_mode_2_primitive_count = 0
        anb_payload_count = 0
        faceb_payload_count = 0
        cmab_payload_count = [int]$property.Value
        format_roles = ($formatRoles -join ";")
        semantic_axes = Semantic-Axes $formatRoles $scope $group
        support_status_counts_json = "{}"
        target_resolution_status_counts_json = "{}"
        strategic_reason = "CMAB-only actor archive with no skinned CMB/CSAB comparison target; kept as a material-format subtraction pool, not character ANB evidence."
        sample_csab_names = ""
    })
}

$rowArray = @($rows.ToArray() | Sort-Object priority, comparison_group, archive_path, target_cmb_name)
$scopeCounts = @{}
$groupCounts = @{}
$anbRoleCounts = @{}
$anbConsensusPolicyCounts = @{}
$comparisonDecisionCounts = @{}
foreach ($row in $rowArray) {
    $scope = [string]$row.comparison_scope
    $group = [string]$row.comparison_group
    $anbRole = [string]$row.anb_semantic_role
    $anbConsensusPolicy = [string]$row.anb_consensus_policy
    $comparisonDecision = [string]$row.comparison_decision
    if (-not $scopeCounts.ContainsKey($scope)) { $scopeCounts[$scope] = 0 }
    if (-not $groupCounts.ContainsKey($group)) { $groupCounts[$group] = 0 }
    if (-not $anbRoleCounts.ContainsKey($anbRole)) { $anbRoleCounts[$anbRole] = 0 }
    if (-not $anbConsensusPolicyCounts.ContainsKey($anbConsensusPolicy)) { $anbConsensusPolicyCounts[$anbConsensusPolicy] = 0 }
    if (-not $comparisonDecisionCounts.ContainsKey($comparisonDecision)) { $comparisonDecisionCounts[$comparisonDecision] = 0 }
    $scopeCounts[$scope] = [int]$scopeCounts[$scope] + 1
    $groupCounts[$group] = [int]$groupCounts[$group] + 1
    $anbRoleCounts[$anbRole] = [int]$anbRoleCounts[$anbRole] + 1
    $anbConsensusPolicyCounts[$anbConsensusPolicy] = [int]$anbConsensusPolicyCounts[$anbConsensusPolicy] + 1
    $comparisonDecisionCounts[$comparisonDecision] = [int]$comparisonDecisionCounts[$comparisonDecision] + 1
}

$activeRows = @($rowArray | Where-Object { $_.comparison_scope -in @("active_core", "active_supporting") })
$deferredRows = @($rowArray | Where-Object { $_.comparison_scope -notin @("active_core", "active_supporting") })
$cmabOnlyRows = @($rowArray | Where-Object { $_.comparison_group -eq "cmab_only_material_pool" })
$audit = [ordered]@{
    format = "oot3d_character_semantic_comparison_strategy_v1"
    skinned_binding_manifest = (Resolve-Path $SkinnedBindingManifest).Path
    animation_like_audit = (Resolve-Path $AnimationLikeAudit).Path
    cmab_audit = (Resolve-Path $CmabAudit).Path
    policy = [ordered]@{
        objective = "Complete semantic reconstruction of Link-prioritized character formats while preserving a general character comparison frontier."
        add_rule = "Add targets that provide direct ANB evidence, Link cross-form sidecars, humanoid/major-character CSAB-CMB controls, horse/root-motion controls, skeleton outliers, and bind-pose-only skinning boundary cases."
        subtract_rule = "Subtract non-Link controls, bind-pose-only targets, static/environmental material animation, and low-signal skinned targets from active ANB inference; keep them as deferred or negative controls instead of consensus evidence."
    }
    row_count = $rowArray.Count
    active_row_count = $activeRows.Count
    deferred_row_count = $deferredRows.Count
    scope_counts = $scopeCounts
    group_counts = $groupCounts
    anb_semantic_role_counts = $anbRoleCounts
    anb_consensus_policy_counts = $anbConsensusPolicyCounts
    comparison_decision_counts = $comparisonDecisionCounts
    source_payload_totals = [ordered]@{
        anb_payload_count = [int]$animationLike.payload_type_counts.anb
        faceb_payload_count = [int]$animationLike.payload_type_counts.faceb
        cmab_payload_count = [int]$cmab.cmab_count
        skinned_target_count = [int]$binding.target_count
        unused_bind_pose_target_count = [int]$binding.unused_bind_pose_target_count
        cmab_only_material_archive_count = $cmabOnlyRows.Count
        cmab_only_material_payload_count = [int](($cmabOnlyRows | Measure-Object cmab_payload_count -Sum).Sum)
        skinned_animation_count = [int]$binding.animation_count
    }
    strategic_summary = [ordered]@{
        link_priority_rows = @($rowArray | Where-Object { $_.comparison_group -match '^link_' }).Count
        direct_or_duplicate_anb_rows = @($rowArray | Where-Object { $_.anb_payload_count -gt 0 }).Count
        faceb_rows = @($rowArray | Where-Object { $_.faceb_payload_count -gt 0 }).Count
        cmab_rows = @($rowArray | Where-Object { $_.cmab_payload_count -gt 0 }).Count
        active_non_link_control_rows = @($activeRows | Where-Object { $_.comparison_group -notmatch '^link_' }).Count
        active_rows_subtracted_from_anb_semantics = @($activeRows | Where-Object { $_.anb_semantic_role -eq "subtract_from_anb_semantics" }).Count
        anb_reconstruction_eligible_rows = @($rowArray | Where-Object { $_.anb_consensus_policy -eq "eligible_for_link_anb_reconstruction" }).Count
        anb_consensus_excluded_rows = @($rowArray | Where-Object { $_.anb_consensus_policy -eq "excluded_from_anb_consensus" }).Count
        link_negative_control_rows = @($rowArray | Where-Object { $_.anb_consensus_policy -eq "negative_control_for_link_anb" }).Count
        deferred_bind_pose_only_rows = @($rowArray | Where-Object { $_.comparison_scope -eq "defer_bind_pose_only_pool" }).Count
        subtracted_material_only_rows = $cmabOnlyRows.Count
    }
    active_comparison_rows = $activeRows
    deferred_comparison_rows = $deferredRows
}

$audit | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Output -Encoding UTF8
$rowArray | Export-Csv -LiteralPath $CsvOutput -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($audit.format -eq "oot3d_character_semantic_comparison_strategy_v1") "unexpected audit format"
    Assert-Condition ([int]$audit.source_payload_totals.skinned_target_count -eq 156) "expected 156 skinned comparison targets from binding manifest"
    Assert-Condition ([int]$audit.source_payload_totals.skinned_animation_count -eq 2271) "expected 2271 skinned CSAB animations from binding manifest"
    Assert-Condition ([int]$audit.source_payload_totals.anb_payload_count -eq 1122) "expected 1122 ANB payloads"
    Assert-Condition ([int]$audit.source_payload_totals.faceb_payload_count -eq 1185) "expected 1185 FACEB payloads"
    Assert-Condition ([int]$audit.source_payload_totals.cmab_payload_count -eq 474) "expected 474 CMAB payloads"
    Assert-Condition ([int]$audit.source_payload_totals.unused_bind_pose_target_count -eq 46) "expected 46 bind-pose-only skinned targets"
    Assert-Condition ([int]$audit.source_payload_totals.cmab_only_material_archive_count -eq 56) "expected 56 CMAB-only material archives outside skinned/bind-pose targets"
    Assert-Condition ([int]$audit.source_payload_totals.cmab_only_material_payload_count -eq 189) "expected 189 CMAB payloads in material-only subtraction archives"
    Assert-Condition ([int]$audit.row_count -eq 260) "expected 156 skinned rows plus 2 Link ANB-only rows, 46 bind-pose-only rows, and 56 CMAB-only subtraction rows"
    Assert-Condition ([int]$audit.active_row_count -eq 49) "expected 49 active strategic comparison rows"
    Assert-Condition ([int]$audit.deferred_row_count -eq 211) "expected 211 deferred/subtracted comparison pool rows"
    Assert-Condition ([int]$audit.scope_counts.active_core -eq 4) "expected 4 active core Link rows"
    Assert-Condition ([int]$audit.scope_counts.active_supporting -eq 45) "expected 45 active supporting rows"
    Assert-Condition ([int]$audit.scope_counts.defer_general_pool -eq 109) "expected 109 deferred rows"
    Assert-Condition ([int]$audit.scope_counts.defer_bind_pose_only_pool -eq 46) "expected 46 bind-pose-only deferred rows"
    Assert-Condition ([int]$audit.scope_counts.subtract_material_only_pool -eq 56) "expected 56 material-only subtraction rows"
    Assert-Condition ([int]$audit.strategic_summary.direct_or_duplicate_anb_rows -eq 2) "expected only the two Link ultra rows to carry ANB payloads"
    Assert-Condition ([int]$audit.strategic_summary.faceb_rows -eq 3) "expected three Link-family FACEB rows"
    Assert-Condition ([int]$audit.strategic_summary.cmab_rows -eq 193) "expected 193 comparison rows with archive-level CMAB payload association"
    Assert-Condition ([int]$audit.strategic_summary.active_non_link_control_rows -eq 44) "expected 44 active non-Link control rows"
    Assert-Condition ([int]$audit.strategic_summary.active_rows_subtracted_from_anb_semantics -eq 44) "expected 44 active controls subtracted from ANB semantic consensus"
    Assert-Condition ([int]$audit.strategic_summary.anb_reconstruction_eligible_rows -eq 4) "expected 4 rows eligible for Link ANB reconstruction"
    Assert-Condition ([int]$audit.strategic_summary.anb_consensus_excluded_rows -eq 255) "expected 255 rows excluded from ANB consensus"
    Assert-Condition ([int]$audit.strategic_summary.link_negative_control_rows -eq 1) "expected 1 Link negative control row"
    Assert-Condition ([int]$audit.strategic_summary.deferred_bind_pose_only_rows -eq 46) "expected 46 deferred bind-pose-only rows"
    Assert-Condition ([int]$audit.strategic_summary.subtracted_material_only_rows -eq 56) "expected 56 subtracted material-only rows"
    Assert-Condition ([int]$audit.anb_consensus_policy_counts.eligible_for_link_anb_reconstruction -eq 4) "expected 4 ANB-reconstruction eligible rows"
    Assert-Condition ([int]$audit.anb_consensus_policy_counts.negative_control_for_link_anb -eq 1) "expected 1 ANB negative control row"
    Assert-Condition ([int]$audit.anb_consensus_policy_counts.excluded_from_anb_consensus -eq 255) "expected 255 ANB-excluded rows"
    Assert-Condition ([int]$audit.comparison_decision_counts.add_primary -eq 4) "expected 4 primary add rows"
    Assert-Condition ([int]$audit.comparison_decision_counts.add_link_negative_control -eq 1) "expected 1 Link negative-control add row"
    Assert-Condition ([int]$audit.comparison_decision_counts.add_supporting_subtract_from_anb -eq 44) "expected 44 active supporting rows subtracted from ANB"
    Assert-Condition ([int]$audit.comparison_decision_counts.defer_general_pool -eq 109) "expected 109 deferred general-pool rows"
    Assert-Condition ([int]$audit.comparison_decision_counts.subtract_defer_bind_pose_only -eq 46) "expected 46 bind-pose subtraction rows"
    Assert-Condition ([int]$audit.comparison_decision_counts.subtract_material_only -eq 56) "expected 56 material-only subtraction rows"
    Assert-Condition ([int]$audit.group_counts.link_child_primary -eq 1) "expected one child Link primary row"
    Assert-Condition ([int]$audit.group_counts.link_adult_primary -eq 1) "expected one adult Link primary row"
    Assert-Condition ([int]$audit.group_counts.link_child_anb_payloads -eq 1) "expected one child Link ANB row"
    Assert-Condition ([int]$audit.group_counts.link_adult_anb_payloads -eq 1) "expected one adult Link ANB row"
    Assert-Condition ([int]$audit.group_counts.link_opening_sidecar -eq 1) "expected one Link opening sidecar row"
    Assert-Condition ([int]$audit.group_counts.major_humanoid_npc_controls -eq 21) "expected 21 active major humanoid/NPC rows"
    Assert-Condition ([int]$audit.group_counts.boss_enemy_outlier_controls -eq 19) "expected 19 active boss/enemy outlier rows"
    Assert-Condition ([int]$audit.group_counts.horse_quadruped_controls -eq 4) "expected 4 active horse/quadruped rows"
    Assert-Condition ([int]$audit.group_counts.general_character_pool -eq 70) "expected 70 deferred general character pool rows"
    Assert-Condition ([int]$audit.group_counts.low_signal_skinned_pool -eq 39) "expected 39 deferred low-signal skinned rows"
    Assert-Condition ([int]$audit.group_counts.bind_pose_only_skinned_pool -eq 46) "expected 46 bind-pose-only skinned rows"
    Assert-Condition ([int]$audit.group_counts.cmab_only_material_pool -eq 56) "expected 56 CMAB-only material rows"
    $csvRows = @(Import-Csv -LiteralPath $CsvOutput)
    Assert-Condition ($csvRows.Count -eq [int]$audit.row_count) "expected JSON/CSV row counts to match"
}

Write-Host "OOT3D character semantic comparison strategy: $Output"
Write-Host "OOT3D character semantic comparison strategy CSV: $CsvOutput"
Write-Host "rows=$($audit.row_count) active=$($audit.active_row_count) deferred=$($audit.deferred_row_count)"
