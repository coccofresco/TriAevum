param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\ownership_decision_matrix"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child ownership decision matrix verification failed: $Message"
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

function Get-Decision([object]$FrontierRow, [object]$ArbitrationRow, [object]$RouteRow) {
    $postGate = [string]$FrontierRow.post_derivative_gate
    if ($postGate -eq "alternate_source_or_runtime_skinned_parity") {
        return [pscustomobject]@{
            decision_status = "ownership_plus_pose_source_risk_requires_alternate_source_or_runtime_parity"
            decision_class = "ownership_blocked_by_pose_or_source_risk"
            acceptance_gate = "alternate_source_or_runtime_skinned_parity_before_ownership"
            required_next_step = "resolve alternate source or installed runtime/skinned parity before making an ownership/reuse decision"
        }
    }
    if ($null -ne $RouteRow) {
        return [pscustomobject]@{
            decision_status = "route_proven_pending_installed_capture_and_policy"
            decision_class = "route_proven_ownership_candidate"
            acceptance_gate = "route_callsite_package_policy_and_installed_capture"
            required_next_step = "package/capture the route-proven derivative and accept explicit ownership policy before promotion"
        }
    }
    $arbitrationClass = if ($null -eq $ArbitrationRow) { "" } else { [string]$ArbitrationRow.ownership_reuse_arbitration_class }
    if ($arbitrationClass -eq "route_absent_multi_claim_intentional_reuse_policy_candidate") {
        return [pscustomobject]@{
            decision_status = "route_absent_multi_claim_intentional_reuse_policy_candidate"
            decision_class = "route_absent_multi_claim_source_group"
            acceptance_gate = "source_group_intentional_reuse_policy_and_installed_capture"
            required_next_step = "decide intentional reuse for the source group, then capture installed runtime draw for the derived rows"
        }
    }
    if ($arbitrationClass -eq "route_absent_candidate_owner_single_alias_policy_candidate") {
        return [pscustomobject]@{
            decision_status = "route_absent_candidate_owner_single_alias_policy_candidate"
            decision_class = "route_absent_candidate_owner_alias"
            acceptance_gate = "single_candidate_owner_alias_policy_and_installed_capture"
            required_next_step = "prove the candidate owner and alias intent, then capture installed runtime draw"
        }
    }
    if ($arbitrationClass -eq "route_absent_promoted_owner_single_alias_policy_candidate") {
        return [pscustomobject]@{
            decision_status = "route_absent_promoted_owner_single_alias_policy_candidate"
            decision_class = "route_absent_promoted_owner_alias"
            acceptance_gate = "single_promoted_owner_alias_policy_and_installed_capture"
            required_next_step = "prove aliasing to an already promoted owner and capture installed runtime draw"
        }
    }
    return [pscustomobject]@{
        decision_status = "manual_ownership_decision_required"
        decision_class = "manual_ownership_review"
        acceptance_gate = "manual_review"
        required_next_step = "inspect missing arbitration evidence"
    }
}

$characterRoot = Join-Path $WorkRoot "character_conversion"
$frontierCsv = Join-Path $characterRoot "link_child_animation_semantic_ownership_derivative_frontier.csv"
$arbitrationCsv = Join-Path $characterRoot "link_child_animation_semantic_ownership_reuse_arbitration.csv"
$routeCallsiteCsv = Join-Path $characterRoot "link_child_animation_semantic_route_proven_ownership_callsite.csv"

Require-Path $frontierCsv "Ownership derivative frontier CSV"
Require-Path $arbitrationCsv "Ownership reuse arbitration CSV"
Require-Path $routeCallsiteCsv "Route-proven ownership callsite CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$frontierRows = @(Import-Csv -LiteralPath $frontierCsv)
$arbitrationByName = @{}
foreach ($row in @(Import-Csv -LiteralPath $arbitrationCsv)) {
    $arbitrationByName[[string]$row.n64_name] = $row
}
$routeByName = @{}
foreach ($row in @(Import-Csv -LiteralPath $routeCallsiteCsv)) {
    $routeByName[[string]$row.n64_name] = $row
}

$records = New-Object "System.Collections.Generic.List[object]"
$decisionStatusCounts = @{}
$decisionClassCounts = @{}
$acceptanceGateCounts = @{}
$sourceGroupCounts = @{}
$frontierClassCounts = @{}
$postGateCounts = @{}
$issueCount = 0
$ownershipOnlyPoseReadyCount = 0
$ownershipPlusPoseRiskCount = 0
$routeProvenCount = 0
$routeAbsentPolicyCount = 0

foreach ($frontier in $frontierRows) {
    $n64Name = [string]$frontier.n64_name
    $arbitration = $arbitrationByName[$n64Name]
    $route = $routeByName[$n64Name]
    $decision = Get-Decision $frontier $arbitration $route
    $issues = New-Object "System.Collections.Generic.List[object]"
    if ([string]$frontier.post_derivative_gate -eq "ownership_or_intentional_reuse_policy") {
        $ownershipOnlyPoseReadyCount++
        if ($null -eq $arbitration) {
            $issues.Add([pscustomobject]@{ reason = "missing_arbitration_row"; detail = $n64Name }) | Out-Null
        }
    }
    else {
        $ownershipPlusPoseRiskCount++
    }
    if ($null -ne $route) {
        $routeProvenCount++
    }
    elseif ([string]$frontier.post_derivative_gate -eq "ownership_or_intentional_reuse_policy") {
        $routeAbsentPolicyCount++
    }
    if ([string]$decision.decision_status -eq "manual_ownership_decision_required") {
        $issues.Add([pscustomobject]@{ reason = "manual_decision_status"; detail = $n64Name }) | Out-Null
    }
    $issueCount += $issues.Count

    Add-Count $decisionStatusCounts ([string]$decision.decision_status)
    Add-Count $decisionClassCounts ([string]$decision.decision_class)
    Add-Count $acceptanceGateCounts ([string]$decision.acceptance_gate)
    Add-Count $sourceGroupCounts ([string]$frontier.source_csab_name)
    Add-Count $frontierClassCounts ([string]$frontier.frontier_class)
    Add-Count $postGateCounts ([string]$frontier.post_derivative_gate)

    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        n64_data_name = [string]$frontier.n64_data_name
        source_csab_name = [string]$frontier.source_csab_name
        source_csab_claim_count = [int]$frontier.source_csab_claim_count
        source_group_n64_names = if ($null -eq $arbitration) { "" } else { [string]$arbitration.source_group_n64_names }
        frontier_class = [string]$frontier.frontier_class
        derivative_frontier_class = [string]$frontier.derivative_frontier_class
        post_derivative_gate = [string]$frontier.post_derivative_gate
        reference_envelope_status = [string]$frontier.reference_envelope_status
        materialization_class = [string]$frontier.materialization_class
        output_csab_name = [string]$frontier.output_csab_name
        route_evidence_status = if ($null -eq $arbitration) { "" } else { [string]$arbitration.route_evidence_status }
        direct_player_actor_source_reference_count = if ($null -eq $arbitration) { 0 } else { [int]$arbitration.direct_player_actor_source_reference_count }
        route_callsite_package_candidate_status = if ($null -eq $route) { "" } else { [string]$route.package_candidate_status }
        ownership_reuse_arbitration_class = if ($null -eq $arbitration) { "" } else { [string]$arbitration.ownership_reuse_arbitration_class }
        policy_decision_scope = if ($null -eq $arbitration) { "" } else { [string]$arbitration.policy_decision_scope }
        decision_status = [string]$decision.decision_status
        decision_class = [string]$decision.decision_class
        acceptance_gate = [string]$decision.acceptance_gate
        runtime_acceptance_status = "diagnostic_ownership_decision_pending"
        semantic_effect = "ownership decision workorder only; no mapping promotion"
        required_next_step = [string]$decision.required_next_step
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray())
$summaryOutput = Join-Path $OutputRoot "link_child_ownership_decision_matrix_summary.json"
$csvOutput = Join-Path $OutputRoot "link_child_ownership_decision_matrix.csv"
$status = if ($issueCount -eq 0 -and $recordArray.Count -gt 0) {
    "ownership_decision_workorders_ready"
}
elseif ($recordArray.Count -eq 0) {
    "complete_no_ownership_rows"
}
else {
    "ownership_decision_workorders_have_issues"
}

$summary = [pscustomobject]@{
    format = "oot3d_link_child_ownership_decision_matrix_v1"
    status = $status
    policy = [ordered]@{
        scope = "decision workorders for Link child CSAB source-ownership frontier rows"
        semantic_effect = "classifies ownership/reuse decisions only; it does not accept ownership or promote mappings"
        acceptance_policy = "ownership rows require explicit route, reuse policy, or installed runtime draw evidence before semantic acceptance"
    }
    ownership_derivative_frontier_csv = (Resolve-Path -LiteralPath $frontierCsv).Path
    ownership_reuse_arbitration_csv = (Resolve-Path -LiteralPath $arbitrationCsv).Path
    route_proven_callsite_csv = (Resolve-Path -LiteralPath $routeCallsiteCsv).Path
    output_root = $OutputRoot
    record_count = $recordArray.Count
    ownership_only_pose_ready_count = $ownershipOnlyPoseReadyCount
    ownership_plus_pose_source_risk_count = $ownershipPlusPoseRiskCount
    route_proven_package_candidate_count = $routeProvenCount
    route_absent_policy_candidate_count = $routeAbsentPolicyCount
    unique_source_csab_count = $sourceGroupCounts.Count
    accepted_ownership_count = 0
    accepted_reuse_policy_count = 0
    installed_runtime_dump_valid_count = 0
    issue_count = $issueCount
    decision_status_counts = $decisionStatusCounts
    decision_class_counts = $decisionClassCounts
    acceptance_gate_counts = $acceptanceGateCounts
    frontier_class_counts = $frontierClassCounts
    post_derivative_gate_counts = $postGateCounts
    source_csab_row_counts = $sourceGroupCounts
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryOutput -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,source_csab_claim_count,frontier_class,derivative_frontier_class,post_derivative_gate,reference_envelope_status,materialization_class,route_evidence_status,direct_player_actor_source_reference_count,route_callsite_package_candidate_status,ownership_reuse_arbitration_class,policy_decision_scope,decision_status,decision_class,acceptance_gate,runtime_acceptance_status,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvOutput -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_ownership_decision_matrix_v1") "unexpected matrix format"
    Assert-Condition ($summary.status -eq "ownership_decision_workorders_ready") "expected ready ownership decision workorders"
    Assert-Condition ([int]$summary.record_count -eq 28) "expected 28 ownership decision rows"
    Assert-Condition ([int]$summary.ownership_only_pose_ready_count -eq 26) "expected 26 ownership-only pose-ready rows"
    Assert-Condition ([int]$summary.ownership_plus_pose_source_risk_count -eq 2) "expected 2 ownership plus pose/source-risk rows"
    Assert-Condition ([int]$summary.route_proven_package_candidate_count -eq 5) "expected 5 route-proven package candidate rows"
    Assert-Condition ([int]$summary.route_absent_policy_candidate_count -eq 21) "expected 21 route-absent policy candidate rows"
    Assert-Condition ([int]$summary.unique_source_csab_count -eq 20) "expected 20 unique ownership source CSABs"
    Assert-Condition ([int]$summary.accepted_ownership_count -eq 0) "expected zero accepted ownership rows"
    Assert-Condition ([int]$summary.accepted_reuse_policy_count -eq 0) "expected zero accepted reuse policies"
    Assert-Condition ([int]$summary.installed_runtime_dump_valid_count -eq 0) "expected zero installed ownership runtime dumps"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero ownership decision matrix issues"
    Assert-Condition ([int]$summary.decision_status_counts.route_proven_pending_installed_capture_and_policy -eq 5) "expected 5 route-proven ownership decisions"
    Assert-Condition ([int]$summary.decision_status_counts.route_absent_multi_claim_intentional_reuse_policy_candidate -eq 9) "expected 9 multi-claim reuse policy decisions"
    Assert-Condition ([int]$summary.decision_status_counts.route_absent_candidate_owner_single_alias_policy_candidate -eq 5) "expected 5 candidate-owner alias decisions"
    Assert-Condition ([int]$summary.decision_status_counts.route_absent_promoted_owner_single_alias_policy_candidate -eq 7) "expected 7 promoted-owner alias decisions"
    Assert-Condition ([int]$summary.decision_status_counts.ownership_plus_pose_source_risk_requires_alternate_source_or_runtime_parity -eq 2) "expected 2 ownership plus pose/source-risk decisions"
    Assert-Condition (Test-Path -LiteralPath $csvOutput) "expected ownership decision CSV output"
}

Write-Host "OOT3D Link child ownership decision matrix: $summaryOutput"
Write-Host "status=$($summary.status) rows=$($summary.record_count) routeProven=$($summary.route_proven_package_candidate_count) routeAbsent=$($summary.route_absent_policy_candidate_count) issues=$($summary.issue_count)"
