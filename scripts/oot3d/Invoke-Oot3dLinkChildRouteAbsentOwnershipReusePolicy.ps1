param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\route_absent_ownership_reuse_policy"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child route-absent ownership/reuse policy verification failed: $Message"
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

function Split-ListValue([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return @()
    }
    return @($Value -split ";" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Get-PolicySpec([string]$PostHarnessBlocker) {
    switch ($PostHarnessBlocker) {
        "source_group_intentional_reuse_policy_and_natural_capture_required" {
            return [pscustomobject]@{
                decision_status = "route_absent_multi_claim_intentional_reuse_policy_candidate"
                decision_class = "route_absent_multi_claim_source_group"
                acceptance_gate = "source_group_intentional_reuse_policy_and_installed_capture"
                arbitration_class = "route_absent_multi_claim_intentional_reuse_policy_candidate"
                source_group_class = "multi_claim_source_group_intentional_reuse_policy_candidate"
                policy_scope = "source_group"
                pending_status = "source_group_intentional_reuse_policy_ready_pending_natural_capture"
                natural_status = "source_group_intentional_reuse_policy_ready_with_natural_capture"
                ready_kind = "source_group"
            }
        }
        "single_candidate_owner_policy_and_natural_capture_required" {
            return [pscustomobject]@{
                decision_status = "route_absent_candidate_owner_single_alias_policy_candidate"
                decision_class = "route_absent_candidate_owner_alias"
                acceptance_gate = "single_candidate_owner_alias_policy_and_installed_capture"
                arbitration_class = "route_absent_candidate_owner_single_alias_policy_candidate"
                source_group_class = "single_candidate_owner_alias_policy_candidate"
                policy_scope = "single_owner_alias"
                pending_status = "single_candidate_owner_alias_policy_ready_pending_natural_capture"
                natural_status = "single_candidate_owner_alias_policy_ready_with_natural_capture"
                ready_kind = "single_candidate"
            }
        }
    }
    return $null
}

$characterRoot = Join-Path $WorkRoot "character_conversion"
$ownershipDecisionCsv = Join-Path $characterRoot "ownership_decision_matrix\link_child_ownership_decision_matrix.csv"
$arbitrationCsv = Join-Path $characterRoot "link_child_animation_semantic_ownership_reuse_arbitration.csv"
$packageSummaryPath = Join-Path $characterRoot "ownership_reuse_diagnostic_package\link_child_ownership_reuse_diagnostic_package_summary.json"
$runtimeParitySummaryPath = Join-Path $characterRoot "ownership_reuse_diagnostic_runtime_parity\link_child_ownership_reuse_diagnostic_runtime_parity_summary.json"
$runtimeConfigSummaryPath = Join-Path $characterRoot "ownership_reuse_runtime_capture_config\link_child_ownership_reuse_runtime_config_matrix_summary.json"
$closureSummaryPath = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix_summary.json"
$closureCsv = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix.csv"
$runtimeCaptureCsv = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix.csv"

Require-Path $ownershipDecisionCsv "Ownership decision CSV"
Require-Path $arbitrationCsv "Ownership reuse arbitration CSV"
Require-Path $packageSummaryPath "Ownership reuse package summary"
Require-Path $runtimeParitySummaryPath "Ownership reuse runtime parity summary"
Require-Path $runtimeConfigSummaryPath "Ownership reuse runtime config summary"
Require-Path $closureSummaryPath "Semantic frontier closure summary"
Require-Path $closureCsv "Semantic frontier closure CSV"
Require-Path $runtimeCaptureCsv "Runtime config capture matrix CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$ownershipRows = @(Import-Csv -LiteralPath $ownershipDecisionCsv)
$arbitrationRows = @(Import-Csv -LiteralPath $arbitrationCsv)
$closureSummary = Read-Json $closureSummaryPath
$closureRows = @(Import-Csv -LiteralPath $closureCsv)
$runtimeCaptureRows = @(Import-Csv -LiteralPath $runtimeCaptureCsv)
$packageSummary = Read-Json $packageSummaryPath
$runtimeParitySummary = Read-Json $runtimeParitySummaryPath
$runtimeConfigSummary = Read-Json $runtimeConfigSummaryPath

$ownershipByName = @{}
$arbitrationByName = @{}
$runtimeCaptureByName = @{}
$runtimeParityByName = @{}
$runtimeConfigByName = @{}
Add-ByName $ownershipByName $ownershipRows
Add-ByName $arbitrationByName $arbitrationRows
Add-ByName $runtimeCaptureByName $runtimeCaptureRows
Add-ByName $runtimeParityByName @($runtimeParitySummary.records)
Add-ByName $runtimeConfigByName @($runtimeConfigSummary.records)

$targetBlockers = @(
    "source_group_intentional_reuse_policy_and_natural_capture_required",
    "single_candidate_owner_policy_and_natural_capture_required"
)
$targetRows = @(
    $closureRows | Where-Object { $targetBlockers -contains [string]$_.post_harness_blocker }
)

$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$policyReadyCount = 0
$sourceGroupPolicyReadyCount = 0
$singleCandidatePolicyReadyCount = 0
$ownershipDecisionReadyCount = 0
$arbitrationReadyCount = 0
$runtimeConfigReadyCount = 0
$offlineParityValidCount = 0
$runtimeHarnessValidCount = 0
$naturalCaptureRequiredCount = 0
$directReferenceTotal = 0
$candidateOwnerReferenceTotal = 0
$sourceGroupCsabCounts = @{}
$singleCandidateSourceCounts = @{}
$statusCounts = @{}
$sourceGroupClassCounts = @{}
$acceptanceGateCounts = @{}
$issueReasonCounts = @{}

foreach ($target in $targetRows) {
    $n64Name = [string]$target.n64_name
    $sourceCsab = [string]$target.source_csab_name
    $spec = Get-PolicySpec ([string]$target.post_harness_blocker)
    $ownership = $ownershipByName[$n64Name]
    $arbitration = $arbitrationByName[$n64Name]
    $runtimeCapture = $runtimeCaptureByName[$n64Name]
    $runtimeParity = $runtimeParityByName[$n64Name]
    $runtimeConfig = $runtimeConfigByName[$n64Name]
    $issues = New-Object "System.Collections.Generic.List[object]"

    if ($null -eq $spec) {
        $issues.Add([pscustomobject]@{ reason = "unsupported_post_harness_blocker"; detail = [string]$target.post_harness_blocker }) | Out-Null
    }

    if ($null -eq $ownership) {
        $issues.Add([pscustomobject]@{ reason = "missing_ownership_decision_row"; detail = $n64Name }) | Out-Null
    }
    else {
        if ([string]$ownership.decision_status -ne [string]$spec.decision_status) {
            $issues.Add([pscustomobject]@{ reason = "ownership_decision_status_mismatch"; detail = [string]$ownership.decision_status }) | Out-Null
        }
        if ([string]$ownership.decision_class -ne [string]$spec.decision_class) {
            $issues.Add([pscustomobject]@{ reason = "ownership_decision_class_mismatch"; detail = [string]$ownership.decision_class }) | Out-Null
        }
        if ([string]$ownership.acceptance_gate -ne [string]$spec.acceptance_gate) {
            $issues.Add([pscustomobject]@{ reason = "ownership_acceptance_gate_mismatch"; detail = [string]$ownership.acceptance_gate }) | Out-Null
        }
        if ([int]$ownership.direct_player_actor_source_reference_count -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "route_absent_row_has_direct_player_actor_reference"; detail = [string]$ownership.direct_player_actor_source_reference_count }) | Out-Null
        }
        if ([int]$ownership.issue_count -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "ownership_decision_issue_count"; detail = [string]$ownership.issue_count }) | Out-Null
        }
    }

    if ($null -eq $arbitration) {
        $issues.Add([pscustomobject]@{ reason = "missing_ownership_reuse_arbitration_row"; detail = $n64Name }) | Out-Null
    }
    else {
        $sourceGroupNames = @(Split-ListValue ([string]$arbitration.source_group_n64_names))
        $candidateOwnerNames = @(Split-ListValue ([string]$arbitration.candidate_csab_collision_n64_names))
        $runtimeOwnerNames = @(Split-ListValue ([string]$arbitration.runtime_csab_collision_n64_names))
        $candidateOwnerReferenceCount = $candidateOwnerNames.Count + $runtimeOwnerNames.Count
        $candidateOwnerReferenceTotal += $candidateOwnerReferenceCount

        if ([string]$arbitration.source_csab_name -ne $sourceCsab) {
            $issues.Add([pscustomobject]@{ reason = "arbitration_source_csab_mismatch"; detail = [string]$arbitration.source_csab_name }) | Out-Null
        }
        if ([string]$arbitration.ownership_reuse_arbitration_class -ne [string]$spec.arbitration_class) {
            $issues.Add([pscustomobject]@{ reason = "arbitration_class_mismatch"; detail = [string]$arbitration.ownership_reuse_arbitration_class }) | Out-Null
        }
        if ([string]$arbitration.source_group_class -ne [string]$spec.source_group_class) {
            $issues.Add([pscustomobject]@{ reason = "source_group_class_mismatch"; detail = [string]$arbitration.source_group_class }) | Out-Null
        }
        if ([string]$arbitration.policy_decision_scope -ne [string]$spec.policy_scope) {
            $issues.Add([pscustomobject]@{ reason = "policy_decision_scope_mismatch"; detail = [string]$arbitration.policy_decision_scope }) | Out-Null
        }
        if ([string]$arbitration.route_evidence_status -ne "no_direct_source_reference_found") {
            $issues.Add([pscustomobject]@{ reason = "route_absent_evidence_status_mismatch"; detail = [string]$arbitration.route_evidence_status }) | Out-Null
        }
        if ([int]$arbitration.direct_player_actor_source_reference_count -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "arbitration_has_direct_player_actor_reference"; detail = [string]$arbitration.direct_player_actor_source_reference_count }) | Out-Null
        }
        if ([string]$arbitration.route_acceptance_status -ne "route_not_proven_by_current_source_scan") {
            $issues.Add([pscustomobject]@{ reason = "route_acceptance_status_mismatch"; detail = [string]$arbitration.route_acceptance_status }) | Out-Null
        }
        if ([string]$arbitration.runtime_acceptance_status -ne "diagnostic_ownership_reuse_not_runtime_accepted") {
            $issues.Add([pscustomobject]@{ reason = "runtime_acceptance_status_mismatch"; detail = [string]$arbitration.runtime_acceptance_status }) | Out-Null
        }
        if ($sourceGroupNames -notcontains $n64Name) {
            $issues.Add([pscustomobject]@{ reason = "source_group_missing_target_row"; detail = [string]$arbitration.source_group_n64_names }) | Out-Null
        }
        if ([string]$spec.ready_kind -eq "source_group") {
            if ([int]$arbitration.source_csab_claim_count -le 1) {
                $issues.Add([pscustomobject]@{ reason = "source_group_policy_not_multi_claim"; detail = [string]$arbitration.source_csab_claim_count }) | Out-Null
            }
            Add-Count $sourceGroupCsabCounts $sourceCsab
        }
        elseif ([string]$spec.ready_kind -eq "single_candidate") {
            if ([int]$arbitration.source_csab_claim_count -ne 1) {
                $issues.Add([pscustomobject]@{ reason = "single_candidate_policy_not_single_claim"; detail = [string]$arbitration.source_csab_claim_count }) | Out-Null
            }
            if ($candidateOwnerReferenceCount -le 0) {
                $issues.Add([pscustomobject]@{ reason = "single_candidate_policy_missing_candidate_owner_reference"; detail = $n64Name }) | Out-Null
            }
            Add-Count $singleCandidateSourceCounts $sourceCsab
        }
        $directReferenceTotal += [int]$arbitration.direct_player_actor_source_reference_count
    }

    $packageReady = [string]$packageSummary.status -eq "package_ready_pending_runtime_skinned_parity" -and
        [int]$packageSummary.exported -eq 21 -and
        [int]$packageSummary.failed -eq 0 -and
        [int]$packageSummary.package_audit_counts.issue_counts.total -eq 0
    if (-not $packageReady) {
        $issues.Add([pscustomobject]@{ reason = "ownership_reuse_package_not_ready"; detail = [string]$packageSummary.status }) | Out-Null
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
        $issues.Add([pscustomobject]@{ reason = "ownership_reuse_runtime_config_not_ready"; detail = if ($null -eq $runtimeConfig) { "missing" } else { [string]$runtimeConfig.status } }) | Out-Null
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

    $policyStatus = if ($installedRuntimeValid) { [string]$spec.natural_status } else { [string]$spec.pending_status }
    if ($issues.Count -ne 0) {
        $policyStatus = "route_absent_ownership_reuse_policy_has_issues"
    }
    else {
        $policyReadyCount++
        $ownershipDecisionReadyCount++
        $arbitrationReadyCount++
        if ([string]$spec.ready_kind -eq "source_group") {
            $sourceGroupPolicyReadyCount++
        }
        elseif ([string]$spec.ready_kind -eq "single_candidate") {
            $singleCandidatePolicyReadyCount++
        }
    }

    Add-Count $statusCounts $policyStatus
    $sourceGroupClassValue = if ($null -eq $arbitration) { "" } else { [string]$arbitration.source_group_class }
    Add-Count $sourceGroupClassCounts $sourceGroupClassValue
    Add-Count $acceptanceGateCounts ([string]$spec.acceptance_gate)
    foreach ($issue in @($issues.ToArray())) {
        Add-Count $issueReasonCounts ([string]$issue.reason)
    }
    $issueCount += $issues.Count

    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        source_csab_name = $sourceCsab
        output_csab_name = if ($null -eq $runtimeParity) { "" } else { [string]$runtimeParity.output_csab_name }
        policy_family = [string]$spec.ready_kind
        source_group_n64_names = if ($null -eq $arbitration) { "" } else { [string]$arbitration.source_group_n64_names }
        source_csab_claim_count = if ($null -eq $arbitration) { 0 } else { [int]$arbitration.source_csab_claim_count }
        source_group_class = if ($null -eq $arbitration) { "" } else { [string]$arbitration.source_group_class }
        ownership_decision_status = if ($null -eq $ownership) { "" } else { [string]$ownership.decision_status }
        ownership_decision_class = if ($null -eq $ownership) { "" } else { [string]$ownership.decision_class }
        ownership_reuse_arbitration_class = if ($null -eq $arbitration) { "" } else { [string]$arbitration.ownership_reuse_arbitration_class }
        policy_decision_scope = if ($null -eq $arbitration) { "" } else { [string]$arbitration.policy_decision_scope }
        candidate_csab_collision_n64_names = if ($null -eq $arbitration) { "" } else { [string]$arbitration.candidate_csab_collision_n64_names }
        runtime_csab_collision_n64_names = if ($null -eq $arbitration) { "" } else { [string]$arbitration.runtime_csab_collision_n64_names }
        route_evidence_status = if ($null -eq $arbitration) { "" } else { [string]$arbitration.route_evidence_status }
        direct_player_actor_source_reference_count = if ($null -eq $arbitration) { 0 } else { [int]$arbitration.direct_player_actor_source_reference_count }
        offline_runtime_parity_status = if ($null -eq $runtimeParity) { "missing" } else { [string]$runtimeParity.status }
        runtime_config_status = if ($null -eq $runtimeConfig) { "missing" } else { [string]$runtimeConfig.status }
        runtime_harness_dump_valid = $runtimeHarnessValid
        natural_capture_status = if ($installedRuntimeValid) { "natural_capture_valid" } else { "pending_natural_capture" }
        policy_decision_status = $policyStatus
        semantic_acceptance_status = "blocked_pending_natural_capture"
        mapping_promotion_allowed = $false
        natural_capture_command = if ($null -eq $runtimeCapture) { "" } else { [string]$runtimeCapture.runner_command }
        harness_capture_command = if ($null -eq $runtimeCapture) { "" } else { [string]$runtimeCapture.harness_runner_command }
        required_next_step = if ($null -eq $ownership) { "" } else { [string]$ownership.required_next_step }
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray())
$summaryPath = Join-Path $OutputRoot "link_child_route_absent_ownership_reuse_policy_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_route_absent_ownership_reuse_policy.csv"
$status = if ($issueCount -eq 0) { "route_absent_ownership_reuse_policy_ready" } else { "route_absent_ownership_reuse_policy_has_issues" }

$summary = [pscustomobject]@{
    format = "oot3d_link_child_route_absent_ownership_reuse_policy_v1"
    status = $status
    policy = [ordered]@{
        scope = "Link child route-absent ownership/reuse residual rows with source-group or single-candidate owner policies"
        semantic_effect = "proves policy readiness only; no semantic identities, natural captures, or mapping promotions are accepted"
        acceptance_policy = "each row still requires a non-harness installed runtime draw before promotion"
    }
    ownership_decision_csv = (Resolve-Path -LiteralPath $ownershipDecisionCsv).Path
    ownership_reuse_arbitration_csv = (Resolve-Path -LiteralPath $arbitrationCsv).Path
    ownership_reuse_package_summary = (Resolve-Path -LiteralPath $packageSummaryPath).Path
    ownership_reuse_runtime_parity_summary = (Resolve-Path -LiteralPath $runtimeParitySummaryPath).Path
    ownership_reuse_runtime_config_summary = (Resolve-Path -LiteralPath $runtimeConfigSummaryPath).Path
    semantic_frontier_closure_summary = (Resolve-Path -LiteralPath $closureSummaryPath).Path
    runtime_config_capture_csv = (Resolve-Path -LiteralPath $runtimeCaptureCsv).Path
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    record_count = $recordArray.Count
    closure_runtime_harness_dump_valid_count = [int](Get-JsonValue $closureSummary "runtime_harness_dump_valid_count")
    source_group_policy_row_count = [int]($targetRows | Where-Object { [string]$_.post_harness_blocker -eq "source_group_intentional_reuse_policy_and_natural_capture_required" }).Count
    single_candidate_policy_row_count = [int]($targetRows | Where-Object { [string]$_.post_harness_blocker -eq "single_candidate_owner_policy_and_natural_capture_required" }).Count
    unique_source_group_count = $sourceGroupCsabCounts.Count
    unique_single_candidate_source_count = $singleCandidateSourceCounts.Count
    ownership_decision_ready_count = $ownershipDecisionReadyCount
    arbitration_ready_count = $arbitrationReadyCount
    offline_runtime_parity_valid_count = $offlineParityValidCount
    runtime_config_ready_count = $runtimeConfigReadyCount
    runtime_harness_dump_valid_count = $runtimeHarnessValidCount
    natural_capture_required_count = $naturalCaptureRequiredCount
    direct_player_actor_source_reference_count = $directReferenceTotal
    candidate_or_runtime_owner_reference_count = $candidateOwnerReferenceTotal
    policy_ready_count = $policyReadyCount
    source_group_policy_ready_count = $sourceGroupPolicyReadyCount
    single_candidate_policy_ready_count = $singleCandidatePolicyReadyCount
    semantic_accepted_count = 0
    mapping_promotion_allowed_count = 0
    issue_count = $issueCount
    policy_decision_status_counts = $statusCounts
    source_group_class_counts = $sourceGroupClassCounts
    acceptance_gate_counts = $acceptanceGateCounts
    issue_reason_counts = $issueReasonCounts
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,output_csab_name,policy_family,source_group_n64_names,source_csab_claim_count,source_group_class,ownership_decision_status,ownership_reuse_arbitration_class,policy_decision_scope,candidate_csab_collision_n64_names,runtime_csab_collision_n64_names,offline_runtime_parity_status,runtime_config_status,runtime_harness_dump_valid,natural_capture_status,policy_decision_status,semantic_acceptance_status,mapping_promotion_allowed,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_route_absent_ownership_reuse_policy_v1") "unexpected policy summary format"
    Assert-Condition ($summary.status -eq "route_absent_ownership_reuse_policy_ready") "expected route-absent ownership/reuse policy to be ready"
    Assert-Condition ([int]$summary.record_count -eq 14) "expected 14 route-absent ownership/reuse rows"
    Assert-Condition ([int]$summary.source_group_policy_row_count -eq 9) "expected nine source-group policy rows"
    Assert-Condition ([int]$summary.single_candidate_policy_row_count -eq 5) "expected five single-candidate policy rows"
    Assert-Condition ([int]$summary.unique_source_group_count -eq 5) "expected five unique source groups"
    Assert-Condition ([int]$summary.unique_single_candidate_source_count -eq 5) "expected five unique single-candidate sources"
    Assert-Condition ([int]$summary.ownership_decision_ready_count -eq 14) "expected 14 ownership decision rows ready"
    Assert-Condition ([int]$summary.arbitration_ready_count -eq 14) "expected 14 arbitration rows ready"
    Assert-Condition ([int]$summary.offline_runtime_parity_valid_count -eq 14) "expected 14 valid offline runtime parity rows"
    Assert-Condition ([int]$summary.runtime_config_ready_count -eq 14) "expected 14 ownership/reuse runtime configs ready"
    Assert-Condition ([int]$summary.runtime_harness_dump_valid_count -eq 14) "expected 14 harness-valid route-absent rows"
    Assert-Condition ([int]$summary.natural_capture_required_count -eq 14) "expected 14 natural captures still required"
    Assert-Condition ([int]$summary.direct_player_actor_source_reference_count -eq 0) "expected no direct player-actor references for route-absent rows"
    Assert-Condition ([int]$summary.candidate_or_runtime_owner_reference_count -ge 10) "expected candidate/runtime owner references for route-absent policy rows"
    Assert-Condition ([int]$summary.policy_ready_count -eq 14) "expected every route-absent ownership/reuse policy to be ready"
    Assert-Condition ([int]$summary.source_group_policy_ready_count -eq 9) "expected nine source-group policies ready"
    Assert-Condition ([int]$summary.single_candidate_policy_ready_count -eq 5) "expected five single-candidate policies ready"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected no semantic acceptances"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected no mapping promotions"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero policy issues"
    Assert-Condition ((Get-JsonValue $summary.policy_decision_status_counts "source_group_intentional_reuse_policy_ready_pending_natural_capture") -eq 9) "expected nine source-group policies ready pending natural capture"
    Assert-Condition ((Get-JsonValue $summary.policy_decision_status_counts "single_candidate_owner_alias_policy_ready_pending_natural_capture") -eq 5) "expected five single-candidate policies ready pending natural capture"
    Require-Path $csvPath "Route-absent ownership/reuse policy CSV"
}

Write-Host "OOT3D Link child route-absent ownership/reuse policy: $summaryPath"
Write-Host "status=$($summary.status) rows=$($summary.record_count) policyReady=$($summary.policy_ready_count) sourceGroup=$($summary.source_group_policy_ready_count) singleCandidate=$($summary.single_candidate_policy_ready_count) harnessValid=$($summary.runtime_harness_dump_valid_count) naturalRequired=$($summary.natural_capture_required_count) issues=$($summary.issue_count)"
