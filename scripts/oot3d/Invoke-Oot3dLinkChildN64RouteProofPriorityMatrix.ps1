param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\n64_route_proof_priority_matrix"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child N64 route proof priority verification failed: $Message"
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

function Split-ListValue([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return @()
    }
    return @($Value -split ";" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function New-RouteRecord(
    [object]$Closure,
    [string]$Bucket,
    [int]$Rank,
    [string]$RouteProofStatus,
    [string]$EvidenceKind,
    [int]$DirectRefCount,
    [int]$FamilyRefCount,
    [int]$CandidateOwnerRefCount,
    [int]$SourceWindowCount,
    [string]$RouteContext,
    [string]$RuntimeTrigger,
    [string]$OfflineParityStatus,
    [string]$RuntimeConfigStatus,
    [bool]$RuntimeHarnessValid,
    [string]$RouteProofBlocker,
    [string]$RequiredNextStep,
    [object[]]$Issues
) {
    return [pscustomobject]@{
        n64_name = [string]$Closure.n64_name
        source_csab_name = [string]$Closure.source_csab_name
        frontier_class = [string]$Closure.frontier_class
        frontier_gate = [string]$Closure.frontier_gate
        workorder_kind = [string]$Closure.workorder_kind
        post_harness_blocker = [string]$Closure.post_harness_blocker
        route_proof_bucket = $Bucket
        route_proof_rank = $Rank
        route_proof_status = $RouteProofStatus
        route_proof_blocker = $RouteProofBlocker
        n64_route_evidence_kind = $EvidenceKind
        direct_player_actor_reference_count = $DirectRefCount
        family_or_sibling_reference_count = $FamilyRefCount
        candidate_owner_reference_count = $CandidateOwnerRefCount
        source_window_count = $SourceWindowCount
        route_context = $RouteContext
        runtime_trigger = $RuntimeTrigger
        offline_runtime_parity_status = $OfflineParityStatus
        runtime_config_status = $RuntimeConfigStatus
        runtime_harness_dump_valid = $RuntimeHarnessValid
        installed_runtime_dump_valid = Get-BoolValue $Closure.installed_runtime_dump_valid
        installed_static_frame_dump_valid = Get-BoolValue $Closure.installed_static_frame_dump_valid
        runtime_valid_evidence = ($RuntimeHarnessValid -or (Get-BoolValue $Closure.installed_runtime_dump_valid) -or (Get-BoolValue $Closure.installed_static_frame_dump_valid))
        semantic_acceptance_status = "blocked_pending_route_proof_policy_or_installed_draw"
        mapping_promotion_allowed = $false
        required_next_step = $RequiredNextStep
        issue_count = [int]$Issues.Count
        issues = @($Issues)
    }
}

$characterRoot = Join-Path $WorkRoot "character_conversion"
$closureSummaryPath = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix_summary.json"
$closureCsv = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix.csv"
$routeProvenCsv = Join-Path $characterRoot "route_proven_ownership_policy\link_child_route_proven_ownership_policy.csv"
$poseSourceCsv = Join-Path $characterRoot "link_child_animation_pose_source_callsite_context.csv"
$routeRuntimeCsv = Join-Path $characterRoot "route_runtime_dependency_policy\link_child_route_runtime_dependency_policy.csv"
$anonymousCsv = Join-Path $characterRoot "anonymous_identity_owner_dependency_policy\link_child_anonymous_identity_owner_dependency_policy.csv"
$promotedAliasCsv = Join-Path $characterRoot "promoted_owner_alias_policy\link_child_promoted_owner_alias_policy.csv"
$routeAbsentCsv = Join-Path $characterRoot "route_absent_ownership_reuse_policy\link_child_route_absent_ownership_reuse_policy.csv"
$sourceRiskCsv = Join-Path $characterRoot "ownership_source_risk_dependency_policy\link_child_ownership_source_risk_dependency_policy.csv"

Require-Path $closureSummaryPath "Semantic frontier closure summary"
Require-Path $closureCsv "Semantic frontier closure CSV"
Require-Path $routeProvenCsv "Route-proven ownership policy CSV"
Require-Path $poseSourceCsv "Pose/source callsite context CSV"
Require-Path $routeRuntimeCsv "Route/runtime dependency policy CSV"
Require-Path $anonymousCsv "Anonymous identity/owner dependency policy CSV"
Require-Path $promotedAliasCsv "Promoted owner alias policy CSV"
Require-Path $routeAbsentCsv "Route-absent ownership/reuse policy CSV"
Require-Path $sourceRiskCsv "Ownership source-risk dependency policy CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$closureSummary = Read-Json $closureSummaryPath
$closureRows = @(Import-Csv -LiteralPath $closureCsv)
$routeProvenRows = @(Import-Csv -LiteralPath $routeProvenCsv)
$poseSourceRows = @(Import-Csv -LiteralPath $poseSourceCsv)
$routeRuntimeRows = @(Import-Csv -LiteralPath $routeRuntimeCsv)
$anonymousRows = @(Import-Csv -LiteralPath $anonymousCsv)
$promotedAliasRows = @(Import-Csv -LiteralPath $promotedAliasCsv)
$routeAbsentRows = @(Import-Csv -LiteralPath $routeAbsentCsv)
$sourceRiskRows = @(Import-Csv -LiteralPath $sourceRiskCsv)

$routeProvenByName = @{}
$poseSourceByName = @{}
$routeRuntimeByName = @{}
$anonymousByName = @{}
$promotedAliasByName = @{}
$routeAbsentByName = @{}
$sourceRiskByName = @{}
Add-ByName $routeProvenByName $routeProvenRows
Add-ByName $poseSourceByName $poseSourceRows
Add-ByName $routeRuntimeByName $routeRuntimeRows
Add-ByName $anonymousByName $anonymousRows
Add-ByName $promotedAliasByName $promotedAliasRows
Add-ByName $routeAbsentByName $routeAbsentRows
Add-ByName $sourceRiskByName $sourceRiskRows

$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$directReferenceTotal = 0
$familyReferenceTotal = 0
$candidateOwnerReferenceTotal = 0
$sourceWindowTotal = 0
$exactDirectRouteCount = 0
$directCallsiteContextCount = 0
$familyOrSiblingRouteCount = 0
$anonymousOwnerContextCount = 0
$promotedOwnerAliasCount = 0
$routeAbsentPolicyOnlyCount = 0
$sourceRiskNoRouteCount = 0
$harnessValidCount = 0
$installedRuntimeDumpValidCount = 0
$installedStaticFrameDumpValidCount = 0
$runtimeValidEvidenceCount = 0
$mappingPromotionAllowedCount = 0
$semanticAcceptedCount = 0
$bucketCounts = @{}
$statusCounts = @{}
$evidenceKindCounts = @{}
$blockerCounts = @{}
$issueReasonCounts = @{}

foreach ($closure in $closureRows) {
    $n64Name = [string]$closure.n64_name
    $issues = New-Object "System.Collections.Generic.List[object]"
    $record = $null
    $closureRuntimeHarnessValid = Get-BoolValue $closure.runtime_harness_dump_valid
    $closureInstalledRuntimeValid = Get-BoolValue $closure.installed_runtime_dump_valid
    $closureInstalledStaticFrameValid = Get-BoolValue $closure.installed_static_frame_dump_valid

    if ($routeProvenByName.ContainsKey($n64Name)) {
        $row = $routeProvenByName[$n64Name]
        $directRefs = Get-IntValue $row.direct_player_actor_source_reference_count
        if ($directRefs -le 0) {
            $issues.Add([pscustomobject]@{ reason = "route_proven_without_direct_refs"; detail = $n64Name }) | Out-Null
        }
        if ([string]$row.policy_decision_status -notlike "route_callsite_policy_ready*") {
            $issues.Add([pscustomobject]@{ reason = "route_proven_policy_not_ready"; detail = [string]$row.policy_decision_status }) | Out-Null
        }
        $record = New-RouteRecord $closure `
            "exact_direct_n64_route_proven" 1 `
            "route_proof_ready_pending_installed_draw" `
            ([string]$row.callsite_review_classes) `
            $directRefs 0 0 0 `
            ([string]$row.route_context_names) `
            ([string]$row.callsite_review_classes) `
            ([string]$row.offline_runtime_parity_status) `
            "ready_for_capture" `
            $closureRuntimeHarnessValid `
            "needs_non_harness_installed_draw_before_replacement" `
            ([string]$row.required_next_step) `
            @($issues.ToArray())
        $exactDirectRouteCount++
    }
    elseif ($poseSourceByName.ContainsKey($n64Name)) {
        $row = $poseSourceByName[$n64Name]
        $directRefs = Get-IntValue $row.direct_player_actor_source_reference_count
        if ($directRefs -le 0) {
            $issues.Add([pscustomobject]@{ reason = "pose_source_without_direct_refs"; detail = $n64Name }) | Out-Null
        }
        if ([string]$row.callsite_context_status -ne "direct_player_actor_callsite_found") {
            $issues.Add([pscustomobject]@{ reason = "pose_source_callsite_status_mismatch"; detail = [string]$row.callsite_context_status }) | Out-Null
        }
        $poseSourceRouteStatus = if ($closureInstalledRuntimeValid) {
            "direct_context_ready_with_installed_draw_pending_source_identity"
        }
        else {
            "direct_context_ready_pending_source_identity_or_installed_draw"
        }
        $poseSourceBlocker = if ($closureInstalledRuntimeValid) {
            "source_identity_review_required_after_installed_draw"
        }
        else {
            "source_identity_or_pose_source_risk_not_accepted"
        }
        $poseSourceRequiredNextStep = if ($closureInstalledRuntimeValid) {
            "review pose/source identity and semantic acceptance using installed dynamic draw evidence"
        }
        else {
            [string]$row.required_next_step
        }
        $record = New-RouteRecord $closure `
            "direct_n64_player_callsite_context" 2 `
            $poseSourceRouteStatus `
            ([string]$row.callsite_review_class) `
            $directRefs 0 0 0 `
            ([string]$row.sample_callsite_contexts) `
            ([string]$row.next_evidence) `
            "valid" `
            "ready_for_capture" `
            $closureRuntimeHarnessValid `
            $poseSourceBlocker `
            $poseSourceRequiredNextStep `
            @($issues.ToArray())
        $directCallsiteContextCount++
    }
    elseif ($routeRuntimeByName.ContainsKey($n64Name)) {
        $row = $routeRuntimeByName[$n64Name]
        $familyRefs = Get-IntValue $row.family_or_sibling_reference_count
        $sourceWindows = Get-IntValue $row.source_window_count
        if ((Get-IntValue $row.exact_direct_player_actor_reference_count) -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "route_runtime_has_unexpected_exact_refs"; detail = [string]$row.exact_direct_player_actor_reference_count }) | Out-Null
        }
        if ($familyRefs -le 0) {
            $issues.Add([pscustomobject]@{ reason = "route_runtime_missing_family_refs"; detail = $n64Name }) | Out-Null
        }
        $record = New-RouteRecord $closure `
            "family_or_sibling_n64_route_only" 3 `
            "indirect_route_dependency_ready_pending_route_policy_or_installed_draw" `
            ([string]$row.evidence_kind) `
            0 $familyRefs 0 $sourceWindows `
            ([string]$row.route_context_class) `
            ([string]$row.route_trigger) `
            ([string]$row.offline_runtime_parity_status) `
            ([string]$row.runtime_config_status) `
            $closureRuntimeHarnessValid `
            "exact_symbol_route_absent" `
            ([string]$row.required_next_step) `
            @($issues.ToArray())
        $familyOrSiblingRouteCount++
    }
    elseif ($anonymousByName.ContainsKey($n64Name)) {
        $row = $anonymousByName[$n64Name]
        $candidateRefs = Get-IntValue $row.candidate_owner_symbol_reference_count
        if ((Get-IntValue $row.direct_anonymous_symbol_reference_count) -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "anonymous_has_direct_symbol_refs"; detail = [string]$row.direct_anonymous_symbol_reference_count }) | Out-Null
        }
        if ($candidateRefs -le 0) {
            $issues.Add([pscustomobject]@{ reason = "anonymous_missing_candidate_owner_refs"; detail = $n64Name }) | Out-Null
        }
        $record = New-RouteRecord $closure `
            "n64_table_identity_candidate_owner_context" 4 `
            "table_owner_context_ready_pending_owner_policy_or_installed_draw" `
            ([string]$row.owner_context_class) `
            0 0 $candidateRefs 0 `
            ([string]$row.candidate_owner_symbols) `
            ([string]$row.runtime_trigger) `
            ([string]$row.offline_runtime_parity_status) `
            ([string]$row.runtime_config_status) `
            $closureRuntimeHarnessValid `
            "anonymous_symbol_has_no_direct_route" `
            ([string]$row.required_next_step) `
            @($issues.ToArray())
        $anonymousOwnerContextCount++
    }
    elseif ($promotedAliasByName.ContainsKey($n64Name)) {
        $row = $promotedAliasByName[$n64Name]
        if ([string]::IsNullOrWhiteSpace([string]$row.promoted_owner_n64_name)) {
            $issues.Add([pscustomobject]@{ reason = "promoted_alias_missing_owner"; detail = $n64Name }) | Out-Null
        }
        $record = New-RouteRecord $closure `
            "promoted_owner_route_delegated_alias" 5 `
            "promoted_owner_alias_policy_ready_pending_installed_draw" `
            "promoted_runtime_owner_alias" `
            0 0 0 0 `
            ([string]$row.promoted_owner_n64_name) `
            "drive alias natural player context" `
            "valid" `
            "ready_for_capture" `
            $closureRuntimeHarnessValid `
            "alias_symbol_route_not_directly_proven" `
            ([string]$row.required_next_step) `
            @($issues.ToArray())
        $promotedOwnerAliasCount++
    }
    elseif ($routeAbsentByName.ContainsKey($n64Name)) {
        $row = $routeAbsentByName[$n64Name]
        $ownerNames = @(Split-ListValue ([string]$row.candidate_csab_collision_n64_names)) + @(Split-ListValue ([string]$row.runtime_csab_collision_n64_names))
        if ([string]$row.policy_decision_status -notlike "*policy_ready*") {
            $issues.Add([pscustomobject]@{ reason = "route_absent_policy_not_ready"; detail = [string]$row.policy_decision_status }) | Out-Null
        }
        $record = New-RouteRecord $closure `
            "route_absent_owner_policy_only" 6 `
            "owner_policy_ready_but_n64_route_absent" `
            ([string]$row.policy_family) `
            0 0 $ownerNames.Count 0 `
            ([string]$row.source_group_n64_names) `
            ([string]$row.policy_decision_scope) `
            ([string]$row.offline_runtime_parity_status) `
            ([string]$row.runtime_config_status) `
            $closureRuntimeHarnessValid `
            "no_direct_n64_route_proof" `
            ([string]$row.required_next_step) `
            @($issues.ToArray())
        $routeAbsentPolicyOnlyCount++
    }
    elseif ($sourceRiskByName.ContainsKey($n64Name)) {
        $row = $sourceRiskByName[$n64Name]
        $record = New-RouteRecord $closure `
            "source_risk_no_route_proof" 7 `
            "source_risk_dependency_ready_pending_alternate_source_or_installed_draw" `
            ([string]$row.derivative_frontier_class) `
            0 0 0 0 `
            ([string]$row.frontier_class) `
            ([string]$row.acceptance_gate) `
            ([string]$row.offline_runtime_parity_status) `
            ([string]$row.runtime_config_status) `
            $closureRuntimeHarnessValid `
            "alternate_source_or_natural_capture_required" `
            ([string]$row.required_next_step) `
            @($issues.ToArray())
        $sourceRiskNoRouteCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "row_not_found_in_route_priority_inputs"; detail = $n64Name }) | Out-Null
        $record = New-RouteRecord $closure `
            "unclassified_route_proof" 99 `
            "route_proof_unclassified" `
            "" 0 0 0 0 "" "" "" "" $false `
            "missing_route_priority_input" `
            "classify this residual row before replacement" `
            @($issues.ToArray())
    }

    if ([bool]$record.runtime_harness_dump_valid) {
        $harnessValidCount++
    }
    if ([bool]$record.installed_runtime_dump_valid) {
        $installedRuntimeDumpValidCount++
    }
    if ([bool]$record.installed_static_frame_dump_valid) {
        $installedStaticFrameDumpValidCount++
    }
    if ([bool]$record.runtime_valid_evidence) {
        $runtimeValidEvidenceCount++
    }
    if ([bool]$record.mapping_promotion_allowed) {
        $mappingPromotionAllowedCount++
    }
    $directReferenceTotal += Get-IntValue $record.direct_player_actor_reference_count
    $familyReferenceTotal += Get-IntValue $record.family_or_sibling_reference_count
    $candidateOwnerReferenceTotal += Get-IntValue $record.candidate_owner_reference_count
    $sourceWindowTotal += Get-IntValue $record.source_window_count
    Add-Count $bucketCounts ([string]$record.route_proof_bucket)
    Add-Count $statusCounts ([string]$record.route_proof_status)
    Add-Count $evidenceKindCounts ([string]$record.n64_route_evidence_kind)
    Add-Count $blockerCounts ([string]$record.route_proof_blocker)
    foreach ($issue in @($record.issues)) {
        Add-Count $issueReasonCounts ([string]$issue.reason)
    }
    $issueCount += [int]$record.issue_count
    $records.Add($record) | Out-Null
}

$recordArray = @($records.ToArray() | Sort-Object route_proof_rank,n64_name)
$summaryPath = Join-Path $OutputRoot "link_child_n64_route_proof_priority_matrix_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_n64_route_proof_priority_matrix.csv"
$status = if ($issueCount -eq 0) { "n64_route_proof_priority_ready" } else { "n64_route_proof_priority_has_issues" }

$summary = [pscustomobject]@{
    format = "oot3d_link_child_n64_route_proof_priority_matrix_v1"
    status = $status
    policy = [ordered]@{
        scope = "Route-proof priority view over all Link child residual N64 PlayerAnimation rows"
        semantic_effect = "prioritizes N64-side route proof work only; no semantic acceptances or mapping promotions are granted"
        replacement_gate = "safe in-game replacement still requires accepted route/owner policy and installed runtime draw proof for every residual row"
    }
    semantic_frontier_closure_summary = (Resolve-Path -LiteralPath $closureSummaryPath).Path
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    record_count = $recordArray.Count
    closure_record_count = Get-IntValue (Get-JsonValue $closureSummary "record_count")
    exact_direct_route_proven_count = $exactDirectRouteCount
    direct_player_actor_callsite_context_count = $directCallsiteContextCount
    direct_n64_route_or_callsite_context_count = $exactDirectRouteCount + $directCallsiteContextCount
    family_or_sibling_route_only_count = $familyOrSiblingRouteCount
    anonymous_table_owner_context_count = $anonymousOwnerContextCount
    promoted_owner_alias_delegated_count = $promotedOwnerAliasCount
    route_absent_policy_only_count = $routeAbsentPolicyOnlyCount
    source_risk_no_route_proof_count = $sourceRiskNoRouteCount
    direct_player_actor_reference_count = $directReferenceTotal
    family_or_sibling_reference_count = $familyReferenceTotal
    candidate_owner_reference_count = $candidateOwnerReferenceTotal
    source_window_count = $sourceWindowTotal
    runtime_harness_dump_valid_count = $harnessValidCount
    installed_runtime_dump_valid_count = $installedRuntimeDumpValidCount
    installed_static_frame_dump_valid_count = $installedStaticFrameDumpValidCount
    runtime_valid_evidence_count = $runtimeValidEvidenceCount
    semantic_accepted_count = $semanticAcceptedCount
    mapping_promotion_allowed_count = $mappingPromotionAllowedCount
    issue_count = $issueCount
    route_proof_bucket_counts = $bucketCounts
    route_proof_status_counts = $statusCounts
    n64_route_evidence_kind_counts = $evidenceKindCounts
    route_proof_blocker_counts = $blockerCounts
    issue_reason_counts = $issueReasonCounts
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,frontier_class,frontier_gate,workorder_kind,post_harness_blocker,route_proof_bucket,route_proof_rank,route_proof_status,route_proof_blocker,n64_route_evidence_kind,direct_player_actor_reference_count,family_or_sibling_reference_count,candidate_owner_reference_count,source_window_count,route_context,runtime_trigger,offline_runtime_parity_status,runtime_config_status,runtime_harness_dump_valid,installed_runtime_dump_valid,installed_static_frame_dump_valid,runtime_valid_evidence,semantic_acceptance_status,mapping_promotion_allowed,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_n64_route_proof_priority_matrix_v1") "unexpected route proof priority format"
    Assert-Condition ($summary.status -eq "n64_route_proof_priority_ready") "expected route proof priority matrix ready"
    Assert-Condition ([int]$summary.record_count -eq 63) "expected 63 residual rows"
    Assert-Condition ([int]$summary.closure_record_count -eq 63) "expected closure matrix to cover 63 residual rows"
    Assert-Condition ([int]$summary.exact_direct_route_proven_count -eq 5) "expected five exact direct route-proven rows"
    Assert-Condition ([int]$summary.direct_player_actor_callsite_context_count -eq 16) "expected sixteen direct player callsite context rows"
    Assert-Condition ([int]$summary.direct_n64_route_or_callsite_context_count -eq 21) "expected 21 direct N64 route/callsite-context rows"
    Assert-Condition ([int]$summary.family_or_sibling_route_only_count -eq 8) "expected eight family/sibling route-only rows"
    Assert-Condition ([int]$summary.anonymous_table_owner_context_count -eq 11) "expected eleven anonymous table-owner context rows"
    Assert-Condition ([int]$summary.promoted_owner_alias_delegated_count -eq 7) "expected seven promoted-owner alias rows"
    Assert-Condition ([int]$summary.route_absent_policy_only_count -eq 14) "expected fourteen route-absent owner policy rows"
    Assert-Condition ([int]$summary.source_risk_no_route_proof_count -eq 2) "expected two source-risk rows without route proof"
    Assert-Condition ([int]$summary.direct_player_actor_reference_count -eq 47) "expected 47 direct player-actor source references"
    Assert-Condition ([int]$summary.family_or_sibling_reference_count -eq 54) "expected 54 family/sibling source references"
    Assert-Condition ([int]$summary.candidate_owner_reference_count -eq 31) "expected 31 candidate-owner or route-absent owner references"
    Assert-Condition ([int]$summary.source_window_count -eq 54) "expected 54 route/runtime source windows"
    Assert-Condition ([int]$summary.runtime_valid_evidence_count -eq 63) "expected 63 runtime-valid residual rows"
    Assert-Condition ([int]$summary.runtime_harness_dump_valid_count -eq 62) "expected 62 harness-valid residual rows"
    Assert-Condition ([int]$summary.installed_runtime_dump_valid_count -eq 1) "expected one dynamic installed runtime dump in route priority matrix"
    Assert-Condition ([int]$summary.installed_static_frame_dump_valid_count -eq 0) "expected no static-frame runtime dumps in route priority matrix"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected no semantic acceptances"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected no mapping promotions"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero route proof priority issues"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "exact_direct_n64_route_proven") -eq 5) "expected five exact direct route bucket rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "direct_n64_player_callsite_context") -eq 16) "expected sixteen direct callsite bucket rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "family_or_sibling_n64_route_only") -eq 8) "expected eight family/sibling route bucket rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "n64_table_identity_candidate_owner_context") -eq 11) "expected eleven anonymous table-owner rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "promoted_owner_route_delegated_alias") -eq 7) "expected seven promoted-owner alias rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "route_absent_owner_policy_only") -eq 14) "expected fourteen route-absent policy rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "source_risk_no_route_proof") -eq 2) "expected two source-risk route proof rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_status_counts "direct_context_ready_pending_source_identity_or_installed_draw") -eq 15) "expected fifteen direct callsite rows pending source identity or installed draw"
    Assert-Condition ((Get-JsonValue $summary.route_proof_status_counts "direct_context_ready_with_installed_draw_pending_source_identity") -eq 1) "expected one direct callsite row with installed draw pending source identity"
    Require-Path $csvPath "N64 route proof priority CSV"
}

Write-Host "OOT3D Link child N64 route proof priority matrix: $summaryPath"
Write-Host "status=$($summary.status) rows=$($summary.record_count) directRows=$($summary.direct_n64_route_or_callsite_context_count) familyRows=$($summary.family_or_sibling_route_only_count) anonymousRows=$($summary.anonymous_table_owner_context_count) routeAbsent=$($summary.route_absent_policy_only_count) runtimeValid=$($summary.runtime_valid_evidence_count) installedValid=$($summary.installed_runtime_dump_valid_count) staticFrameValid=$($summary.installed_static_frame_dump_valid_count) issues=$($summary.issue_count)"
