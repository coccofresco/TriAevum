param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\in_game_replacement_gate"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child in-game replacement gate verification failed: $Message"
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

function Get-RouteOrOwnerPolicyGateStatus([string]$Bucket, [bool]$N64RouteAccepted, [bool]$RouteOrOwnerPolicyReady) {
    switch ($Bucket) {
        "exact_direct_n64_route_proven" {
            if ($N64RouteAccepted -and $RouteOrOwnerPolicyReady) {
                return "direct_route_and_callsite_policy_ready_pending_installed_draw"
            }
            if ($N64RouteAccepted) {
                return "direct_route_accepted_pending_callsite_policy_or_installed_draw"
            }
            return "direct_route_dependency_ready"
        }
        "direct_n64_player_callsite_context" { return "direct_callsite_dependency_ready_pending_source_identity" }
        "family_or_sibling_n64_route_only" { return "family_route_dependency_ready_pending_exact_route_policy" }
        "n64_table_identity_candidate_owner_context" { return "owner_context_dependency_ready_pending_identity_policy" }
        "promoted_owner_route_delegated_alias" { return "alias_owner_dependency_ready_pending_installed_draw" }
        "route_absent_owner_policy_only" { return "owner_policy_ready_but_route_absent" }
        "source_risk_no_route_proof" { return "source_risk_dependency_ready_pending_alternate_source" }
        default { return "route_or_owner_policy_unclassified" }
    }
}

function Get-SourceIdentityGateStatus([string]$Bucket, [bool]$SourceIdentityAccepted, [bool]$SemanticAccepted) {
    if ($SemanticAccepted) {
        return "source_identity_accepted"
    }
    if ($SourceIdentityAccepted) {
        return "source_identity_accepted_pending_semantic_acceptance"
    }
    switch ($Bucket) {
        "direct_n64_player_callsite_context" { return "source_identity_not_accepted" }
        "n64_table_identity_candidate_owner_context" { return "owner_identity_not_accepted" }
        "route_absent_owner_policy_only" { return "owner_policy_not_sufficient_for_replacement" }
        "source_risk_no_route_proof" { return "alternate_source_not_resolved" }
        default { return "source_identity_or_policy_not_accepted" }
    }
}

function Get-NextReplacementBlocker([string]$Bucket, [bool]$InstalledRuntimeDumpValid, [bool]$N64RouteAccepted, [bool]$RouteOrOwnerPolicyReady, [bool]$SourceIdentityAccepted) {
    switch ($Bucket) {
        "exact_direct_n64_route_proven" {
            if ($N64RouteAccepted -and $RouteOrOwnerPolicyReady) {
                return $(if ($InstalledRuntimeDumpValid) { "semantic_acceptance_or_mapping_promotion_required_after_installed_draw" } else { "installed_draw_required" })
            }
            return $(if ($InstalledRuntimeDumpValid) { "replacement_policy_required_after_installed_draw" } else { "installed_draw_or_replacement_policy_required" })
        }
        "direct_n64_player_callsite_context" {
            if ($InstalledRuntimeDumpValid -and $SourceIdentityAccepted) {
                return "semantic_acceptance_or_mapping_promotion_required_after_source_identity"
            }
            return $(if ($InstalledRuntimeDumpValid) { "source_identity_review_required_after_installed_draw" } else { "source_identity_or_installed_draw_required" })
        }
        "family_or_sibling_n64_route_only" { return $(if ($InstalledRuntimeDumpValid) { "exact_route_policy_required_after_installed_draw" } else { "exact_route_policy_or_family_route_acceptance_required" }) }
        "n64_table_identity_candidate_owner_context" { return $(if ($InstalledRuntimeDumpValid) { "anonymous_owner_identity_policy_required_after_installed_draw" } else { "anonymous_owner_identity_policy_or_installed_draw_required" }) }
        "promoted_owner_route_delegated_alias" { return $(if ($InstalledRuntimeDumpValid) { "alias_owner_policy_required_after_installed_draw" } else { "alias_owner_policy_or_installed_draw_required" }) }
        "route_absent_owner_policy_only" { return $(if ($InstalledRuntimeDumpValid) { "n64_route_absent_owner_policy_required_after_installed_draw" } else { "n64_route_absent_owner_policy_and_installed_draw_required" }) }
        "source_risk_no_route_proof" { return $(if ($InstalledRuntimeDumpValid) { "alternate_source_review_required_after_installed_draw" } else { "alternate_source_or_installed_draw_required" }) }
        default { return "route_proof_classification_required" }
    }
}

$characterRoot = Join-Path $WorkRoot "character_conversion"
$routeSummaryPath = Join-Path $characterRoot "n64_route_proof_priority_matrix\link_child_n64_route_proof_priority_matrix_summary.json"
$routeCsv = Join-Path $characterRoot "n64_route_proof_priority_matrix\link_child_n64_route_proof_priority_matrix.csv"
$routeAcceptanceSummaryPath = Join-Path $characterRoot "n64_route_proof_acceptance_policy\link_child_n64_route_proof_acceptance_policy_summary.json"
$routeAcceptanceCsv = Join-Path $characterRoot "n64_route_proof_acceptance_policy\link_child_n64_route_proof_acceptance_policy.csv"
$runtimeSummaryPath = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix_summary.json"
$runtimeCsv = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix.csv"
$closureSummaryPath = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix_summary.json"

Require-Path $routeSummaryPath "N64 route proof priority summary"
Require-Path $routeCsv "N64 route proof priority CSV"
Require-Path $routeAcceptanceSummaryPath "N64 route proof acceptance policy summary"
Require-Path $routeAcceptanceCsv "N64 route proof acceptance policy CSV"
Require-Path $runtimeSummaryPath "Runtime config capture matrix summary"
Require-Path $runtimeCsv "Runtime config capture matrix CSV"
Require-Path $closureSummaryPath "Semantic frontier closure summary"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$routeSummary = Read-Json $routeSummaryPath
$routeAcceptanceSummary = Read-Json $routeAcceptanceSummaryPath
$runtimeSummary = Read-Json $runtimeSummaryPath
$closureSummary = Read-Json $closureSummaryPath
$routeRows = @(Import-Csv -LiteralPath $routeCsv)
$routeAcceptanceRows = @(Import-Csv -LiteralPath $routeAcceptanceCsv)
$runtimeRows = @(Import-Csv -LiteralPath $runtimeCsv)

$routeAcceptanceByName = @{}
$runtimeByName = @{}
Add-ByName $routeAcceptanceByName $routeAcceptanceRows
Add-ByName $runtimeByName $runtimeRows

$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$replacementReadyCount = 0
$inGameReplacementAllowedCount = 0
$unsafeToReplaceCount = 0
$harnessValidCount = 0
$harnessOnlyCount = 0
$installedRuntimeDumpValidCount = 0
$installedStaticFrameDumpValidCount = 0
$installedDrawMissingCount = 0
$sourceIdentityAcceptedCount = 0
$semanticAcceptedCount = 0
$mappingPromotionAllowedCount = 0
$routeProofReadyOrDependencyCount = 0
$n64RouteAcceptedCount = 0
$routeOrOwnerPolicyReadyCount = 0
$exactDirectRouteProvenCount = 0
$directPlayerCallsiteContextCount = 0
$familyOrSiblingRouteOnlyCount = 0
$anonymousTableOwnerContextCount = 0
$promotedOwnerAliasDelegatedCount = 0
$routeAbsentPolicyOnlyCount = 0
$sourceRiskNoRouteProofCount = 0
$directPlayerActorReferenceTotal = 0
$familyOrSiblingReferenceTotal = 0
$candidateOwnerReferenceTotal = 0
$sourceWindowTotal = 0
$routeBucketCounts = @{}
$routeStatusCounts = @{}
$routeOrOwnerPolicyGateStatusCounts = @{}
$sourceIdentityGateStatusCounts = @{}
$installedDrawGateStatusCounts = @{}
$replacementGateStatusCounts = @{}
$nextReplacementBlockerCounts = @{}
$issueReasonCounts = @{}

foreach ($route in $routeRows) {
    $n64Name = [string]$route.n64_name
    $routeAcceptance = $routeAcceptanceByName[$n64Name]
    $runtime = $runtimeByName[$n64Name]
    $issues = New-Object "System.Collections.Generic.List[object]"
    if ($null -eq $runtime) {
        $issues.Add([pscustomobject]@{ reason = "missing_runtime_capture_row"; detail = $n64Name }) | Out-Null
    }
    if ($null -eq $routeAcceptance) {
        $issues.Add([pscustomobject]@{ reason = "missing_route_acceptance_policy_row"; detail = $n64Name }) | Out-Null
    }

    $routeIssueCount = Get-IntValue $route.issue_count
    if ($routeIssueCount -ne 0) {
        $issues.Add([pscustomobject]@{ reason = "route_proof_priority_issue"; detail = [string]$route.issue_count }) | Out-Null
    }

    $runtimeIssueCount = 0
    if ($null -ne $runtime) {
        $runtimeIssueCount = Get-IntValue $runtime.issue_count
        if ($runtimeIssueCount -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "runtime_capture_issue"; detail = [string]$runtime.issue_count }) | Out-Null
        }
        if ([string]$runtime.audit_status -ne "valid" -and [string]$runtime.capture_status -ne "ready_for_capture") {
            $issues.Add([pscustomobject]@{ reason = "runtime_capture_audit_not_valid"; detail = [string]$runtime.audit_status }) | Out-Null
        }
    }

    $bucket = [string]$route.route_proof_bucket
    $routeStatus = [string]$route.route_proof_status
    $n64RouteAccepted = if ($null -eq $routeAcceptance) { $false } else { Get-BoolValue $routeAcceptance.n64_route_accepted }
    $routeOrOwnerPolicyReady = if ($null -eq $routeAcceptance) { $false } else { Get-BoolValue $routeAcceptance.route_or_owner_policy_ready }
    $routeAcceptanceStatus = if ($null -eq $routeAcceptance) { "missing" } else { [string]$routeAcceptance.n64_route_acceptance_status }
    $routeAcceptanceRequiredPolicyGate = if ($null -eq $routeAcceptance) { "missing" } else { [string]$routeAcceptance.required_policy_gate }
    $sourceIdentityReviewStatus = if ($null -eq $routeAcceptance) { "missing" } else { [string]$routeAcceptance.source_identity_review_status }
    $sourceIdentityAccepted = if ($null -eq $routeAcceptance) { $false } else { Get-BoolValue $routeAcceptance.source_identity_accepted }
    $routeOrOwnerPolicyGateStatus = Get-RouteOrOwnerPolicyGateStatus $bucket $n64RouteAccepted $routeOrOwnerPolicyReady
    $routeProofReadyOrDependency = $routeOrOwnerPolicyGateStatus -ne "route_or_owner_policy_unclassified"
    $installedRuntimeDumpValid = $false
    $installedStaticFrameDumpValid = $false
    $runtimeHarnessDumpValid = Get-BoolValue $route.runtime_harness_dump_valid
    if ($null -ne $runtime) {
        $installedRuntimeDumpValid = Get-BoolValue $runtime.installed_runtime_dump_valid
        $installedStaticFrameDumpValid = Get-BoolValue $runtime.installed_static_frame_dump_valid
        $runtimeHarnessDumpValid = $runtimeHarnessDumpValid -and (Get-BoolValue $runtime.runtime_harness_dump_valid)
    }
    $semanticAcceptanceStatus = if ($null -eq $routeAcceptance) { [string]$route.semantic_acceptance_status } else { [string]$routeAcceptance.semantic_acceptance_status }
    $semanticAccepted = ($semanticAcceptanceStatus -eq "semantic_accepted")
    $mappingPromotionAllowed = if ($null -eq $routeAcceptance) { Get-BoolValue $route.mapping_promotion_allowed } else { Get-BoolValue $routeAcceptance.mapping_promotion_allowed }
    $sourceIdentityGateStatus = Get-SourceIdentityGateStatus $bucket $sourceIdentityAccepted $semanticAccepted
    $installedDrawGateStatus = if ($installedRuntimeDumpValid) {
        "installed_runtime_draw_valid"
    }
    elseif ($installedStaticFrameDumpValid) {
        "static_frame_draw_valid_dynamic_playback_missing"
    }
    elseif ($runtimeHarnessDumpValid) {
        "harness_only_installed_draw_missing"
    }
    else {
        "runtime_draw_missing_or_invalid"
    }

    $rowIssueCount = [int]$issues.Count
    $replacementReady = (
        $rowIssueCount -eq 0 -and
        $routeProofReadyOrDependency -and
        $installedRuntimeDumpValid -and
        $semanticAccepted -and
        $mappingPromotionAllowed
    )
    $replacementGateStatus = if ($rowIssueCount -ne 0) {
        "replacement_gate_has_issues"
    }
    elseif ($replacementReady) {
        "in_game_replacement_ready"
    }
    else {
        "blocked_before_in_game_replacement"
    }
    $inGameReplacementAllowed = $replacementReady
    $nextReplacementBlocker = if ($replacementReady) {
        "none"
    }
    else {
        Get-NextReplacementBlocker $bucket $installedRuntimeDumpValid $n64RouteAccepted $routeOrOwnerPolicyReady $sourceIdentityAccepted
    }
    $requiredNextStep = if ($null -eq $routeAcceptance) { [string]$route.required_next_step } else { [string]$routeAcceptance.required_next_step }

    $record = [pscustomobject]@{
        n64_name = $n64Name
        source_csab_name = [string]$route.source_csab_name
        frontier_class = [string]$route.frontier_class
        frontier_gate = [string]$route.frontier_gate
        workorder_kind = [string]$route.workorder_kind
        post_harness_blocker = [string]$route.post_harness_blocker
        route_proof_bucket = $bucket
        route_proof_rank = Get-IntValue $route.route_proof_rank
        route_proof_status = $routeStatus
        route_proof_blocker = [string]$route.route_proof_blocker
        n64_route_evidence_kind = [string]$route.n64_route_evidence_kind
        direct_player_actor_reference_count = Get-IntValue $route.direct_player_actor_reference_count
        family_or_sibling_reference_count = Get-IntValue $route.family_or_sibling_reference_count
        candidate_owner_reference_count = Get-IntValue $route.candidate_owner_reference_count
        source_window_count = Get-IntValue $route.source_window_count
        route_context = [string]$route.route_context
        runtime_trigger = [string]$route.runtime_trigger
        runtime_lane = if ($null -eq $runtime) { "" } else { [string]$runtime.lane }
        runtime_capture_status = if ($null -eq $runtime) { "missing" } else { [string]$runtime.capture_status }
        runtime_audit_status = if ($null -eq $runtime) { "missing" } else { [string]$runtime.audit_status }
        route_or_owner_policy_gate_status = $routeOrOwnerPolicyGateStatus
        route_proof_ready_or_dependency = $routeProofReadyOrDependency
        n64_route_acceptance_status = $routeAcceptanceStatus
        n64_route_accepted = $n64RouteAccepted
        route_or_owner_policy_ready = $routeOrOwnerPolicyReady
        route_acceptance_required_policy_gate = $routeAcceptanceRequiredPolicyGate
        source_identity_review_status = $sourceIdentityReviewStatus
        source_identity_accepted = $sourceIdentityAccepted
        source_identity_gate_status = $sourceIdentityGateStatus
        installed_draw_gate_status = $installedDrawGateStatus
        installed_runtime_dump_valid = $installedRuntimeDumpValid
        installed_static_frame_dump_valid = $installedStaticFrameDumpValid
        runtime_harness_dump_valid = $runtimeHarnessDumpValid
        semantic_acceptance_status = $semanticAcceptanceStatus
        semantic_accepted = $semanticAccepted
        mapping_promotion_allowed = $mappingPromotionAllowed
        replacement_gate_status = $replacementGateStatus
        replacement_ready = $replacementReady
        in_game_replacement_allowed = $inGameReplacementAllowed
        unsafe_to_replace = (-not $replacementReady)
        next_replacement_blocker = $nextReplacementBlocker
        required_next_step = $requiredNextStep
        issue_count = $rowIssueCount
        issues = @($issues.ToArray())
    }

    if ($routeProofReadyOrDependency) {
        $routeProofReadyOrDependencyCount++
    }
    if ([bool]$record.n64_route_accepted) {
        $n64RouteAcceptedCount++
    }
    if ([bool]$record.route_or_owner_policy_ready) {
        $routeOrOwnerPolicyReadyCount++
    }
    if ([bool]$record.runtime_harness_dump_valid) {
        $harnessValidCount++
    }
    if ([bool]$record.runtime_harness_dump_valid -and -not [bool]$record.installed_runtime_dump_valid) {
        $harnessOnlyCount++
    }
    if ([bool]$record.installed_runtime_dump_valid) {
        $installedRuntimeDumpValidCount++
    }
    if ([bool]$record.installed_static_frame_dump_valid) {
        $installedStaticFrameDumpValidCount++
    }
    if (-not [bool]$record.installed_runtime_dump_valid) {
        $installedDrawMissingCount++
    }
    if ([bool]$record.source_identity_accepted) {
        $sourceIdentityAcceptedCount++
    }
    if ([bool]$record.semantic_accepted) {
        $semanticAcceptedCount++
    }
    if ([bool]$record.mapping_promotion_allowed) {
        $mappingPromotionAllowedCount++
    }
    if ([bool]$record.replacement_ready) {
        $replacementReadyCount++
    }
    if ([bool]$record.in_game_replacement_allowed) {
        $inGameReplacementAllowedCount++
    }
    if ([bool]$record.unsafe_to_replace) {
        $unsafeToReplaceCount++
    }
    switch ($bucket) {
        "exact_direct_n64_route_proven" { $exactDirectRouteProvenCount++ }
        "direct_n64_player_callsite_context" { $directPlayerCallsiteContextCount++ }
        "family_or_sibling_n64_route_only" { $familyOrSiblingRouteOnlyCount++ }
        "n64_table_identity_candidate_owner_context" { $anonymousTableOwnerContextCount++ }
        "promoted_owner_route_delegated_alias" { $promotedOwnerAliasDelegatedCount++ }
        "route_absent_owner_policy_only" { $routeAbsentPolicyOnlyCount++ }
        "source_risk_no_route_proof" { $sourceRiskNoRouteProofCount++ }
    }
    $directPlayerActorReferenceTotal += [int]$record.direct_player_actor_reference_count
    $familyOrSiblingReferenceTotal += [int]$record.family_or_sibling_reference_count
    $candidateOwnerReferenceTotal += [int]$record.candidate_owner_reference_count
    $sourceWindowTotal += [int]$record.source_window_count
    $issueCount += $rowIssueCount
    Add-Count $routeBucketCounts ([string]$record.route_proof_bucket)
    Add-Count $routeStatusCounts ([string]$record.route_proof_status)
    Add-Count $routeOrOwnerPolicyGateStatusCounts ([string]$record.route_or_owner_policy_gate_status)
    Add-Count $sourceIdentityGateStatusCounts ([string]$record.source_identity_gate_status)
    Add-Count $installedDrawGateStatusCounts ([string]$record.installed_draw_gate_status)
    Add-Count $replacementGateStatusCounts ([string]$record.replacement_gate_status)
    Add-Count $nextReplacementBlockerCounts ([string]$record.next_replacement_blocker)
    foreach ($issue in @($record.issues)) {
        Add-Count $issueReasonCounts ([string]$issue.reason)
    }
    $records.Add($record) | Out-Null
}

$recordArray = @($records.ToArray() | Sort-Object route_proof_rank,n64_name)
$summaryPath = Join-Path $OutputRoot "link_child_in_game_replacement_gate_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_in_game_replacement_gate.csv"
$status = if ($issueCount -ne 0) {
    "in_game_replacement_gate_has_issues"
}
elseif ($replacementReadyCount -eq $recordArray.Count -and $recordArray.Count -gt 0) {
    "in_game_replacement_gate_ready"
}
else {
    "in_game_replacement_gate_blocked"
}

$summary = [pscustomobject]@{
    format = "oot3d_link_child_in_game_replacement_gate_v1"
    status = $status
    policy = [ordered]@{
        scope = "Replacement-readiness gate over all Link child residual N64 PlayerAnimation rows"
        semantic_effect = "does not promote mappings; separates N64 route evidence from in-game replacement readiness"
        replacement_gate = "a row is replacement-ready only with route/owner dependency coverage, accepted source identity, mapping promotion allowance, and installed runtime draw proof"
    }
    n64_route_proof_priority_summary = (Resolve-Path -LiteralPath $routeSummaryPath).Path
    n64_route_proof_acceptance_policy_summary = (Resolve-Path -LiteralPath $routeAcceptanceSummaryPath).Path
    runtime_config_capture_matrix_summary = (Resolve-Path -LiteralPath $runtimeSummaryPath).Path
    semantic_frontier_closure_summary = (Resolve-Path -LiteralPath $closureSummaryPath).Path
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    record_count = $recordArray.Count
    route_proof_matrix_row_count = Get-IntValue (Get-JsonValue $routeSummary "record_count")
    route_acceptance_policy_row_count = Get-IntValue (Get-JsonValue $routeAcceptanceSummary "record_count")
    runtime_capture_row_count = Get-IntValue (Get-JsonValue $runtimeSummary "record_count")
    closure_record_count = Get-IntValue (Get-JsonValue $closureSummary "record_count")
    route_proof_ready_or_dependency_count = $routeProofReadyOrDependencyCount
    n64_route_accepted_count = $n64RouteAcceptedCount
    route_or_owner_policy_ready_count = $routeOrOwnerPolicyReadyCount
    exact_direct_route_proven_count = $exactDirectRouteProvenCount
    direct_player_callsite_context_count = $directPlayerCallsiteContextCount
    direct_route_or_callsite_count = $exactDirectRouteProvenCount + $directPlayerCallsiteContextCount
    family_or_sibling_route_only_count = $familyOrSiblingRouteOnlyCount
    anonymous_table_owner_context_count = $anonymousTableOwnerContextCount
    promoted_owner_alias_delegated_count = $promotedOwnerAliasDelegatedCount
    route_absent_policy_only_count = $routeAbsentPolicyOnlyCount
    source_risk_no_route_proof_count = $sourceRiskNoRouteProofCount
    direct_player_actor_reference_count = $directPlayerActorReferenceTotal
    family_or_sibling_reference_count = $familyOrSiblingReferenceTotal
    candidate_owner_reference_count = $candidateOwnerReferenceTotal
    source_window_count = $sourceWindowTotal
    harness_valid_count = $harnessValidCount
    harness_only_count = $harnessOnlyCount
    installed_runtime_dump_valid_count = $installedRuntimeDumpValidCount
    installed_static_frame_dump_valid_count = $installedStaticFrameDumpValidCount
    installed_draw_missing_count = $installedDrawMissingCount
    source_identity_accepted_count = $sourceIdentityAcceptedCount
    semantic_accepted_count = $semanticAcceptedCount
    mapping_promotion_allowed_count = $mappingPromotionAllowedCount
    replacement_ready_count = $replacementReadyCount
    in_game_replacement_allowed_count = $inGameReplacementAllowedCount
    unsafe_to_replace_count = $unsafeToReplaceCount
    issue_count = $issueCount
    route_proof_bucket_counts = $routeBucketCounts
    route_proof_status_counts = $routeStatusCounts
    route_or_owner_policy_gate_status_counts = $routeOrOwnerPolicyGateStatusCounts
    source_identity_gate_status_counts = $sourceIdentityGateStatusCounts
    installed_draw_gate_status_counts = $installedDrawGateStatusCounts
    replacement_gate_status_counts = $replacementGateStatusCounts
    next_replacement_blocker_counts = $nextReplacementBlockerCounts
    issue_reason_counts = $issueReasonCounts
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,frontier_class,frontier_gate,workorder_kind,post_harness_blocker,route_proof_bucket,route_proof_rank,route_proof_status,route_proof_blocker,n64_route_evidence_kind,direct_player_actor_reference_count,family_or_sibling_reference_count,candidate_owner_reference_count,source_window_count,route_context,runtime_trigger,runtime_lane,runtime_capture_status,runtime_audit_status,route_or_owner_policy_gate_status,route_proof_ready_or_dependency,n64_route_acceptance_status,n64_route_accepted,route_or_owner_policy_ready,route_acceptance_required_policy_gate,source_identity_review_status,source_identity_accepted,source_identity_gate_status,installed_draw_gate_status,installed_runtime_dump_valid,installed_static_frame_dump_valid,runtime_harness_dump_valid,semantic_acceptance_status,semantic_accepted,mapping_promotion_allowed,replacement_gate_status,replacement_ready,in_game_replacement_allowed,unsafe_to_replace,next_replacement_blocker,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_in_game_replacement_gate_v1") "unexpected replacement gate format"
    Assert-Condition ($summary.status -eq "in_game_replacement_gate_blocked") "expected replacement gate to be blocked"
    Assert-Condition ([int]$summary.record_count -eq 63) "expected 63 replacement gate rows"
    Assert-Condition ([int]$summary.route_proof_matrix_row_count -eq 63) "expected 63 route proof rows"
    Assert-Condition ([int]$summary.route_acceptance_policy_row_count -eq 63) "expected 63 route acceptance policy rows"
    Assert-Condition ([int]$summary.runtime_capture_row_count -eq 63) "expected 63 runtime capture rows"
    Assert-Condition ([int]$summary.closure_record_count -eq 63) "expected 63 closure rows"
    Assert-Condition ([int]$summary.route_proof_ready_or_dependency_count -eq 63) "expected all rows to have route/owner dependency classification"
    Assert-Condition ([int]$summary.n64_route_accepted_count -eq 5) "expected five accepted N64 routes"
    Assert-Condition ([int]$summary.route_or_owner_policy_ready_count -eq 5) "expected five route/owner policies ready"
    Assert-Condition ([int]$summary.exact_direct_route_proven_count -eq 5) "expected five exact direct route-proven rows"
    Assert-Condition ([int]$summary.direct_player_callsite_context_count -eq 16) "expected sixteen direct player callsite context rows"
    Assert-Condition ([int]$summary.direct_route_or_callsite_count -eq 21) "expected 21 direct N64 route/callsite rows"
    Assert-Condition ([int]$summary.family_or_sibling_route_only_count -eq 8) "expected eight family/sibling route rows"
    Assert-Condition ([int]$summary.anonymous_table_owner_context_count -eq 11) "expected eleven anonymous table-owner rows"
    Assert-Condition ([int]$summary.promoted_owner_alias_delegated_count -eq 7) "expected seven promoted-owner alias rows"
    Assert-Condition ([int]$summary.route_absent_policy_only_count -eq 14) "expected fourteen route-absent policy rows"
    Assert-Condition ([int]$summary.source_risk_no_route_proof_count -eq 2) "expected two source-risk no-route rows"
    Assert-Condition ([int]$summary.direct_player_actor_reference_count -eq 47) "expected 47 direct player-actor references"
    Assert-Condition ([int]$summary.family_or_sibling_reference_count -eq 54) "expected 54 family/sibling references"
    Assert-Condition ([int]$summary.candidate_owner_reference_count -eq 31) "expected 31 candidate-owner references"
    Assert-Condition ([int]$summary.source_window_count -eq 54) "expected 54 source windows"
    Assert-Condition ([int]$summary.harness_valid_count -eq 62) "expected 62 harness-valid rows"
    Assert-Condition ([int]$summary.harness_only_count -eq 62) "expected 62 harness-only rows"
    Assert-Condition ([int]$summary.installed_runtime_dump_valid_count -eq 1) "expected one dynamic installed draw row"
    Assert-Condition ([int]$summary.installed_static_frame_dump_valid_count -eq 0) "expected no static-frame installed draw rows"
    Assert-Condition ([int]$summary.installed_draw_missing_count -eq 62) "expected 62 rows missing dynamic installed draw"
    Assert-Condition ([int]$summary.source_identity_accepted_count -eq 1) "expected one accepted source identity"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected no accepted semantics"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected no mapping promotions"
    Assert-Condition ([int]$summary.replacement_ready_count -eq 0) "expected zero replacement-ready rows"
    Assert-Condition ([int]$summary.in_game_replacement_allowed_count -eq 0) "expected zero allowed in-game replacements"
    Assert-Condition ([int]$summary.unsafe_to_replace_count -eq 63) "expected 63 unsafe-to-replace rows"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero replacement gate issues"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "exact_direct_n64_route_proven") -eq 5) "expected five exact direct bucket rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "direct_n64_player_callsite_context") -eq 16) "expected sixteen direct callsite bucket rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "family_or_sibling_n64_route_only") -eq 8) "expected eight family/sibling bucket rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "n64_table_identity_candidate_owner_context") -eq 11) "expected eleven anonymous table-owner bucket rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "promoted_owner_route_delegated_alias") -eq 7) "expected seven promoted-owner alias bucket rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "route_absent_owner_policy_only") -eq 14) "expected fourteen route-absent bucket rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "source_risk_no_route_proof") -eq 2) "expected two source-risk bucket rows"
    Assert-Condition ((Get-JsonValue $summary.replacement_gate_status_counts "blocked_before_in_game_replacement") -eq 63) "expected all rows blocked before replacement"
    Assert-Condition ((Get-JsonValue $summary.route_or_owner_policy_gate_status_counts "direct_route_and_callsite_policy_ready_pending_installed_draw") -eq 5) "expected five direct route/callsite policy ready rows"
    Assert-Condition ((Get-JsonValue $summary.installed_draw_gate_status_counts "harness_only_installed_draw_missing") -eq 62) "expected 62 rows to be harness-only"
    Assert-Condition ((Get-JsonValue $summary.installed_draw_gate_status_counts "installed_runtime_draw_valid") -eq 1) "expected one installed runtime draw row"
    Assert-Condition ((Get-IntValue (Get-JsonValue $summary.installed_draw_gate_status_counts "static_frame_draw_valid_dynamic_playback_missing")) -eq 0) "expected no static-frame draw rows"
    Assert-Condition ((Get-IntValue (Get-JsonValue $summary.installed_draw_gate_status_counts "runtime_draw_missing_or_invalid")) -eq 0) "expected zero rows waiting for dynamic runtime draw"
    Assert-Condition ((Get-JsonValue $summary.next_replacement_blocker_counts "installed_draw_required") -eq 5) "expected five installed-draw-only blockers"
    Assert-Condition ((Get-IntValue (Get-JsonValue $summary.next_replacement_blocker_counts "installed_draw_or_replacement_policy_required")) -eq 0) "expected no direct route rows still blocked on replacement policy"
    Assert-Condition ((Get-JsonValue $summary.next_replacement_blocker_counts "source_identity_or_installed_draw_required") -eq 15) "expected fifteen source-identity/installed-draw blockers"
    Assert-Condition ((Get-IntValue (Get-JsonValue $summary.next_replacement_blocker_counts "source_identity_review_required_after_installed_draw")) -eq 0) "expected zero source-identity review blockers after accepted source identity"
    Assert-Condition ((Get-JsonValue $summary.next_replacement_blocker_counts "semantic_acceptance_or_mapping_promotion_required_after_source_identity") -eq 1) "expected one semantic/promotion blocker after accepted source identity"
    Assert-Condition ((Get-JsonValue $summary.next_replacement_blocker_counts "exact_route_policy_or_family_route_acceptance_required") -eq 8) "expected eight family-route blockers"
    Assert-Condition ((Get-JsonValue $summary.next_replacement_blocker_counts "anonymous_owner_identity_policy_or_installed_draw_required") -eq 11) "expected eleven anonymous-owner blockers"
    Assert-Condition ((Get-JsonValue $summary.next_replacement_blocker_counts "alias_owner_policy_or_installed_draw_required") -eq 7) "expected seven alias-owner blockers"
    Assert-Condition ((Get-JsonValue $summary.next_replacement_blocker_counts "n64_route_absent_owner_policy_and_installed_draw_required") -eq 14) "expected fourteen route-absent blockers"
    Assert-Condition ((Get-JsonValue $summary.next_replacement_blocker_counts "alternate_source_or_installed_draw_required") -eq 2) "expected two alternate-source blockers"
    Require-Path $csvPath "In-game replacement gate CSV"
}

Write-Host "OOT3D Link child in-game replacement gate: $summaryPath"
Write-Host "status=$($summary.status) rows=$($summary.record_count) replacementReady=$($summary.replacement_ready_count) unsafe=$($summary.unsafe_to_replace_count) harnessOnly=$($summary.harness_only_count) staticFrameValid=$($summary.installed_static_frame_dump_valid_count) installedDrawMissing=$($summary.installed_draw_missing_count) issues=$($summary.issue_count)"
