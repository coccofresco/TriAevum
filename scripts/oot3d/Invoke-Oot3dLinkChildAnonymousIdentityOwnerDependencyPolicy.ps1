param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\anonymous_identity_owner_dependency_policy"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child anonymous identity/owner dependency verification failed: $Message"
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

function Get-ExpectedIdentitySpec([string]$SourceContractKind) {
    switch ($SourceContractKind) {
        "blocked_candidate_csab_reuse" {
            return [pscustomobject]@{
                decision_status = "candidate_source_identity_owner_pending"
                identity_class = "anonymous_numeric_candidate_source_reuse"
                ownership_class = "candidate_source_owner_alias"
                acceptance_gate = "table_order_identity_candidate_owner_policy_and_installed_capture"
                dependency_status = "candidate_source_identity_owner_dependency_ready_pending_owner_policy_or_natural_capture"
            }
        }
        "blocked_promoted_csab_reuse" {
            return [pscustomobject]@{
                decision_status = "promoted_source_reuse_identity_owner_pending"
                identity_class = "anonymous_numeric_promoted_source_reuse"
                ownership_class = "promoted_source_reuse_candidate"
                acceptance_gate = "table_order_identity_promoted_owner_policy_and_installed_capture"
                dependency_status = "promoted_source_reuse_identity_owner_dependency_ready_pending_owner_policy_or_natural_capture"
            }
        }
    }
    return $null
}

$characterRoot = Join-Path $WorkRoot "character_conversion"
$identitySummaryPath = Join-Path $characterRoot "anonymous_identity_matrix\link_child_anonymous_identity_matrix_summary.json"
$identityCsv = Join-Path $characterRoot "anonymous_identity_matrix\link_child_anonymous_identity_matrix.csv"
$tableContextSummaryPath = Join-Path $characterRoot "anonymous_n64_table_context\link_child_anonymous_n64_table_context_summary.json"
$tableContextCsv = Join-Path $characterRoot "anonymous_n64_table_context\link_child_anonymous_n64_table_context.csv"
$ownerCaptureSummaryPath = Join-Path $characterRoot "anonymous_owner_capture_matrix\link_child_anonymous_owner_capture_matrix_summary.json"
$ownerCaptureCsv = Join-Path $characterRoot "anonymous_owner_capture_matrix\link_child_anonymous_owner_capture_matrix.csv"
$packageSummaryPath = Join-Path $characterRoot "anonymous_numeric_diagnostic_package\link_child_anonymous_numeric_diagnostic_package_summary.json"
$runtimeParitySummaryPath = Join-Path $characterRoot "anonymous_numeric_diagnostic_runtime_parity\link_child_anonymous_numeric_diagnostic_runtime_parity_summary.json"
$runtimeConfigSummaryPath = Join-Path $characterRoot "anonymous_owner_runtime_capture_config\link_child_anonymous_owner_runtime_config_matrix_summary.json"
$closureSummaryPath = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix_summary.json"
$closureCsv = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix.csv"
$runtimeCaptureCsv = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix.csv"

Require-Path $identitySummaryPath "Anonymous identity summary"
Require-Path $identityCsv "Anonymous identity CSV"
Require-Path $tableContextSummaryPath "Anonymous N64 table context summary"
Require-Path $tableContextCsv "Anonymous N64 table context CSV"
Require-Path $ownerCaptureSummaryPath "Anonymous owner capture summary"
Require-Path $ownerCaptureCsv "Anonymous owner capture CSV"
Require-Path $packageSummaryPath "Anonymous diagnostic package summary"
Require-Path $runtimeParitySummaryPath "Anonymous diagnostic runtime parity summary"
Require-Path $runtimeConfigSummaryPath "Anonymous owner runtime config summary"
Require-Path $closureSummaryPath "Semantic frontier closure summary"
Require-Path $closureCsv "Semantic frontier closure CSV"
Require-Path $runtimeCaptureCsv "Runtime config capture matrix CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$identitySummary = Read-Json $identitySummaryPath
$tableContextSummary = Read-Json $tableContextSummaryPath
$ownerCaptureSummary = Read-Json $ownerCaptureSummaryPath
$packageSummary = Read-Json $packageSummaryPath
$runtimeParitySummary = Read-Json $runtimeParitySummaryPath
$runtimeConfigSummary = Read-Json $runtimeConfigSummaryPath
$closureSummary = Read-Json $closureSummaryPath

$identityRows = @(Import-Csv -LiteralPath $identityCsv)
$tableContextRows = @(Import-Csv -LiteralPath $tableContextCsv)
$ownerCaptureRows = @(Import-Csv -LiteralPath $ownerCaptureCsv)
$closureRows = @(Import-Csv -LiteralPath $closureCsv)
$runtimeCaptureRows = @(Import-Csv -LiteralPath $runtimeCaptureCsv)

$identityByName = @{}
$tableContextByName = @{}
$ownerCaptureByName = @{}
$runtimeParityByName = @{}
$runtimeConfigByName = @{}
$runtimeCaptureByName = @{}
Add-ByName $identityByName $identityRows
Add-ByName $tableContextByName $tableContextRows
Add-ByName $ownerCaptureByName $ownerCaptureRows
Add-ByName $runtimeParityByName @($runtimeParitySummary.records)
Add-ByName $runtimeConfigByName @($runtimeConfigSummary.records)
Add-ByName $runtimeCaptureByName $runtimeCaptureRows

$targetRows = @(
    $closureRows | Where-Object {
        [string]$_.post_harness_blocker -eq "anonymous_identity_owner_policy_and_natural_capture_required"
    }
)

$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$dependencyReadyCount = 0
$identityMatrixReadyCount = 0
$tableContextReadyCount = 0
$ownerCaptureReadyCount = 0
$packageReadyCount = 0
$offlineParityValidCount = 0
$runtimeConfigReadyCount = 0
$runtimeHarnessValidCount = 0
$naturalCaptureRequiredCount = 0
$numericOffsetMatchCount = 0
$dataOrderMatchCount = 0
$frameCountMatchCount = 0
$candidateOwnerReferencePresentCount = 0
$candidateOwnerReferenceTotal = 0
$directAnonymousReferenceTotal = 0
$candidateSourceReuseCount = 0
$promotedSourceReuseCount = 0
$identityAcceptedCount = 0
$ownershipAcceptedCount = 0
$acceptedReusePolicyCount = 0
$semanticAcceptedCount = 0
$mappingPromotionAllowedCount = 0
$statusCounts = @{}
$identityClassCounts = @{}
$ownershipClassCounts = @{}
$ownerContextClassCounts = @{}
$captureStatusCounts = @{}
$sourceContractCounts = @{}
$issueReasonCounts = @{}

foreach ($target in $targetRows) {
    $n64Name = [string]$target.n64_name
    $sourceCsab = [string]$target.source_csab_name
    $identity = $identityByName[$n64Name]
    $tableContext = $tableContextByName[$n64Name]
    $ownerCapture = $ownerCaptureByName[$n64Name]
    $runtimeParity = $runtimeParityByName[$n64Name]
    $runtimeConfig = $runtimeConfigByName[$n64Name]
    $runtimeCapture = $runtimeCaptureByName[$n64Name]
    $issues = New-Object "System.Collections.Generic.List[object]"

    if ([string]$target.frontier_gate -ne "anonymous_symbol_identity") {
        $issues.Add([pscustomobject]@{ reason = "frontier_gate_mismatch"; detail = [string]$target.frontier_gate }) | Out-Null
    }
    if ([string]$target.workorder_kind -ne "anonymous_owner_capture") {
        $issues.Add([pscustomobject]@{ reason = "workorder_kind_mismatch"; detail = [string]$target.workorder_kind }) | Out-Null
    }
    if ([string]$target.coverage_status -ne "covered_by_verified_workorder") {
        $issues.Add([pscustomobject]@{ reason = "closure_coverage_status_mismatch"; detail = [string]$target.coverage_status }) | Out-Null
    }
    if ([string]$target.harness_evidence_status -ne "harness_geometry_valid") {
        $issues.Add([pscustomobject]@{ reason = "closure_harness_evidence_status_mismatch"; detail = [string]$target.harness_evidence_status }) | Out-Null
    }

    $spec = if ($null -eq $identity) { $null } else { Get-ExpectedIdentitySpec ([string]$identity.source_contract_kind) }
    if ($null -eq $identity) {
        $issues.Add([pscustomobject]@{ reason = "missing_identity_matrix_row"; detail = $n64Name }) | Out-Null
    }
    elseif ($null -eq $spec) {
        $issues.Add([pscustomobject]@{ reason = "unsupported_source_contract_kind"; detail = [string]$identity.source_contract_kind }) | Out-Null
    }
    else {
        if ([string]$identity.source_csab_name -ne $sourceCsab) {
            $issues.Add([pscustomobject]@{ reason = "identity_source_csab_mismatch"; detail = [string]$identity.source_csab_name }) | Out-Null
        }
        if ([string]$identity.decision_status -ne [string]$spec.decision_status) {
            $issues.Add([pscustomobject]@{ reason = "identity_decision_status_mismatch"; detail = [string]$identity.decision_status }) | Out-Null
        }
        if ([string]$identity.identity_class -ne [string]$spec.identity_class) {
            $issues.Add([pscustomobject]@{ reason = "identity_class_mismatch"; detail = [string]$identity.identity_class }) | Out-Null
        }
        if ([string]$identity.ownership_class -ne [string]$spec.ownership_class) {
            $issues.Add([pscustomobject]@{ reason = "ownership_class_mismatch"; detail = [string]$identity.ownership_class }) | Out-Null
        }
        if ([string]$identity.acceptance_gate -ne [string]$spec.acceptance_gate) {
            $issues.Add([pscustomobject]@{ reason = "identity_acceptance_gate_mismatch"; detail = [string]$identity.acceptance_gate }) | Out-Null
        }
        if ([string]$identity.runtime_acceptance_status -ne "anonymous_identity_ownership_pending_installed_runtime_capture") {
            $issues.Add([pscustomobject]@{ reason = "identity_runtime_acceptance_status_mismatch"; detail = [string]$identity.runtime_acceptance_status }) | Out-Null
        }
        if ((Get-IntValue $identity.direct_player_actor_reference_count) -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "identity_has_direct_player_actor_reference"; detail = [string]$identity.direct_player_actor_reference_count }) | Out-Null
        }
        if ((Get-IntValue $identity.candidate_csab_collision_count) -le 0 -and (Get-IntValue $identity.runtime_csab_collision_count) -le 0) {
            $issues.Add([pscustomobject]@{ reason = "identity_has_no_candidate_or_runtime_owner_collision"; detail = $n64Name }) | Out-Null
        }
        if ((Get-IntValue $identity.issue_count) -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "identity_issue_count"; detail = [string]$identity.issue_count }) | Out-Null
        }
        Add-Count $identityClassCounts ([string]$identity.identity_class)
        Add-Count $ownershipClassCounts ([string]$identity.ownership_class)
        Add-Count $sourceContractCounts ([string]$identity.source_contract_kind)
        if ([string]$identity.source_contract_kind -eq "blocked_candidate_csab_reuse") {
            $candidateSourceReuseCount++
        }
        elseif ([string]$identity.source_contract_kind -eq "blocked_promoted_csab_reuse") {
            $promotedSourceReuseCount++
        }
    }

    if ($null -eq $tableContext) {
        $issues.Add([pscustomobject]@{ reason = "missing_n64_table_context_row"; detail = $n64Name }) | Out-Null
    }
    else {
        if ([string]$tableContext.source_csab_name -ne $sourceCsab) {
            $issues.Add([pscustomobject]@{ reason = "table_context_source_csab_mismatch"; detail = [string]$tableContext.source_csab_name }) | Out-Null
        }
        if (-not (Get-BoolValue $tableContext.numeric_name_offset_match)) {
            $issues.Add([pscustomobject]@{ reason = "numeric_name_offset_not_matched"; detail = $n64Name }) | Out-Null
        }
        else {
            $numericOffsetMatchCount++
        }
        if (-not (Get-BoolValue $tableContext.data_xml_order_match)) {
            $issues.Add([pscustomobject]@{ reason = "data_xml_order_not_matched"; detail = $n64Name }) | Out-Null
        }
        else {
            $dataOrderMatchCount++
        }
        if (-not (Get-BoolValue $tableContext.frame_count_match)) {
            $issues.Add([pscustomobject]@{ reason = "frame_count_not_matched"; detail = $n64Name }) | Out-Null
        }
        else {
            $frameCountMatchCount++
        }
        $directAnonymousReferenceTotal += (Get-IntValue $tableContext.direct_anonymous_symbol_reference_count)
        if ((Get-IntValue $tableContext.direct_anonymous_symbol_reference_count) -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "table_context_has_direct_anonymous_symbol_reference"; detail = [string]$tableContext.direct_anonymous_symbol_reference_count }) | Out-Null
        }
        $candidateOwnerReferenceTotal += (Get-IntValue $tableContext.candidate_owner_symbol_reference_count)
        if ((Get-IntValue $tableContext.candidate_owner_symbol_reference_count) -le 0) {
            $issues.Add([pscustomobject]@{ reason = "missing_candidate_owner_symbol_reference"; detail = $n64Name }) | Out-Null
        }
        else {
            $candidateOwnerReferencePresentCount++
        }
        if ([string]$tableContext.identity_source_usage_status -ne "anonymous_symbol_absent_candidate_owner_source_refs_present") {
            $issues.Add([pscustomobject]@{ reason = "identity_source_usage_status_mismatch"; detail = [string]$tableContext.identity_source_usage_status }) | Out-Null
        }
        if ([string]$tableContext.runtime_acceptance_status -ne "n64_table_context_ready_pending_identity_ownership_and_installed_capture") {
            $issues.Add([pscustomobject]@{ reason = "table_context_runtime_acceptance_status_mismatch"; detail = [string]$tableContext.runtime_acceptance_status }) | Out-Null
        }
        if ((Get-IntValue $tableContext.issue_count) -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "table_context_issue_count"; detail = [string]$tableContext.issue_count }) | Out-Null
        }
    }

    if ($null -eq $ownerCapture) {
        $issues.Add([pscustomobject]@{ reason = "missing_owner_capture_row"; detail = $n64Name }) | Out-Null
    }
    else {
        $ownerSymbols = @(Split-ListValue ([string]$ownerCapture.candidate_owner_symbols))
        if ([string]$ownerCapture.source_csab_name -ne $sourceCsab) {
            $issues.Add([pscustomobject]@{ reason = "owner_capture_source_csab_mismatch"; detail = [string]$ownerCapture.source_csab_name }) | Out-Null
        }
        if ($ownerSymbols.Count -eq 0) {
            $issues.Add([pscustomobject]@{ reason = "owner_capture_missing_candidate_owner_symbols"; detail = $n64Name }) | Out-Null
        }
        if ((Get-IntValue $ownerCapture.direct_anonymous_symbol_reference_count) -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "owner_capture_has_direct_anonymous_symbol_reference"; detail = [string]$ownerCapture.direct_anonymous_symbol_reference_count }) | Out-Null
        }
        if ((Get-IntValue $ownerCapture.candidate_owner_symbol_reference_count) -le 0) {
            $issues.Add([pscustomobject]@{ reason = "owner_capture_missing_candidate_owner_reference"; detail = $n64Name }) | Out-Null
        }
        if ([string]::IsNullOrWhiteSpace([string]$ownerCapture.owner_context_class)) {
            $issues.Add([pscustomobject]@{ reason = "owner_context_class_missing"; detail = $n64Name }) | Out-Null
        }
        if ([string]::IsNullOrWhiteSpace([string]$ownerCapture.capture_status)) {
            $issues.Add([pscustomobject]@{ reason = "capture_status_missing"; detail = $n64Name }) | Out-Null
        }
        if ([string]::IsNullOrWhiteSpace([string]$ownerCapture.runtime_trigger)) {
            $issues.Add([pscustomobject]@{ reason = "runtime_trigger_missing"; detail = $n64Name }) | Out-Null
        }
        if ([string]$ownerCapture.runtime_acceptance_status -ne "anonymous_owner_capture_pending_installed_runtime_draw") {
            $issues.Add([pscustomobject]@{ reason = "owner_capture_runtime_acceptance_status_mismatch"; detail = [string]$ownerCapture.runtime_acceptance_status }) | Out-Null
        }
        if ((Get-IntValue $ownerCapture.issue_count) -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "owner_capture_issue_count"; detail = [string]$ownerCapture.issue_count }) | Out-Null
        }
        Add-Count $ownerContextClassCounts ([string]$ownerCapture.owner_context_class)
        Add-Count $captureStatusCounts ([string]$ownerCapture.capture_status)
    }

    $packageReady = (
        [string]$packageSummary.status -eq "package_ready_pending_runtime_skinned_parity" -and
        (Get-IntValue $packageSummary.exported) -eq 11 -and
        (Get-IntValue $packageSummary.failed) -eq 0 -and
        (Get-IntValue $packageSummary.package_audit_counts.issue_counts.total) -eq 0
    )
    if ($packageReady) {
        $packageReadyCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "anonymous_package_not_ready"; detail = [string]$packageSummary.status }) | Out-Null
    }

    $offlineParityValid = (
        $null -ne $runtimeParity -and
        [string]$runtimeParity.status -eq "valid" -and
        (Get-IntValue $runtimeParity.issue_count) -eq 0
    )
    if ($offlineParityValid) {
        $offlineParityValidCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "offline_runtime_parity_not_valid"; detail = if ($null -eq $runtimeParity) { "missing" } else { [string]$runtimeParity.status } }) | Out-Null
    }

    $runtimeConfigReady = (
        $null -ne $runtimeConfig -and
        [string]$runtimeConfig.status -eq "ready_for_capture" -and
        [string]$runtimeConfig.expected_n64_animation_name -eq $n64Name -and
        -not [string]::IsNullOrWhiteSpace([string]$runtimeConfig.runtime_context_trace_path) -and
        (Get-IntValue $runtimeConfig.issue_count) -eq 0
    )
    if ($runtimeConfigReady) {
        $runtimeConfigReadyCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "runtime_config_not_ready"; detail = if ($null -eq $runtimeConfig) { "missing" } else { [string]$runtimeConfig.status } }) | Out-Null
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

    if ($null -ne $identity -and $null -ne $tableContext -and $null -ne $ownerCapture) {
        $identityMatrixReadyCount++
        $tableContextReadyCount++
        $ownerCaptureReadyCount++
    }

    $dependencyStatus = if ($issues.Count -ne 0) {
        "anonymous_identity_owner_dependency_has_issues"
    }
    elseif ($installedRuntimeValid) {
        "anonymous_identity_owner_dependency_ready_with_natural_capture_pending_owner_policy"
    }
    elseif ($null -eq $spec) {
        "anonymous_identity_owner_dependency_has_issues"
    }
    else {
        [string]$spec.dependency_status
    }
    if ($dependencyStatus -like "*dependency_ready*") {
        $dependencyReadyCount++
    }
    Add-Count $statusCounts $dependencyStatus
    foreach ($issue in @($issues.ToArray())) {
        Add-Count $issueReasonCounts ([string]$issue.reason)
    }
    $issueCount += $issues.Count

    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        source_csab_name = $sourceCsab
        source_contract_kind = if ($null -eq $identity) { "" } else { [string]$identity.source_contract_kind }
        n64_table_index = if ($null -eq $tableContext) { "" } else { [string]$tableContext.n64_table_index }
        n64_table_offset = if ($null -eq $tableContext) { "" } else { [string]$tableContext.n64_table_offset }
        numeric_name_offset_match = if ($null -eq $tableContext) { $false } else { Get-BoolValue $tableContext.numeric_name_offset_match }
        data_xml_order_match = if ($null -eq $tableContext) { $false } else { Get-BoolValue $tableContext.data_xml_order_match }
        frame_count_match = if ($null -eq $tableContext) { $false } else { Get-BoolValue $tableContext.frame_count_match }
        identity_class = if ($null -eq $identity) { "" } else { [string]$identity.identity_class }
        ownership_class = if ($null -eq $identity) { "" } else { [string]$identity.ownership_class }
        candidate_owner_symbols = if ($null -eq $ownerCapture) { "" } else { [string]$ownerCapture.candidate_owner_symbols }
        candidate_owner_symbol_reference_count = if ($null -eq $tableContext) { 0 } else { Get-IntValue $tableContext.candidate_owner_symbol_reference_count }
        direct_anonymous_symbol_reference_count = if ($null -eq $tableContext) { 0 } else { Get-IntValue $tableContext.direct_anonymous_symbol_reference_count }
        owner_context_class = if ($null -eq $ownerCapture) { "" } else { [string]$ownerCapture.owner_context_class }
        capture_status = if ($null -eq $ownerCapture) { "" } else { [string]$ownerCapture.capture_status }
        runtime_trigger = if ($null -eq $ownerCapture) { "" } else { [string]$ownerCapture.runtime_trigger }
        offline_runtime_parity_status = if ($null -eq $runtimeParity) { "missing" } else { [string]$runtimeParity.status }
        runtime_config_status = if ($null -eq $runtimeConfig) { "missing" } else { [string]$runtimeConfig.status }
        runtime_harness_dump_valid = $runtimeHarnessValid
        installed_runtime_dump_valid = $installedRuntimeValid
        dependency_decision_status = $dependencyStatus
        semantic_acceptance_status = "blocked_pending_owner_policy_and_natural_capture"
        mapping_promotion_allowed = $false
        required_next_step = if ($null -eq $ownerCapture) { "recover anonymous owner capture workorder" } else { [string]$ownerCapture.required_next_step }
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray())
$summaryPath = Join-Path $OutputRoot "link_child_anonymous_identity_owner_dependency_policy_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_anonymous_identity_owner_dependency_policy.csv"
$status = if ($issueCount -eq 0) { "anonymous_identity_owner_dependency_ready" } else { "anonymous_identity_owner_dependency_has_issues" }

$summary = [pscustomobject]@{
    format = "oot3d_link_child_anonymous_identity_owner_dependency_policy_v1"
    status = $status
    policy = [ordered]@{
        scope = "Link child residual rows whose N64 PlayerAnimation symbol is numeric/anonymous and needs identity plus owner proof"
        semantic_effect = "proves dependency readiness only; no anonymous identities, ownership decisions, reuse policies, semantic acceptances, or mapping promotions are accepted"
        acceptance_policy = "each row still requires explicit owner/reuse policy acceptance and a non-harness installed runtime draw before promotion"
    }
    anonymous_identity_summary = (Resolve-Path -LiteralPath $identitySummaryPath).Path
    anonymous_n64_table_context_summary = (Resolve-Path -LiteralPath $tableContextSummaryPath).Path
    anonymous_owner_capture_matrix_summary = (Resolve-Path -LiteralPath $ownerCaptureSummaryPath).Path
    anonymous_numeric_diagnostic_package_summary = (Resolve-Path -LiteralPath $packageSummaryPath).Path
    anonymous_numeric_diagnostic_runtime_parity_summary = (Resolve-Path -LiteralPath $runtimeParitySummaryPath).Path
    anonymous_owner_runtime_config_summary = (Resolve-Path -LiteralPath $runtimeConfigSummaryPath).Path
    semantic_frontier_closure_summary = (Resolve-Path -LiteralPath $closureSummaryPath).Path
    runtime_config_capture_csv = (Resolve-Path -LiteralPath $runtimeCaptureCsv).Path
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    record_count = $recordArray.Count
    closure_runtime_harness_dump_valid_count = Get-IntValue (Get-JsonValue $closureSummary "runtime_harness_dump_valid_count")
    dependency_ready_count = $dependencyReadyCount
    identity_matrix_ready_count = $identityMatrixReadyCount
    table_context_ready_count = $tableContextReadyCount
    owner_capture_workorder_ready_count = $ownerCaptureReadyCount
    package_ready_count = $packageReadyCount
    offline_runtime_parity_valid_count = $offlineParityValidCount
    runtime_config_ready_count = $runtimeConfigReadyCount
    runtime_harness_dump_valid_count = $runtimeHarnessValidCount
    natural_capture_required_count = $naturalCaptureRequiredCount
    numeric_name_offset_match_count = $numericOffsetMatchCount
    data_xml_order_match_count = $dataOrderMatchCount
    frame_count_match_count = $frameCountMatchCount
    candidate_owner_reference_present_count = $candidateOwnerReferencePresentCount
    candidate_owner_symbol_reference_count = $candidateOwnerReferenceTotal
    direct_anonymous_symbol_reference_count = $directAnonymousReferenceTotal
    candidate_source_reuse_count = $candidateSourceReuseCount
    promoted_source_reuse_count = $promotedSourceReuseCount
    identity_accepted_count = $identityAcceptedCount
    ownership_accepted_count = $ownershipAcceptedCount
    accepted_reuse_policy_count = $acceptedReusePolicyCount
    semantic_accepted_count = $semanticAcceptedCount
    mapping_promotion_allowed_count = $mappingPromotionAllowedCount
    issue_count = $issueCount
    dependency_decision_status_counts = $statusCounts
    identity_class_counts = $identityClassCounts
    ownership_class_counts = $ownershipClassCounts
    owner_context_class_counts = $ownerContextClassCounts
    capture_status_counts = $captureStatusCounts
    source_contract_counts = $sourceContractCounts
    issue_reason_counts = $issueReasonCounts
    upstream_counts = [ordered]@{
        identity_matrix_record_count = Get-IntValue $identitySummary.record_count
        table_context_record_count = Get-IntValue $tableContextSummary.record_count
        owner_capture_record_count = Get-IntValue $ownerCaptureSummary.record_count
        package_exported_count = Get-IntValue $packageSummary.exported
        runtime_parity_exported_count = Get-IntValue $runtimeParitySummary.exported
        runtime_config_record_count = Get-IntValue $runtimeConfigSummary.record_count
    }
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,source_contract_kind,n64_table_index,n64_table_offset,numeric_name_offset_match,data_xml_order_match,frame_count_match,identity_class,ownership_class,candidate_owner_symbols,candidate_owner_symbol_reference_count,direct_anonymous_symbol_reference_count,owner_context_class,capture_status,runtime_trigger,offline_runtime_parity_status,runtime_config_status,runtime_harness_dump_valid,installed_runtime_dump_valid,dependency_decision_status,semantic_acceptance_status,mapping_promotion_allowed,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_anonymous_identity_owner_dependency_policy_v1") "unexpected dependency summary format"
    Assert-Condition ($summary.status -eq "anonymous_identity_owner_dependency_ready") "expected anonymous identity/owner dependency policy to be ready"
    Assert-Condition ([int]$summary.record_count -eq 11) "expected 11 anonymous identity/owner dependency rows"
    Assert-Condition ([int]$summary.dependency_ready_count -eq 11) "expected all anonymous rows dependency-ready"
    Assert-Condition ([int]$summary.identity_matrix_ready_count -eq 11) "expected 11 identity matrix rows"
    Assert-Condition ([int]$summary.table_context_ready_count -eq 11) "expected 11 N64 table context rows"
    Assert-Condition ([int]$summary.owner_capture_workorder_ready_count -eq 11) "expected 11 owner capture workorders"
    Assert-Condition ([int]$summary.package_ready_count -eq 11) "expected package readiness for all 11 rows"
    Assert-Condition ([int]$summary.offline_runtime_parity_valid_count -eq 11) "expected offline runtime parity for all 11 rows"
    Assert-Condition ([int]$summary.runtime_config_ready_count -eq 11) "expected runtime config readiness for all 11 rows"
    Assert-Condition ([int]$summary.runtime_harness_dump_valid_count -eq 11) "expected harness-valid dumps for all 11 rows"
    Assert-Condition ([int]$summary.natural_capture_required_count -eq 11) "expected 11 natural captures still required"
    Assert-Condition ([int]$summary.numeric_name_offset_match_count -eq 11) "expected 11 numeric name/offset matches"
    Assert-Condition ([int]$summary.data_xml_order_match_count -eq 11) "expected 11 data XML order matches"
    Assert-Condition ([int]$summary.frame_count_match_count -eq 11) "expected 11 frame-count matches"
    Assert-Condition ([int]$summary.candidate_owner_reference_present_count -eq 11) "expected candidate-owner source refs for all rows"
    Assert-Condition ([int]$summary.candidate_owner_symbol_reference_count -eq 14) "expected 14 candidate-owner source refs"
    Assert-Condition ([int]$summary.direct_anonymous_symbol_reference_count -eq 0) "expected no direct anonymous symbol references"
    Assert-Condition ([int]$summary.candidate_source_reuse_count -eq 9) "expected 9 candidate-source anonymous rows"
    Assert-Condition ([int]$summary.promoted_source_reuse_count -eq 2) "expected 2 promoted-source anonymous rows"
    Assert-Condition ([int]$summary.identity_accepted_count -eq 0) "expected no accepted anonymous identities"
    Assert-Condition ([int]$summary.ownership_accepted_count -eq 0) "expected no accepted anonymous ownership decisions"
    Assert-Condition ([int]$summary.accepted_reuse_policy_count -eq 0) "expected no accepted anonymous reuse policies"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected no semantic acceptances"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected no mapping promotions"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero dependency issues"
    Assert-Condition ((Get-JsonValue $summary.dependency_decision_status_counts "candidate_source_identity_owner_dependency_ready_pending_owner_policy_or_natural_capture") -eq 9) "expected 9 candidate-source dependency-ready rows"
    Assert-Condition ((Get-JsonValue $summary.dependency_decision_status_counts "promoted_source_reuse_identity_owner_dependency_ready_pending_owner_policy_or_natural_capture") -eq 2) "expected 2 promoted-source dependency-ready rows"
    Assert-Condition ((Get-JsonValue $summary.capture_status_counts "needs_model_anim_type_equipment_capture") -eq 5) "expected five model/equipment captures"
    Assert-Condition ((Get-JsonValue $summary.capture_status_counts "needs_wait_defense_transition_table_capture") -eq 2) "expected two wait-defense captures"
    Assert-Condition ((Get-JsonValue $summary.capture_status_counts "needs_boomerang_upper_action_branch_capture") -eq 1) "expected one boomerang capture"
    Assert-Condition ((Get-JsonValue $summary.capture_status_counts "needs_cutscene_actor_cue_runtime_capture") -eq 1) "expected one cutscene capture"
    Assert-Condition ((Get-JsonValue $summary.capture_status_counts "needs_power_kiru_table_and_cutscene_capture") -eq 1) "expected one power-kiru/cutscene capture"
    Assert-Condition ((Get-JsonValue $summary.capture_status_counts "needs_climb_side_table_capture") -eq 1) "expected one climb-side capture"
    Require-Path $csvPath "Anonymous identity/owner dependency CSV"
}

Write-Host "OOT3D Link child anonymous identity/owner dependency policy: $summaryPath"
Write-Host "status=$($summary.status) rows=$($summary.record_count) dependencyReady=$($summary.dependency_ready_count) ownerRefs=$($summary.candidate_owner_symbol_reference_count) harnessValid=$($summary.runtime_harness_dump_valid_count) naturalRequired=$($summary.natural_capture_required_count) issues=$($summary.issue_count)"
