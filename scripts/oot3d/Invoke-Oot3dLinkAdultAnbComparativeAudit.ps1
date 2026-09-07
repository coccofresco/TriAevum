param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$PrimaryArchive = "",
    [string]$DuplicateArchive = "",
    [string]$CsabBindingManifest = "",
    [string]$TrackRoot = "",
    [string]$ChildAnbSemanticAudit = "",
    [string]$ChildUnresolvedChannelCsv = "",
    [string]$N64ReferenceSummary = "",
    [double]$MinAbsCorrelation = 0.98,
    [int]$MinConsensusCount = 2,
    [int]$SampleLimit = 20,
    [switch]$DisableFrameMismatchResample,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($PrimaryArchive)) {
    $PrimaryArchive = Join-Path $RomFs "actor\zelda_link_boy_ultra.zar"
}
if ([string]::IsNullOrWhiteSpace($DuplicateArchive)) {
    $DuplicateArchive = Join-Path $RomFs "actor\zelda_link_child_ultra.zar"
}
if ([string]::IsNullOrWhiteSpace($CsabBindingManifest)) {
    $CsabBindingManifest = Join-Path $WorkRoot "skinned_animation_binding\oot3d_skinned_animation_binding_manifest.json"
}
if ([string]::IsNullOrWhiteSpace($TrackRoot)) {
    $TrackRoot = Join-Path $WorkRoot "skinned_animation_batch\tracks"
}
if ([string]::IsNullOrWhiteSpace($ChildAnbSemanticAudit)) {
    $ChildAnbSemanticAudit = Join-Path $WorkRoot "anb_export\link_child_anb_semantic_candidate_audit.json"
}
if ([string]::IsNullOrWhiteSpace($ChildUnresolvedChannelCsv)) {
    $ChildUnresolvedChannelCsv = Join-Path $WorkRoot "anb_export\link_child_anb_unresolved_channel_worklist.csv"
}
if ([string]::IsNullOrWhiteSpace($N64ReferenceSummary)) {
    $N64ReferenceSummary = Join-Path $WorkRoot "character_conversion\link_child_n64_animation_reference_summary.json"
}

$anbOutputRoot = Join-Path $WorkRoot "anb_export"
$characterOutputRoot = Join-Path $WorkRoot "character_conversion"
$adultAnbExport = Join-Path $anbOutputRoot "link_adult_anb_payload_batch.json"
$adultManifest = Join-Path $characterOutputRoot "link_adult_anb_comparative_manifest.json"
$adultSemanticAudit = Join-Path $anbOutputRoot "link_adult_anb_semantic_candidate_audit.json"
$adultUnresolvedCsv = Join-Path $anbOutputRoot "link_adult_anb_unresolved_channel_worklist.csv"
$comparisonJson = Join-Path $anbOutputRoot "link_child_adult_anb_semantic_comparison.json"
$stableComparisonCsv = Join-Path $anbOutputRoot "link_child_adult_anb_stable_channel_comparison.csv"
$closureQueueCsv = Join-Path $anbOutputRoot "link_child_anb_cross_form_closure_queue.csv"
$semanticContractJson = Join-Path $anbOutputRoot "link_child_anb_semantic_channel_contract.json"
$semanticContractCsv = Join-Path $anbOutputRoot "link_child_anb_semantic_channel_contract.csv"
$rawLayoutCsabCorrelationJson = Join-Path $anbOutputRoot "link_child_anb_raw_layout_csab_correlation_divergence.json"
$rawLayoutCsabCorrelationCsv = Join-Path $anbOutputRoot "link_child_anb_raw_layout_csab_correlation_divergence.csv"
$unresolvedClosureLedgerJson = Join-Path $anbOutputRoot "link_child_anb_unresolved_semantic_closure_ledger.json"
$unresolvedClosureLedgerCsv = Join-Path $anbOutputRoot "link_child_anb_unresolved_semantic_closure_ledger.csv"
New-Item -ItemType Directory -Force -Path $anbOutputRoot | Out-Null
New-Item -ItemType Directory -Force -Path $characterOutputRoot | Out-Null

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
        throw "OOT3D Link adult ANB comparative audit verification failed: $Message"
    }
}

function Get-JsonValue([object]$Object, [string]$Name) {
    if ($null -eq $Object) {
        return $null
    }
    if ($Object -is [System.Collections.IDictionary]) {
        if ($Object.Contains($Name)) {
            return $Object[$Name]
        }
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

function Stable-ChannelMap([object]$Audit) {
    $map = @{}
    foreach ($mapping in @($Audit.stable_candidate_mappings)) {
        $map[[int]$mapping.anb_channel] = $mapping
    }
    return $map
}

function Channel-CandidateMap([object]$Audit) {
    $map = @{}
    foreach ($record in @($Audit.channel_candidates)) {
        $channel = [int]$record.anb_channel
        $counts = @{}
        if ($null -ne $record.component_counts) {
            foreach ($property in @($record.component_counts.PSObject.Properties)) {
                $counts[$property.Name] = [int]$property.Value
            }
        }
        $map[$channel] = [pscustomobject]@{
            candidate_count = [int]$record.candidate_count
            component_counts = $counts
            mean_abs_correlation = $record.mean_abs_correlation
            best_candidate = $record.best_candidate
            component_evidence = @($record.component_evidence)
        }
    }
    return $map
}

function Candidate-ComponentCount([hashtable]$CandidateMap, [int]$Channel, [string]$Component) {
    if (-not $CandidateMap.ContainsKey($Channel)) {
        return 0
    }
    $counts = $CandidateMap[$Channel].component_counts
    if ($null -eq $counts -or -not $counts.Contains($Component)) {
        return 0
    }
    return [int]$counts[$Component]
}

function Candidate-ComponentEvidence([hashtable]$CandidateMap, [int]$Channel, [string]$Component) {
    if (-not $CandidateMap.ContainsKey($Channel)) {
        return $null
    }
    foreach ($record in @($CandidateMap[$Channel].component_evidence)) {
        if ($record.component -eq $Component) {
            return $record
        }
    }
    return $null
}

function Zero-ChannelMap([object]$Audit) {
    $map = @{}
    foreach ($record in @($Audit.zero_candidate_channel_classification)) {
        $map[[int]$record.anb_channel] = $record
    }
    return $map
}

function Component-Descriptor([string]$Component) {
    $boneIndex = ""
    $trackSemantic = ""
    $family = "none"
    if ($Component -match "^bone_(\d+):(.+)$") {
        $boneIndex = [int]$Matches[1]
        $trackSemantic = $Matches[2]
        if ($trackSemantic -like "rotation_*") {
            $family = "rotation"
        }
        elseif ($trackSemantic -like "translation_*") {
            $family = "translation"
        }
        else {
            $family = "track_component"
        }
    }
    return [pscustomobject]@{
        bone_index = $boneIndex
        track_semantic = $trackSemantic
        component_family = $family
    }
}

function Build-N64AnbChannelLayoutMap([object]$ReferenceSummary) {
    $map = @{}
    $payloadDecode = $ReferenceSummary.n64_payload_decode
    $frameIr = $payloadDecode.frame_ir
    $limbNames = @($frameIr.limb_names)
    $limbMappingBySlot = @{}
    foreach ($mapping in @($ReferenceSummary.limb_mapping.mapping)) {
        $limbMappingBySlot[[int]$mapping.n64_slot] = $mapping
    }
    for ($channel = 0; $channel -lt 67; $channel++) {
        if ($channel -eq 66) {
            $map[$channel] = [pscustomobject]@{
                status = "n64_trailing_s16"
                n64_slot = ""
                player_limb = "trailing_s16"
                axis = ""
                track_semantic = "trailing_s16"
                component_family = "none"
                oot3d_bone_index = ""
                oot3d_component = ""
                transform_role = "trailing_value"
                confidence = "n64_frame_ir_trailing_s16"
                formula = "preserve raw trailing s16; not a PlayerLimb Vec3s component"
            }
            continue
        }
        $slot = [int][Math]::Floor($channel / 3)
        $axisIndex = $channel % 3
        $axis = @("x", "y", "z")[$axisIndex]
        $limbName = if ($slot -lt $limbNames.Count) { $limbNames[$slot] } else { "PLAYER_LIMB_UNKNOWN" }
        $limbMapping = $limbMappingBySlot[$slot]
        $isRootTranslation = $slot -eq 0
        $componentFamily = if ($isRootTranslation) { "translation" } else { "rotation" }
        $trackSemantic = "${componentFamily}_${axis}"
        $boneIndex = if ($null -ne $limbMapping) { $limbMapping.oot3d_bone_index } else { "" }
        $component = if (-not [string]::IsNullOrWhiteSpace([string]$boneIndex)) {
            "bone_${boneIndex}:$trackSemantic"
        }
        else {
            ""
        }
        $map[$channel] = [pscustomobject]@{
            status = "n64_player_limb_transform"
            n64_slot = $slot
            player_limb = $limbName
            axis = $axis
            track_semantic = $trackSemantic
            component_family = $componentFamily
            oot3d_bone_index = $boneIndex
            oot3d_component = $component
            transform_role = if ($null -ne $limbMapping) { $limbMapping.transform_role } elseif ($isRootTranslation) { "root_translation" } else { "limb_rotation" }
            confidence = if ($null -ne $limbMapping) { $limbMapping.confidence } else { "n64_frame_ir_layout" }
            formula = if ($isRootTranslation) {
                "preserve N64 root translation s16; OOT3D scale/axis conversion is handled by pose/bake validation"
            }
            else {
                "n64_rotation_binary_angle = anb_s16_channel_value; radians = anb_s16_channel_value * 2*pi/65536 before OOT3D retarget/bake validation"
            }
        }
    }
    return $map
}

function New-AnbContractRow(
    [int]$Channel,
    [string]$ResolutionStatus,
    [string]$SemanticClass,
    [string]$EvidenceSource,
    [string]$Component,
    [object]$Mapping,
    [object]$Evidence,
    [object]$Unresolved,
    [object]$ZeroRecord,
    [object]$CrossFormMapping,
    [object]$N64Layout,
    [string]$RequiredNextStep
) {
    $descriptor = Component-Descriptor $Component
    $canonicalComponent = $Component
    if (($null -ne $N64Layout) -and ($N64Layout.status -eq "n64_player_limb_transform")) {
        $canonicalComponent = $N64Layout.oot3d_component
    }
    elseif (($null -ne $N64Layout) -and ($N64Layout.status -eq "n64_trailing_s16")) {
        $canonicalComponent = ""
    }
    $canonicalDescriptor = Component-Descriptor $canonicalComponent
    $hasCsabCorrelation = $ResolutionStatus -in @("resolved_child_stable_transform", "resolved_cross_form_transform")
    $csabCorrelationComponent = if ($hasCsabCorrelation) { $Component } else { "" }
    $csabCorrelationDescriptor = Component-Descriptor $csabCorrelationComponent
    $csabCorrelationToRawLayoutStatus = ""
    $csabCorrelationPlaybackPolicy = ""
    if ($hasCsabCorrelation) {
        if (($null -ne $N64Layout) -and ($N64Layout.status -eq "n64_player_limb_transform")) {
            if ($csabCorrelationComponent -eq $canonicalComponent) {
                $csabCorrelationToRawLayoutStatus = "matches_raw_n64_player_limb_layout"
                $csabCorrelationPlaybackPolicy = "raw_layout_and_csab_correlation_agree"
            }
            else {
                $csabCorrelationToRawLayoutStatus = "diverges_from_raw_n64_player_limb_layout"
                $csabCorrelationPlaybackPolicy = "use_raw_n64_player_limb_layout_for_anb_playback; treat_csab_correlation_as_retarget_evidence_only"
            }
        }
        else {
            $csabCorrelationToRawLayoutStatus = "no_raw_layout_component"
            $csabCorrelationPlaybackPolicy = "csab_correlation_only"
        }
    }
    elseif (($null -ne $N64Layout) -and ($N64Layout.status -eq "n64_player_limb_transform")) {
        $csabCorrelationToRawLayoutStatus = "no_stable_csab_correlation"
        $csabCorrelationPlaybackPolicy = "use_raw_n64_player_limb_layout_for_anb_playback"
    }
    $conversionPolicy = "blocked"
    $formula = ""
    $playbackReadiness = "blocked_requires_more_semantic_evidence"
    if ($ResolutionStatus -in @("resolved_child_stable_transform", "resolved_cross_form_transform")) {
        $conversionPolicy = "linear_regression_to_csab_component"
        $formula = "csab_component_value = mean_slope * anb_s16_channel_value + mean_intercept"
        $playbackReadiness = "semantic_mapping_ready_for_bake_or_runtime_validation"
    }
    elseif ($ResolutionStatus -like "resolved_n64_player_limb_layout*") {
        $conversionPolicy = "n64_player_limb_s16_to_oot3d_layout_component"
        $formula = if ($null -ne $N64Layout) { $N64Layout.formula } else { "n64 PlayerLimb layout transform" }
        $playbackReadiness = "raw_anb_layout_resolved_pending_csab_bake_or_runtime_validation"
    }
    elseif ($ResolutionStatus -eq "resolved_n64_trailing_s16") {
        $conversionPolicy = "preserve_raw_n64_player_animation_trailing_s16"
        $formula = if ($null -ne $N64Layout) { $N64Layout.formula } else { "preserve raw trailing s16" }
        $playbackReadiness = "raw_anb_layout_resolved_trailing_s16"
    }
    elseif ($ResolutionStatus -eq "resolved_static_zero_padding_or_reserved") {
        $conversionPolicy = "ignore_or_emit_zero_slot"
        $playbackReadiness = "resolved_non_transform_static_zero"
    }
    elseif ($ResolutionStatus -eq "resolved_constant_per_record_control_tuple") {
        $conversionPolicy = "preserve_raw_per_record_control_tuple"
        $playbackReadiness = "resolved_non_transform_control_tuple"
    }

    return [pscustomobject]@{
        anb_channel = $Channel
        resolution_status = $ResolutionStatus
        semantic_class = $SemanticClass
        evidence_source = $EvidenceSource
        component = $Component
        bone_index = $descriptor.bone_index
        track_semantic = $descriptor.track_semantic
        component_family = $descriptor.component_family
        canonical_raw_component = $canonicalComponent
        canonical_raw_bone_index = $canonicalDescriptor.bone_index
        canonical_raw_track_semantic = $canonicalDescriptor.track_semantic
        canonical_raw_component_family = $canonicalDescriptor.component_family
        csab_correlation_component = $csabCorrelationComponent
        csab_correlation_bone_index = $csabCorrelationDescriptor.bone_index
        csab_correlation_track_semantic = $csabCorrelationDescriptor.track_semantic
        csab_correlation_component_family = $csabCorrelationDescriptor.component_family
        csab_correlation_to_raw_layout_status = $csabCorrelationToRawLayoutStatus
        csab_correlation_playback_policy = $csabCorrelationPlaybackPolicy
        n64_layout_status = if ($null -ne $N64Layout) { $N64Layout.status } else { "" }
        n64_slot = if ($null -ne $N64Layout) { $N64Layout.n64_slot } else { "" }
        n64_player_limb = if ($null -ne $N64Layout) { $N64Layout.player_limb } else { "" }
        n64_axis = if ($null -ne $N64Layout) { $N64Layout.axis } else { "" }
        n64_transform_role = if ($null -ne $N64Layout) { $N64Layout.transform_role } else { "" }
        n64_oot3d_bone_index = if ($null -ne $N64Layout) { $N64Layout.oot3d_bone_index } else { "" }
        n64_oot3d_component = if ($null -ne $N64Layout) { $N64Layout.oot3d_component } else { "" }
        n64_layout_confidence = if ($null -ne $N64Layout) { $N64Layout.confidence } else { "" }
        conversion_policy = $conversionPolicy
        formula = $formula
        playback_readiness = $playbackReadiness
        child_consensus_count = if ($null -ne $Mapping) { $Mapping.consensus_count } else { "" }
        child_total_candidate_count = if ($null -ne $Mapping) { $Mapping.total_candidate_count } elseif ($null -ne $Unresolved) { $Unresolved.candidate_count } else { "" }
        child_consensus_share = if ($null -ne $Mapping) { $Mapping.consensus_share } else { "" }
        adult_consensus_count = if ($null -ne $CrossFormMapping) { $CrossFormMapping.adult_consensus_count } else { "" }
        child_support_count = if ($null -ne $CrossFormMapping) { $CrossFormMapping.child_support_count } else { "" }
        mean_abs_correlation = if ($null -ne $Evidence) { $Evidence.mean_abs_correlation } elseif ($null -ne $Mapping) { $Mapping.mean_abs_correlation } elseif ($null -ne $Unresolved) { $Unresolved.mean_abs_correlation } else { "" }
        mean_signed_correlation = if ($null -ne $Evidence) { $Evidence.mean_signed_correlation } elseif ($null -ne $Mapping) { $Mapping.mean_signed_correlation } else { "" }
        mean_slope = if ($null -ne $Evidence) { $Evidence.mean_slope } elseif ($null -ne $Mapping) { $Mapping.mean_slope } elseif ($null -ne $Unresolved) { $Unresolved.best_slope } else { "" }
        mean_intercept = if ($null -ne $Evidence) { $Evidence.mean_intercept } elseif ($null -ne $Mapping) { $Mapping.mean_intercept } elseif ($null -ne $Unresolved) { $Unresolved.best_intercept } else { "" }
        max_abs_error = if ($null -ne $Evidence) { $Evidence.max_abs_error } elseif ($null -ne $Mapping) { $Mapping.max_abs_error } elseif ($null -ne $Unresolved) { $Unresolved.best_max_abs_error } else { "" }
        direct_record_count = if ($null -ne $Evidence) { $Evidence.direct_record_count } elseif ($null -ne $CrossFormMapping) { $CrossFormMapping.child_direct_record_count } else { "" }
        resampled_record_count = if ($null -ne $Evidence) { $Evidence.resampled_record_count } else { "" }
        child_work_kind = if ($null -ne $Unresolved) { $Unresolved.work_kind } else { "" }
        child_unstable_candidate_kind = if ($null -ne $Unresolved) { $Unresolved.unstable_candidate_kind } else { "" }
        child_unstable_candidate_semantic_class = if ($null -ne $Unresolved) { $Unresolved.unstable_candidate_semantic_class } else { "" }
        child_top_component = if ($null -ne $Unresolved) { $Unresolved.top_component } else { "" }
        zero_candidate_kind = if ($null -ne $ZeroRecord) { $ZeroRecord.kind } elseif ($null -ne $Unresolved) { $Unresolved.zero_candidate_kind } else { "" }
        zero_candidate_semantic_class = if ($null -ne $ZeroRecord) { $ZeroRecord.semantic_class } elseif ($null -ne $Unresolved) { $Unresolved.zero_candidate_semantic_class } else { "" }
        zero_candidate_resolution_status = if ($null -ne $ZeroRecord) { $ZeroRecord.resolution_status } elseif ($null -ne $Unresolved) { $Unresolved.zero_candidate_resolution_status } else { "" }
        channel_min = if ($null -ne $ZeroRecord) { $ZeroRecord.min } elseif ($null -ne $Unresolved) { $Unresolved.min } else { "" }
        channel_max = if ($null -ne $ZeroRecord) { $ZeroRecord.max } elseif ($null -ne $Unresolved) { $Unresolved.max } else { "" }
        active_record_count = if ($null -ne $ZeroRecord) { $ZeroRecord.active_record_count } elseif ($null -ne $Unresolved) { $Unresolved.active_record_count } else { "" }
        active_constant_record_count = if ($null -ne $ZeroRecord) { $ZeroRecord.active_constant_record_count } elseif ($null -ne $Unresolved) { $Unresolved.active_constant_record_count } else { "" }
        per_record_varying_record_count = if ($null -ne $ZeroRecord) { $ZeroRecord.per_record_varying_record_count } elseif ($null -ne $Unresolved) { $Unresolved.per_record_varying_record_count } else { "" }
        active_constant_values = if ($null -ne $ZeroRecord) { $ZeroRecord.active_constant_values } elseif ($null -ne $Unresolved) { $Unresolved.active_constant_values } else { "" }
        required_next_step = $RequiredNextStep
    }
}

function Cross-FormSupportClass(
    [object]$ChildMapping,
    [object]$AdultMapping,
    [int]$ChildOtherSupport,
    [int]$AdultOtherSupport
) {
    if (($null -ne $ChildMapping) -and ($null -ne $AdultMapping)) {
        if ($ChildMapping.component -eq $AdultMapping.component) {
            return "same_stable_component"
        }
        return "conflicting_stable_component"
    }
    if ($null -ne $ChildMapping) {
        if ($AdultOtherSupport -gt 0) {
            return "child_stable_adult_candidate_support"
        }
        return "child_stable_no_adult_candidate_support"
    }
    if ($null -ne $AdultMapping) {
        if ($ChildOtherSupport -gt 0) {
            return "adult_stable_child_candidate_support"
        }
        return "adult_stable_no_child_candidate_support"
    }
    return "not_stable_in_either_form"
}

function Closure-ReadinessClass(
    [string]$ClosureClass,
    [int]$ChildSupportForAdult,
    [int]$AdultStableConsensusCount
) {
    if ($ClosureClass -eq "adult_stable_child_candidate_support") {
        if ($ChildSupportForAdult -ge 2 -and $AdultStableConsensusCount -ge 4) {
            return "cross_form_promotion_candidate"
        }
        return "needs_child_pose_or_family_validation"
    }
    if ($ClosureClass -eq "adult_stable_no_child_candidate_support") {
        return "adult_only_hold_for_child_evidence"
    }
    return "requires_new_evidence_in_both_forms"
}

function Promotion-ReviewClass([string]$ReadinessClass, [object]$ChildEvidenceForAdult) {
    if ($ReadinessClass -eq "cross_form_promotion_candidate") {
        if ($null -eq $ChildEvidenceForAdult) {
            return "promotion_review_missing_child_evidence"
        }
        $directRecordCount = [int]$ChildEvidenceForAdult.direct_record_count
        $resampledRecordCount = [int]$ChildEvidenceForAdult.resampled_record_count
        $directMaxError = [double]$ChildEvidenceForAdult.direct_max_abs_error
        if ($directRecordCount -ge 2 -and $resampledRecordCount -eq 0 -and $directMaxError -le 0.1) {
            return "high_confidence_direct_transform_candidate"
        }
        return "promotion_review_insufficient_direct_or_resampled"
    }
    if ($ReadinessClass -eq "needs_child_pose_or_family_validation") {
        if ($null -ne $ChildEvidenceForAdult) {
            $directRecordCount = [int]$ChildEvidenceForAdult.direct_record_count
            $resampledRecordCount = [int]$ChildEvidenceForAdult.resampled_record_count
            $directMaxError = [double]$ChildEvidenceForAdult.direct_max_abs_error
            if ($directRecordCount -eq 1 -and $resampledRecordCount -eq 0 -and $directMaxError -le 0.1) {
                return "single_direct_clean_needs_second_child_hit"
            }
        }
        return "needs_more_child_support"
    }
    if ($ReadinessClass -eq "adult_only_hold_for_child_evidence") {
        return "adult_only_hold"
    }
    return "not_ready_for_promotion_review"
}

function Count-ByProperty([object[]]$Rows, [string]$PropertyName) {
    $counts = @{}
    foreach ($group in @($Rows | Group-Object $PropertyName)) {
        $name = if ([string]::IsNullOrWhiteSpace($group.Name)) { "none" } else { $group.Name }
        $counts[$name] = $group.Count
    }
    return $counts
}

function RawLayout-CsabCorrelationDivergenceClass([object]$ContractRow) {
    if ([string]::IsNullOrWhiteSpace([string]$ContractRow.csab_correlation_component)) {
        return "no_stable_csab_correlation"
    }
    if ($ContractRow.csab_correlation_component -eq $ContractRow.canonical_raw_component) {
        return "csab_correlation_matches_raw_layout"
    }
    if (($ContractRow.csab_correlation_component_family -eq "translation") -and ($ContractRow.canonical_raw_component_family -eq "rotation")) {
        return "csab_translation_correlation_for_raw_rotation"
    }
    if (($ContractRow.csab_correlation_component_family -eq "rotation") -and ($ContractRow.canonical_raw_component_family -eq "translation")) {
        return "csab_rotation_correlation_for_raw_translation"
    }
    if ($ContractRow.csab_correlation_component_family -eq $ContractRow.canonical_raw_component_family) {
        return "same_transform_family_different_bone_or_axis"
    }
    return "different_transform_family"
}

function RawLayout-CsabCorrelationRiskClass([object]$ContractRow, [string]$DivergenceClass) {
    if ($DivergenceClass -eq "csab_correlation_matches_raw_layout") {
        if ($ContractRow.n64_transform_role -like "root_*") {
            return "root_transform_agrees_but_runtime_scale_axis_validation_required"
        }
        return "raw_and_csab_correlation_agree"
    }
    if ($DivergenceClass -eq "csab_translation_correlation_for_raw_rotation") {
        return "high_false_root_or_model_translation_risk"
    }
    if ($ContractRow.canonical_raw_component_family -eq "translation") {
        return "root_translation_conversion_risk"
    }
    if ($ContractRow.n64_transform_role -like "root_*") {
        return "root_rotation_retarget_validation_required"
    }
    if ($DivergenceClass -eq "no_stable_csab_correlation") {
        return "raw_layout_only_pending_csab_retarget_validation"
    }
    return "limb_retarget_correlation_divergence"
}

function New-RawLayoutCsabCorrelationRow([object]$ContractRow) {
    $divergenceClass = RawLayout-CsabCorrelationDivergenceClass $ContractRow
    $riskClass = RawLayout-CsabCorrelationRiskClass $ContractRow $divergenceClass
    $priority = switch ($riskClass) {
        "high_false_root_or_model_translation_risk" { 0; break }
        "root_translation_conversion_risk" { 1; break }
        "root_rotation_retarget_validation_required" { 2; break }
        "limb_retarget_correlation_divergence" { 3; break }
        "raw_layout_only_pending_csab_retarget_validation" { 4; break }
        default { 5; break }
    }
    return [pscustomobject]@{
        priority = $priority
        anb_channel = $ContractRow.anb_channel
        resolution_status = $ContractRow.resolution_status
        n64_slot = $ContractRow.n64_slot
        n64_player_limb = $ContractRow.n64_player_limb
        n64_axis = $ContractRow.n64_axis
        n64_transform_role = $ContractRow.n64_transform_role
        canonical_raw_component = $ContractRow.canonical_raw_component
        canonical_raw_component_family = $ContractRow.canonical_raw_component_family
        csab_correlation_component = $ContractRow.csab_correlation_component
        csab_correlation_component_family = $ContractRow.csab_correlation_component_family
        legacy_contract_component = $ContractRow.component
        csab_correlation_to_raw_layout_status = $ContractRow.csab_correlation_to_raw_layout_status
        divergence_class = $divergenceClass
        risk_class = $riskClass
        mean_abs_correlation = $ContractRow.mean_abs_correlation
        mean_slope = $ContractRow.mean_slope
        mean_intercept = $ContractRow.mean_intercept
        max_abs_error = $ContractRow.max_abs_error
        playback_policy = $ContractRow.csab_correlation_playback_policy
        required_next_step = if ($riskClass -eq "high_false_root_or_model_translation_risk") {
            "do not apply this CSAB correlation as ANB/root translation; validate raw N64 PlayerLimb rotation in bake/runtime pose"
        }
        elseif ($ContractRow.n64_transform_role -like "root_*") {
            "validate raw root transform scale, axis, and sign against CSAB/runtime pose before realtime playback"
        }
        else {
            "use raw N64 PlayerLimb component for ANB playback and keep CSAB correlation as retarget evidence"
        }
    }
}

function Unresolved-ClosureActionClass([object]$ContractRow, [object]$ClosureRow) {
    if ($null -ne $ClosureRow) {
        if ($ClosureRow.promotion_review_class -eq "promotion_review_insufficient_direct_or_resampled") {
            return "cross_form_candidate_fix_direct_or_resampled_evidence"
        }
        if ($ClosureRow.promotion_review_class -eq "single_direct_clean_needs_second_child_hit") {
            return "cross_form_single_direct_needs_second_child_hit"
        }
        if ($ClosureRow.promotion_review_class -eq "adult_only_hold") {
            return "adult_only_hold_needs_child_source_evidence"
        }
    }
    if ($ContractRow.resolution_status -eq "unresolved_requires_semantic_source") {
        return "semantic_source_required_control_or_curve"
    }
    if ($ContractRow.child_unstable_candidate_kind -eq "tied_top_component_consensus") {
        return "shared_tied_component_disambiguation"
    }
    if ($ContractRow.child_unstable_candidate_kind -in @("single_record_candidate", "low_evidence_component_pair")) {
        return "shared_low_evidence_add_matched_records"
    }
    if ($ContractRow.child_unstable_candidate_kind -eq "diffuse_single_hit_components") {
        return "shared_diffuse_family_constraints"
    }
    return "manual_unresolved_anb_semantic_review"
}

function Unresolved-ClosurePriority([string]$ClosureActionClass) {
    switch ($ClosureActionClass) {
        "cross_form_candidate_fix_direct_or_resampled_evidence" { return 1 }
        "cross_form_single_direct_needs_second_child_hit" { return 2 }
        "adult_only_hold_needs_child_source_evidence" { return 3 }
        "shared_tied_component_disambiguation" { return 4 }
        "shared_low_evidence_add_matched_records" { return 5 }
        "shared_diffuse_family_constraints" { return 6 }
        "semantic_source_required_control_or_curve" { return 7 }
        default { return 9 }
    }
}

function Unresolved-ClosureGate([string]$ClosureActionClass) {
    switch ($ClosureActionClass) {
        "cross_form_candidate_fix_direct_or_resampled_evidence" {
            return "inspect the child records that support the adult stable component; require direct-only or explicitly accepted resample evidence before promotion"
        }
        "cross_form_single_direct_needs_second_child_hit" {
            return "find a second independent child hit for the adult stable component or reject it as adult-only evidence"
        }
        "adult_only_hold_needs_child_source_evidence" {
            return "find child ANB/CSAB support, runtime capture, or source callsite evidence before using adult-only mapping"
        }
        "shared_tied_component_disambiguation" {
            return "break tied component consensus with more matched records, pose constraints, or channel-family constraints"
        }
        "shared_low_evidence_add_matched_records" {
            return "add independent matched records for the same component before treating the channel as transform evidence"
        }
        "shared_diffuse_family_constraints" {
            return "derive channel-family or pose constraints before selecting one component from diffuse single-hit evidence"
        }
        "semantic_source_required_control_or_curve" {
            return "identify a non-CSAB semantic source, control/event meaning, or derived transform before mapping this channel"
        }
        default {
            return "manual unresolved ANB semantic review"
        }
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $PrimaryArchive "Link adult ultra ANB archive"
Require-Path $DuplicateArchive "Link child ultra duplicate ANB archive"
Require-Path $CsabBindingManifest "OOT3D skinned animation binding manifest"
Require-Path $TrackRoot "CSAB track root"
Require-Path $ChildAnbSemanticAudit "Link child ANB semantic candidate audit"
Require-Path $ChildUnresolvedChannelCsv "Link child unresolved ANB channel worklist"
Require-Path $N64ReferenceSummary "Link child N64 animation reference summary"

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "export-anb-payload-batch",
        $PrimaryArchive,
        "--output",
        $adultAnbExport,
        "--duplicate-archive",
        $DuplicateArchive,
        "--csab-binding-manifest",
        $CsabBindingManifest,
        "--csab-target-archive",
        "zelda_link_boy_new.zar",
        "--csab-target-cmb",
        "boy/model/link_v2.cmb",
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

$manifest = [ordered]@{
    format = "oot3d_character_conversion_manifest_v1"
    profile_id = "link_adult_anb_comparative"
    target = [ordered]@{
        status = "resolved"
        archive_path = "zelda_link_boy_new.zar"
        target_cmb_name = "boy/model/link_v2.cmb"
    }
    source_manifests = [ordered]@{
        skinned_animation_binding = (Resolve-Path $CsabBindingManifest).Path
    }
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $adultManifest -Encoding UTF8

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $arguments = @(
        "audit-anb-semantic-candidates",
        $adultAnbExport,
        $adultManifest,
        "--output",
        $adultSemanticAudit,
        "--unresolved-channel-csv-output",
        $adultUnresolvedCsv,
        "--track-root",
        $TrackRoot,
        "--min-abs-correlation",
        [string]$MinAbsCorrelation,
        "--min-consensus-count",
        [string]$MinConsensusCount,
        "--sample-limit",
        [string]$SampleLimit
    )
    if (-not $DisableFrameMismatchResample) {
        $arguments += "--include-frame-mismatch-resample"
    }
    Invoke-Oot3dTool -Arguments $arguments
}
finally {
    Pop-Location
}

Require-Path $adultAnbExport "Link adult ANB export"
Require-Path $adultSemanticAudit "Link adult ANB semantic candidate audit"
Require-Path $adultUnresolvedCsv "Link adult unresolved ANB channel worklist"
$adultExport = Get-Content -LiteralPath $adultAnbExport -Raw | ConvertFrom-Json
$adultAudit = Get-Content -LiteralPath $adultSemanticAudit -Raw | ConvertFrom-Json
$childAudit = Get-Content -LiteralPath $ChildAnbSemanticAudit -Raw | ConvertFrom-Json
$n64ReferenceSummaryJson = Get-Content -LiteralPath $N64ReferenceSummary -Raw | ConvertFrom-Json
$n64LayoutByChannel = Build-N64AnbChannelLayoutMap $n64ReferenceSummaryJson
$childUnresolvedRows = @(Import-Csv -LiteralPath $ChildUnresolvedChannelCsv)
$adultUnresolvedRows = @(Import-Csv -LiteralPath $adultUnresolvedCsv)
$childUnresolvedByChannel = @{}
foreach ($row in $childUnresolvedRows) {
    $childUnresolvedByChannel[[int]$row.anb_channel] = $row
}
$adultUnresolvedByChannel = @{}
foreach ($row in $adultUnresolvedRows) {
    $adultUnresolvedByChannel[[int]$row.anb_channel] = $row
}
$childStable = Stable-ChannelMap $childAudit
$adultStable = Stable-ChannelMap $adultAudit
$childCandidates = Channel-CandidateMap $childAudit
$adultCandidates = Channel-CandidateMap $adultAudit
$childZeroChannels = Zero-ChannelMap $childAudit

$stableRows = New-Object System.Collections.Generic.List[object]
for ($channel = 0; $channel -lt 67; $channel++) {
    $childMapping = $childStable[$channel]
    $adultMapping = $adultStable[$channel]
    if (($null -eq $childMapping) -and ($null -eq $adultMapping)) {
        continue
    }
    $status = "same_stable"
    if (($null -ne $childMapping) -and ($null -ne $adultMapping)) {
        if ($childMapping.component -ne $adultMapping.component) {
            $status = "changed_stable"
        }
    }
    elseif ($null -ne $adultMapping) {
        $status = "adult_only_stable"
    }
    else {
        $status = "child_only_stable"
    }
    $childSupportForAdult = 0
    if ($null -ne $adultMapping) {
        $childSupportForAdult = Candidate-ComponentCount $childCandidates $channel $adultMapping.component
    }
    $adultSupportForChild = 0
    if ($null -ne $childMapping) {
        $adultSupportForChild = Candidate-ComponentCount $adultCandidates $channel $childMapping.component
    }
    $crossFormSupportClass = Cross-FormSupportClass $childMapping $adultMapping $childSupportForAdult $adultSupportForChild
    $stableRows.Add([pscustomobject]@{
        anb_channel = $channel
        child_component = if ($null -ne $childMapping) { $childMapping.component } else { "" }
        adult_component = if ($null -ne $adultMapping) { $adultMapping.component } else { "" }
        child_consensus_count = if ($null -ne $childMapping) { $childMapping.consensus_count } else { 0 }
        adult_consensus_count = if ($null -ne $adultMapping) { $adultMapping.consensus_count } else { 0 }
        child_support_for_adult_component = $childSupportForAdult
        adult_support_for_child_component = $adultSupportForChild
        cross_form_support_class = $crossFormSupportClass
        status = $status
    })
}

$stableDifferenceRows = @($stableRows.ToArray() | Where-Object { $_.status -ne "same_stable" })
$stableStatusCounts = @{}
foreach ($group in @($stableRows.ToArray() | Group-Object status)) {
    $stableStatusCounts[$group.Name] = $group.Count
}
$crossFormSupportCounts = @{}
foreach ($group in @($stableRows.ToArray() | Group-Object cross_form_support_class)) {
    $crossFormSupportCounts[$group.Name] = $group.Count
}

$closureRows = New-Object System.Collections.Generic.List[object]
foreach ($childRow in $childUnresolvedRows) {
    $channel = [int]$childRow.anb_channel
    $adultMapping = $adultStable[$channel]
    $adultWork = $adultUnresolvedByChannel[$channel]
    $childSupportForAdult = 0
    $childEvidenceForAdult = $null
    $closureClass = "shared_child_adult_unresolved"
    $priority = 3
    if ($null -ne $adultMapping) {
        $childSupportForAdult = Candidate-ComponentCount $childCandidates $channel $adultMapping.component
        $childEvidenceForAdult = Candidate-ComponentEvidence $childCandidates $channel $adultMapping.component
        if ($childSupportForAdult -gt 0) {
            $closureClass = "adult_stable_child_candidate_support"
            $priority = 1
        }
        else {
            $closureClass = "adult_stable_no_child_candidate_support"
            $priority = 2
        }
    }
    elseif ($null -eq $adultWork) {
        $closureClass = "child_unresolved_adult_not_in_worklist"
        $priority = 4
    }
    $adultStableConsensusCount = if ($null -ne $adultMapping) { [int]$adultMapping.consensus_count } else { 0 }
    $readinessClass = Closure-ReadinessClass $closureClass $childSupportForAdult $adultStableConsensusCount
    $promotionReviewClass = Promotion-ReviewClass $readinessClass $childEvidenceForAdult
    $closureRows.Add([pscustomobject]@{
        priority = $priority
        anb_channel = $channel
        closure_class = $closureClass
        readiness_class = $readinessClass
        promotion_review_class = $promotionReviewClass
        child_work_kind = $childRow.work_kind
        child_unstable_candidate_kind = $childRow.unstable_candidate_kind
        child_unstable_candidate_semantic_class = $childRow.unstable_candidate_semantic_class
        child_top_component = $childRow.top_component
        child_top_component_count = $childRow.top_component_count
        child_candidate_count = $childRow.candidate_count
        adult_stable_component = if ($null -ne $adultMapping) { $adultMapping.component } else { "" }
        adult_stable_consensus_count = $adultStableConsensusCount
        child_support_for_adult_component = $childSupportForAdult
        child_adult_component_mean_abs_correlation = if ($null -ne $childEvidenceForAdult) { $childEvidenceForAdult.mean_abs_correlation } else { "" }
        child_adult_component_max_abs_error = if ($null -ne $childEvidenceForAdult) { $childEvidenceForAdult.max_abs_error } else { "" }
        child_adult_component_direct_record_count = if ($null -ne $childEvidenceForAdult) { $childEvidenceForAdult.direct_record_count } else { "" }
        child_adult_component_resampled_record_count = if ($null -ne $childEvidenceForAdult) { $childEvidenceForAdult.resampled_record_count } else { "" }
        child_adult_component_direct_max_abs_error = if ($null -ne $childEvidenceForAdult) { $childEvidenceForAdult.direct_max_abs_error } else { "" }
        child_adult_component_resampled_max_abs_error = if ($null -ne $childEvidenceForAdult) { $childEvidenceForAdult.resampled_max_abs_error } else { "" }
        child_adult_component_sample_records = if ($null -ne $childEvidenceForAdult) {
            ConvertTo-Json @($childEvidenceForAdult.sample_records) -Compress -Depth 8
        }
        else {
            ""
        }
        adult_work_kind = if ($null -ne $adultWork) { $adultWork.work_kind } else { "" }
        adult_unstable_candidate_kind = if ($null -ne $adultWork) { $adultWork.unstable_candidate_kind } else { "" }
        adult_top_component = if ($null -ne $adultWork) { $adultWork.top_component } else { "" }
        zero_candidate_kind = $childRow.zero_candidate_kind
        zero_candidate_semantic_class = $childRow.zero_candidate_semantic_class
        zero_candidate_resolution_status = $childRow.zero_candidate_resolution_status
        required_evidence = if ($readinessClass -eq "cross_form_promotion_candidate") {
            "validate final child pose/channel-family constraint; cross-form evidence is strong enough for promotion review"
        }
        elseif ($null -ne $adultMapping -and $childSupportForAdult -gt 0) {
            "validate adult stable component against child pose/channel-family constraints before promotion"
        }
        elseif ($null -ne $adultMapping) {
            "treat as adult-only evidence until child source/callsite evidence explains the missing child candidate"
        }
        else {
            $childRow.required_evidence
        }
    })
}
$closureRowsSorted = @($closureRows.ToArray() | Sort-Object priority, {[int]$_.anb_channel})
$closureByChannel = @{}
foreach ($row in $closureRowsSorted) {
    $closureByChannel[[int]$row.anb_channel] = $row
}
$closureClassCounts = @{}
foreach ($group in @($closureRowsSorted | Group-Object closure_class)) {
    $closureClassCounts[$group.Name] = $group.Count
}
$closureReadinessCounts = @{}
foreach ($group in @($closureRowsSorted | Group-Object readiness_class)) {
    $closureReadinessCounts[$group.Name] = $group.Count
}
$promotionReviewCounts = @{}
foreach ($group in @($closureRowsSorted | Group-Object promotion_review_class)) {
    $promotionReviewCounts[$group.Name] = $group.Count
}
$promotedCrossFormMappings = New-Object System.Collections.Generic.List[object]
foreach ($row in @($closureRowsSorted | Where-Object { $_.promotion_review_class -eq "high_confidence_direct_transform_candidate" })) {
    $evidenceSampleRecords = @()
    if (-not [string]::IsNullOrWhiteSpace($row.child_adult_component_sample_records)) {
        $evidenceSampleRecords = @(ConvertFrom-Json -InputObject $row.child_adult_component_sample_records)
    }
    $promotedCrossFormMappings.Add([pscustomobject]@{
        anb_channel = [int]$row.anb_channel
        component = $row.adult_stable_component
        promotion_source = "child_adult_cross_form_high_confidence_direct"
        child_support_count = [int]$row.child_support_for_adult_component
        adult_consensus_count = [int]$row.adult_stable_consensus_count
        child_mean_abs_correlation = $row.child_adult_component_mean_abs_correlation
        child_max_abs_error = $row.child_adult_component_max_abs_error
        child_direct_record_count = [int]$row.child_adult_component_direct_record_count
        child_direct_max_abs_error = $row.child_adult_component_direct_max_abs_error
        evidence_sample_records = $evidenceSampleRecords
    })
}
$promotedCrossFormMappingArray = @($promotedCrossFormMappings.ToArray())
$effectiveResolvedChannelCountAfterCrossForm = [int]$childAudit.stable_candidate_channel_count `
    + [int]$childAudit.resolved_zero_candidate_channel_count `
    + $promotedCrossFormMappingArray.Count
$effectiveUnresolvedChannelCountAfterCrossForm = 67 - $effectiveResolvedChannelCountAfterCrossForm
$promotedCrossFormByChannel = @{}
foreach ($mapping in $promotedCrossFormMappingArray) {
    $promotedCrossFormByChannel[[int]$mapping.anb_channel] = $mapping
}

$contractRows = New-Object System.Collections.Generic.List[object]
for ($channel = 0; $channel -lt 67; $channel++) {
    $childMapping = $childStable[$channel]
    $crossFormMapping = $promotedCrossFormByChannel[$channel]
    $zeroRecord = $childZeroChannels[$channel]
    $unresolvedRow = $childUnresolvedByChannel[$channel]
    $n64Layout = $n64LayoutByChannel[$channel]
    if (($null -ne $n64Layout) -and ($n64Layout.status -eq "n64_trailing_s16")) {
        $contractRows.Add((New-AnbContractRow `
            -Channel $channel `
            -ResolutionStatus "resolved_n64_trailing_s16" `
            -SemanticClass "n64_player_animation_trailing_s16" `
            -EvidenceSource "n64_player_animation_frame_ir_layout" `
            -Component "" `
            -Mapping $null `
            -Evidence $null `
            -Unresolved $unresolvedRow `
            -ZeroRecord $zeroRecord `
            -CrossFormMapping $null `
            -N64Layout $n64Layout `
            -RequiredNextStep "preserve as raw N64 PlayerAnimation trailing s16; do not map to CSAB transform"))
    }
    elseif ($null -ne $childMapping) {
        $component = [string]$childMapping.component
        $evidence = Candidate-ComponentEvidence $childCandidates $channel $component
        $contractRows.Add((New-AnbContractRow `
            -Channel $channel `
            -ResolutionStatus "resolved_child_stable_transform" `
            -SemanticClass "transform_component" `
            -EvidenceSource "child_stable_candidate_mapping" `
            -Component $component `
            -Mapping $childMapping `
            -Evidence $evidence `
            -Unresolved $null `
            -ZeroRecord $null `
            -CrossFormMapping $null `
            -N64Layout $n64Layout `
            -RequiredNextStep "bake or runtime-validate this linear channel mapping against playback"))
    }
    elseif ($null -ne $crossFormMapping) {
        $component = [string]$crossFormMapping.component
        $evidence = Candidate-ComponentEvidence $childCandidates $channel $component
        $contractRows.Add((New-AnbContractRow `
            -Channel $channel `
            -ResolutionStatus "resolved_cross_form_transform" `
            -SemanticClass "transform_component" `
            -EvidenceSource "child_adult_cross_form_high_confidence_direct" `
            -Component $component `
            -Mapping $null `
            -Evidence $evidence `
            -Unresolved $unresolvedRow `
            -ZeroRecord $null `
            -CrossFormMapping $crossFormMapping `
            -N64Layout $n64Layout `
            -RequiredNextStep "bake or runtime-validate this cross-form linear channel mapping against playback"))
    }
    elseif (($null -ne $zeroRecord) -and ($zeroRecord.resolution_status -eq "resolved_static_zero_padding_or_reserved")) {
        if (($null -ne $n64Layout) -and ($n64Layout.status -eq "n64_player_limb_transform")) {
            $contractRows.Add((New-AnbContractRow `
                -Channel $channel `
                -ResolutionStatus "resolved_n64_player_limb_layout_static_transform" `
                -SemanticClass "n64_player_limb_static_transform_component" `
                -EvidenceSource "n64_player_animation_frame_ir_layout" `
                -Component ([string]$n64Layout.oot3d_component) `
                -Mapping $null `
                -Evidence (Candidate-ComponentEvidence $childCandidates $channel ([string]$n64Layout.oot3d_component)) `
                -Unresolved $null `
                -ZeroRecord $zeroRecord `
                -CrossFormMapping $null `
                -N64Layout $n64Layout `
                -RequiredNextStep "preserve as a named N64 PlayerLimb static transform slot unless runtime playback proves otherwise"))
        }
        else {
            $contractRows.Add((New-AnbContractRow `
                -Channel $channel `
                -ResolutionStatus "resolved_static_zero_padding_or_reserved" `
                -SemanticClass "static_zero_padding_or_reserved" `
                -EvidenceSource "child_zero_candidate_classification" `
                -Component "" `
                -Mapping $null `
                -Evidence $null `
                -Unresolved $null `
                -ZeroRecord $zeroRecord `
                -CrossFormMapping $null `
                -N64Layout $n64Layout `
                -RequiredNextStep "keep as static zero unless runtime playback proves an implicit transform slot"))
        }
    }
    elseif (($null -ne $zeroRecord) -and ($zeroRecord.resolution_status -eq "resolved_constant_per_record_control_tuple")) {
        if (($null -ne $n64Layout) -and ($n64Layout.status -eq "n64_player_limb_transform")) {
            $contractRows.Add((New-AnbContractRow `
                -Channel $channel `
                -ResolutionStatus "resolved_n64_player_limb_layout_constant_transform" `
                -SemanticClass "n64_player_limb_constant_transform_component" `
                -EvidenceSource "n64_player_animation_frame_ir_layout" `
                -Component ([string]$n64Layout.oot3d_component) `
                -Mapping $null `
                -Evidence (Candidate-ComponentEvidence $childCandidates $channel ([string]$n64Layout.oot3d_component)) `
                -Unresolved $null `
                -ZeroRecord $zeroRecord `
                -CrossFormMapping $null `
                -N64Layout $n64Layout `
                -RequiredNextStep "preserve as a named N64 PlayerLimb constant transform slot pending CSAB bake/runtime validation"))
        }
        else {
            $contractRows.Add((New-AnbContractRow `
                -Channel $channel `
                -ResolutionStatus "resolved_constant_per_record_control_tuple" `
                -SemanticClass "constant_per_record_control_tuple" `
                -EvidenceSource "child_zero_candidate_classification" `
                -Component "" `
                -Mapping $null `
                -Evidence $null `
                -Unresolved $null `
                -ZeroRecord $zeroRecord `
                -CrossFormMapping $null `
                -N64Layout $n64Layout `
                -RequiredNextStep "preserve as non-transform per-record control tuple; do not map to CSAB transform"))
        }
    }
    elseif (
        ($null -ne $unresolvedRow) `
        -and ($unresolvedRow.work_kind -eq "candidate_but_not_stable") `
        -and ($null -ne $n64Layout) `
        -and ($n64Layout.status -eq "n64_player_limb_transform") `
        -and (-not [string]::IsNullOrWhiteSpace([string]$n64Layout.oot3d_component))
    ) {
        $component = [string]$n64Layout.oot3d_component
        $contractRows.Add((New-AnbContractRow `
            -Channel $channel `
            -ResolutionStatus "resolved_n64_player_limb_layout_transform" `
            -SemanticClass "n64_player_limb_transform_component" `
            -EvidenceSource "n64_player_animation_frame_ir_layout" `
            -Component $component `
            -Mapping $null `
            -Evidence (Candidate-ComponentEvidence $childCandidates $channel $component) `
            -Unresolved $unresolvedRow `
            -ZeroRecord $null `
            -CrossFormMapping $null `
            -N64Layout $n64Layout `
            -RequiredNextStep "bake or runtime-validate this N64 PlayerLimb layout channel against OOT3D CSAB retargeting"))
    }
    elseif (($null -ne $unresolvedRow) -and ($unresolvedRow.work_kind -eq "candidate_but_not_stable")) {
        $contractRows.Add((New-AnbContractRow `
            -Channel $channel `
            -ResolutionStatus "unresolved_candidate_but_not_stable" `
            -SemanticClass $unresolvedRow.unstable_candidate_semantic_class `
            -EvidenceSource "child_unresolved_channel_worklist" `
            -Component $unresolvedRow.best_component `
            -Mapping $null `
            -Evidence $null `
            -Unresolved $unresolvedRow `
            -ZeroRecord $null `
            -CrossFormMapping $null `
            -N64Layout $n64Layout `
            -RequiredNextStep $unresolvedRow.required_evidence))
    }
    else {
        $semanticClass = "unknown"
        $requiredNextStep = "add semantic source evidence for this channel"
        if ($null -ne $unresolvedRow) {
            $semanticClass = $unresolvedRow.zero_candidate_semantic_class
            $requiredNextStep = $unresolvedRow.required_evidence
        }
        $contractRows.Add((New-AnbContractRow `
            -Channel $channel `
            -ResolutionStatus "unresolved_requires_semantic_source" `
            -SemanticClass $semanticClass `
            -EvidenceSource "child_unresolved_channel_worklist" `
            -Component "" `
            -Mapping $null `
            -Evidence $null `
            -Unresolved $unresolvedRow `
            -ZeroRecord $zeroRecord `
            -CrossFormMapping $null `
            -N64Layout $n64Layout `
            -RequiredNextStep $requiredNextStep))
    }
}
$contractRowsArray = @($contractRows.ToArray() | Sort-Object {[int]$_.anb_channel})
$effectiveResolvedChannelCount = @($contractRowsArray | Where-Object { $_.resolution_status -notlike "unresolved*" }).Count
$effectiveUnresolvedChannelCount = 67 - $effectiveResolvedChannelCount
$n64LayoutTransformChannelCount = @($contractRowsArray | Where-Object { $_.n64_layout_status -eq "n64_player_limb_transform" }).Count
$n64LayoutTrailingChannelCount = @($contractRowsArray | Where-Object { $_.n64_layout_status -eq "n64_trailing_s16" }).Count
$n64LayoutClosureResolvedChannelCount = @($contractRowsArray | Where-Object {
    $_.resolution_status -eq "resolved_n64_player_limb_layout_transform"
}).Count
$n64LayoutStaticTransformChannelCount = @($contractRowsArray | Where-Object {
    $_.resolution_status -eq "resolved_n64_player_limb_layout_static_transform"
}).Count
$n64LayoutConstantTransformChannelCount = @($contractRowsArray | Where-Object {
    $_.resolution_status -eq "resolved_n64_player_limb_layout_constant_transform"
}).Count
$unresolvedClosureRows = New-Object System.Collections.Generic.List[object]
foreach ($row in @($contractRowsArray | Where-Object { $_.resolution_status -like "unresolved*" })) {
    $channel = [int]$row.anb_channel
    $closureRow = $closureByChannel[$channel]
    $closureActionClass = Unresolved-ClosureActionClass $row $closureRow
    $unresolvedClosureRows.Add([pscustomobject]@{
        priority = Unresolved-ClosurePriority $closureActionClass
        anb_channel = $channel
        closure_action_class = $closureActionClass
        unresolved_status = $row.resolution_status
        semantic_class = $row.semantic_class
        contract_component = $row.component
        component_family = $row.component_family
        bone_index = $row.bone_index
        track_semantic = $row.track_semantic
        child_work_kind = $row.child_work_kind
        child_unstable_candidate_kind = $row.child_unstable_candidate_kind
        child_unstable_candidate_semantic_class = $row.child_unstable_candidate_semantic_class
        child_top_component = $row.child_top_component
        child_total_candidate_count = $row.child_total_candidate_count
        mean_abs_correlation = $row.mean_abs_correlation
        mean_slope = $row.mean_slope
        mean_intercept = $row.mean_intercept
        max_abs_error = $row.max_abs_error
        closure_class = if ($null -ne $closureRow) { $closureRow.closure_class } else { "" }
        readiness_class = if ($null -ne $closureRow) { $closureRow.readiness_class } else { "" }
        promotion_review_class = if ($null -ne $closureRow) { $closureRow.promotion_review_class } else { "" }
        adult_stable_component = if ($null -ne $closureRow) { $closureRow.adult_stable_component } else { "" }
        adult_stable_consensus_count = if ($null -ne $closureRow) { $closureRow.adult_stable_consensus_count } else { "" }
        child_support_for_adult_component = if ($null -ne $closureRow) { $closureRow.child_support_for_adult_component } else { "" }
        child_adult_component_mean_abs_correlation = if ($null -ne $closureRow) { $closureRow.child_adult_component_mean_abs_correlation } else { "" }
        child_adult_component_direct_record_count = if ($null -ne $closureRow) { $closureRow.child_adult_component_direct_record_count } else { "" }
        child_adult_component_resampled_record_count = if ($null -ne $closureRow) { $closureRow.child_adult_component_resampled_record_count } else { "" }
        child_adult_component_direct_max_abs_error = if ($null -ne $closureRow) { $closureRow.child_adult_component_direct_max_abs_error } else { "" }
        zero_candidate_kind = $row.zero_candidate_kind
        zero_candidate_semantic_class = $row.zero_candidate_semantic_class
        channel_min = $row.channel_min
        channel_max = $row.channel_max
        active_record_count = $row.active_record_count
        closure_gate = Unresolved-ClosureGate $closureActionClass
        required_next_step = $row.required_next_step
    })
}
$unresolvedClosureRowsArray = @($unresolvedClosureRows.ToArray() | Sort-Object priority, anb_channel)
$contractResolutionCounts = @{}
foreach ($group in @($contractRowsArray | Group-Object resolution_status)) {
    $contractResolutionCounts[$group.Name] = $group.Count
}
$resolvedStaticZeroChannelCount = [int](Get-JsonValue $contractResolutionCounts "resolved_static_zero_padding_or_reserved") `
    + [int](Get-JsonValue $contractResolutionCounts "resolved_n64_player_limb_layout_static_transform")
$resolvedConstantControlTupleChannelCount = [int](Get-JsonValue $contractResolutionCounts "resolved_constant_per_record_control_tuple") `
    + [int](Get-JsonValue $contractResolutionCounts "resolved_n64_player_limb_layout_constant_transform")
$contractComponentFamilyCounts = @{}
foreach ($group in @($contractRowsArray | Group-Object canonical_raw_component_family)) {
    $contractComponentFamilyCounts[$group.Name] = $group.Count
}
$contractTransformChannelCount = @($contractRowsArray | Where-Object {
    $_.canonical_raw_component_family -in @("rotation", "translation") -and $_.resolution_status -like "resolved_*_transform"
}).Count
$rawLayoutCsabCorrelationRowsArray = @($contractRowsArray | Where-Object {
    $_.n64_layout_status -eq "n64_player_limb_transform"
} | ForEach-Object {
    New-RawLayoutCsabCorrelationRow $_
} | Sort-Object priority, anb_channel)
$rawLayoutCsabCorrelationStatusCounts = Count-ByProperty $rawLayoutCsabCorrelationRowsArray "csab_correlation_to_raw_layout_status"
$rawLayoutCsabDivergenceClassCounts = Count-ByProperty $rawLayoutCsabCorrelationRowsArray "divergence_class"
$rawLayoutCsabRiskClassCounts = Count-ByProperty $rawLayoutCsabCorrelationRowsArray "risk_class"
$rawLayoutCsabExactMatchCount = @($rawLayoutCsabCorrelationRowsArray | Where-Object {
    $_.divergence_class -eq "csab_correlation_matches_raw_layout"
}).Count
$rawLayoutCsabDivergenceCount = @($rawLayoutCsabCorrelationRowsArray | Where-Object {
    $_.divergence_class -ne "csab_correlation_matches_raw_layout"
}).Count
$semanticContract = [ordered]@{
    format = "oot3d_link_child_anb_semantic_channel_contract_v1"
    child_audit = (Resolve-Path $ChildAnbSemanticAudit).Path
    child_adult_comparison = $comparisonJson
    channel_count = 67
    row_count = $contractRowsArray.Count
    contract_status = if ($effectiveUnresolvedChannelCount -eq 0) { "raw_layout_complete" } else { "partial" }
    formula_policy = "Canonical ANB semantics are the N64 PlayerLimb raw layout fields. Child/adult CSAB-correlation rows preserve regression formulae as retarget evidence only; they are not direct channel identity when they diverge from the raw layout."
    resolved_transform_channel_count = $contractTransformChannelCount
    resolved_static_zero_channel_count = $resolvedStaticZeroChannelCount
    resolved_control_tuple_channel_count = $resolvedConstantControlTupleChannelCount
    effective_resolved_channel_count = $effectiveResolvedChannelCount
    effective_unresolved_channel_count = $effectiveUnresolvedChannelCount
    resolution_status_counts = $contractResolutionCounts
    component_family_counts = $contractComponentFamilyCounts
    raw_layout_csab_correlation = [ordered]@{
        format = "oot3d_link_child_anb_raw_layout_csab_correlation_divergence_v1"
        json = $rawLayoutCsabCorrelationJson
        csv = $rawLayoutCsabCorrelationCsv
        row_count = $rawLayoutCsabCorrelationRowsArray.Count
        exact_raw_layout_match_count = $rawLayoutCsabExactMatchCount
        divergent_or_raw_only_count = $rawLayoutCsabDivergenceCount
        status_counts = $rawLayoutCsabCorrelationStatusCounts
        divergence_class_counts = $rawLayoutCsabDivergenceClassCounts
        risk_class_counts = $rawLayoutCsabRiskClassCounts
        playback_policy = "Use canonical_raw_component/n64_oot3d_component for raw ANB playback. CSAB correlation fields are secondary retarget/bake evidence and must not drive root scale or position directly when divergence_class is not csab_correlation_matches_raw_layout."
    }
    n64_player_limb_layout = [ordered]@{
        status = "resolved"
        summary = (Resolve-Path $N64ReferenceSummary).Path
        frame_ir_source = $n64ReferenceSummaryJson.n64_payload_decode.frame_ir.source
        frame_ir_layout = $n64ReferenceSummaryJson.n64_payload_decode.frame_ir.layout
        transform_channel_count = $n64LayoutTransformChannelCount
        trailing_s16_channel_count = $n64LayoutTrailingChannelCount
        closure_resolved_channel_count = $n64LayoutClosureResolvedChannelCount
        static_transform_channel_count = $n64LayoutStaticTransformChannelCount
        constant_transform_channel_count = $n64LayoutConstantTransformChannelCount
        effective_unresolved_channel_count_after_layout = $effectiveUnresolvedChannelCount
    }
    unresolved_closure_ledger = [ordered]@{
        format = "oot3d_link_child_anb_unresolved_semantic_closure_ledger_v1"
        json = $unresolvedClosureLedgerJson
        csv = $unresolvedClosureLedgerCsv
        row_count = $unresolvedClosureRowsArray.Count
        closure_action_class_counts = Count-ByProperty $unresolvedClosureRowsArray "closure_action_class"
        readiness_class_counts = Count-ByProperty $unresolvedClosureRowsArray "readiness_class"
        promotion_review_class_counts = Count-ByProperty $unresolvedClosureRowsArray "promotion_review_class"
    }
    channels = $contractRowsArray
}

$rawLayoutCsabCorrelationLedger = [ordered]@{
    format = "oot3d_link_child_anb_raw_layout_csab_correlation_divergence_v1"
    semantic_channel_contract = $semanticContractJson
    row_count = $rawLayoutCsabCorrelationRowsArray.Count
    exact_raw_layout_match_count = $rawLayoutCsabExactMatchCount
    divergent_or_raw_only_count = $rawLayoutCsabDivergenceCount
    status_counts = $rawLayoutCsabCorrelationStatusCounts
    divergence_class_counts = $rawLayoutCsabDivergenceClassCounts
    risk_class_counts = $rawLayoutCsabRiskClassCounts
    policy = [ordered]@{
        canonical_semantics = "N64 PlayerLimb raw layout fields are authoritative for ANB channel identity."
        csab_correlation_scope = "CSAB correlations are retarget/bake evidence and can be numerically strong even when they are not the raw channel identity."
        realtime_scale_position_gate = "Realtime playback must validate root translation scale/axis and root rotation signs from canonical raw slots before using CSAB-correlation formulae."
    }
    sample_high_risk_rows = @($rawLayoutCsabCorrelationRowsArray | Where-Object {
        $_.risk_class -eq "high_false_root_or_model_translation_risk"
    } | Select-Object -First $SampleLimit)
    sample_rows = @($rawLayoutCsabCorrelationRowsArray | Select-Object -First $SampleLimit)
    rows = $rawLayoutCsabCorrelationRowsArray
}

$unresolvedClosureLedger = [ordered]@{
    format = "oot3d_link_child_anb_unresolved_semantic_closure_ledger_v1"
    semantic_channel_contract = $semanticContractJson
    child_adult_comparison = $comparisonJson
    row_count = $unresolvedClosureRowsArray.Count
    contract_status = if ($unresolvedClosureRowsArray.Count -eq 0) { "closed_by_n64_player_limb_layout" } else { "partial" }
    policy = [ordered]@{
        scope = "post-contract unresolved Link child ANB channels only"
        acceptance_policy = "ledger rows are closure work items; resolved N64 PlayerLimb layout rows are accepted as raw ANB channel semantics pending CSAB bake/runtime validation"
        ordering_policy = "priorities prefer cross-form evidence that needs narrow verification, then adult-only holds, then shared unresolved child/adult evidence, then non-CSAB semantic-source work"
    }
    unresolved_status_counts = Count-ByProperty $unresolvedClosureRowsArray "unresolved_status"
    semantic_class_counts = Count-ByProperty $unresolvedClosureRowsArray "semantic_class"
    closure_action_class_counts = Count-ByProperty $unresolvedClosureRowsArray "closure_action_class"
    closure_class_counts = Count-ByProperty $unresolvedClosureRowsArray "closure_class"
    readiness_class_counts = Count-ByProperty $unresolvedClosureRowsArray "readiness_class"
    promotion_review_class_counts = Count-ByProperty $unresolvedClosureRowsArray "promotion_review_class"
    priority_counts = Count-ByProperty $unresolvedClosureRowsArray "priority"
    sample_rows = @($unresolvedClosureRowsArray | Select-Object -First $SampleLimit)
    rows = $unresolvedClosureRowsArray
}

$comparison = [ordered]@{
    format = "oot3d_link_child_adult_anb_semantic_comparison_v1"
    child_audit = (Resolve-Path $ChildAnbSemanticAudit).Path
    adult_anb_export = (Resolve-Path $adultAnbExport).Path
    adult_manifest = (Resolve-Path $adultManifest).Path
    adult_audit = (Resolve-Path $adultSemanticAudit).Path
    child = [ordered]@{
        compared_record_count = $childAudit.compared_record_count
        candidate_channel_count = $childAudit.candidate_channel_count
        strong_candidate_count = $childAudit.strong_candidate_count
        stable_candidate_channel_count = $childAudit.stable_candidate_channel_count
        unstable_candidate_channel_count = $childAudit.unstable_candidate_channel_count
        zero_candidate_channel_count = $childAudit.zero_candidate_channel_count
        resolved_zero_candidate_channel_count = $childAudit.resolved_zero_candidate_channel_count
        unresolved_zero_candidate_channel_count = $childAudit.unresolved_zero_candidate_channel_count
        unresolved_channel_count = $childAudit.unresolved_channel_count
        status_counts = $childAudit.status_counts
    }
    adult = [ordered]@{
        payload_count = $adultExport.payload_count
        duplicate_payload_match_count = $adultExport.duplicate_payload_match_count
        compared_record_count = $adultAudit.compared_record_count
        candidate_channel_count = $adultAudit.candidate_channel_count
        strong_candidate_count = $adultAudit.strong_candidate_count
        stable_candidate_channel_count = $adultAudit.stable_candidate_channel_count
        unstable_candidate_channel_count = $adultAudit.unstable_candidate_channel_count
        zero_candidate_channel_count = $adultAudit.zero_candidate_channel_count
        resolved_zero_candidate_channel_count = $adultAudit.resolved_zero_candidate_channel_count
        unresolved_zero_candidate_channel_count = $adultAudit.unresolved_zero_candidate_channel_count
        unresolved_channel_count = $adultAudit.unresolved_channel_count
        status_counts = $adultAudit.status_counts
        unstable_candidate_channel_class_counts = $adultAudit.unstable_candidate_channel_class_counts
        zero_candidate_channel_class_counts = $adultAudit.zero_candidate_channel_class_counts
        zero_candidate_resolution_status_counts = $adultAudit.zero_candidate_resolution_status_counts
    }
    stable_channel_status_counts = $stableStatusCounts
    cross_form_support_class_counts = $crossFormSupportCounts
    child_unresolved_cross_form_closure_class_counts = $closureClassCounts
    child_unresolved_cross_form_readiness_class_counts = $closureReadinessCounts
    child_unresolved_cross_form_promotion_review_class_counts = $promotionReviewCounts
    child_unresolved_cross_form_closure_queue_count = $closureRowsSorted.Count
    promoted_cross_form_channel_count = $promotedCrossFormMappingArray.Count
    promoted_cross_form_mappings = $promotedCrossFormMappingArray
    effective_resolved_channel_count_after_cross_form_promotions = $effectiveResolvedChannelCountAfterCrossForm
    effective_unresolved_channel_count_after_cross_form_promotions = $effectiveUnresolvedChannelCountAfterCrossForm
    effective_resolved_channel_count_after_n64_layout_closure = $effectiveResolvedChannelCount
    effective_unresolved_channel_count_after_n64_layout_closure = $effectiveUnresolvedChannelCount
    n64_layout_closure_resolved_channel_count = $n64LayoutClosureResolvedChannelCount
    n64_layout_static_transform_channel_count = $n64LayoutStaticTransformChannelCount
    n64_layout_constant_transform_channel_count = $n64LayoutConstantTransformChannelCount
    n64_layout_trailing_s16_channel_count = $n64LayoutTrailingChannelCount
    stable_difference_count = $stableDifferenceRows.Count
    stable_differences = $stableDifferenceRows
    outputs = [ordered]@{
        adult_unresolved_channel_worklist_csv = (Resolve-Path $adultUnresolvedCsv).Path
        stable_channel_comparison_csv = $stableComparisonCsv
        child_cross_form_closure_queue_csv = $closureQueueCsv
        child_semantic_channel_contract_json = $semanticContractJson
        child_semantic_channel_contract_csv = $semanticContractCsv
        child_unresolved_semantic_closure_ledger_json = $unresolvedClosureLedgerJson
        child_unresolved_semantic_closure_ledger_csv = $unresolvedClosureLedgerCsv
    }
}
$comparison | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $comparisonJson -Encoding UTF8
$stableRows.ToArray() | Export-Csv -LiteralPath $stableComparisonCsv -NoTypeInformation -Encoding UTF8
$closureRowsSorted | Export-Csv -LiteralPath $closureQueueCsv -NoTypeInformation -Encoding UTF8
$semanticContract | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $semanticContractJson -Encoding UTF8
$contractRowsArray | Export-Csv -LiteralPath $semanticContractCsv -NoTypeInformation -Encoding UTF8
$rawLayoutCsabCorrelationLedger | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $rawLayoutCsabCorrelationJson -Encoding UTF8
$rawLayoutCsabCorrelationRowsArray | Export-Csv -LiteralPath $rawLayoutCsabCorrelationCsv -NoTypeInformation -Encoding UTF8
$unresolvedClosureLedger | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $unresolvedClosureLedgerJson -Encoding UTF8
$unresolvedClosureRowsArray | Export-Csv -LiteralPath $unresolvedClosureLedgerCsv -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($adultExport.format -eq "oot3d_anb_payload_batch_export_v1") "unexpected adult ANB export format"
    Assert-Condition ([int]$adultExport.payload_count -eq 561) "expected 561 adult ANB payloads"
    Assert-Condition ([int]$adultExport.duplicate_payload_match_count -eq 561) "expected every adult ANB payload to match child ultra duplicate"
    Assert-Condition ([int]$adultExport.issue_count -eq 0) "expected zero adult ANB export issues"
    Assert-Condition ([int]$adultExport.csab_lookup.matched_anb_count -eq 508) "expected 508 adult ANB payloads with CSAB matches"
    Assert-Condition ([int]$adultExport.csab_lookup.matched_frame_count_status_count -eq 449) "expected 449 adult matched ANB/CSAB records with equal frame counts"
    Assert-Condition ([int]$adultExport.csab_lookup.mismatched_frame_count_status_count -eq 59) "expected 59 adult matched ANB/CSAB records with differing frame counts"
    Assert-Condition ($adultAudit.format -eq "oot3d_anb_semantic_candidate_audit_v1") "unexpected adult ANB semantic audit format"
    Assert-Condition ([int]$adultAudit.compared_record_count -eq 508) "expected 508 compared adult ANB/CSAB records"
    Assert-Condition ([int]$adultAudit.candidate_channel_count -eq 58) "expected 58 adult ANB channels with semantic candidates"
    Assert-Condition ([int]$adultAudit.strong_candidate_count -eq 5862) "expected 5862 adult ANB strong candidates"
    Assert-Condition ([int]$adultAudit.stable_candidate_channel_count -eq 37) "expected 37 stable adult ANB semantic channels"
    Assert-Condition ([int]$adultAudit.unstable_candidate_channel_count -eq 21) "expected 21 unstable adult ANB semantic channels"
    Assert-Condition ([int]$adultAudit.zero_candidate_channel_count -eq 9) "expected 9 adult ANB zero-candidate channels"
    Assert-Condition ([int]$adultAudit.resolved_zero_candidate_channel_count -eq 9) "expected 9 adult ANB zero-candidate channels resolved as non-transform fields"
    Assert-Condition ([int]$adultAudit.resolved_static_zero_candidate_channel_count -eq 6) "expected 6 adult ANB zero-candidate channels resolved as static zero"
    Assert-Condition ([int]$adultAudit.resolved_constant_control_tuple_channel_count -eq 3) "expected 3 adult ANB zero-candidate channels resolved as constant per-record control tuples"
    Assert-Condition ([int]$adultAudit.unresolved_zero_candidate_channel_count -eq 0) "expected 0 adult ANB zero-candidate channels still requiring semantic source evidence"
    Assert-Condition ([int]$adultAudit.unresolved_channel_count -eq 21) "expected 21 unresolved adult ANB channels after non-transform resolution"
    Assert-Condition ([int]$adultAudit.status_counts.direct_frame_aligned -eq 449) "expected 449 direct frame-aligned adult ANB comparisons"
    Assert-Condition ([int]$adultAudit.status_counts.resampled_frame_count_mismatch -eq 59) "expected 59 resampled adult ANB comparisons"
    Assert-Condition ([int]$adultAudit.unstable_candidate_channel_class_counts.tied_top_component_consensus -eq 14) "expected 14 tied-top adult unstable channels"
    Assert-Condition ([int]$adultAudit.unstable_candidate_channel_class_counts.low_evidence_component_pair -eq 1) "expected 1 low-evidence-pair adult unstable channel"
    Assert-Condition ([int]$adultAudit.unstable_candidate_channel_class_counts.diffuse_single_hit_components -eq 5) "expected 5 diffuse single-hit adult unstable channels"
    Assert-Condition ([int]$adultAudit.unstable_candidate_channel_class_counts.single_record_candidate -eq 1) "expected 1 single-record adult unstable channel"
    Assert-Condition ([int]$adultAudit.zero_candidate_resolution_status_counts.resolved_static_zero_padding_or_reserved -eq 6) "expected 6 adult static-zero resolutions"
    Assert-Condition ([int]$adultAudit.zero_candidate_resolution_status_counts.resolved_constant_per_record_control_tuple -eq 3) "expected 3 adult constant-control-tuple resolutions"
    Assert-Condition ([int](Get-JsonValue $adultAudit.zero_candidate_resolution_status_counts "unresolved_requires_semantic_source") -eq 0) "expected 0 adult unresolved zero-candidate rows"
    Assert-Condition ([int](Get-JsonValue $stableStatusCounts "same_stable") -eq 34) "expected 34 child/adult stable channels with identical component"
    Assert-Condition ([int](Get-JsonValue $stableStatusCounts "adult_only_stable") -eq 2) "expected 2 adult-only stable channels"
    Assert-Condition ([int](Get-JsonValue $stableStatusCounts "child_only_stable") -eq 9) "expected 9 child-only stable channels"
    Assert-Condition ([int](Get-JsonValue $stableStatusCounts "changed_stable") -eq 1) "expected 1 changed stable child/adult channel"
    Assert-Condition ([int](Get-JsonValue $crossFormSupportCounts "same_stable_component") -eq 34) "expected 34 stable channels with same child/adult component"
    Assert-Condition ([int](Get-JsonValue $crossFormSupportCounts "adult_stable_child_candidate_support") -eq 1) "expected 1 adult-stable channel with child candidate support"
    Assert-Condition ([int](Get-JsonValue $crossFormSupportCounts "adult_stable_no_child_candidate_support") -eq 1) "expected 1 adult-stable channel without child candidate support"
    Assert-Condition ([int](Get-JsonValue $crossFormSupportCounts "child_stable_adult_candidate_support") -eq 9) "expected 9 child-stable channels with adult candidate support"
    Assert-Condition ([int](Get-JsonValue $crossFormSupportCounts "conflicting_stable_component") -eq 1) "expected 1 conflicting stable child/adult component"
    Assert-Condition ($closureRowsSorted.Count -eq 14) "expected 14 child unresolved cross-form closure queue rows"
    Assert-Condition ([int](Get-JsonValue $closureClassCounts "adult_stable_child_candidate_support") -eq 1) "expected 1 child unresolved channel with adult stable plus child candidate support"
    Assert-Condition ([int](Get-JsonValue $closureClassCounts "adult_stable_no_child_candidate_support") -eq 1) "expected 1 child unresolved channel with adult stable but no child candidate support"
    Assert-Condition ([int](Get-JsonValue $closureClassCounts "shared_child_adult_unresolved") -eq 12) "expected 12 child unresolved channels also unresolved in adult"
    Assert-Condition ([int](Get-JsonValue $closureReadinessCounts "cross_form_promotion_candidate") -eq 1) "expected 1 cross-form ANB promotion candidate"
    Assert-Condition ([int](Get-JsonValue $closureReadinessCounts "needs_child_pose_or_family_validation") -eq 0) "expected 0 cross-form ANB channels needing child pose or family validation"
    Assert-Condition ([int](Get-JsonValue $closureReadinessCounts "adult_only_hold_for_child_evidence") -eq 1) "expected 1 adult-only ANB channel held for child evidence"
    Assert-Condition ([int](Get-JsonValue $closureReadinessCounts "requires_new_evidence_in_both_forms") -eq 12) "expected 12 ANB channels requiring new evidence in both forms"
    Assert-Condition ([int](Get-JsonValue $promotionReviewCounts "high_confidence_direct_transform_candidate") -eq 1) "expected 1 high-confidence direct ANB transform candidate"
    Assert-Condition ([int](Get-JsonValue $promotionReviewCounts "promotion_review_insufficient_direct_or_resampled") -eq 0) "expected 0 ANB promotion-review candidates held for insufficient direct or resampled evidence"
    Assert-Condition ([int](Get-JsonValue $promotionReviewCounts "single_direct_clean_needs_second_child_hit") -eq 0) "expected 0 single-direct clean ANB candidates needing a second child hit"
    Assert-Condition ([int](Get-JsonValue $promotionReviewCounts "adult_only_hold") -eq 1) "expected 1 adult-only ANB hold"
    Assert-Condition ([int](Get-JsonValue $promotionReviewCounts "not_ready_for_promotion_review") -eq 12) "expected 12 ANB channels not ready for promotion review"
    Assert-Condition ($promotedCrossFormMappingArray.Count -eq 1) "expected 1 promoted cross-form ANB mapping"
    Assert-Condition ($effectiveResolvedChannelCountAfterCrossForm -eq 54) "expected 54 effective resolved ANB channels after cross-form promotions"
    Assert-Condition ($effectiveUnresolvedChannelCountAfterCrossForm -eq 13) "expected 13 effective unresolved ANB channels after cross-form promotions"
    Assert-Condition ($n64LayoutTransformChannelCount -eq 66) "expected 66 N64 PlayerLimb Vec3s transform channels"
    Assert-Condition ($n64LayoutTrailingChannelCount -eq 1) "expected 1 N64 PlayerAnimation trailing s16 channel"
    Assert-Condition ($n64LayoutClosureResolvedChannelCount -eq 13) "expected N64 PlayerLimb layout to close the remaining 13 ANB channels"
    Assert-Condition ($n64LayoutStaticTransformChannelCount -eq 6) "expected 6 N64 PlayerLimb static transform channels"
    Assert-Condition ($n64LayoutConstantTransformChannelCount -eq 3) "expected 3 N64 PlayerLimb constant transform channels"
    Assert-Condition ($semanticContract.format -eq "oot3d_link_child_anb_semantic_channel_contract_v1") "unexpected Link child ANB semantic channel contract format"
    Assert-Condition ($semanticContract.contract_status -eq "raw_layout_complete") "expected raw-layout-complete ANB semantic channel contract"
    Assert-Condition ($contractRowsArray.Count -eq 67) "expected 67 Link child ANB semantic contract channel rows"
    Assert-Condition ($semanticContract.resolved_transform_channel_count -eq 66) "expected 66 resolved ANB transform channel rows"
    Assert-Condition ($semanticContract.resolved_static_zero_channel_count -eq 6) "expected 6 resolved ANB static-zero channel rows"
    Assert-Condition ($semanticContract.resolved_control_tuple_channel_count -eq 3) "expected 3 resolved ANB control-tuple channel rows"
    Assert-Condition ($semanticContract.effective_resolved_channel_count -eq 67) "expected 67 effective resolved ANB channel contract rows"
    Assert-Condition ($semanticContract.effective_unresolved_channel_count -eq 0) "expected 0 unresolved ANB channel contract rows"
    Assert-Condition ([int](Get-JsonValue $contractComponentFamilyCounts "translation") -eq 3) "expected 3 canonical raw ANB translation channels"
    Assert-Condition ([int](Get-JsonValue $contractComponentFamilyCounts "rotation") -eq 63) "expected 63 canonical raw ANB rotation channels"
    Assert-Condition ([int](Get-JsonValue $contractComponentFamilyCounts "none") -eq 1) "expected 1 canonical raw ANB non-transform trailing channel"
    Assert-Condition ([int](Get-JsonValue $contractResolutionCounts "resolved_child_stable_transform") -eq 43) "expected 43 child-stable transform contract rows after raw trailing-s16 override"
    Assert-Condition ([int](Get-JsonValue $contractResolutionCounts "resolved_cross_form_transform") -eq 1) "expected 1 cross-form transform contract row"
    Assert-Condition ([int](Get-JsonValue $contractResolutionCounts "resolved_n64_player_limb_layout_transform") -eq 13) "expected 13 N64 PlayerLimb layout transform contract rows"
    Assert-Condition ([int](Get-JsonValue $contractResolutionCounts "resolved_n64_player_limb_layout_static_transform") -eq 6) "expected 6 N64 PlayerLimb static transform contract rows"
    Assert-Condition ([int](Get-JsonValue $contractResolutionCounts "resolved_n64_player_limb_layout_constant_transform") -eq 3) "expected 3 N64 PlayerLimb constant transform contract rows"
    Assert-Condition ([int](Get-JsonValue $contractResolutionCounts "resolved_n64_trailing_s16") -eq 1) "expected 1 N64 trailing-s16 contract row"
    Assert-Condition ([int](Get-JsonValue $contractResolutionCounts "resolved_static_zero_padding_or_reserved") -eq 0) "expected 0 legacy static-zero contract rows after N64 layout closure"
    Assert-Condition ([int](Get-JsonValue $contractResolutionCounts "resolved_constant_per_record_control_tuple") -eq 0) "expected 0 legacy constant-control-tuple contract rows after N64 layout closure"
    Assert-Condition ([int](Get-JsonValue $contractResolutionCounts "unresolved_candidate_but_not_stable") -eq 0) "expected 0 unresolved candidate-but-not-stable contract rows"
    Assert-Condition ([int](Get-JsonValue $contractResolutionCounts "unresolved_requires_semantic_source") -eq 0) "expected 0 unresolved semantic-source contract rows"
    $contractCsvRows = @(Import-Csv -LiteralPath $semanticContractCsv)
    Assert-Condition ($contractCsvRows.Count -eq 67) "expected 67 Link child ANB semantic contract CSV rows"
    Assert-Condition ($rawLayoutCsabCorrelationLedger.format -eq "oot3d_link_child_anb_raw_layout_csab_correlation_divergence_v1") "unexpected raw-layout/CSAB correlation divergence ledger format"
    Assert-Condition ($rawLayoutCsabCorrelationRowsArray.Count -eq 66) "expected 66 raw-layout/CSAB transform correlation rows"
    Assert-Condition ($rawLayoutCsabExactMatchCount -eq 3) "expected 3 CSAB correlations to match raw N64 PlayerLimb layout components"
    Assert-Condition ($rawLayoutCsabDivergenceCount -eq 63) "expected 63 CSAB correlations to diverge from or lack raw N64 PlayerLimb layout identity"
    Assert-Condition ([int](Get-JsonValue $rawLayoutCsabCorrelationStatusCounts "diverges_from_raw_n64_player_limb_layout") -eq 41) "expected 41 stable CSAB correlations to diverge from raw N64 PlayerLimb layout"
    Assert-Condition ([int](Get-JsonValue $rawLayoutCsabDivergenceClassCounts "no_stable_csab_correlation") -eq 22) "expected 22 raw-layout channels without stable CSAB correlation"
    Assert-Condition ([int](Get-JsonValue $rawLayoutCsabDivergenceClassCounts "csab_translation_correlation_for_raw_rotation") -eq 6) "expected 6 high-risk CSAB translation correlations for raw rotation channels"
    Assert-Condition ([int](Get-JsonValue $rawLayoutCsabRiskClassCounts "high_false_root_or_model_translation_risk") -eq 6) "expected 6 high-risk false root/model translation rows"
    $rawLayoutCsabCorrelationCsvRows = @(Import-Csv -LiteralPath $rawLayoutCsabCorrelationCsv)
    Assert-Condition ($rawLayoutCsabCorrelationCsvRows.Count -eq 66) "expected 66 raw-layout/CSAB correlation divergence CSV rows"
    Assert-Condition ($unresolvedClosureLedger.format -eq "oot3d_link_child_anb_unresolved_semantic_closure_ledger_v1") "unexpected unresolved ANB closure ledger format"
    Assert-Condition ($unresolvedClosureLedger.contract_status -eq "closed_by_n64_player_limb_layout") "expected unresolved ANB closure ledger closed by N64 layout"
    Assert-Condition ($unresolvedClosureRowsArray.Count -eq 0) "expected 0 final unresolved ANB closure ledger rows"
    $unresolvedClosureCsvRows = @()
    if ((Test-Path -LiteralPath $unresolvedClosureLedgerCsv) -and ((Get-Item -LiteralPath $unresolvedClosureLedgerCsv).Length -gt 0)) {
        $unresolvedClosureCsvRows = @(Import-Csv -LiteralPath $unresolvedClosureLedgerCsv)
    }
    Assert-Condition ($unresolvedClosureCsvRows.Count -eq 0) "expected 0 final unresolved ANB closure CSV rows"
    Assert-Condition (@($unresolvedClosureRowsArray | Where-Object { $_.unresolved_status -notlike "unresolved*" }).Count -eq 0) "expected every closure ledger row to be unresolved"
    Assert-Condition (@($unresolvedClosureRowsArray | Where-Object { $_.anb_channel -eq 22 }).Count -eq 0) "expected promoted cross-form channel 22 to be excluded from unresolved closure ledger"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.unresolved_status_counts "unresolved_candidate_but_not_stable") -eq 0) "expected 0 candidate-but-not-stable unresolved closure rows"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.unresolved_status_counts "unresolved_requires_semantic_source") -eq 0) "expected 0 semantic-source unresolved closure rows"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.closure_action_class_counts "cross_form_candidate_fix_direct_or_resampled_evidence") -eq 0) "expected 0 cross-form candidates requiring direct/resampled evidence review"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.closure_action_class_counts "cross_form_single_direct_needs_second_child_hit") -eq 0) "expected 0 cross-form single-direct rows needing second child hit"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.closure_action_class_counts "adult_only_hold_needs_child_source_evidence") -eq 0) "expected 0 adult-only unresolved hold rows"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.closure_action_class_counts "shared_tied_component_disambiguation") -eq 0) "expected 0 shared tied-component unresolved rows"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.closure_action_class_counts "shared_low_evidence_add_matched_records") -eq 0) "expected 0 shared low-evidence unresolved rows"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.closure_action_class_counts "shared_diffuse_family_constraints") -eq 0) "expected 0 shared diffuse-family unresolved rows"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.closure_action_class_counts "semantic_source_required_control_or_curve") -eq 0) "expected 0 semantic-source closure rows"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.readiness_class_counts "cross_form_promotion_candidate") -eq 0) "expected 0 remaining cross-form promotion candidates"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.readiness_class_counts "needs_child_pose_or_family_validation") -eq 0) "expected 0 remaining child pose/family validation rows"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.readiness_class_counts "adult_only_hold_for_child_evidence") -eq 0) "expected 0 remaining adult-only hold rows"
    Assert-Condition ([int](Get-JsonValue $unresolvedClosureLedger.readiness_class_counts "requires_new_evidence_in_both_forms") -eq 0) "expected 0 shared unresolved child/adult rows"
}

Write-Host "OOT3D Link adult ANB export: $adultAnbExport"
Write-Host "OOT3D Link adult ANB semantic audit: $adultSemanticAudit"
Write-Host "OOT3D Link child/adult ANB semantic comparison: $comparisonJson"
Write-Host "OOT3D Link child ANB cross-form closure queue: $closureQueueCsv"
Write-Host "OOT3D Link child ANB semantic channel contract: $semanticContractJson"
Write-Host "OOT3D Link child ANB raw-layout/CSAB correlation divergence: $rawLayoutCsabCorrelationJson"
Write-Host "OOT3D Link child ANB unresolved semantic closure ledger: $unresolvedClosureLedgerJson"
Write-Host "adultComparedRecords=$($adultAudit.compared_record_count) adultStableChannels=$($adultAudit.stable_candidate_channel_count) adultUnresolvedChannels=$($adultAudit.unresolved_channel_count)"
