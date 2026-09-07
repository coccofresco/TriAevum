param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\semantic_frontier_closure_matrix"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child semantic frontier closure matrix verification failed: $Message"
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

function Get-Workorder([object]$FrontierRow, [hashtable]$PoseRows, [hashtable]$RouteRows, [hashtable]$OwnershipRows, [hashtable]$AnonymousRows) {
    $n64Name = [string]$FrontierRow.n64_name
    switch ([string]$FrontierRow.frontier_gate) {
        "pose_metric_or_source_identity" {
            $row = $PoseRows[$n64Name]
            return [pscustomobject]@{
                kind = "pose_source_runtime_capture"
                row = $row
                matrix_status = "pose_source_runtime_capture_workorders_ready"
                action_status = if ($null -eq $row) { "" } else { [string]$row.capture_status }
                action_class = if ($null -eq $row) { "" } else { [string]$row.callsite_review_class }
                acceptance_gate = "installed_context_runtime_capture"
                runtime_trigger = if ($null -eq $row) { "" } else { [string]$row.runtime_trigger }
                runtime_acceptance_status = "pending_installed_context_runtime_capture"
                required_next_step = if ($null -eq $row) { "" } else { [string]$row.capture_runner_requirement }
            }
        }
        "route_or_runtime_capture" {
            $row = $RouteRows[$n64Name]
            return [pscustomobject]@{
                kind = "route_runtime_capture"
                row = $row
                matrix_status = "route_runtime_capture_workorders_ready"
                action_status = if ($null -eq $row) { "" } else { [string]$row.capture_status }
                action_class = if ($null -eq $row) { "" } else { [string]$row.route_context_class }
                acceptance_gate = if ($null -eq $row) { "" } else { [string]$row.route_acceptance_gate }
                runtime_trigger = if ($null -eq $row) { "" } else { [string]$row.route_trigger }
                runtime_acceptance_status = if ($null -eq $row) { "" } else { [string]$row.runtime_acceptance_status }
                required_next_step = if ($null -eq $row) { "" } else { [string]$row.route_acceptance_gate }
            }
        }
        "csab_source_ownership" {
            $row = $OwnershipRows[$n64Name]
            return [pscustomobject]@{
                kind = "ownership_decision"
                row = $row
                matrix_status = "ownership_decision_workorders_ready"
                action_status = if ($null -eq $row) { "" } else { [string]$row.decision_status }
                action_class = if ($null -eq $row) { "" } else { [string]$row.decision_class }
                acceptance_gate = if ($null -eq $row) { "" } else { [string]$row.acceptance_gate }
                runtime_trigger = if ($null -eq $row) { "" } else { [string]$row.route_callsite_package_candidate_status }
                runtime_acceptance_status = if ($null -eq $row) { "" } else { [string]$row.runtime_acceptance_status }
                required_next_step = if ($null -eq $row) { "" } else { [string]$row.required_next_step }
            }
        }
        "anonymous_symbol_identity" {
            $row = $AnonymousRows[$n64Name]
            return [pscustomobject]@{
                kind = "anonymous_owner_capture"
                row = $row
                matrix_status = "anonymous_owner_capture_workorders_ready"
                action_status = if ($null -eq $row) { "" } else { [string]$row.capture_status }
                action_class = if ($null -eq $row) { "" } else { [string]$row.owner_context_class }
                acceptance_gate = if ($null -eq $row) { "" } else { [string]$row.acceptance_gate }
                runtime_trigger = if ($null -eq $row) { "" } else { [string]$row.runtime_trigger }
                runtime_acceptance_status = if ($null -eq $row) { "" } else { [string]$row.runtime_acceptance_status }
                required_next_step = if ($null -eq $row) { "" } else { [string]$row.required_next_step }
            }
        }
    }
    return [pscustomobject]@{
        kind = "unknown"
        row = $null
        matrix_status = ""
        action_status = ""
        action_class = ""
        acceptance_gate = ""
        runtime_trigger = ""
        runtime_acceptance_status = ""
        required_next_step = ""
    }
}

function Get-PostHarnessBlocker([string]$WorkorderKind, [string]$AcceptanceGate, [bool]$RuntimeHarnessDumpValid, [bool]$InstalledRuntimeDumpValid, [bool]$InstalledStaticFrameDumpValid) {
    if ($InstalledRuntimeDumpValid) {
        return "semantic_acceptance_review_required"
    }
    if (-not ($RuntimeHarnessDumpValid -or $InstalledStaticFrameDumpValid)) {
        return "runtime_geometry_harness_required"
    }
    switch ($WorkorderKind) {
        "pose_source_runtime_capture" { return "pose_source_natural_context_capture_required" }
        "route_runtime_capture" { return "route_runtime_natural_context_or_route_policy_required" }
        "anonymous_owner_capture" { return "anonymous_identity_owner_policy_and_natural_capture_required" }
        "ownership_decision" {
            switch ($AcceptanceGate) {
                "source_group_intentional_reuse_policy_and_installed_capture" {
                    return "source_group_intentional_reuse_policy_and_natural_capture_required"
                }
                "route_callsite_package_policy_and_installed_capture" {
                    return "route_callsite_policy_and_natural_capture_required"
                }
                "single_candidate_owner_alias_policy_and_installed_capture" {
                    return "single_candidate_owner_policy_and_natural_capture_required"
                }
                "single_promoted_owner_alias_policy_and_installed_capture" {
                    return "single_promoted_owner_policy_and_natural_capture_required"
                }
                "alternate_source_or_runtime_skinned_parity_before_ownership" {
                    return "alternate_source_or_natural_capture_required"
                }
                default {
                    return "ownership_policy_and_natural_capture_required"
                }
            }
        }
        default { return "semantic_policy_or_natural_capture_required" }
    }
}

$characterRoot = Join-Path $WorkRoot "character_conversion"
$frontierCsv = Join-Path $characterRoot "link_child_animation_semantic_blocked_resolution_frontier.csv"
$poseSummaryPath = Join-Path $characterRoot "pose_source_runtime_capture_matrix\link_child_pose_source_runtime_capture_matrix_summary.json"
$poseCsv = Join-Path $characterRoot "pose_source_runtime_capture_matrix\link_child_pose_source_runtime_capture_matrix.csv"
$routeSummaryPath = Join-Path $characterRoot "route_runtime_capture_matrix\link_child_route_runtime_capture_matrix_summary.json"
$routeCsv = Join-Path $characterRoot "route_runtime_capture_matrix\link_child_route_runtime_capture_matrix.csv"
$ownershipSummaryPath = Join-Path $characterRoot "ownership_decision_matrix\link_child_ownership_decision_matrix_summary.json"
$ownershipCsv = Join-Path $characterRoot "ownership_decision_matrix\link_child_ownership_decision_matrix.csv"
$anonymousSummaryPath = Join-Path $characterRoot "anonymous_owner_capture_matrix\link_child_anonymous_owner_capture_matrix_summary.json"
$anonymousCsv = Join-Path $characterRoot "anonymous_owner_capture_matrix\link_child_anonymous_owner_capture_matrix.csv"
$runtimeCaptureSummaryPath = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix_summary.json"
$runtimeCaptureCsv = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix.csv"

Require-Path $frontierCsv "Blocked frontier CSV"
Require-Path $poseSummaryPath "Pose/source runtime capture summary"
Require-Path $poseCsv "Pose/source runtime capture CSV"
Require-Path $routeSummaryPath "Route/runtime capture summary"
Require-Path $routeCsv "Route/runtime capture CSV"
Require-Path $ownershipSummaryPath "Ownership decision summary"
Require-Path $ownershipCsv "Ownership decision CSV"
Require-Path $anonymousSummaryPath "Anonymous owner capture summary"
Require-Path $anonymousCsv "Anonymous owner capture CSV"
Require-Path $runtimeCaptureSummaryPath "Runtime config capture matrix summary"
Require-Path $runtimeCaptureCsv "Runtime config capture matrix CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$frontierRows = @(Import-Csv -LiteralPath $frontierCsv)
$poseSummary = Read-Json $poseSummaryPath
$routeSummary = Read-Json $routeSummaryPath
$ownershipSummary = Read-Json $ownershipSummaryPath
$anonymousSummary = Read-Json $anonymousSummaryPath
$runtimeCaptureSummary = Read-Json $runtimeCaptureSummaryPath

$poseRows = @{}
$routeRows = @{}
$ownershipRows = @{}
$anonymousRows = @{}
$runtimeCaptureRows = @{}
Add-ByName $poseRows @(Import-Csv -LiteralPath $poseCsv)
Add-ByName $routeRows @(Import-Csv -LiteralPath $routeCsv)
Add-ByName $ownershipRows @(Import-Csv -LiteralPath $ownershipCsv)
Add-ByName $anonymousRows @(Import-Csv -LiteralPath $anonymousCsv)
Add-ByName $runtimeCaptureRows @(Import-Csv -LiteralPath $runtimeCaptureCsv)

$records = New-Object "System.Collections.Generic.List[object]"
$frontierGateCounts = @{}
$frontierClassCounts = @{}
$workorderKindCounts = @{}
$coverageStatusCounts = @{}
$actionStatusCounts = @{}
$acceptanceGateCounts = @{}
$runtimeAcceptanceStatusCounts = @{}
$matrixStatusCounts = @{}
$runtimeCaptureStatusCounts = @{}
$harnessEvidenceStatusCounts = @{}
$postHarnessBlockerCounts = @{}
$issueCount = 0
$coveredWorkorderCount = 0
$readyWorkorderCount = 0
$semanticAcceptedCount = 0
$installedDumpValidCount = 0
$installedStaticFrameDumpValidCount = 0
$runtimeHarnessDumpValidCount = 0
$runtimeHarnessGeometryGapCount = 0
$postHarnessSemanticBlockedCount = 0

foreach ($frontier in $frontierRows) {
    $n64Name = [string]$frontier.n64_name
    $workorder = Get-Workorder $frontier $poseRows $routeRows $ownershipRows $anonymousRows
    $workorderRow = $workorder.row
    $runtimeCaptureRow = $runtimeCaptureRows[$n64Name]
    $issues = New-Object "System.Collections.Generic.List[object]"
    $coverageStatus = "covered_by_verified_workorder"
    if ($null -eq $workorderRow) {
        $coverageStatus = "missing_workorder"
        $issues.Add([pscustomobject]@{ reason = "missing_workorder"; detail = $n64Name }) | Out-Null
    }
    else {
        $coveredWorkorderCount++
    }
    $rowIssueCount = if ($null -eq $workorderRow) { 1 } else { [int]$workorderRow.issue_count }
    if ($rowIssueCount -ne 0) {
        $coverageStatus = "workorder_has_issues"
        $issues.Add([pscustomobject]@{ reason = "workorder_issue_count"; detail = [string]$rowIssueCount }) | Out-Null
    }
    if ($null -eq $runtimeCaptureRow) {
        $issues.Add([pscustomobject]@{ reason = "missing_runtime_capture_matrix_row"; detail = $n64Name }) | Out-Null
    }
    if ($coverageStatus -eq "covered_by_verified_workorder") {
        $readyWorkorderCount++
    }

    $captureStatus = if ($null -eq $runtimeCaptureRow) { "missing" } else { [string]$runtimeCaptureRow.capture_status }
    $installedRuntimeDumpValid = if ($null -eq $runtimeCaptureRow) { $false } else { Get-BoolValue $runtimeCaptureRow.installed_runtime_dump_valid }
    $installedStaticFrameDumpValid = if ($null -eq $runtimeCaptureRow) { $false } else { Get-BoolValue $runtimeCaptureRow.installed_static_frame_dump_valid }
    $runtimeHarnessDumpValid = if ($null -eq $runtimeCaptureRow) { $false } else { Get-BoolValue $runtimeCaptureRow.runtime_harness_dump_valid }
    $harnessEvidenceStatus = "missing_runtime_capture_matrix_row"
    if ($installedRuntimeDumpValid) {
        $harnessEvidenceStatus = "natural_installed_dump_valid"
        $installedDumpValidCount++
    }
    elseif ($installedStaticFrameDumpValid) {
        $harnessEvidenceStatus = "natural_static_frame_dump_valid"
        $installedStaticFrameDumpValidCount++
    }
    elseif ($runtimeHarnessDumpValid) {
        $harnessEvidenceStatus = "harness_geometry_valid"
        $runtimeHarnessDumpValidCount++
    }
    elseif ($null -ne $runtimeCaptureRow -and (Get-BoolValue $runtimeCaptureRow.dump_exists)) {
        $harnessEvidenceStatus = "runtime_dump_present_but_not_harness_valid"
        $runtimeHarnessGeometryGapCount++
    }
    else {
        $harnessEvidenceStatus = "runtime_geometry_dump_missing"
        $runtimeHarnessGeometryGapCount++
    }
    $postHarnessBlocker = Get-PostHarnessBlocker ([string]$workorder.kind) ([string]$workorder.acceptance_gate) $runtimeHarnessDumpValid $installedRuntimeDumpValid $installedStaticFrameDumpValid
    $requiredNextStep = if ($installedRuntimeDumpValid) {
        "semantic_acceptance_review_required"
    }
    else {
        [string]$workorder.required_next_step
    }
    if (($runtimeHarnessDumpValid -or $installedStaticFrameDumpValid) -and -not $installedRuntimeDumpValid) {
        $postHarnessSemanticBlockedCount++
    }

    $issueCount += $issues.Count

    Add-Count $frontierGateCounts ([string]$frontier.frontier_gate)
    Add-Count $frontierClassCounts ([string]$frontier.frontier_class)
    Add-Count $workorderKindCounts ([string]$workorder.kind)
    Add-Count $coverageStatusCounts $coverageStatus
    Add-Count $actionStatusCounts ([string]$workorder.action_status)
    Add-Count $acceptanceGateCounts ([string]$workorder.acceptance_gate)
    Add-Count $runtimeAcceptanceStatusCounts ([string]$workorder.runtime_acceptance_status)
    Add-Count $matrixStatusCounts ([string]$workorder.matrix_status)
    Add-Count $runtimeCaptureStatusCounts $captureStatus
    Add-Count $harnessEvidenceStatusCounts $harnessEvidenceStatus
    Add-Count $postHarnessBlockerCounts $postHarnessBlocker

    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        n64_data_name = [string]$frontier.n64_data_name
        n64_frame_count = [int]$frontier.n64_frame_count
        source_csab_name = [string]$frontier.source_csab_name
        source_frame_slot_count = [int]$frontier.source_frame_slot_count
        semantic_resolution_status = [string]$frontier.semantic_resolution_status
        source_contract_kind = [string]$frontier.source_contract_kind
        frontier_gate = [string]$frontier.frontier_gate
        frontier_class = [string]$frontier.frontier_class
        workorder_kind = [string]$workorder.kind
        matrix_status = [string]$workorder.matrix_status
        coverage_status = $coverageStatus
        action_status = [string]$workorder.action_status
        action_class = [string]$workorder.action_class
        acceptance_gate = [string]$workorder.acceptance_gate
        runtime_trigger = [string]$workorder.runtime_trigger
        runtime_acceptance_status = [string]$workorder.runtime_acceptance_status
        runtime_capture_status = $captureStatus
        runtime_harness_dump_valid = $runtimeHarnessDumpValid
        harness_evidence_status = $harnessEvidenceStatus
        post_harness_blocker = $postHarnessBlocker
        semantic_acceptance_status = "blocked_pending_workorder_evidence"
        installed_runtime_dump_valid = $installedRuntimeDumpValid
        installed_static_frame_dump_valid = $installedStaticFrameDumpValid
        mapping_promotion_allowed = $false
        required_next_step = $requiredNextStep
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray())
$summaryOutput = Join-Path $OutputRoot "link_child_semantic_frontier_closure_matrix_summary.json"
$csvOutput = Join-Path $OutputRoot "link_child_semantic_frontier_closure_matrix.csv"
$status = if ($issueCount -eq 0 -and $recordArray.Count -gt 0 -and $coveredWorkorderCount -eq $recordArray.Count) {
    "semantic_frontier_closure_workorders_ready"
}
elseif ($recordArray.Count -eq 0) {
    "complete_no_blocked_frontier_rows"
}
else {
    "semantic_frontier_closure_workorders_have_issues"
}

$summary = [pscustomobject]@{
    format = "oot3d_link_child_semantic_frontier_closure_matrix_v1"
    status = $status
    policy = [ordered]@{
        scope = "single closure ledger for all Link child blocked N64 PlayerAnimation semantic frontier rows"
        semantic_effect = "summarizes verified workorders only; no mappings, identities, ownership decisions, reuse policies, routes, or dumps are accepted"
        promotion_policy = "mapping promotion remains forbidden for every row until its workorder evidence and ownership/runtime acceptance gate are satisfied"
    }
    blocked_frontier_csv = (Resolve-Path -LiteralPath $frontierCsv).Path
    workorder_sources = [ordered]@{
        pose_source_runtime_capture_summary = (Resolve-Path -LiteralPath $poseSummaryPath).Path
        route_runtime_capture_summary = (Resolve-Path -LiteralPath $routeSummaryPath).Path
        ownership_decision_summary = (Resolve-Path -LiteralPath $ownershipSummaryPath).Path
        anonymous_owner_capture_summary = (Resolve-Path -LiteralPath $anonymousSummaryPath).Path
        runtime_config_capture_summary = (Resolve-Path -LiteralPath $runtimeCaptureSummaryPath).Path
    }
    output_root = $OutputRoot
    record_count = $recordArray.Count
    blocked_frontier_row_count = $frontierRows.Count
    covered_workorder_count = $coveredWorkorderCount
    ready_workorder_count = $readyWorkorderCount
    missing_workorder_count = $recordArray.Count - $coveredWorkorderCount
    semantic_accepted_count = $semanticAcceptedCount
    installed_runtime_dump_valid_count = $installedDumpValidCount
    installed_static_frame_dump_valid_count = $installedStaticFrameDumpValidCount
    runtime_harness_dump_valid_count = $runtimeHarnessDumpValidCount
    runtime_harness_geometry_gap_count = $runtimeHarnessGeometryGapCount
    post_harness_semantic_blocked_count = $postHarnessSemanticBlockedCount
    mapping_promotion_allowed_count = 0
    issue_count = $issueCount
    matrix_input_counts = [ordered]@{
        pose_source_runtime_capture = [int](Get-JsonValue $poseSummary "record_count")
        route_runtime_capture = [int](Get-JsonValue $routeSummary "record_count")
        ownership_decision = [int](Get-JsonValue $ownershipSummary "record_count")
        anonymous_owner_capture = [int](Get-JsonValue $anonymousSummary "record_count")
        runtime_config_capture = [int](Get-JsonValue $runtimeCaptureSummary "record_count")
    }
    frontier_gate_counts = $frontierGateCounts
    frontier_class_counts = $frontierClassCounts
    workorder_kind_counts = $workorderKindCounts
    coverage_status_counts = $coverageStatusCounts
    action_status_counts = $actionStatusCounts
    acceptance_gate_counts = $acceptanceGateCounts
    runtime_acceptance_status_counts = $runtimeAcceptanceStatusCounts
    matrix_status_counts = $matrixStatusCounts
    runtime_capture_status_counts = $runtimeCaptureStatusCounts
    harness_evidence_status_counts = $harnessEvidenceStatusCounts
    post_harness_blocker_counts = $postHarnessBlockerCounts
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryOutput -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,semantic_resolution_status,source_contract_kind,frontier_gate,frontier_class,workorder_kind,coverage_status,action_status,action_class,acceptance_gate,runtime_trigger,runtime_acceptance_status,runtime_capture_status,runtime_harness_dump_valid,harness_evidence_status,post_harness_blocker,semantic_acceptance_status,installed_runtime_dump_valid,installed_static_frame_dump_valid,mapping_promotion_allowed,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvOutput -NoTypeInformation -Encoding UTF8

if ($Verify) {
    $runtimeValidEvidenceCount = [int]$summary.installed_runtime_dump_valid_count + [int]$summary.installed_static_frame_dump_valid_count + [int]$summary.runtime_harness_dump_valid_count
    Assert-Condition ($summary.format -eq "oot3d_link_child_semantic_frontier_closure_matrix_v1") "unexpected closure matrix format"
    Assert-Condition ($summary.status -eq "semantic_frontier_closure_workorders_ready") "expected ready semantic frontier closure workorders"
    Assert-Condition ([int]$summary.record_count -eq 63) "expected 63 semantic frontier closure rows"
    Assert-Condition ([int]$summary.blocked_frontier_row_count -eq 63) "expected 63 blocked frontier rows"
    Assert-Condition ([int]$summary.covered_workorder_count -eq 63) "expected every frontier row to be covered by a workorder"
    Assert-Condition ([int]$summary.ready_workorder_count -eq 63) "expected every frontier row to have a ready workorder"
    Assert-Condition ([int]$summary.missing_workorder_count -eq 0) "expected no missing workorders"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected no semantic acceptances from closure matrix"
    Assert-Condition ($runtimeValidEvidenceCount -eq 63) "expected 63 runtime-valid dumps across installed and harness evidence"
    Assert-Condition ([int]$summary.runtime_harness_geometry_gap_count -eq 0) "expected zero runtime geometry gaps after hold_free dynamic proof"
    Assert-Condition (([int]$summary.post_harness_semantic_blocked_count + [int]$summary.installed_runtime_dump_valid_count + [int]$summary.runtime_harness_geometry_gap_count) -eq 63) "expected every row to remain semantically blocked, require installed-dump review, or wait for geometry recapture"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected no mapping promotions from closure matrix"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero closure matrix issues"
    Assert-Condition ([int]$summary.matrix_input_counts.pose_source_runtime_capture -eq 16) "expected 16 pose/source workorders"
    Assert-Condition ([int]$summary.matrix_input_counts.route_runtime_capture -eq 8) "expected 8 route/runtime workorders"
    Assert-Condition ([int]$summary.matrix_input_counts.ownership_decision -eq 28) "expected 28 ownership decision workorders"
    Assert-Condition ([int]$summary.matrix_input_counts.anonymous_owner_capture -eq 11) "expected 11 anonymous owner capture workorders"
    Assert-Condition ([int]$summary.matrix_input_counts.runtime_config_capture -eq 63) "expected 63 runtime capture matrix rows"
    Assert-Condition ((Get-JsonValue $summary.frontier_gate_counts "pose_metric_or_source_identity") -eq 16) "expected 16 pose/source frontier rows"
    Assert-Condition ((Get-JsonValue $summary.frontier_gate_counts "route_or_runtime_capture") -eq 8) "expected 8 route/runtime frontier rows"
    Assert-Condition ((Get-JsonValue $summary.frontier_gate_counts "csab_source_ownership") -eq 28) "expected 28 ownership frontier rows"
    Assert-Condition ((Get-JsonValue $summary.frontier_gate_counts "anonymous_symbol_identity") -eq 11) "expected 11 anonymous frontier rows"
    Assert-Condition ((Get-JsonValue $summary.workorder_kind_counts "pose_source_runtime_capture") -eq 16) "expected 16 pose/source closure workorders"
    Assert-Condition ((Get-JsonValue $summary.workorder_kind_counts "route_runtime_capture") -eq 8) "expected 8 route/runtime closure workorders"
    Assert-Condition ((Get-JsonValue $summary.workorder_kind_counts "ownership_decision") -eq 28) "expected 28 ownership closure workorders"
    Assert-Condition ((Get-JsonValue $summary.workorder_kind_counts "anonymous_owner_capture") -eq 11) "expected 11 anonymous closure workorders"
    Assert-Condition ((Get-JsonValue $summary.coverage_status_counts "covered_by_verified_workorder") -eq 63) "expected every closure row covered by a verified workorder"
    Assert-Condition (((Get-IntValue (Get-JsonValue $summary.harness_evidence_status_counts "harness_geometry_valid")) + (Get-IntValue (Get-JsonValue $summary.harness_evidence_status_counts "natural_installed_dump_valid")) + (Get-IntValue (Get-JsonValue $summary.harness_evidence_status_counts "natural_static_frame_dump_valid"))) -eq 63) "expected 63 closure rows to have runtime geometry evidence"
    Assert-Condition ((Get-IntValue (Get-JsonValue $summary.harness_evidence_status_counts "runtime_geometry_dump_missing")) -eq 0) "expected zero closure rows waiting for dynamic geometry recapture"
    Assert-Condition (Test-Path -LiteralPath $csvOutput) "expected semantic frontier closure CSV output"
}

Write-Host "OOT3D Link child semantic frontier closure matrix: $summaryOutput"
Write-Host "status=$($summary.status) rows=$($summary.record_count) covered=$($summary.covered_workorder_count) ready=$($summary.ready_workorder_count) harnessValid=$($summary.runtime_harness_dump_valid_count) installedValid=$($summary.installed_runtime_dump_valid_count) staticFrameValid=$($summary.installed_static_frame_dump_valid_count) postHarnessBlocked=$($summary.post_harness_semantic_blocked_count) issues=$($summary.issue_count)"
