param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\promoted_owner_alias_policy"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child promoted-owner alias policy verification failed: $Message"
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

function Add-ListByKey([hashtable]$Map, [string]$Key, [object]$Value) {
    if (-not $Map.ContainsKey($Key)) {
        $Map[$Key] = New-Object "System.Collections.Generic.List[object]"
    }
    $Map[$Key].Add($Value) | Out-Null
}

$characterRoot = Join-Path $WorkRoot "character_conversion"
$resolutionContractCsv = Join-Path $characterRoot "link_child_animation_semantic_resolution_contract.csv"
$closureSummaryPath = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix_summary.json"
$closureCsv = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix.csv"
$runtimeCaptureCsv = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix.csv"

Require-Path $resolutionContractCsv "Semantic resolution contract CSV"
Require-Path $closureSummaryPath "Semantic frontier closure summary"
Require-Path $closureCsv "Semantic frontier closure CSV"
Require-Path $runtimeCaptureCsv "Runtime config capture matrix CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$resolutionRows = @(Import-Csv -LiteralPath $resolutionContractCsv)
$closureSummary = Read-Json $closureSummaryPath
$closureRows = @(Import-Csv -LiteralPath $closureCsv)
$runtimeCaptureRows = @(Import-Csv -LiteralPath $runtimeCaptureCsv)

$runtimeByName = @{}
foreach ($row in $runtimeCaptureRows) {
    $runtimeByName[[string]$row.n64_name] = $row
}

$promotedOwnersBySource = @{}
foreach ($row in $resolutionRows) {
    if ([string]$row.semantic_resolution_status -eq "runtime_promoted_mapping" -or
        [string]$row.source_contract_kind -eq "promoted_runtime_mapping") {
        Add-ListByKey $promotedOwnersBySource ([string]$row.source_csab_name) $row
    }
}

$targetRows = @(
    $closureRows | Where-Object {
        [string]$_.post_harness_blocker -eq "single_promoted_owner_policy_and_natural_capture_required"
    }
)

$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$policyReadyCount = 0
$naturalCaptureRequiredCount = 0
$harnessValidCount = 0
$uniquePromotedOwnerCount = 0
$statusCounts = @{}
$issueReasonCounts = @{}

foreach ($target in $targetRows) {
    $n64Name = [string]$target.n64_name
    $sourceCsab = [string]$target.source_csab_name
    $runtime = $runtimeByName[$n64Name]
    $owners = @()
    if ($promotedOwnersBySource.ContainsKey($sourceCsab)) {
        $owners = @($promotedOwnersBySource[$sourceCsab].ToArray())
    }

    $issues = New-Object "System.Collections.Generic.List[object]"
    if ($owners.Count -eq 0) {
        $issues.Add([pscustomobject]@{ reason = "missing_promoted_owner"; detail = $sourceCsab }) | Out-Null
    }
    elseif ($owners.Count -ne 1) {
        $issues.Add([pscustomobject]@{ reason = "ambiguous_promoted_owner"; detail = ($owners.n64_name -join ";") }) | Out-Null
    }
    else {
        $uniquePromotedOwnerCount++
    }

    if ($null -eq $runtime) {
        $issues.Add([pscustomobject]@{ reason = "missing_runtime_capture_row"; detail = $n64Name }) | Out-Null
    }
    $runtimeHarnessValid = if ($null -eq $runtime) { $false } else { Get-BoolValue $runtime.runtime_harness_dump_valid }
    $installedRuntimeValid = if ($null -eq $runtime) { $false } else { Get-BoolValue $runtime.installed_runtime_dump_valid }
    if ($runtimeHarnessValid) {
        $harnessValidCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "missing_harness_geometry_evidence"; detail = $n64Name }) | Out-Null
    }

    $naturalCaptureStatus = if ($installedRuntimeValid) { "natural_capture_valid" } else { "pending_natural_capture" }
    if (-not $installedRuntimeValid) {
        $naturalCaptureRequiredCount++
    }

    $policyStatus = "promoted_owner_alias_policy_ready_pending_natural_capture"
    if ($issues.Count -ne 0) {
        $policyStatus = "promoted_owner_alias_policy_has_issues"
    }
    elseif ($installedRuntimeValid) {
        $policyStatus = "promoted_owner_alias_policy_ready_with_natural_capture"
    }
    if ($policyStatus -like "promoted_owner_alias_policy_ready*") {
        $policyReadyCount++
    }
    Add-Count $statusCounts $policyStatus
    foreach ($issue in @($issues.ToArray())) {
        Add-Count $issueReasonCounts ([string]$issue.reason)
    }
    $issueCount += $issues.Count

    $owner = if ($owners.Count -eq 1) { $owners[0] } else { $null }
    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        source_csab_name = $sourceCsab
        promoted_owner_n64_name = if ($null -eq $owner) { ($owners.n64_name -join ";") } else { [string]$owner.n64_name }
        promoted_owner_semantic_resolution_status = if ($null -eq $owner) { "" } else { [string]$owner.semantic_resolution_status }
        promoted_owner_runtime_acceptance_status = if ($null -eq $owner) { "" } else { [string]$owner.runtime_acceptance_status }
        alias_semantic_resolution_status = [string]$target.semantic_resolution_status
        alias_source_contract_kind = [string]$target.source_contract_kind
        alias_frontier_class = [string]$target.frontier_class
        runtime_harness_dump_valid = $runtimeHarnessValid
        natural_capture_status = $naturalCaptureStatus
        policy_decision_status = $policyStatus
        semantic_acceptance_status = "blocked_pending_natural_capture"
        mapping_promotion_allowed = $false
        natural_capture_command = if ($null -eq $runtime) { "" } else { [string]$runtime.runner_command }
        harness_capture_command = if ($null -eq $runtime) { "" } else { [string]$runtime.harness_runner_command }
        required_next_step = "drive the natural player context for this alias and capture a non-harness installed runtime draw"
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray())
$summaryPath = Join-Path $OutputRoot "link_child_promoted_owner_alias_policy_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_promoted_owner_alias_policy.csv"
$status = if ($issueCount -eq 0) { "promoted_owner_alias_policy_ready" } else { "promoted_owner_alias_policy_has_issues" }

$summary = [pscustomobject]@{
    format = "oot3d_link_child_promoted_owner_alias_policy_v1"
    status = $status
    policy = [ordered]@{
        scope = "Link child residual rows whose source CSAB already has a runtime-promoted owner"
        semantic_effect = "proves promoted-owner alias policy readiness only; no semantic identities, natural captures, or mapping promotions are accepted"
        acceptance_policy = "each row still requires a non-harness installed runtime draw before promotion"
    }
    semantic_resolution_contract_csv = (Resolve-Path -LiteralPath $resolutionContractCsv).Path
    semantic_frontier_closure_summary = (Resolve-Path -LiteralPath $closureSummaryPath).Path
    runtime_config_capture_csv = (Resolve-Path -LiteralPath $runtimeCaptureCsv).Path
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    record_count = $recordArray.Count
    closure_runtime_harness_dump_valid_count = [int](Get-JsonValue $closureSummary "runtime_harness_dump_valid_count")
    unique_promoted_owner_count = $uniquePromotedOwnerCount
    policy_ready_count = $policyReadyCount
    runtime_harness_dump_valid_count = $harnessValidCount
    natural_capture_required_count = $naturalCaptureRequiredCount
    semantic_accepted_count = 0
    mapping_promotion_allowed_count = 0
    issue_count = $issueCount
    policy_decision_status_counts = $statusCounts
    issue_reason_counts = $issueReasonCounts
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,promoted_owner_n64_name,promoted_owner_semantic_resolution_status,promoted_owner_runtime_acceptance_status,alias_semantic_resolution_status,alias_source_contract_kind,alias_frontier_class,runtime_harness_dump_valid,natural_capture_status,policy_decision_status,semantic_acceptance_status,mapping_promotion_allowed,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_promoted_owner_alias_policy_v1") "unexpected policy summary format"
    Assert-Condition ($summary.status -eq "promoted_owner_alias_policy_ready") "expected promoted-owner alias policy to be ready"
    Assert-Condition ([int]$summary.record_count -eq 7) "expected 7 promoted-owner alias rows"
    Assert-Condition ([int]$summary.unique_promoted_owner_count -eq 7) "expected each alias row to resolve to one promoted owner"
    Assert-Condition ([int]$summary.policy_ready_count -eq 7) "expected every alias policy to be ready"
    Assert-Condition ([int]$summary.runtime_harness_dump_valid_count -eq 7) "expected 7 harness-valid alias rows"
    Assert-Condition ([int]$summary.natural_capture_required_count -eq 7) "expected 7 natural captures still required"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected no semantic acceptances"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected no mapping promotions"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero policy issues"
    Require-Path $csvPath "Promoted-owner alias policy CSV"
}

Write-Host "OOT3D Link child promoted-owner alias policy: $summaryPath"
Write-Host "status=$($summary.status) rows=$($summary.record_count) policyReady=$($summary.policy_ready_count) harnessValid=$($summary.runtime_harness_dump_valid_count) naturalRequired=$($summary.natural_capture_required_count) issues=$($summary.issue_count)"
