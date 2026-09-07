param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\direct_callsite_semantic_acceptance_review"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child direct-callsite semantic acceptance review failed: $Message"
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

$characterRoot = Join-Path $WorkRoot "character_conversion"
$sourceIdentitySummaryPath = Join-Path $characterRoot "direct_callsite_source_identity_review\link_child_direct_callsite_source_identity_review_summary.json"
$sourceIdentityCsvPath = Join-Path $characterRoot "direct_callsite_source_identity_review\link_child_direct_callsite_source_identity_review.csv"
$runtimeSummaryPath = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix_summary.json"
$runtimeCsvPath = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix.csv"
$captureStatusRoot = Join-Path $characterRoot "runtime_config_capture_status"

Require-Path $sourceIdentitySummaryPath "Direct-callsite source identity review summary"
Require-Path $sourceIdentityCsvPath "Direct-callsite source identity review CSV"
Require-Path $runtimeSummaryPath "Runtime config capture matrix summary"
Require-Path $runtimeCsvPath "Runtime config capture matrix CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$sourceIdentitySummary = Read-Json $sourceIdentitySummaryPath
$runtimeSummary = Read-Json $runtimeSummaryPath
$sourceIdentityRows = @(Import-Csv -LiteralPath $sourceIdentityCsvPath)
$runtimeRows = @(Import-Csv -LiteralPath $runtimeCsvPath)
$runtimeByName = @{}
Add-ByName $runtimeByName $runtimeRows

$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$semanticAcceptedCount = 0
$mappingPromotionAllowedCount = 0
$sourceIdentityAcceptedCount = 0
$runtimeDrawAuditValidCount = 0
$dynamicTraceReadyCount = 0
$cvarDiagnosticProofCount = 0
$statusCounts = @{}
$promotionBlockerCounts = @{}
$selectionSourceCounts = @{}
$issueReasonCounts = @{}

foreach ($identity in $sourceIdentityRows) {
    $n64Name = [string]$identity.n64_name
    $runtime = $runtimeByName[$n64Name]
    $issues = New-Object "System.Collections.Generic.List[object]"
    $sourceIdentityAccepted = Get-BoolValue $identity.source_identity_accepted
    if ($sourceIdentityAccepted) {
        $sourceIdentityAcceptedCount++
    }

    $runtimeInstalledValid = if ($null -eq $runtime) { $false } else { Get-BoolValue $runtime.installed_runtime_dump_valid }
    $dynamicTraceReady = (
        $runtimeInstalledValid -and
        (Get-BoolValue $identity.runtime_trace_valid) -and
        (Get-BoolValue $identity.runtime_variant_context_ready) -and
        (Get-IntValue $identity.context_trace_unique_selected_frame_count) -gt 1 -and
        (Get-IntValue $identity.context_trace_forced_static_frame_row_count) -eq 0
    )
    if ($dynamicTraceReady) {
        $dynamicTraceReadyCount++
    }

    $captureStatusPath = Join-Path $captureStatusRoot "link_child_runtime_config_capture_status_$n64Name.json"
    $captureStatus = $null
    if ($sourceIdentityAccepted) {
        Require-Path $captureStatusPath "Runtime config capture status"
        $captureStatus = Read-Json $captureStatusPath
    }
    $auditPath = if ($null -eq $captureStatus) {
        Join-Path $captureStatusRoot "link_child_runtime_config_draw_dump_audit_$n64Name.json"
    }
    else {
        [string]$captureStatus.audit_output
    }
    $runtimeDumpPath = if ($null -eq $captureStatus) { "" } else { [string]$captureStatus.expected_dump_path }

    $audit = $null
    $runtimeDump = $null
    $auditValid = $false
    $selectionSource = ""
    $animationResourcePath = ""
    $trackExport = ""
    $auditIssueCount = 0
    $primitiveMatchCount = 0
    $expectedPrimitiveCount = 0
    $validationErrorCount = 0
    if ($sourceIdentityAccepted) {
        Require-Path $auditPath "Runtime draw dump audit"
        Require-Path $runtimeDumpPath "Runtime draw dump"
        $audit = Read-Json $auditPath
        $runtimeDump = Read-Json $runtimeDumpPath
        $comparison = Get-JsonValue $audit "comparison"
        $auditIssueCount = Get-IntValue (Get-JsonValue $comparison "issue_count")
        $primitiveMatchCount = Get-IntValue (Get-JsonValue $comparison "matched_primitive_count")
        $expectedPrimitiveCount = Get-IntValue (Get-JsonValue $audit "expected_primitive_count")
        $drawCounts = Get-JsonValue $runtimeDump "draw_counts"
        $validationErrorCount = Get-IntValue (Get-JsonValue $drawCounts "validation_error_count")
        $selectionSource = [string](Get-JsonValue $runtimeDump "selection_source")
        $animationResourcePath = [string](Get-JsonValue $runtimeDump "animation_resource_path")
        $trackExport = [string](Get-JsonValue $audit "track_export")
        $auditValid = (
            [string](Get-JsonValue $audit "status") -eq "valid" -and
            [string](Get-JsonValue $audit "n64_animation_name") -eq $n64Name -and
            [string](Get-JsonValue $audit "csab_name") -eq [string]$identity.output_csab_name -and
            $auditIssueCount -eq 0 -and
            $expectedPrimitiveCount -gt 0 -and
            $primitiveMatchCount -eq $expectedPrimitiveCount -and
            $validationErrorCount -eq 0
        )
        if ($auditValid) {
            $runtimeDrawAuditValidCount++
        }
        else {
            $issues.Add([pscustomobject]@{
                reason = "runtime_draw_audit_not_valid"
                detail = "$auditPath issueCount=$auditIssueCount primitiveMatch=$primitiveMatchCount/$expectedPrimitiveCount validationErrors=$validationErrorCount"
            }) | Out-Null
        }
    }

    if ($sourceIdentityAccepted -and -not $dynamicTraceReady) {
        $issues.Add([pscustomobject]@{ reason = "dynamic_trace_not_ready"; detail = $n64Name }) | Out-Null
    }
    if ($sourceIdentityAccepted -and -not (Get-BoolValue $identity.source_alias_evidence_accepted)) {
        $issues.Add([pscustomobject]@{ reason = "source_alias_not_accepted"; detail = [string]$identity.source_alias_evidence }) | Out-Null
    }
    if ($sourceIdentityAccepted -and [string]$animationResourcePath -notlike "*$n64Name*") {
        $issues.Add([pscustomobject]@{ reason = "runtime_resource_not_bound_to_n64_name"; detail = $animationResourcePath }) | Out-Null
    }

    $semanticAccepted = (
        $sourceIdentityAccepted -and
        [int]$issues.Count -eq 0 -and
        $runtimeInstalledValid -and
        $dynamicTraceReady -and
        $auditValid
    )

    $cvarDiagnosticProof = $semanticAccepted -and $selectionSource -in @("cvar_override", "cvar_override_resource")
    if ($cvarDiagnosticProof) {
        $cvarDiagnosticProofCount++
    }
    $baseProfileSelectionProof = $selectionSource -in @(
        "profile_by_n64_name",
        "profile_by_n64_data_name",
        "profile_by_n64_resource_path",
        "profile_stem_alias",
        "base_free"
    )
    $diagnosticRuntimeResource = (
        [string]$animationResourcePath -like "*diagnostic_package*" -or
        [string]$animationResourcePath -like "*diagnostic_candidates*" -or
        [string]$animationResourcePath -like "*pose_source_risk_diagnostic_package*" -or
        [string]$selectionSource -like "diagnostic_*"
    )
    $promotionBlocker = if (-not $semanticAccepted) {
        "semantic_acceptance_required"
    }
    elseif ($cvarDiagnosticProof) {
        "base_profile_mapping_integration_required_after_cvar_diagnostic_proof"
    }
    elseif (-not $baseProfileSelectionProof) {
        "base_profile_runtime_selection_required_after_semantic_acceptance"
    }
    elseif ($diagnosticRuntimeResource) {
        "base_profile_mapping_integration_required_after_diagnostic_package_proof"
    }
    else {
        "none"
    }
    $mappingPromotionAllowed = $semanticAccepted -and $promotionBlocker -eq "none"

    if ($semanticAccepted) {
        $semanticAcceptedCount++
    }
    if ($mappingPromotionAllowed) {
        $mappingPromotionAllowedCount++
    }

    $semanticStatus = if ([int]$issues.Count -ne 0) {
        "semantic_acceptance_review_has_issues"
    }
    elseif ($semanticAccepted -and $mappingPromotionAllowed) {
        "semantic_accepted_mapping_promotion_allowed"
    }
    elseif ($semanticAccepted) {
        "semantic_accepted_pending_mapping_promotion"
    }
    elseif ($sourceIdentityAccepted) {
        "semantic_blocked_pending_runtime_audit_or_dynamic_trace"
    }
    else {
        "semantic_blocked_pending_source_identity_or_installed_dynamic_draw"
    }
    Add-Count $statusCounts $semanticStatus
    Add-Count $promotionBlockerCounts $promotionBlocker
    Add-Count $selectionSourceCounts $selectionSource
    foreach ($issue in @($issues.ToArray())) {
        Add-Count $issueReasonCounts ([string]$issue.reason)
    }
    $issueCount += [int]$issues.Count

    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        source_csab_name = [string]$identity.source_csab_name
        source_oot3d_stem = [string]$identity.source_oot3d_stem
        output_csab_name = [string]$identity.output_csab_name
        callsite_review_class = [string]$identity.callsite_review_class
        source_identity_review_status = [string]$identity.source_identity_review_status
        source_identity_accepted = $sourceIdentityAccepted
        source_alias_evidence = [string]$identity.source_alias_evidence
        installed_runtime_dump_valid = $runtimeInstalledValid
        dynamic_trace_ready = $dynamicTraceReady
        runtime_draw_audit_valid = $auditValid
        runtime_draw_audit_path = $auditPath
        runtime_dump_path = $runtimeDumpPath
        selection_source = $selectionSource
        base_profile_selection_proof = $baseProfileSelectionProof
        animation_resource_path = $animationResourcePath
        track_export = $trackExport
        matched_primitive_count = $primitiveMatchCount
        expected_primitive_count = $expectedPrimitiveCount
        validation_error_count = $validationErrorCount
        semantic_acceptance_review_status = $semanticStatus
        semantic_acceptance_status = if ($semanticAccepted) { "semantic_accepted" } else { "blocked_pending_source_identity_or_runtime_audit" }
        semantic_accepted = $semanticAccepted
        mapping_promotion_allowed = $mappingPromotionAllowed
        mapping_promotion_blocker = $promotionBlocker
        required_next_step = if ($mappingPromotionAllowed) {
            "replacement gate can consume semantic acceptance and mapping promotion"
        }
        elseif ($semanticAccepted) {
            "integrate accepted diagnostic mapping into the base character profile and verify non-CVar runtime selection"
        }
        else {
            [string]$identity.required_next_step
        }
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray() | Sort-Object semantic_acceptance_review_status,n64_name)
$summaryPath = Join-Path $OutputRoot "link_child_direct_callsite_semantic_acceptance_review_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_direct_callsite_semantic_acceptance_review.csv"
$status = if ($issueCount -eq 0) { "direct_callsite_semantic_acceptance_review_ready" } else { "direct_callsite_semantic_acceptance_review_has_issues" }

$summary = [pscustomobject]@{
    format = "oot3d_link_child_direct_callsite_semantic_acceptance_review_v1"
    status = $status
    policy = [ordered]@{
        scope = "Link child direct-callsite rows after source identity review"
        semantic_rule = "Accept semantics only when source identity, dynamic installed trace, runtime variant context, and runtime draw audit are all valid."
        promotion_rule = "Do not allow mapping promotion while the proof still depends on a CVar diagnostic resource or diagnostic package export."
    }
    direct_callsite_source_identity_review_summary = (Resolve-Path -LiteralPath $sourceIdentitySummaryPath).Path
    runtime_config_capture_matrix_summary = (Resolve-Path -LiteralPath $runtimeSummaryPath).Path
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    record_count = $recordArray.Count
    source_identity_review_row_count = Get-IntValue $sourceIdentitySummary.record_count
    runtime_capture_row_count = Get-IntValue $runtimeSummary.record_count
    source_identity_accepted_count = $sourceIdentityAcceptedCount
    dynamic_trace_ready_count = $dynamicTraceReadyCount
    runtime_draw_audit_valid_count = $runtimeDrawAuditValidCount
    cvar_diagnostic_proof_count = $cvarDiagnosticProofCount
    semantic_accepted_count = $semanticAcceptedCount
    mapping_promotion_allowed_count = $mappingPromotionAllowedCount
    issue_count = $issueCount
    semantic_acceptance_review_status_counts = $statusCounts
    mapping_promotion_blocker_counts = $promotionBlockerCounts
    selection_source_counts = $selectionSourceCounts
    issue_reason_counts = $issueReasonCounts
    records = $recordArray
}

$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8
$recordArray |
    Select-Object n64_name,source_csab_name,source_oot3d_stem,output_csab_name,callsite_review_class,source_identity_review_status,source_identity_accepted,source_alias_evidence,installed_runtime_dump_valid,dynamic_trace_ready,runtime_draw_audit_valid,runtime_draw_audit_path,runtime_dump_path,selection_source,animation_resource_path,track_export,matched_primitive_count,expected_primitive_count,validation_error_count,semantic_acceptance_review_status,semantic_acceptance_status,semantic_accepted,mapping_promotion_allowed,mapping_promotion_blocker,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_direct_callsite_semantic_acceptance_review_v1") "unexpected semantic acceptance review format"
    Assert-Condition ($summary.status -eq "direct_callsite_semantic_acceptance_review_ready") "expected semantic acceptance review ready"
    Assert-Condition ([int]$summary.record_count -eq 16) "expected 16 semantic review rows"
    Assert-Condition ([int]$summary.source_identity_accepted_count -eq 16) "expected sixteen accepted source identities"
    Assert-Condition ([int]$summary.dynamic_trace_ready_count -eq 16) "expected sixteen dynamic trace ready rows"
    Assert-Condition ([int]$summary.runtime_draw_audit_valid_count -eq 16) "expected sixteen valid runtime draw audits"
    Assert-Condition ([int]$summary.cvar_diagnostic_proof_count -eq 0) "expected zero CVar diagnostic proofs after base-profile runtime selection"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 16) "expected sixteen semantic acceptances"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 16) "expected sixteen mapping promotions"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero semantic review issues"
    Assert-Condition ((Get-JsonValue $summary.semantic_acceptance_review_status_counts "semantic_accepted_mapping_promotion_allowed") -eq 16) "expected sixteen semantic acceptances with mapping promotion"
    Assert-Condition ((Get-IntValue (Get-JsonValue $summary.semantic_acceptance_review_status_counts "semantic_blocked_pending_source_identity_or_installed_dynamic_draw")) -eq 0) "expected zero rows pending source identity or installed dynamic draw"
    Assert-Condition ((Get-JsonValue $summary.mapping_promotion_blocker_counts "none") -eq 16) "expected sixteen unblocked mapping promotions"
    Require-Path $csvPath "Direct-callsite semantic acceptance review CSV"
}

Write-Host "OOT3D Link child direct-callsite semantic acceptance review: $summaryPath"
Write-Host "status=$($summary.status) rows=$($summary.record_count) semanticAccepted=$($summary.semantic_accepted_count) promotions=$($summary.mapping_promotion_allowed_count) cvarProofs=$($summary.cvar_diagnostic_proof_count) issues=$($summary.issue_count)"
