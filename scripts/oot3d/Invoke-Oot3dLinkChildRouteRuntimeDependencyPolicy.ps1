param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\route_runtime_dependency_policy"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child route/runtime dependency verification failed: $Message"
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
$routeCsv = Join-Path $characterRoot "route_runtime_capture_matrix\link_child_route_runtime_capture_matrix.csv"
$packageSummaryPath = Join-Path $characterRoot "route_runtime_diagnostic_package\link_child_route_runtime_diagnostic_package_summary.json"
$runtimeParitySummaryPath = Join-Path $characterRoot "route_runtime_diagnostic_runtime_parity\link_child_route_runtime_diagnostic_runtime_parity_summary.json"
$runtimeConfigSummaryPath = Join-Path $characterRoot "route_runtime_capture_config\link_child_route_runtime_config_matrix_summary.json"
$n64UsageSummaryPath = Join-Path $characterRoot "route_runtime_n64_usage_context\link_child_route_runtime_n64_usage_context_summary.json"
$closureSummaryPath = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix_summary.json"
$closureCsv = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix.csv"
$runtimeCaptureCsv = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix.csv"

Require-Path $routeCsv "Route/runtime capture CSV"
Require-Path $packageSummaryPath "Route/runtime package summary"
Require-Path $runtimeParitySummaryPath "Route/runtime runtime parity summary"
Require-Path $runtimeConfigSummaryPath "Route/runtime runtime config summary"
Require-Path $n64UsageSummaryPath "Route/runtime N64 usage context summary"
Require-Path $closureSummaryPath "Semantic frontier closure summary"
Require-Path $closureCsv "Semantic frontier closure CSV"
Require-Path $runtimeCaptureCsv "Runtime config capture matrix CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$routeRows = @(Import-Csv -LiteralPath $routeCsv)
$packageSummary = Read-Json $packageSummaryPath
$runtimeParitySummary = Read-Json $runtimeParitySummaryPath
$runtimeConfigSummary = Read-Json $runtimeConfigSummaryPath
$n64UsageSummary = Read-Json $n64UsageSummaryPath
$closureSummary = Read-Json $closureSummaryPath
$closureRows = @(Import-Csv -LiteralPath $closureCsv)
$runtimeCaptureRows = @(Import-Csv -LiteralPath $runtimeCaptureCsv)

$routeByName = @{}
$runtimeParityByName = @{}
$runtimeConfigByName = @{}
$n64UsageByName = @{}
$runtimeCaptureByName = @{}
Add-ByName $routeByName $routeRows
Add-ByName $runtimeParityByName @($runtimeParitySummary.records)
Add-ByName $runtimeConfigByName @($runtimeConfigSummary.records)
Add-ByName $n64UsageByName @($n64UsageSummary.records)
Add-ByName $runtimeCaptureByName $runtimeCaptureRows

$targetRows = @(
    $closureRows | Where-Object {
        [string]$_.post_harness_blocker -eq "route_runtime_natural_context_or_route_policy_required"
    }
)

$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$dependencyReadyCount = 0
$routeWorkorderReadyCount = 0
$n64UsageReadyCount = 0
$packageReadyCount = 0
$offlineParityValidCount = 0
$runtimeConfigReadyCount = 0
$runtimeHarnessValidCount = 0
$naturalCaptureRequiredCount = 0
$exactSymbolRouteCount = 0
$familyOrSiblingRouteCount = 0
$sourceWindowTotal = 0
$familyOrSiblingReferenceTotal = 0
$statusCounts = @{}
$routeContextClassCounts = @{}
$evidenceKindCounts = @{}
$runnerScopeCounts = @{}
$issueReasonCounts = @{}

foreach ($target in $targetRows) {
    $n64Name = [string]$target.n64_name
    $route = $routeByName[$n64Name]
    $runtimeParity = $runtimeParityByName[$n64Name]
    $runtimeConfig = $runtimeConfigByName[$n64Name]
    $n64Usage = $n64UsageByName[$n64Name]
    $runtimeCapture = $runtimeCaptureByName[$n64Name]
    $issues = New-Object "System.Collections.Generic.List[object]"

    if ([string]$target.frontier_gate -ne "route_or_runtime_capture") {
        $issues.Add([pscustomobject]@{ reason = "frontier_gate_mismatch"; detail = [string]$target.frontier_gate }) | Out-Null
    }
    if ([string]$target.workorder_kind -ne "route_runtime_capture") {
        $issues.Add([pscustomobject]@{ reason = "workorder_kind_mismatch"; detail = [string]$target.workorder_kind }) | Out-Null
    }

    if ($null -eq $route) {
        $issues.Add([pscustomobject]@{ reason = "missing_route_runtime_capture_row"; detail = $n64Name }) | Out-Null
    }
    else {
        if ([int]$route.exact_direct_player_actor_reference_count -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "route_has_exact_direct_reference"; detail = [string]$route.exact_direct_player_actor_reference_count }) | Out-Null
        }
        if ([int]$route.family_route_reference_count -le 0) {
            $issues.Add([pscustomobject]@{ reason = "route_missing_family_reference"; detail = [string]$route.family_route_reference_count }) | Out-Null
        }
        if ([string]::IsNullOrWhiteSpace([string]$route.route_context_class)) {
            $issues.Add([pscustomobject]@{ reason = "route_context_class_missing"; detail = $n64Name }) | Out-Null
        }
        if ([string]::IsNullOrWhiteSpace([string]$route.route_trigger)) {
            $issues.Add([pscustomobject]@{ reason = "route_trigger_missing"; detail = $n64Name }) | Out-Null
        }
        if ([string]::IsNullOrWhiteSpace([string]$route.route_acceptance_gate)) {
            $issues.Add([pscustomobject]@{ reason = "route_acceptance_gate_missing"; detail = $n64Name }) | Out-Null
        }
        if ([string]$route.runtime_acceptance_status -ne "pending_route_or_installed_runtime_capture") {
            $issues.Add([pscustomobject]@{ reason = "route_runtime_acceptance_status_mismatch"; detail = [string]$route.runtime_acceptance_status }) | Out-Null
        }
        if ([int]$route.issue_count -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "route_runtime_capture_issue_count"; detail = [string]$route.issue_count }) | Out-Null
        }
    }

    if ($null -eq $n64Usage) {
        $issues.Add([pscustomobject]@{ reason = "missing_route_runtime_n64_usage_row"; detail = $n64Name }) | Out-Null
    }
    else {
        $exactSymbolRouteCount += [int]$n64Usage.exact_direct_player_actor_reference_count
        if ([int]$n64Usage.exact_direct_player_actor_reference_count -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "n64_usage_has_exact_symbol_context"; detail = [string]$n64Usage.exact_direct_player_actor_reference_count }) | Out-Null
        }
        if ([int]$n64Usage.family_or_sibling_reference_count -le 0) {
            $issues.Add([pscustomobject]@{ reason = "n64_usage_missing_family_or_sibling_context"; detail = [string]$n64Usage.family_or_sibling_reference_count }) | Out-Null
        }
        else {
            $familyOrSiblingRouteCount++
            $familyOrSiblingReferenceTotal += [int]$n64Usage.family_or_sibling_reference_count
        }
        if ([int]$n64Usage.source_window_count -le 0) {
            $issues.Add([pscustomobject]@{ reason = "n64_usage_missing_source_windows"; detail = [string]$n64Usage.source_window_count }) | Out-Null
        }
        else {
            $sourceWindowTotal += [int]$n64Usage.source_window_count
        }
        if (-not (Get-BoolValue $n64Usage.expected_n64_filter_linked)) {
            $issues.Add([pscustomobject]@{ reason = "n64_usage_expected_filter_not_linked"; detail = $n64Name }) | Out-Null
        }
        if (-not (Get-BoolValue $n64Usage.runtime_context_trace_linked)) {
            $issues.Add([pscustomobject]@{ reason = "n64_usage_context_trace_not_linked"; detail = $n64Name }) | Out-Null
        }
        if ([int]$n64Usage.issue_count -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "n64_usage_issue_count"; detail = [string]$n64Usage.issue_count }) | Out-Null
        }
    }

    $packageReady = [string]$packageSummary.status -eq "package_ready_pending_runtime_skinned_parity" -and
        [int]$packageSummary.exported -eq 8 -and
        [int]$packageSummary.failed -eq 0 -and
        [int]$packageSummary.package_audit_counts.issue_counts.total -eq 0
    if ($packageReady) {
        $packageReadyCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "route_runtime_package_not_ready"; detail = [string]$packageSummary.status }) | Out-Null
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
        $issues.Add([pscustomobject]@{ reason = "route_runtime_config_not_ready"; detail = if ($null -eq $runtimeConfig) { "missing" } else { [string]$runtimeConfig.status } }) | Out-Null
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
        "route_runtime_dependency_ready_with_natural_capture"
    }
    else {
        "route_runtime_dependency_ready_pending_route_policy_or_natural_capture"
    }
    if ($issues.Count -ne 0) {
        $dependencyStatus = "route_runtime_dependency_has_issues"
    }
    else {
        $dependencyReadyCount++
        $routeWorkorderReadyCount++
        $n64UsageReadyCount++
    }

    Add-Count $statusCounts $dependencyStatus
    if ($null -ne $route) {
        Add-Count $routeContextClassCounts ([string]$route.route_context_class)
    }
    if ($null -ne $n64Usage) {
        Add-Count $evidenceKindCounts ([string]$n64Usage.evidence_kind)
        Add-Count $runnerScopeCounts ([string]$n64Usage.context_runner_scope)
    }
    foreach ($issue in @($issues.ToArray())) {
        Add-Count $issueReasonCounts ([string]$issue.reason)
    }
    $issueCount += $issues.Count

    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        source_csab_name = [string]$target.source_csab_name
        output_csab_name = if ($null -eq $runtimeParity) { "" } else { [string]$runtimeParity.output_csab_name }
        route_context_class = if ($null -eq $route) { "" } else { [string]$route.route_context_class }
        route_trigger = if ($null -eq $route) { "" } else { [string]$route.route_trigger }
        capture_status = if ($null -eq $route) { "" } else { [string]$route.capture_status }
        route_acceptance_gate = if ($null -eq $route) { "" } else { [string]$route.route_acceptance_gate }
        evidence_kind = if ($null -eq $n64Usage) { "" } else { [string]$n64Usage.evidence_kind }
        semantic_gap_class = if ($null -eq $n64Usage) { "" } else { [string]$n64Usage.semantic_gap_class }
        context_runner_scope = if ($null -eq $n64Usage) { "" } else { [string]$n64Usage.context_runner_scope }
        exact_direct_player_actor_reference_count = if ($null -eq $n64Usage) { 0 } else { [int]$n64Usage.exact_direct_player_actor_reference_count }
        family_or_sibling_reference_count = if ($null -eq $n64Usage) { 0 } else { [int]$n64Usage.family_or_sibling_reference_count }
        source_window_count = if ($null -eq $n64Usage) { 0 } else { [int]$n64Usage.source_window_count }
        offline_runtime_parity_status = if ($null -eq $runtimeParity) { "missing" } else { [string]$runtimeParity.status }
        runtime_config_status = if ($null -eq $runtimeConfig) { "missing" } else { [string]$runtimeConfig.status }
        runtime_harness_dump_valid = $runtimeHarnessValid
        natural_capture_status = if ($installedRuntimeValid) { "natural_capture_valid" } else { "pending_natural_capture" }
        dependency_decision_status = $dependencyStatus
        route_acceptance_status = "blocked_pending_route_policy_or_natural_capture"
        semantic_acceptance_status = "blocked_pending_route_policy_or_natural_capture"
        mapping_promotion_allowed = $false
        natural_capture_command = if ($null -eq $runtimeCapture) { "" } else { [string]$runtimeCapture.runner_command }
        harness_capture_command = if ($null -eq $runtimeCapture) { "" } else { [string]$runtimeCapture.harness_runner_command }
        required_next_step = if ($null -eq $n64Usage) { "" } else { [string]$n64Usage.context_runner_requirement }
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray())
$summaryPath = Join-Path $OutputRoot "link_child_route_runtime_dependency_policy_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_route_runtime_dependency_policy.csv"
$status = if ($issueCount -eq 0) { "route_runtime_dependency_ready" } else { "route_runtime_dependency_has_issues" }

$summary = [pscustomobject]@{
    format = "oot3d_link_child_route_runtime_dependency_policy_v1"
    status = $status
    policy = [ordered]@{
        scope = "Link child route/runtime residual rows with family or sibling N64 route evidence"
        semantic_effect = "proves dependency readiness only; no exact route, natural capture, semantic identity, or mapping promotion is accepted"
        acceptance_policy = "each row requires accepted route policy or a non-harness installed runtime draw before promotion"
    }
    route_runtime_capture_csv = (Resolve-Path -LiteralPath $routeCsv).Path
    route_runtime_package_summary = (Resolve-Path -LiteralPath $packageSummaryPath).Path
    route_runtime_runtime_parity_summary = (Resolve-Path -LiteralPath $runtimeParitySummaryPath).Path
    route_runtime_config_summary = (Resolve-Path -LiteralPath $runtimeConfigSummaryPath).Path
    route_runtime_n64_usage_context_summary = (Resolve-Path -LiteralPath $n64UsageSummaryPath).Path
    semantic_frontier_closure_summary = (Resolve-Path -LiteralPath $closureSummaryPath).Path
    runtime_config_capture_csv = (Resolve-Path -LiteralPath $runtimeCaptureCsv).Path
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    record_count = $recordArray.Count
    closure_runtime_harness_dump_valid_count = [int](Get-JsonValue $closureSummary "runtime_harness_dump_valid_count")
    dependency_ready_count = $dependencyReadyCount
    route_workorder_ready_count = $routeWorkorderReadyCount
    n64_usage_ready_count = $n64UsageReadyCount
    package_ready_count = $packageReadyCount
    offline_runtime_parity_valid_count = $offlineParityValidCount
    runtime_config_ready_count = $runtimeConfigReadyCount
    runtime_harness_dump_valid_count = $runtimeHarnessValidCount
    natural_capture_required_count = $naturalCaptureRequiredCount
    exact_symbol_route_count = $exactSymbolRouteCount
    family_or_sibling_route_count = $familyOrSiblingRouteCount
    family_or_sibling_reference_count = $familyOrSiblingReferenceTotal
    source_window_count = $sourceWindowTotal
    runtime_trace_required_field_count = [int](Get-JsonValue $n64UsageSummary "runtime_trace_required_field_count")
    runtime_trace_required_field_present_count = [int](Get-JsonValue $n64UsageSummary "runtime_trace_required_field_present_count")
    route_accepted_count = 0
    semantic_accepted_count = 0
    mapping_promotion_allowed_count = 0
    issue_count = $issueCount
    dependency_decision_status_counts = $statusCounts
    route_context_class_counts = $routeContextClassCounts
    evidence_kind_counts = $evidenceKindCounts
    context_runner_scope_counts = $runnerScopeCounts
    issue_reason_counts = $issueReasonCounts
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,output_csab_name,route_context_class,route_trigger,capture_status,route_acceptance_gate,evidence_kind,semantic_gap_class,context_runner_scope,exact_direct_player_actor_reference_count,family_or_sibling_reference_count,source_window_count,offline_runtime_parity_status,runtime_config_status,runtime_harness_dump_valid,natural_capture_status,dependency_decision_status,route_acceptance_status,semantic_acceptance_status,mapping_promotion_allowed,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_route_runtime_dependency_policy_v1") "unexpected route/runtime dependency summary format"
    Assert-Condition ($summary.status -eq "route_runtime_dependency_ready") "expected route/runtime dependency ready"
    Assert-Condition ([int]$summary.record_count -eq 8) "expected eight route/runtime rows"
    Assert-Condition ([int]$summary.dependency_ready_count -eq 8) "expected eight route/runtime dependencies ready"
    Assert-Condition ([int]$summary.route_workorder_ready_count -eq 8) "expected eight route/runtime workorders ready"
    Assert-Condition ([int]$summary.n64_usage_ready_count -eq 8) "expected eight N64 usage rows ready"
    Assert-Condition ([int]$summary.package_ready_count -eq 8) "expected package readiness for every route/runtime row"
    Assert-Condition ([int]$summary.offline_runtime_parity_valid_count -eq 8) "expected eight valid offline runtime parity rows"
    Assert-Condition ([int]$summary.runtime_config_ready_count -eq 8) "expected eight runtime configs ready"
    Assert-Condition ([int]$summary.runtime_harness_dump_valid_count -eq 8) "expected eight harness-valid route/runtime rows"
    Assert-Condition ([int]$summary.natural_capture_required_count -eq 8) "expected eight natural captures still required"
    Assert-Condition ([int]$summary.exact_symbol_route_count -eq 0) "expected zero exact-symbol route contexts"
    Assert-Condition ([int]$summary.family_or_sibling_route_count -eq 8) "expected eight family/sibling route contexts"
    Assert-Condition ([int]$summary.family_or_sibling_reference_count -eq 54) "expected 54 family/sibling source references"
    Assert-Condition ([int]$summary.source_window_count -eq 54) "expected 54 route/runtime source windows"
    Assert-Condition ([int]$summary.runtime_trace_required_field_count -eq 20) "expected 20 route/runtime trace fields"
    Assert-Condition ([int]$summary.runtime_trace_required_field_present_count -eq 20) "expected all route/runtime trace fields present"
    Assert-Condition ([int]$summary.route_accepted_count -eq 0) "expected no accepted routes"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected no semantic acceptances"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected no mapping promotions"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero route/runtime dependency issues"
    Assert-Condition ((Get-JsonValue $summary.dependency_decision_status_counts "route_runtime_dependency_ready_pending_route_policy_or_natural_capture") -eq 8) "expected eight dependencies ready pending route policy or natural capture"
    Require-Path $csvPath "Route/runtime dependency policy CSV"
}

Write-Host "OOT3D Link child route/runtime dependency: $summaryPath"
Write-Host "status=$($summary.status) rows=$($summary.record_count) dependencyReady=$($summary.dependency_ready_count) familySibling=$($summary.family_or_sibling_route_count) sourceWindows=$($summary.source_window_count) harnessValid=$($summary.runtime_harness_dump_valid_count) naturalRequired=$($summary.natural_capture_required_count) issues=$($summary.issue_count)"
