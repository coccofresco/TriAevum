param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\route_proven_ownership_policy"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child route-proven ownership policy verification failed: $Message"
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
$callsiteCsv = Join-Path $characterRoot "link_child_animation_semantic_route_proven_ownership_callsite.csv"
$packageSummaryPath = Join-Path $characterRoot "route_proven_ownership_package\link_child_route_proven_ownership_package_summary.json"
$runtimeParitySummaryPath = Join-Path $characterRoot "route_proven_ownership_runtime_parity\link_child_route_proven_ownership_runtime_parity_summary.json"
$closureSummaryPath = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix_summary.json"
$closureCsv = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix.csv"
$runtimeCaptureCsv = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix.csv"

Require-Path $ownershipDecisionCsv "Ownership decision CSV"
Require-Path $callsiteCsv "Route-proven ownership callsite CSV"
Require-Path $packageSummaryPath "Route-proven package summary"
Require-Path $runtimeParitySummaryPath "Route-proven runtime parity summary"
Require-Path $closureSummaryPath "Semantic frontier closure summary"
Require-Path $closureCsv "Semantic frontier closure CSV"
Require-Path $runtimeCaptureCsv "Runtime config capture matrix CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$ownershipRows = @(Import-Csv -LiteralPath $ownershipDecisionCsv)
$callsiteRows = @(Import-Csv -LiteralPath $callsiteCsv)
$closureSummary = Read-Json $closureSummaryPath
$closureRows = @(Import-Csv -LiteralPath $closureCsv)
$runtimeCaptureRows = @(Import-Csv -LiteralPath $runtimeCaptureCsv)
$packageSummary = Read-Json $packageSummaryPath
$runtimeParitySummary = Read-Json $runtimeParitySummaryPath

$ownershipByName = @{}
$callsiteByName = @{}
$runtimeCaptureByName = @{}
$runtimeParityByName = @{}
Add-ByName $ownershipByName $ownershipRows
Add-ByName $callsiteByName $callsiteRows
Add-ByName $runtimeCaptureByName $runtimeCaptureRows
Add-ByName $runtimeParityByName @($runtimeParitySummary.records)

$targetRows = @(
    $closureRows | Where-Object {
        [string]$_.post_harness_blocker -eq "route_callsite_policy_and_natural_capture_required"
    }
)

$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$policyReadyCount = 0
$callsiteReadyCount = 0
$packageReadyCount = 0
$offlineParityValidCount = 0
$runtimeHarnessValidCount = 0
$naturalCaptureRequiredCount = 0
$directReferenceTotal = 0
$statusCounts = @{}
$issueReasonCounts = @{}

foreach ($target in $targetRows) {
    $n64Name = [string]$target.n64_name
    $ownership = $ownershipByName[$n64Name]
    $callsite = $callsiteByName[$n64Name]
    $runtimeCapture = $runtimeCaptureByName[$n64Name]
    $runtimeParity = $runtimeParityByName[$n64Name]
    $issues = New-Object "System.Collections.Generic.List[object]"

    if ($null -eq $ownership) {
        $issues.Add([pscustomobject]@{ reason = "missing_ownership_decision_row"; detail = $n64Name }) | Out-Null
    }
    else {
        if ([string]$ownership.decision_status -ne "route_proven_pending_installed_capture_and_policy") {
            $issues.Add([pscustomobject]@{ reason = "ownership_decision_status_mismatch"; detail = [string]$ownership.decision_status }) | Out-Null
        }
        if ([string]$ownership.route_callsite_package_candidate_status -ne "route_callsite_package_candidate_ready") {
            $issues.Add([pscustomobject]@{ reason = "ownership_route_callsite_package_not_ready"; detail = [string]$ownership.route_callsite_package_candidate_status }) | Out-Null
        }
        if ([int]$ownership.direct_player_actor_source_reference_count -le 0) {
            $issues.Add([pscustomobject]@{ reason = "ownership_missing_direct_player_actor_reference"; detail = [string]$ownership.direct_player_actor_source_reference_count }) | Out-Null
        }
        if ([int]$ownership.issue_count -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "ownership_decision_issue_count"; detail = [string]$ownership.issue_count }) | Out-Null
        }
    }

    if ($null -eq $callsite) {
        $issues.Add([pscustomobject]@{ reason = "missing_route_callsite_row"; detail = $n64Name }) | Out-Null
    }
    else {
        $directReferenceCount = [int]$callsite.direct_player_actor_source_reference_count
        $directReferenceTotal += $directReferenceCount
        if ($directReferenceCount -le 0) {
            $issues.Add([pscustomobject]@{ reason = "callsite_missing_direct_player_actor_reference"; detail = [string]$directReferenceCount }) | Out-Null
        }
        if ([int]$callsite.unique_callsite_signature_count -le 0) {
            $issues.Add([pscustomobject]@{ reason = "callsite_missing_signature"; detail = [string]$callsite.unique_callsite_signature_count }) | Out-Null
        }
        if ([int]$callsite.unique_context_signature_count -le 0) {
            $issues.Add([pscustomobject]@{ reason = "callsite_missing_context"; detail = [string]$callsite.unique_context_signature_count }) | Out-Null
        }
        if ([string]$callsite.package_candidate_status -ne "route_callsite_package_candidate_ready") {
            $issues.Add([pscustomobject]@{ reason = "callsite_package_candidate_not_ready"; detail = [string]$callsite.package_candidate_status }) | Out-Null
        }
    }

    $packageReady = [string]$packageSummary.status -eq "package_ready_pending_runtime_skinned_parity" -and
        [int]$packageSummary.exported -eq 5 -and
        [int]$packageSummary.failed -eq 0 -and
        [int]$packageSummary.package_audit_counts.issue_counts.total -eq 0
    if ($packageReady) {
        $packageReadyCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "route_proven_package_not_ready"; detail = [string]$packageSummary.status }) | Out-Null
    }

    $offlineParityValid = $null -ne $runtimeParity -and [string]$runtimeParity.status -eq "valid" -and [int]$runtimeParity.issue_count -eq 0
    if ($offlineParityValid) {
        $offlineParityValidCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "offline_runtime_parity_not_valid"; detail = if ($null -eq $runtimeParity) { "missing" } else { [string]$runtimeParity.status } }) | Out-Null
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

    if ($null -ne $callsite -and [string]$callsite.package_candidate_status -eq "route_callsite_package_candidate_ready") {
        $callsiteReadyCount++
    }

    $policyStatus = "route_callsite_policy_ready_pending_natural_capture"
    if ($issues.Count -ne 0) {
        $policyStatus = "route_callsite_policy_has_issues"
    }
    elseif ($installedRuntimeValid) {
        $policyStatus = "route_callsite_policy_ready_with_natural_capture"
    }
    if ($policyStatus -like "route_callsite_policy_ready*") {
        $policyReadyCount++
    }
    Add-Count $statusCounts $policyStatus
    foreach ($issue in @($issues.ToArray())) {
        Add-Count $issueReasonCounts ([string]$issue.reason)
    }
    $issueCount += $issues.Count

    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        source_csab_name = [string]$target.source_csab_name
        route_context_names = if ($null -eq $callsite) { "" } else { [string]$callsite.context_names }
        callsite_review_classes = if ($null -eq $callsite) { "" } else { [string]$callsite.callsite_review_classes }
        direct_player_actor_source_reference_count = if ($null -eq $callsite) { 0 } else { [int]$callsite.direct_player_actor_source_reference_count }
        unique_callsite_signature_count = if ($null -eq $callsite) { 0 } else { [int]$callsite.unique_callsite_signature_count }
        unique_context_signature_count = if ($null -eq $callsite) { 0 } else { [int]$callsite.unique_context_signature_count }
        package_candidate_status = if ($null -eq $callsite) { "" } else { [string]$callsite.package_candidate_status }
        offline_runtime_parity_status = if ($null -eq $runtimeParity) { "missing" } else { [string]$runtimeParity.status }
        runtime_harness_dump_valid = $runtimeHarnessValid
        natural_capture_status = if ($installedRuntimeValid) { "natural_capture_valid" } else { "pending_natural_capture" }
        policy_decision_status = $policyStatus
        semantic_acceptance_status = "blocked_pending_natural_capture"
        mapping_promotion_allowed = $false
        natural_capture_command = if ($null -eq $runtimeCapture) { "" } else { [string]$runtimeCapture.runner_command }
        harness_capture_command = if ($null -eq $runtimeCapture) { "" } else { [string]$runtimeCapture.harness_runner_command }
        required_next_step = "drive the natural route context and capture a non-harness installed runtime draw"
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray())
$summaryPath = Join-Path $OutputRoot "link_child_route_proven_ownership_policy_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_route_proven_ownership_policy.csv"
$status = if ($issueCount -eq 0) { "route_proven_ownership_policy_ready" } else { "route_proven_ownership_policy_has_issues" }

$summary = [pscustomobject]@{
    format = "oot3d_link_child_route_proven_ownership_policy_v1"
    status = $status
    policy = [ordered]@{
        scope = "Link child residual rows with direct N64 player-actor source references and route-proven ownership packages"
        semantic_effect = "proves route/policy readiness only; no semantic identities, natural captures, or mapping promotions are accepted"
        acceptance_policy = "each row still requires a non-harness installed runtime draw before promotion"
    }
    ownership_decision_csv = (Resolve-Path -LiteralPath $ownershipDecisionCsv).Path
    route_callsite_csv = (Resolve-Path -LiteralPath $callsiteCsv).Path
    route_proven_package_summary = (Resolve-Path -LiteralPath $packageSummaryPath).Path
    route_proven_runtime_parity_summary = (Resolve-Path -LiteralPath $runtimeParitySummaryPath).Path
    semantic_frontier_closure_summary = (Resolve-Path -LiteralPath $closureSummaryPath).Path
    runtime_config_capture_csv = (Resolve-Path -LiteralPath $runtimeCaptureCsv).Path
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    record_count = $recordArray.Count
    closure_runtime_harness_dump_valid_count = [int](Get-JsonValue $closureSummary "runtime_harness_dump_valid_count")
    route_callsite_ready_count = $callsiteReadyCount
    route_package_ready_count = $packageReadyCount
    offline_runtime_parity_valid_count = $offlineParityValidCount
    runtime_harness_dump_valid_count = $runtimeHarnessValidCount
    natural_capture_required_count = $naturalCaptureRequiredCount
    direct_player_actor_source_reference_count = $directReferenceTotal
    policy_ready_count = $policyReadyCount
    semantic_accepted_count = 0
    mapping_promotion_allowed_count = 0
    issue_count = $issueCount
    policy_decision_status_counts = $statusCounts
    issue_reason_counts = $issueReasonCounts
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,route_context_names,callsite_review_classes,direct_player_actor_source_reference_count,unique_callsite_signature_count,unique_context_signature_count,package_candidate_status,offline_runtime_parity_status,runtime_harness_dump_valid,natural_capture_status,policy_decision_status,semantic_acceptance_status,mapping_promotion_allowed,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_route_proven_ownership_policy_v1") "unexpected policy summary format"
    Assert-Condition ($summary.status -eq "route_proven_ownership_policy_ready") "expected route-proven ownership policy to be ready"
    Assert-Condition ([int]$summary.record_count -eq 5) "expected five route-proven ownership rows"
    Assert-Condition ([int]$summary.route_callsite_ready_count -eq 5) "expected five route callsite-ready rows"
    Assert-Condition ([int]$summary.route_package_ready_count -eq 5) "expected package readiness for every route-proven row"
    Assert-Condition ([int]$summary.offline_runtime_parity_valid_count -eq 5) "expected five valid offline runtime parity rows"
    Assert-Condition ([int]$summary.runtime_harness_dump_valid_count -eq 5) "expected five harness-valid route-proven rows"
    Assert-Condition ([int]$summary.natural_capture_required_count -eq 5) "expected five natural captures still required"
    Assert-Condition ([int]$summary.policy_ready_count -eq 5) "expected every route-proven policy to be ready"
    Assert-Condition ([int]$summary.direct_player_actor_source_reference_count -ge 10) "expected direct player-actor references for route-proven rows"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected no semantic acceptances"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected no mapping promotions"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero policy issues"
    Require-Path $csvPath "Route-proven ownership policy CSV"
}

Write-Host "OOT3D Link child route-proven ownership policy: $summaryPath"
Write-Host "status=$($summary.status) rows=$($summary.record_count) policyReady=$($summary.policy_ready_count) callsiteReady=$($summary.route_callsite_ready_count) harnessValid=$($summary.runtime_harness_dump_valid_count) naturalRequired=$($summary.natural_capture_required_count) issues=$($summary.issue_count)"
