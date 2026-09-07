param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\direct_callsite_source_identity_review"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child direct-callsite source identity review failed: $Message"
    }
}

function Add-Count([hashtable]$Counts, [string]$Key) {
    if ([string]::IsNullOrWhiteSpace($Key)) {
        $Key = "unknown"
    }
    if (-not $Counts.ContainsKey($Key)) {
        $Counts[$Key] = 0
    }
    $Counts[$Key] = [int]$Counts[$Key] + 1
}

function Read-Json([string]$Path) {
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
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

function Get-BoolValue([object]$Value) {
    if ($Value -is [bool]) {
        return [bool]$Value
    }
    if ($null -eq $Value) {
        return $false
    }
    $text = [string]$Value
    return $text -ieq "true" -or $text -eq "1"
}

function Get-IntValue([object]$Value) {
    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) {
        return 0
    }
    return [int]$Value
}

function Add-ByName([hashtable]$Map, [object[]]$Rows) {
    foreach ($row in @($Rows)) {
        $Map[[string]$row.n64_name] = $row
    }
}

function Get-TraceProperty([object]$Row, [string]$Container, [string]$Name) {
    if ($null -eq $Row) {
        return $null
    }
    $containerValue = Get-JsonValue $Row $Container
    if ($null -eq $containerValue) {
        return $null
    }
    return Get-JsonValue $containerValue $Name
}

function Get-N64AliasBaseCandidates([string]$N64Stem) {
    $candidates = New-Object "System.Collections.Generic.List[string]"
    if (-not [string]::IsNullOrWhiteSpace($N64Stem)) {
        $candidates.Add($N64Stem) | Out-Null
        if ($N64Stem.StartsWith("link_")) {
            $candidates.Add($N64Stem.Substring(5)) | Out-Null
        }
        if ($N64Stem.StartsWith("clink_")) {
            $candidates.Add($N64Stem.Substring(6)) | Out-Null
        }
        if ($N64Stem.StartsWith("kolink_")) {
            $candidates.Add($N64Stem.Substring(7)) | Out-Null
        }
    }
    return @($candidates.ToArray() | Select-Object -Unique)
}

function Get-SourceAliasEvidence([object]$Contract, [hashtable]$OneOffAliases) {
    if ($null -eq $Contract) {
        return [pscustomobject]@{
            accepted = $false
            evidence = "missing_semantic_contract"
        }
    }
    $n64Stem = [string]$Contract.n64_stem
    $sourceStem = [string]$Contract.source_oot3d_stem
    if (-not [string]::IsNullOrWhiteSpace($n64Stem) -and $n64Stem -eq $sourceStem) {
        return [pscustomobject]@{
            accepted = $true
            evidence = "exact_stem_match_$n64Stem"
        }
    }
    foreach ($candidate in @(Get-N64AliasBaseCandidates $n64Stem)) {
        if ($OneOffAliases.ContainsKey($candidate) -and [string]$OneOffAliases[$candidate] -eq $sourceStem) {
            return [pscustomobject]@{
                accepted = $true
                evidence = "explicit_one_off_alias_${candidate}_to_${sourceStem}"
            }
        }
    }
    return [pscustomobject]@{
        accepted = $false
        evidence = "no_explicit_alias_or_exact_stem_match"
    }
}

function Get-VariantTraceRow([string]$ReviewClass, [object[]]$TargetRows) {
    foreach ($row in @($TargetRows)) {
        $runtimeState = Get-JsonValue $row "route_runtime_state"
        if ($null -eq $runtimeState) {
            continue
        }
        switch ($ReviewClass) {
            "player_model_anim_group_climb_hold_variant" {
                if (
                    $null -ne (Get-JsonValue $runtimeState "model_anim_type") -and
                    $null -ne (Get-JsonValue $runtimeState "model_group") -and
                    $null -ne (Get-JsonValue $runtimeState "next_model_group") -and
                    $null -ne (Get-JsonValue $runtimeState "ledge_climb_type") -and
                    $null -ne (Get-JsonValue $row "right_hand_type") -and
                    $null -ne (Get-JsonValue $row "current_shield") -and
                    $null -ne (Get-JsonValue $row "sheath_type")
                ) {
                    return $row
                }
            }
            "player_model_anim_group_slope_slip_variant" {
                if (
                    $null -ne (Get-JsonValue $runtimeState "model_anim_type") -and
                    $null -ne (Get-JsonValue $runtimeState "model_group") -and
                    $null -ne (Get-JsonValue $runtimeState "next_model_group")
                ) {
                    return $row
                }
            }
            "player_water_ledge_climb_runtime_action" {
                if (
                    $null -ne (Get-JsonValue $runtimeState "ledge_climb_type") -or
                    $null -ne (Get-JsonValue $runtimeState "y_dist_to_ledge")
                ) {
                    return $row
                }
            }
            "player_swim_state_runtime_action" {
                if (
                    $null -ne (Get-JsonValue $runtimeState "action_func_key") -and
                    $null -ne (Get-JsonValue $runtimeState "model_group")
                ) {
                    return $row
                }
            }
            default {
                return $row
            }
        }
    }
    return $null
}

function Get-TraceEvidence([object]$RuntimeRow, [string]$N64Name, [string]$SourceStem, [string]$ReviewClass) {
    $path = if ($null -eq $RuntimeRow) { "" } else { [string]$RuntimeRow.expected_context_trace_path }
    $traceExists = -not [string]::IsNullOrWhiteSpace($path) -and (Test-Path -LiteralPath $path -PathType Leaf)
    $targetRows = @()
    $matchedRows = @()
    $resourceMatchedRows = @()
    $variantRow = $null
    $rowCount = 0
    if ($traceExists) {
        $trace = Read-Json $path
        $rowCount = Get-IntValue (Get-JsonValue $trace "row_count")
        $allRows = @($trace.rows)
        $targetRows = @(
            $allRows | Where-Object {
                [string](Get-JsonValue $_ "n64_animation_name") -eq $N64Name -or
                [string](Get-JsonValue $_ "expected_n64_animation_name") -eq $N64Name -or
                [string](Get-JsonValue $_ "normalized_n64_animation_name") -eq $N64Name
            }
        )
        $matchedRows = @(
            $targetRows | Where-Object {
                (Get-BoolValue (Get-JsonValue $_ "expected_n64_animation_matched")) -or
                [string](Get-JsonValue $_ "n64_animation_name") -eq $N64Name
            }
        )
        $resourceMatchedRows = @(
            $matchedRows | Where-Object {
                $resource = [string](Get-JsonValue $_ "animation_resource_path")
                $resource -like "*$N64Name*" -and $resource -like "*__from__$SourceStem*"
            }
        )
        $variantRow = Get-VariantTraceRow $ReviewClass $resourceMatchedRows
    }
    $selectedFrames = @($resourceMatchedRows | ForEach-Object { [string](Get-JsonValue $_ "selected_frame") } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)
    $forcedStaticRows = @($resourceMatchedRows | Where-Object { (Get-IntValue (Get-JsonValue $_ "forced_frame")) -ge 0 })
    return [pscustomobject]@{
        path = $path
        exists = $traceExists
        row_count = $rowCount
        target_row_count = $targetRows.Count
        matched_row_count = $matchedRows.Count
        resource_matched_row_count = $resourceMatchedRows.Count
        unique_selected_frame_count = $selectedFrames.Count
        forced_static_frame_row_count = $forcedStaticRows.Count
        variant_row = $variantRow
    }
}

$characterRoot = Join-Path $WorkRoot "character_conversion"
$callsiteSummaryPath = Join-Path $characterRoot "link_child_animation_pose_source_callsite_context.json"
$callsiteCsv = Join-Path $characterRoot "link_child_animation_pose_source_callsite_context.csv"
$semanticContractCsv = Join-Path $characterRoot "link_child_animation_semantic_resolution_contract.csv"
$dependencySummaryPath = Join-Path $characterRoot "pose_source_direct_callsite_dependency_policy\link_child_pose_source_direct_callsite_dependency_policy_summary.json"
$dependencyCsv = Join-Path $characterRoot "pose_source_direct_callsite_dependency_policy\link_child_pose_source_direct_callsite_dependency_policy.csv"
$runtimeSummaryPath = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix_summary.json"
$runtimeCsv = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix.csv"

Require-Path $callsiteSummaryPath "Pose/source callsite summary"
Require-Path $callsiteCsv "Pose/source callsite CSV"
Require-Path $semanticContractCsv "Semantic resolution contract CSV"
Require-Path $dependencySummaryPath "Pose/source direct-callsite dependency summary"
Require-Path $dependencyCsv "Pose/source direct-callsite dependency CSV"
Require-Path $runtimeSummaryPath "Runtime config capture matrix summary"
Require-Path $runtimeCsv "Runtime config capture matrix CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$callsiteSummary = Read-Json $callsiteSummaryPath
$dependencySummary = Read-Json $dependencySummaryPath
$runtimeSummary = Read-Json $runtimeSummaryPath
$callsiteRows = @(Import-Csv -LiteralPath $callsiteCsv)
$contractRows = @(Import-Csv -LiteralPath $semanticContractCsv)
$dependencyRows = @(Import-Csv -LiteralPath $dependencyCsv)
$runtimeRows = @(Import-Csv -LiteralPath $runtimeCsv)

$contractByName = @{}
$dependencyByName = @{}
$runtimeByName = @{}
Add-ByName $contractByName $contractRows
Add-ByName $dependencyByName $dependencyRows
Add-ByName $runtimeByName $runtimeRows

$oneOffAliases = @{
    "normal_jump_climb_hold" = "nml_hang_hold"
    "normal_jump_climb_hold_free" = "nml_hang_hold_free"
    "normal_jump_climb_up_free" = "nml_hang_up_free"
    "normal_jump_climb_wait" = "nml_hang_wait"
    "normal_jump_climb_wait_free" = "nml_hang_wait_free"
}

$directRows = @($callsiteRows | Where-Object { [string]$_.callsite_context_status -eq "direct_player_actor_callsite_found" })
$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$installedDynamicCandidateCount = 0
$runtimeTraceValidCount = 0
$runtimeVariantContextReadyCount = 0
$sourceAliasAcceptedCount = 0
$sourceIdentityAcceptedCount = 0
$semanticAcceptedCount = 0
$mappingPromotionAllowedCount = 0
$statusCounts = @{}
$reviewClassCounts = @{}
$sourceAliasEvidenceCounts = @{}
$runtimeVariantEvidenceCounts = @{}
$issueReasonCounts = @{}

foreach ($callsite in $directRows) {
    $n64Name = [string]$callsite.n64_name
    $contract = $contractByName[$n64Name]
    $dependency = $dependencyByName[$n64Name]
    $runtime = $runtimeByName[$n64Name]
    $issues = New-Object "System.Collections.Generic.List[object]"

    if ($null -eq $contract) {
        $issues.Add([pscustomobject]@{ reason = "missing_semantic_contract_row"; detail = $n64Name }) | Out-Null
    }
    if ($null -eq $dependency) {
        $issues.Add([pscustomobject]@{ reason = "missing_direct_callsite_dependency_row"; detail = $n64Name }) | Out-Null
    }
    if ($null -eq $runtime) {
        $issues.Add([pscustomobject]@{ reason = "missing_runtime_capture_row"; detail = $n64Name }) | Out-Null
    }
    if ((Get-IntValue $callsite.direct_player_actor_source_reference_count) -le 0) {
        $issues.Add([pscustomobject]@{ reason = "missing_direct_player_actor_reference"; detail = $n64Name }) | Out-Null
    }
    if ([string]$callsite.reference_envelope_status -ne "outside_reference_envelope") {
        $issues.Add([pscustomobject]@{ reason = "unexpected_reference_envelope_status"; detail = [string]$callsite.reference_envelope_status }) | Out-Null
    }

    $installedRuntimeValid = if ($null -eq $runtime) { $false } else { Get-BoolValue $runtime.installed_runtime_dump_valid }
    $dynamicPlaybackReady = if ($null -eq $runtime) { $false } else { Get-BoolValue $runtime.context_trace_dynamic_playback_ready }
    $auditValid = if ($null -eq $runtime) { $false } else { [string]$runtime.audit_status -eq "valid" -and (Get-IntValue $runtime.audit_issue_count) -eq 0 }
    if ($installedRuntimeValid) {
        $installedDynamicCandidateCount++
    }

    $sourceAlias = Get-SourceAliasEvidence $contract $oneOffAliases
    if ([bool]$sourceAlias.accepted) {
        $sourceAliasAcceptedCount++
    }
    Add-Count $sourceAliasEvidenceCounts ([string]$sourceAlias.evidence)

    $sourceStem = if ($null -eq $contract) { "" } else { [string]$contract.source_oot3d_stem }
    $traceEvidence = Get-TraceEvidence $runtime $n64Name $sourceStem ([string]$callsite.callsite_review_class)
    $runtimeTraceValid = (
        $installedRuntimeValid -and
        $dynamicPlaybackReady -and
        $auditValid -and
        [bool]$traceEvidence.exists -and
        [int]$traceEvidence.resource_matched_row_count -gt 0 -and
        [int]$traceEvidence.forced_static_frame_row_count -eq 0
    )
    if ($runtimeTraceValid) {
        $runtimeTraceValidCount++
    }
    elseif ($installedRuntimeValid) {
        if (-not $dynamicPlaybackReady) {
            $issues.Add([pscustomobject]@{ reason = "installed_runtime_trace_not_dynamic"; detail = $n64Name }) | Out-Null
        }
        if (-not $auditValid) {
            $issues.Add([pscustomobject]@{ reason = "runtime_capture_audit_not_valid"; detail = if ($null -eq $runtime) { "missing" } else { [string]$runtime.audit_status } }) | Out-Null
        }
        if (-not [bool]$traceEvidence.exists) {
            $issues.Add([pscustomobject]@{ reason = "context_trace_missing"; detail = [string]$traceEvidence.path }) | Out-Null
        }
        elseif ([int]$traceEvidence.resource_matched_row_count -le 0) {
            $issues.Add([pscustomobject]@{ reason = "context_trace_resource_identity_missing"; detail = $n64Name }) | Out-Null
        }
        if ([int]$traceEvidence.forced_static_frame_row_count -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "context_trace_has_forced_static_rows"; detail = [string]$traceEvidence.forced_static_frame_row_count }) | Out-Null
        }
    }

    $variantRow = $traceEvidence.variant_row
    $runtimeVariantReady = $runtimeTraceValid -and $null -ne $variantRow
    if ($runtimeVariantReady) {
        $runtimeVariantContextReadyCount++
    }
    $runtimeVariantEvidenceStatus = if ($runtimeVariantReady) {
        "runtime_variant_context_ready"
    }
    elseif ($installedRuntimeValid) {
        "runtime_variant_context_missing_or_unmatched"
    }
    else {
        "runtime_variant_context_pending_installed_dynamic_draw"
    }
    Add-Count $runtimeVariantEvidenceCounts $runtimeVariantEvidenceStatus

    if ($installedRuntimeValid -and -not [bool]$sourceAlias.accepted) {
        $issues.Add([pscustomobject]@{ reason = "source_alias_evidence_not_accepted"; detail = [string]$sourceAlias.evidence }) | Out-Null
    }
    if ($installedRuntimeValid -and -not $runtimeVariantReady) {
        $issues.Add([pscustomobject]@{ reason = "runtime_variant_context_not_ready"; detail = [string]$callsite.callsite_review_class }) | Out-Null
    }

    $sourceIdentityAccepted = (
        [int]$issues.Count -eq 0 -and
        $installedRuntimeValid -and
        $runtimeTraceValid -and
        $runtimeVariantReady -and
        [bool]$sourceAlias.accepted
    )
    if ($sourceIdentityAccepted) {
        $sourceIdentityAcceptedCount++
    }

    $reviewStatus = if ([int]$issues.Count -ne 0) {
        "source_identity_review_has_issues"
    }
    elseif ($sourceIdentityAccepted) {
        "source_identity_accepted_pending_semantic_acceptance"
    }
    elseif ($installedRuntimeValid) {
        "source_identity_blocked_pending_review_evidence"
    }
    else {
        "source_identity_blocked_pending_installed_dynamic_draw"
    }
    Add-Count $statusCounts $reviewStatus
    Add-Count $reviewClassCounts ([string]$callsite.callsite_review_class)
    foreach ($issue in @($issues.ToArray())) {
        Add-Count $issueReasonCounts ([string]$issue.reason)
    }
    $issueCount += [int]$issues.Count

    $semanticAcceptanceStatus = if ($sourceIdentityAccepted) {
        "source_identity_accepted_pending_semantic_policy"
    }
    else {
        "blocked_pending_source_identity_or_installed_draw"
    }
    $mappingPromotionAllowed = $false

    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        source_csab_name = [string]$callsite.source_csab_name
        source_oot3d_stem = $sourceStem
        output_csab_name = if ($null -eq $dependency) { "" } else { [string]$dependency.output_csab_name }
        callsite_review_class = [string]$callsite.callsite_review_class
        callsite_context_status = [string]$callsite.callsite_context_status
        direct_player_actor_source_reference_count = Get-IntValue $callsite.direct_player_actor_source_reference_count
        sample_callsite_contexts = [string]$callsite.sample_callsite_contexts
        source_alias_evidence = [string]$sourceAlias.evidence
        source_alias_evidence_accepted = [bool]$sourceAlias.accepted
        installed_runtime_dump_valid = $installedRuntimeValid
        runtime_trace_valid = $runtimeTraceValid
        runtime_variant_evidence_status = $runtimeVariantEvidenceStatus
        runtime_variant_context_ready = $runtimeVariantReady
        context_trace_path = [string]$traceEvidence.path
        context_trace_row_count = [int]$traceEvidence.row_count
        context_trace_target_row_count = [int]$traceEvidence.target_row_count
        context_trace_resource_matched_row_count = [int]$traceEvidence.resource_matched_row_count
        context_trace_unique_selected_frame_count = [int]$traceEvidence.unique_selected_frame_count
        context_trace_forced_static_frame_row_count = [int]$traceEvidence.forced_static_frame_row_count
        model_anim_type = if ($null -eq $variantRow) { "" } else { [string](Get-TraceProperty $variantRow "route_runtime_state" "model_anim_type") }
        model_group = if ($null -eq $variantRow) { "" } else { [string](Get-TraceProperty $variantRow "route_runtime_state" "model_group") }
        next_model_group = if ($null -eq $variantRow) { "" } else { [string](Get-TraceProperty $variantRow "route_runtime_state" "next_model_group") }
        ledge_climb_type = if ($null -eq $variantRow) { "" } else { [string](Get-TraceProperty $variantRow "route_runtime_state" "ledge_climb_type") }
        y_dist_to_ledge = if ($null -eq $variantRow) { "" } else { [string](Get-TraceProperty $variantRow "route_runtime_state" "y_dist_to_ledge") }
        dist_to_interact_wall = if ($null -eq $variantRow) { "" } else { [string](Get-TraceProperty $variantRow "route_runtime_state" "dist_to_interact_wall") }
        right_hand_type = if ($null -eq $variantRow) { "" } else { [string](Get-JsonValue $variantRow "right_hand_type") }
        current_shield = if ($null -eq $variantRow) { "" } else { [string](Get-JsonValue $variantRow "current_shield") }
        sheath_type = if ($null -eq $variantRow) { "" } else { [string](Get-JsonValue $variantRow "sheath_type") }
        source_identity_review_status = $reviewStatus
        source_identity_accepted = $sourceIdentityAccepted
        semantic_acceptance_status = $semanticAcceptanceStatus
        semantic_accepted = $false
        mapping_promotion_allowed = $mappingPromotionAllowed
        required_next_step = if ($sourceIdentityAccepted) { "semantic acceptance review using installed dynamic draw and accepted source identity" } else { [string]$callsite.required_next_step }
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray() | Sort-Object source_identity_review_status,n64_name)
$summaryPath = Join-Path $OutputRoot "link_child_direct_callsite_source_identity_review_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_direct_callsite_source_identity_review.csv"
$status = if ($issueCount -eq 0) { "direct_callsite_source_identity_review_ready" } else { "direct_callsite_source_identity_review_has_issues" }

$summary = [pscustomobject]@{
    format = "oot3d_link_child_direct_callsite_source_identity_review_v1"
    status = $status
    policy = [ordered]@{
        scope = "Link child residual rows with direct N64 player-actor callsite context"
        semantic_effect = "accepts source identity only when direct callsite, explicit alias/source evidence, installed dynamic draw, and runtime variant context all agree"
        promotion_effect = "does not accept final semantics and does not allow mapping promotion"
    }
    callsite_context_summary = (Resolve-Path -LiteralPath $callsiteSummaryPath).Path
    direct_callsite_dependency_summary = (Resolve-Path -LiteralPath $dependencySummaryPath).Path
    runtime_config_capture_matrix_summary = (Resolve-Path -LiteralPath $runtimeSummaryPath).Path
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    record_count = $recordArray.Count
    upstream_callsite_row_count = Get-IntValue $callsiteSummary.row_count
    upstream_dependency_row_count = Get-IntValue $dependencySummary.record_count
    upstream_runtime_capture_row_count = Get-IntValue $runtimeSummary.record_count
    installed_dynamic_review_candidate_count = $installedDynamicCandidateCount
    runtime_trace_valid_count = $runtimeTraceValidCount
    runtime_variant_context_ready_count = $runtimeVariantContextReadyCount
    source_alias_evidence_accepted_count = $sourceAliasAcceptedCount
    source_identity_accepted_count = $sourceIdentityAcceptedCount
    semantic_accepted_count = $semanticAcceptedCount
    mapping_promotion_allowed_count = $mappingPromotionAllowedCount
    issue_count = $issueCount
    source_identity_review_status_counts = $statusCounts
    callsite_review_class_counts = $reviewClassCounts
    source_alias_evidence_counts = $sourceAliasEvidenceCounts
    runtime_variant_evidence_status_counts = $runtimeVariantEvidenceCounts
    issue_reason_counts = $issueReasonCounts
    records = $recordArray
}

$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8
$recordArray |
    Select-Object n64_name,source_csab_name,source_oot3d_stem,output_csab_name,callsite_review_class,callsite_context_status,direct_player_actor_source_reference_count,sample_callsite_contexts,source_alias_evidence,source_alias_evidence_accepted,installed_runtime_dump_valid,runtime_trace_valid,runtime_variant_evidence_status,runtime_variant_context_ready,context_trace_path,context_trace_row_count,context_trace_target_row_count,context_trace_resource_matched_row_count,context_trace_unique_selected_frame_count,context_trace_forced_static_frame_row_count,model_anim_type,model_group,next_model_group,ledge_climb_type,y_dist_to_ledge,dist_to_interact_wall,right_hand_type,current_shield,sheath_type,source_identity_review_status,source_identity_accepted,semantic_acceptance_status,semantic_accepted,mapping_promotion_allowed,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_direct_callsite_source_identity_review_v1") "unexpected direct-callsite source identity review format"
    Assert-Condition ($summary.status -eq "direct_callsite_source_identity_review_ready") "expected source identity review ready"
    Assert-Condition ([int]$summary.record_count -eq 16) "expected 16 direct-callsite review rows"
    Assert-Condition ([int]$summary.upstream_dependency_row_count -eq 16) "expected 16 direct-callsite dependency rows"
    Assert-Condition ([int]$summary.upstream_runtime_capture_row_count -eq 63) "expected 63 runtime capture rows"
    Assert-Condition ([int]$summary.installed_dynamic_review_candidate_count -eq 1) "expected one installed dynamic direct-callsite review candidate"
    Assert-Condition ([int]$summary.runtime_trace_valid_count -eq 1) "expected one valid installed runtime trace"
    Assert-Condition ([int]$summary.runtime_variant_context_ready_count -eq 1) "expected one runtime variant context ready row"
    Assert-Condition ([int]$summary.source_identity_accepted_count -eq 1) "expected one accepted direct-callsite source identity"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected no semantic acceptances"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected no mapping promotions"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero source identity review issues"
    Assert-Condition ((Get-JsonValue $summary.source_identity_review_status_counts "source_identity_accepted_pending_semantic_acceptance") -eq 1) "expected one accepted source identity pending semantics"
    Assert-Condition ((Get-JsonValue $summary.source_identity_review_status_counts "source_identity_blocked_pending_installed_dynamic_draw") -eq 15) "expected fifteen direct-callsite rows still pending installed dynamic draw"
    Assert-Condition ((Get-JsonValue $summary.runtime_variant_evidence_status_counts "runtime_variant_context_ready") -eq 1) "expected one runtime variant context ready status"
    Require-Path $csvPath "Direct-callsite source identity review CSV"
}

Write-Host "OOT3D Link child direct-callsite source identity review: $summaryPath"
Write-Host "status=$($summary.status) rows=$($summary.record_count) installedCandidates=$($summary.installed_dynamic_review_candidate_count) sourceIdentityAccepted=$($summary.source_identity_accepted_count) semanticAccepted=$($summary.semantic_accepted_count) promotions=$($summary.mapping_promotion_allowed_count) issues=$($summary.issue_count)"
