param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$ExecutionPlan = "",
    [string]$ClosureSummary = "",
    [string]$RouteProvenRuntimeCaptureMatrix = "",
    [string]$PoseSourceRuntimeConfigMatrix = "",
    [string]$RouteRuntimeConfigMatrix = "",
    [string]$AnonymousOwnerRuntimeConfigMatrix = "",
    [string]$OwnershipReuseRuntimeConfigMatrix = "",
    [string]$OwnershipSourceRiskRuntimeConfigMatrix = "",
    [string]$OwnershipDecisionMatrix = "",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ExecutionPlan)) {
    $ExecutionPlan = Join-Path $WorkRoot "character_conversion\runtime_capture_execution_plan\link_child_runtime_capture_execution_plan_summary.json"
}
if ([string]::IsNullOrWhiteSpace($ClosureSummary)) {
    $ClosureSummary = Join-Path $WorkRoot "character_conversion\semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix_summary.json"
}
if ([string]::IsNullOrWhiteSpace($RouteProvenRuntimeCaptureMatrix)) {
    $RouteProvenRuntimeCaptureMatrix = Join-Path $WorkRoot "character_conversion\route_proven_ownership_runtime_capture\link_child_route_proven_runtime_capture_matrix_summary.json"
}
if ([string]::IsNullOrWhiteSpace($PoseSourceRuntimeConfigMatrix)) {
    $PoseSourceRuntimeConfigMatrix = Join-Path $WorkRoot "character_conversion\pose_source_runtime_capture_config\link_child_pose_source_runtime_config_matrix_summary.json"
}
if ([string]::IsNullOrWhiteSpace($RouteRuntimeConfigMatrix)) {
    $RouteRuntimeConfigMatrix = Join-Path $WorkRoot "character_conversion\route_runtime_capture_config\link_child_route_runtime_config_matrix_summary.json"
}
if ([string]::IsNullOrWhiteSpace($AnonymousOwnerRuntimeConfigMatrix)) {
    $AnonymousOwnerRuntimeConfigMatrix = Join-Path $WorkRoot "character_conversion\anonymous_owner_runtime_capture_config\link_child_anonymous_owner_runtime_config_matrix_summary.json"
}
if ([string]::IsNullOrWhiteSpace($OwnershipReuseRuntimeConfigMatrix)) {
    $OwnershipReuseRuntimeConfigMatrix = Join-Path $WorkRoot "character_conversion\ownership_reuse_runtime_capture_config\link_child_ownership_reuse_runtime_config_matrix_summary.json"
}
if ([string]::IsNullOrWhiteSpace($OwnershipSourceRiskRuntimeConfigMatrix)) {
    $OwnershipSourceRiskRuntimeConfigMatrix = Join-Path $WorkRoot "character_conversion\ownership_source_risk_runtime_capture_config\link_child_ownership_source_risk_runtime_config_matrix_summary.json"
}
if ([string]::IsNullOrWhiteSpace($OwnershipDecisionMatrix)) {
    $OwnershipDecisionMatrix = Join-Path $WorkRoot "character_conversion\ownership_decision_matrix\link_child_ownership_decision_matrix_summary.json"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\residual_closure_kit"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child residual closure kit verification failed: $Message"
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

function Add-Count([hashtable]$Counts, [string]$Key) {
    if ([string]::IsNullOrWhiteSpace($Key)) {
        $Key = "unknown"
    }
    if (-not $Counts.ContainsKey($Key)) {
        $Counts[$Key] = 0
    }
    $Counts[$Key] = [int]$Counts[$Key] + 1
}

function Add-Issue([System.Collections.Generic.List[object]]$Issues, [string]$Reason, [object]$Detail = $null) {
    $Issues.Add([pscustomobject]@{
        reason = $Reason
        detail = $Detail
    }) | Out-Null
}

function New-RecordMap([object[]]$Records) {
    $map = @{}
    foreach ($record in @($Records)) {
        $name = [string](Get-JsonValue $record "n64_name")
        if (-not [string]::IsNullOrWhiteSpace($name)) {
            $map[$name] = $record
        }
    }
    return $map
}

function Get-Lane([string]$WorkorderKind, [string]$ActionStatus) {
    if ($WorkorderKind -eq "ownership_decision" -and $ActionStatus -eq "route_proven_pending_installed_capture_and_policy") {
        return "route_proven_existing_capture"
    }
    if ($WorkorderKind -eq "pose_source_runtime_capture") {
        return "pose_source_context_capture"
    }
    if ($WorkorderKind -eq "route_runtime_capture") {
        return "route_runtime_context_capture"
    }
    if ($WorkorderKind -eq "anonymous_owner_capture") {
        return "anonymous_owner_context_capture"
    }
    if ($WorkorderKind -eq "ownership_decision" -and $ActionStatus -eq "ownership_plus_pose_source_risk_requires_alternate_source_or_runtime_parity") {
        return "pose_source_dependency"
    }
    if ($WorkorderKind -eq "ownership_decision") {
        return "ownership_reuse_policy"
    }
    return "manual_review"
}

function Get-ConfigRecord([string]$Lane, [string]$N64Name, [hashtable]$Maps) {
    switch ($Lane) {
        "route_proven_existing_capture" { return $Maps.routeProven[$N64Name] }
        "pose_source_context_capture" { return $Maps.poseSource[$N64Name] }
        "route_runtime_context_capture" { return $Maps.routeRuntime[$N64Name] }
        "anonymous_owner_context_capture" { return $Maps.anonymousOwner[$N64Name] }
        "ownership_reuse_policy" { return $Maps.ownershipReuse[$N64Name] }
        "pose_source_dependency" { return $Maps.ownershipSourceRisk[$N64Name] }
    }
    return $null
}

Require-Path $ExecutionPlan "Runtime capture execution plan"
Require-Path $ClosureSummary "Semantic frontier closure summary"
Require-Path $RouteProvenRuntimeCaptureMatrix "Route-proven runtime capture matrix"
Require-Path $PoseSourceRuntimeConfigMatrix "Pose/source runtime config matrix"
Require-Path $RouteRuntimeConfigMatrix "Route/runtime config matrix"
Require-Path $AnonymousOwnerRuntimeConfigMatrix "Anonymous-owner runtime config matrix"
Require-Path $OwnershipReuseRuntimeConfigMatrix "Ownership/reuse runtime config matrix"
Require-Path $OwnershipSourceRiskRuntimeConfigMatrix "Ownership source-risk runtime config matrix"
Require-Path $OwnershipDecisionMatrix "Ownership decision matrix"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$executionPlanObject = Get-Content -LiteralPath $ExecutionPlan -Raw | ConvertFrom-Json
$closure = Get-Content -LiteralPath $ClosureSummary -Raw | ConvertFrom-Json
$routeProven = Get-Content -LiteralPath $RouteProvenRuntimeCaptureMatrix -Raw | ConvertFrom-Json
$poseSource = Get-Content -LiteralPath $PoseSourceRuntimeConfigMatrix -Raw | ConvertFrom-Json
$routeRuntime = Get-Content -LiteralPath $RouteRuntimeConfigMatrix -Raw | ConvertFrom-Json
$anonymousOwner = Get-Content -LiteralPath $AnonymousOwnerRuntimeConfigMatrix -Raw | ConvertFrom-Json
$ownershipReuse = Get-Content -LiteralPath $OwnershipReuseRuntimeConfigMatrix -Raw | ConvertFrom-Json
$ownershipSourceRisk = Get-Content -LiteralPath $OwnershipSourceRiskRuntimeConfigMatrix -Raw | ConvertFrom-Json
$ownership = Get-Content -LiteralPath $OwnershipDecisionMatrix -Raw | ConvertFrom-Json

$maps = @{
    routeProven = New-RecordMap @($routeProven.records)
    poseSource = New-RecordMap @($poseSource.records)
    routeRuntime = New-RecordMap @($routeRuntime.records)
    anonymousOwner = New-RecordMap @($anonymousOwner.records)
    ownershipReuse = New-RecordMap @($ownershipReuse.records)
    ownershipSourceRisk = New-RecordMap @($ownershipSourceRisk.records)
}

$records = New-Object "System.Collections.Generic.List[object]"
$laneCounts = @{}
$workorderKindCounts = @{}
$actionStatusCounts = @{}
$runnerStatusCounts = @{}
$configStatusCounts = @{}
$issueCount = 0
$captureConfigReadyCount = 0
$contextCaptureConfigReadyCount = 0
$routeProvenCaptureConfigReadyCount = 0
$ownershipReusePolicyConfigReadyCount = 0
$poseSourceDependencyConfigReadyCount = 0
$installedDumpValidCount = 0
$semanticAcceptedCount = 0
$mappingPromotionAllowedCount = 0

foreach ($row in @($executionPlanObject.records)) {
    $n64Name = [string](Get-JsonValue $row "n64_name")
    $workorderKind = [string](Get-JsonValue $row "workorder_kind")
    $actionStatus = [string](Get-JsonValue $row "action_status")
    $lane = Get-Lane -WorkorderKind $workorderKind -ActionStatus $actionStatus
    $configRecord = Get-ConfigRecord -Lane $lane -N64Name $n64Name -Maps $maps
    $rowIssues = New-Object "System.Collections.Generic.List[object]"

    $requiresConfig = $lane -in @(
        "route_proven_existing_capture",
        "pose_source_context_capture",
        "route_runtime_context_capture",
        "anonymous_owner_context_capture",
        "ownership_reuse_policy",
        "pose_source_dependency"
    )
    if ($requiresConfig -and $null -eq $configRecord) {
        Add-Issue $rowIssues "missing_runtime_config_record" @{
            lane = $lane
            n64_name = $n64Name
        }
    }

    $configStatus = if ($null -eq $configRecord) { "not_applicable" } else { [string](Get-JsonValue $configRecord "status") }
    $configSummary = if ($null -eq $configRecord) { "" } else { [string](Get-JsonValue $configRecord "config_summary") }
    $statusSummary = if ($null -eq $configRecord) { "" } else {
        $statusPath = [string](Get-JsonValue $configRecord "status_summary")
        if ([string]::IsNullOrWhiteSpace($statusPath)) {
            [string](Get-JsonValue $configRecord "capture_status")
        }
        else {
            $statusPath
        }
    }
    $expectedDumpPath = if ($null -eq $configRecord) {
        [string](Get-JsonValue $row "absolute_expected_dump_path")
    }
    else {
        [string](Get-JsonValue $configRecord "expected_dump_path")
    }
    $expectedContextTracePath = if ($null -eq $configRecord) { "" } else { [string](Get-JsonValue $configRecord "expected_context_trace_path") }
    $dumpValid = $configStatus -eq "dump_valid"
    $configReady = $configStatus -eq "ready_for_capture" -or $configStatus -eq "dump_present" -or $dumpValid
    if ($configReady) {
        $captureConfigReadyCount++
        if ($lane -eq "route_proven_existing_capture") {
            $routeProvenCaptureConfigReadyCount++
        }
        elseif ($lane -in @("pose_source_context_capture", "route_runtime_context_capture", "anonymous_owner_context_capture")) {
            $contextCaptureConfigReadyCount++
        }
        elseif ($lane -eq "ownership_reuse_policy") {
            $ownershipReusePolicyConfigReadyCount++
        }
        elseif ($lane -eq "pose_source_dependency") {
            $poseSourceDependencyConfigReadyCount++
        }
    }
    if ($dumpValid) {
        $installedDumpValidCount++
    }

    $issueCount += $rowIssues.Count
    Add-Count $laneCounts $lane
    Add-Count $workorderKindCounts $workorderKind
    Add-Count $actionStatusCounts $actionStatus
    Add-Count $runnerStatusCounts ([string](Get-JsonValue $row "runner_status"))
    Add-Count $configStatusCounts $configStatus

    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        n64_data_name = [string](Get-JsonValue $row "n64_data_name")
        source_csab_name = [string](Get-JsonValue $row "source_csab_name")
        workorder_kind = $workorderKind
        action_status = $actionStatus
        lane = $lane
        phase = [string](Get-JsonValue $row "phase")
        runner_status = [string](Get-JsonValue $row "runner_status")
        execution_status = [string](Get-JsonValue $row "execution_status")
        operator_command = [string](Get-JsonValue $row "operator_command")
        config_status = $configStatus
        config_summary = $configSummary
        status_summary = $statusSummary
        expected_dump_path = $expectedDumpPath
        expected_context_trace_path = $expectedContextTracePath
        capture_config_ready = [bool]$configReady
        installed_runtime_dump_valid = [bool]$dumpValid
        semantic_acceptance_status = "blocked_pending_lane_evidence"
        mapping_promotion_allowed = $false
        acceptance_gate = [string](Get-JsonValue $row "acceptance_gate")
        required_next_step = [string](Get-JsonValue $row "required_next_step")
        issue_count = [int]$rowIssues.Count
        issues = @($rowIssues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray())
$summaryOutput = Join-Path $OutputRoot "link_child_residual_closure_kit_summary.json"
$csvOutput = Join-Path $OutputRoot "link_child_residual_closure_kit.csv"
$status = if ($issueCount -eq 0 -and $contextCaptureConfigReadyCount -eq 35 -and $routeProvenCaptureConfigReadyCount -eq 5 -and $ownershipReusePolicyConfigReadyCount -eq 21 -and $poseSourceDependencyConfigReadyCount -eq 2) {
    "residual_closure_kit_ready"
}
else {
    "residual_closure_kit_has_issues"
}

$summary = [ordered]@{
    format = "oot3d_link_child_residual_closure_kit_v1"
    status = $status
    policy = [ordered]@{
        scope = "operational closure kit for the 63 Link child semantic frontier residues"
        semantic_effect = "readiness and command queue only; no identities, ownership decisions, route acceptances, dumps, semantic overlays, or mapping promotions are accepted"
        acceptance_policy = "each row still needs its lane evidence: installed dump plus route/context proof, ownership policy, or pose/source dependency resolution"
    }
    execution_plan = (Resolve-Path -LiteralPath $ExecutionPlan).Path
    closure_summary = (Resolve-Path -LiteralPath $ClosureSummary).Path
    route_proven_runtime_capture_matrix = (Resolve-Path -LiteralPath $RouteProvenRuntimeCaptureMatrix).Path
    pose_source_runtime_config_matrix = (Resolve-Path -LiteralPath $PoseSourceRuntimeConfigMatrix).Path
    route_runtime_config_matrix = (Resolve-Path -LiteralPath $RouteRuntimeConfigMatrix).Path
    anonymous_owner_runtime_config_matrix = (Resolve-Path -LiteralPath $AnonymousOwnerRuntimeConfigMatrix).Path
    ownership_reuse_runtime_config_matrix = (Resolve-Path -LiteralPath $OwnershipReuseRuntimeConfigMatrix).Path
    ownership_source_risk_runtime_config_matrix = (Resolve-Path -LiteralPath $OwnershipSourceRiskRuntimeConfigMatrix).Path
    ownership_decision_matrix = (Resolve-Path -LiteralPath $OwnershipDecisionMatrix).Path
    output_root = $OutputRoot
    record_count = $recordArray.Count
    closure_record_count = [int](Get-JsonValue $closure "record_count")
    route_proven_existing_capture_config_ready_count = $routeProvenCaptureConfigReadyCount
    context_capture_passive_config_ready_count = $contextCaptureConfigReadyCount
    ownership_reuse_policy_config_ready_count = $ownershipReusePolicyConfigReadyCount
    pose_source_dependency_config_ready_count = $poseSourceDependencyConfigReadyCount
    capture_config_ready_count = $captureConfigReadyCount
    pose_source_runtime_config_ready_count = [int](Get-JsonValue $poseSource "ready_for_capture_count")
    route_runtime_config_ready_count = [int](Get-JsonValue $routeRuntime "ready_for_capture_count")
    anonymous_owner_runtime_config_ready_count = [int](Get-JsonValue $anonymousOwner "ready_for_capture_count")
    ownership_reuse_runtime_config_ready_count = [int](Get-JsonValue $ownershipReuse "ready_for_capture_count")
    ownership_source_risk_runtime_config_ready_count = [int](Get-JsonValue $ownershipSourceRisk "ready_for_capture_count")
    ownership_policy_queue_count = [int](Get-JsonValue $ownership "route_absent_policy_candidate_count")
    pose_source_dependency_count = [int](Get-JsonValue $executionPlanObject "pose_source_dependency_count")
    installed_runtime_dump_valid_count = $installedDumpValidCount
    semantic_accepted_count = $semanticAcceptedCount
    mapping_promotion_allowed_count = $mappingPromotionAllowedCount
    issue_count = $issueCount
    lane_counts = $laneCounts
    workorder_kind_counts = $workorderKindCounts
    action_status_counts = $actionStatusCounts
    runner_status_counts = $runnerStatusCounts
    config_status_counts = $configStatusCounts
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryOutput -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,workorder_kind,action_status,lane,runner_status,execution_status,config_status,config_summary,status_summary,expected_dump_path,expected_context_trace_path,operator_command,acceptance_gate,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvOutput -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_residual_closure_kit_v1") "unexpected closure kit format"
    Assert-Condition ($summary.status -eq "residual_closure_kit_ready") "expected closure kit ready"
    Assert-Condition ([int]$summary.record_count -eq 63) "expected 63 closure kit rows"
    Assert-Condition ([int]$summary.closure_record_count -eq 63) "expected 63 closure input rows"
    Assert-Condition ([int]$summary.route_proven_existing_capture_config_ready_count -eq 5) "expected five route-proven capture configs ready"
    Assert-Condition ([int]$summary.context_capture_passive_config_ready_count -eq 35) "expected all 35 context-capture rows to have passive configs ready"
    Assert-Condition ([int]$summary.ownership_reuse_policy_config_ready_count -eq 21) "expected all 21 ownership/reuse policy rows to have passive configs ready"
    Assert-Condition ([int]$summary.pose_source_dependency_config_ready_count -eq 2) "expected both pose/source dependency rows to have passive configs ready"
    Assert-Condition ([int]$summary.capture_config_ready_count -eq 63) "expected 63 capture-config-ready rows"
    Assert-Condition ([int]$summary.pose_source_runtime_config_ready_count -eq 16) "expected 16 pose/source configs ready"
    Assert-Condition ([int]$summary.route_runtime_config_ready_count -eq 8) "expected 8 route/runtime configs ready"
    Assert-Condition ([int]$summary.anonymous_owner_runtime_config_ready_count -eq 11) "expected 11 anonymous-owner configs ready"
    Assert-Condition ([int]$summary.ownership_reuse_runtime_config_ready_count -eq 21) "expected 21 ownership/reuse configs ready"
    Assert-Condition ([int]$summary.ownership_source_risk_runtime_config_ready_count -eq 2) "expected 2 ownership source-risk configs ready"
    Assert-Condition ([int]$summary.ownership_policy_queue_count -eq 21) "expected 21 ownership policy rows"
    Assert-Condition ([int]$summary.pose_source_dependency_count -eq 2) "expected 2 pose/source dependency rows"
    Assert-Condition ([int]$summary.installed_runtime_dump_valid_count -eq 0) "expected zero accepted installed runtime dumps"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected zero semantic acceptances"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected zero mapping promotions"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero closure kit issues"
    Assert-Condition ((Get-JsonValue $summary.lane_counts "route_proven_existing_capture") -eq 5) "expected five route-proven lane rows"
    Assert-Condition ((Get-JsonValue $summary.lane_counts "pose_source_context_capture") -eq 16) "expected 16 pose/source lane rows"
    Assert-Condition ((Get-JsonValue $summary.lane_counts "route_runtime_context_capture") -eq 8) "expected 8 route/runtime lane rows"
    Assert-Condition ((Get-JsonValue $summary.lane_counts "anonymous_owner_context_capture") -eq 11) "expected 11 anonymous-owner lane rows"
    Assert-Condition ((Get-JsonValue $summary.lane_counts "ownership_reuse_policy") -eq 21) "expected 21 ownership policy lane rows"
    Assert-Condition ((Get-JsonValue $summary.lane_counts "pose_source_dependency") -eq 2) "expected 2 pose/source dependency lane rows"
    Assert-Condition (Test-Path -LiteralPath $csvOutput) "expected closure kit CSV output"
}

Write-Host "OOT3D Link child residual closure kit: $summaryOutput"
Write-Host "status=$($summary.status) rows=$($summary.record_count) captureConfigs=$($summary.capture_config_ready_count) contextConfigs=$($summary.context_capture_passive_config_ready_count) policy=$($summary.ownership_policy_queue_count) issues=$($summary.issue_count)"
