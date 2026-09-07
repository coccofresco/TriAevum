param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\ownership_source_risk_dependency_policy"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child ownership source-risk dependency verification failed: $Message"
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

function Add-ByName([hashtable]$Map, [object[]]$Rows) {
    foreach ($row in @($Rows)) {
        $Map[[string]$row.n64_name] = $row
    }
}

$characterRoot = Join-Path $WorkRoot "character_conversion"
$ownershipDecisionCsv = Join-Path $characterRoot "ownership_decision_matrix\link_child_ownership_decision_matrix.csv"
$packageSummaryPath = Join-Path $characterRoot "ownership_source_risk_diagnostic_package\link_child_ownership_source_risk_diagnostic_package_summary.json"
$runtimeParitySummaryPath = Join-Path $characterRoot "ownership_source_risk_diagnostic_runtime_parity\link_child_ownership_source_risk_diagnostic_runtime_parity_summary.json"
$runtimeConfigSummaryPath = Join-Path $characterRoot "ownership_source_risk_runtime_capture_config\link_child_ownership_source_risk_runtime_config_matrix_summary.json"
$closureSummaryPath = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix_summary.json"
$closureCsv = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix.csv"
$runtimeCaptureCsv = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix.csv"

Require-Path $ownershipDecisionCsv "Ownership decision CSV"
Require-Path $packageSummaryPath "Ownership source-risk package summary"
Require-Path $runtimeParitySummaryPath "Ownership source-risk runtime parity summary"
Require-Path $runtimeConfigSummaryPath "Ownership source-risk runtime config summary"
Require-Path $closureSummaryPath "Semantic frontier closure summary"
Require-Path $closureCsv "Semantic frontier closure CSV"
Require-Path $runtimeCaptureCsv "Runtime config capture matrix CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$ownershipRows = @(Import-Csv -LiteralPath $ownershipDecisionCsv)
$closureSummary = Read-Json $closureSummaryPath
$closureRows = @(Import-Csv -LiteralPath $closureCsv)
$runtimeCaptureRows = @(Import-Csv -LiteralPath $runtimeCaptureCsv)
$packageSummary = Read-Json $packageSummaryPath
$runtimeParitySummary = Read-Json $runtimeParitySummaryPath
$runtimeConfigSummary = Read-Json $runtimeConfigSummaryPath

$ownershipByName = @{}
$runtimeCaptureByName = @{}
$runtimeParityByName = @{}
$runtimeConfigByName = @{}
Add-ByName $ownershipByName $ownershipRows
Add-ByName $runtimeCaptureByName $runtimeCaptureRows
Add-ByName $runtimeParityByName @($runtimeParitySummary.records)
Add-ByName $runtimeConfigByName @($runtimeConfigSummary.records)

$targetRows = @(
    $closureRows | Where-Object {
        [string]$_.post_harness_blocker -eq "alternate_source_or_natural_capture_required"
    }
)

$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$dependencyReadyCount = 0
$ownershipBlockedCount = 0
$packageReadyCount = 0
$offlineParityValidCount = 0
$runtimeConfigReadyCount = 0
$runtimeHarnessValidCount = 0
$naturalCaptureRequiredCount = 0
$outsideEnvelopeCount = 0
$directReferenceTotal = 0
$statusCounts = @{}
$frontierClassCounts = @{}
$issueReasonCounts = @{}

foreach ($target in $targetRows) {
    $n64Name = [string]$target.n64_name
    $ownership = $ownershipByName[$n64Name]
    $runtimeCapture = $runtimeCaptureByName[$n64Name]
    $runtimeParity = $runtimeParityByName[$n64Name]
    $runtimeConfig = $runtimeConfigByName[$n64Name]
    $issues = New-Object "System.Collections.Generic.List[object]"

    if ($null -eq $ownership) {
        $issues.Add([pscustomobject]@{ reason = "missing_ownership_decision_row"; detail = $n64Name }) | Out-Null
    }
    else {
        if ([string]$ownership.decision_status -ne "ownership_plus_pose_source_risk_requires_alternate_source_or_runtime_parity") {
            $issues.Add([pscustomobject]@{ reason = "ownership_decision_status_mismatch"; detail = [string]$ownership.decision_status }) | Out-Null
        }
        if ([string]$ownership.decision_class -ne "ownership_blocked_by_pose_or_source_risk") {
            $issues.Add([pscustomobject]@{ reason = "ownership_decision_class_mismatch"; detail = [string]$ownership.decision_class }) | Out-Null
        }
        if ([string]$ownership.acceptance_gate -ne "alternate_source_or_runtime_skinned_parity_before_ownership") {
            $issues.Add([pscustomobject]@{ reason = "ownership_acceptance_gate_mismatch"; detail = [string]$ownership.acceptance_gate }) | Out-Null
        }
        if ([string]$ownership.reference_envelope_status -ne "outside_reference_envelope") {
            $issues.Add([pscustomobject]@{ reason = "ownership_reference_envelope_not_outside"; detail = [string]$ownership.reference_envelope_status }) | Out-Null
        }
        else {
            $outsideEnvelopeCount++
        }
        if ([string]$ownership.post_derivative_gate -ne "alternate_source_or_runtime_skinned_parity") {
            $issues.Add([pscustomobject]@{ reason = "ownership_post_derivative_gate_mismatch"; detail = [string]$ownership.post_derivative_gate }) | Out-Null
        }
        if ([int]$ownership.direct_player_actor_source_reference_count -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "source_risk_row_has_direct_player_actor_reference"; detail = [string]$ownership.direct_player_actor_source_reference_count }) | Out-Null
        }
        if ([int]$ownership.issue_count -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "ownership_decision_issue_count"; detail = [string]$ownership.issue_count }) | Out-Null
        }
        $directReferenceTotal += [int]$ownership.direct_player_actor_source_reference_count
    }

    $packageReady = [string]$packageSummary.status -eq "package_ready_pending_runtime_skinned_parity" -and
        [int]$packageSummary.exported -eq 2 -and
        [int]$packageSummary.failed -eq 0 -and
        [int]$packageSummary.package_audit_counts.issue_counts.total -eq 0
    if ($packageReady) {
        $packageReadyCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "ownership_source_risk_package_not_ready"; detail = [string]$packageSummary.status }) | Out-Null
    }

    $offlineParityValid = $null -ne $runtimeParity -and [string]$runtimeParity.status -eq "valid" -and [int]$runtimeParity.issue_count -eq 0
    if ($offlineParityValid) {
        $offlineParityValidCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "offline_runtime_parity_not_valid"; detail = if ($null -eq $runtimeParity) { "missing" } else { [string]$runtimeParity.status } }) | Out-Null
    }

    $runtimeConfigReady = $null -ne $runtimeConfig -and
        [string]$runtimeConfig.status -eq "ready_for_capture" -and
        [string]$runtimeConfig.expected_n64_animation_name -eq $n64Name -and
        -not [string]::IsNullOrWhiteSpace([string]$runtimeConfig.runtime_context_trace_path) -and
        [int]$runtimeConfig.issue_count -eq 0
    if ($runtimeConfigReady) {
        $runtimeConfigReadyCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "ownership_source_risk_runtime_config_not_ready"; detail = if ($null -eq $runtimeConfig) { "missing" } else { [string]$runtimeConfig.status } }) | Out-Null
    }

    $runtimeHarnessValid = if ($null -eq $runtimeCapture) { $false } else { Get-BoolValue $runtimeCapture.runtime_harness_dump_valid }
    $installedRuntimeValid = if ($null -eq $runtimeCapture) { $false } else { Get-BoolValue $runtimeCapture.installed_runtime_dump_valid }
    if ($runtimeHarnessValid) {
        $runtimeHarnessValidCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "missing_harness_geometry_evidence"; detail = $n64Name }) | Out-Null
    }
    if (-not $installedRuntimeValid) {
        $naturalCaptureRequiredCount++
    }

    $dependencyStatus = if ($installedRuntimeValid) {
        "ownership_source_risk_dependency_ready_with_natural_capture"
    }
    else {
        "ownership_source_risk_dependency_ready_pending_alternate_source_or_natural_capture"
    }
    if ($issues.Count -ne 0) {
        $dependencyStatus = "ownership_source_risk_dependency_has_issues"
    }
    else {
        $dependencyReadyCount++
        $ownershipBlockedCount++
    }

    Add-Count $statusCounts $dependencyStatus
    if ($null -ne $ownership) {
        Add-Count $frontierClassCounts ([string]$ownership.frontier_class)
    }
    foreach ($issue in @($issues.ToArray())) {
        Add-Count $issueReasonCounts ([string]$issue.reason)
    }
    $issueCount += $issues.Count

    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        source_csab_name = [string]$target.source_csab_name
        output_csab_name = if ($null -eq $runtimeParity) { "" } else { [string]$runtimeParity.output_csab_name }
        frontier_class = if ($null -eq $ownership) { "" } else { [string]$ownership.frontier_class }
        derivative_frontier_class = if ($null -eq $ownership) { "" } else { [string]$ownership.derivative_frontier_class }
        materialization_class = if ($null -eq $ownership) { "" } else { [string]$ownership.materialization_class }
        reference_envelope_status = if ($null -eq $ownership) { "" } else { [string]$ownership.reference_envelope_status }
        ownership_decision_status = if ($null -eq $ownership) { "" } else { [string]$ownership.decision_status }
        acceptance_gate = if ($null -eq $ownership) { "" } else { [string]$ownership.acceptance_gate }
        direct_player_actor_source_reference_count = if ($null -eq $ownership) { 0 } else { [int]$ownership.direct_player_actor_source_reference_count }
        offline_runtime_parity_status = if ($null -eq $runtimeParity) { "missing" } else { [string]$runtimeParity.status }
        runtime_config_status = if ($null -eq $runtimeConfig) { "missing" } else { [string]$runtimeConfig.status }
        runtime_harness_dump_valid = $runtimeHarnessValid
        natural_capture_status = if ($installedRuntimeValid) { "natural_capture_valid" } else { "pending_natural_capture" }
        dependency_decision_status = $dependencyStatus
        ownership_acceptance_status = "blocked_pending_alternate_source_or_natural_capture"
        semantic_acceptance_status = "blocked_pending_alternate_source_or_natural_capture"
        mapping_promotion_allowed = $false
        natural_capture_command = if ($null -eq $runtimeCapture) { "" } else { [string]$runtimeCapture.runner_command }
        harness_capture_command = if ($null -eq $runtimeCapture) { "" } else { [string]$runtimeCapture.harness_runner_command }
        required_next_step = if ($null -eq $ownership) { "" } else { [string]$ownership.required_next_step }
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray())
$summaryPath = Join-Path $OutputRoot "link_child_ownership_source_risk_dependency_policy_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_ownership_source_risk_dependency_policy.csv"
$status = if ($issueCount -eq 0) { "ownership_source_risk_dependency_ready" } else { "ownership_source_risk_dependency_has_issues" }

$summary = [pscustomobject]@{
    format = "oot3d_link_child_ownership_source_risk_dependency_policy_v1"
    status = $status
    policy = [ordered]@{
        scope = "Link child CSAB ownership rows blocked by outside-envelope pose/source risk"
        semantic_effect = "proves dependency readiness only; no ownership decision, semantic identity, natural capture, or mapping promotion is accepted"
        acceptance_policy = "each row requires alternate-source proof or a non-harness installed runtime draw before ownership acceptance"
    }
    ownership_decision_csv = (Resolve-Path -LiteralPath $ownershipDecisionCsv).Path
    ownership_source_risk_package_summary = (Resolve-Path -LiteralPath $packageSummaryPath).Path
    ownership_source_risk_runtime_parity_summary = (Resolve-Path -LiteralPath $runtimeParitySummaryPath).Path
    ownership_source_risk_runtime_config_summary = (Resolve-Path -LiteralPath $runtimeConfigSummaryPath).Path
    semantic_frontier_closure_summary = (Resolve-Path -LiteralPath $closureSummaryPath).Path
    runtime_config_capture_csv = (Resolve-Path -LiteralPath $runtimeCaptureCsv).Path
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    record_count = $recordArray.Count
    closure_runtime_harness_dump_valid_count = [int](Get-JsonValue $closureSummary "runtime_harness_dump_valid_count")
    dependency_ready_count = $dependencyReadyCount
    ownership_blocked_count = $ownershipBlockedCount
    outside_reference_envelope_count = $outsideEnvelopeCount
    package_ready_count = $packageReadyCount
    offline_runtime_parity_valid_count = $offlineParityValidCount
    runtime_config_ready_count = $runtimeConfigReadyCount
    runtime_harness_dump_valid_count = $runtimeHarnessValidCount
    natural_capture_required_count = $naturalCaptureRequiredCount
    direct_player_actor_source_reference_count = $directReferenceTotal
    ownership_accepted_count = 0
    semantic_accepted_count = 0
    mapping_promotion_allowed_count = 0
    issue_count = $issueCount
    dependency_decision_status_counts = $statusCounts
    frontier_class_counts = $frontierClassCounts
    issue_reason_counts = $issueReasonCounts
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,output_csab_name,frontier_class,derivative_frontier_class,materialization_class,reference_envelope_status,ownership_decision_status,acceptance_gate,offline_runtime_parity_status,runtime_config_status,runtime_harness_dump_valid,natural_capture_status,dependency_decision_status,ownership_acceptance_status,semantic_acceptance_status,mapping_promotion_allowed,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_ownership_source_risk_dependency_policy_v1") "unexpected dependency summary format"
    Assert-Condition ($summary.status -eq "ownership_source_risk_dependency_ready") "expected ownership source-risk dependency ready"
    Assert-Condition ([int]$summary.record_count -eq 2) "expected two ownership source-risk dependency rows"
    Assert-Condition ([int]$summary.dependency_ready_count -eq 2) "expected two dependency-ready rows"
    Assert-Condition ([int]$summary.ownership_blocked_count -eq 2) "expected two ownership-blocked rows"
    Assert-Condition ([int]$summary.outside_reference_envelope_count -eq 2) "expected two outside-envelope rows"
    Assert-Condition ([int]$summary.package_ready_count -eq 2) "expected package readiness for both source-risk rows"
    Assert-Condition ([int]$summary.offline_runtime_parity_valid_count -eq 2) "expected two valid offline runtime parity rows"
    Assert-Condition ([int]$summary.runtime_config_ready_count -eq 2) "expected two runtime configs ready"
    Assert-Condition ([int]$summary.runtime_harness_dump_valid_count -eq 2) "expected two harness-valid source-risk rows"
    Assert-Condition ([int]$summary.natural_capture_required_count -eq 2) "expected two natural captures still required"
    Assert-Condition ([int]$summary.direct_player_actor_source_reference_count -eq 0) "expected no direct player-actor references"
    Assert-Condition ([int]$summary.ownership_accepted_count -eq 0) "expected no accepted ownership decisions"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected no semantic acceptances"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected no mapping promotions"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero source-risk dependency issues"
    Assert-Condition ((Get-JsonValue $summary.dependency_decision_status_counts "ownership_source_risk_dependency_ready_pending_alternate_source_or_natural_capture") -eq 2) "expected two dependencies ready pending alternate source or natural capture"
    Require-Path $csvPath "Ownership source-risk dependency policy CSV"
}

Write-Host "OOT3D Link child ownership source-risk dependency: $summaryPath"
Write-Host "status=$($summary.status) rows=$($summary.record_count) dependencyReady=$($summary.dependency_ready_count) outsideEnvelope=$($summary.outside_reference_envelope_count) harnessValid=$($summary.runtime_harness_dump_valid_count) naturalRequired=$($summary.natural_capture_required_count) issues=$($summary.issue_count)"
