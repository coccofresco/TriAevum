param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\pose_source_direct_callsite_dependency_policy"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child pose/source direct-callsite dependency verification failed: $Message"
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
$callsiteSummaryPath = Join-Path $characterRoot "link_child_animation_pose_source_callsite_context.json"
$callsiteCsv = Join-Path $characterRoot "link_child_animation_pose_source_callsite_context.csv"
$workorderSummaryPath = Join-Path $characterRoot "pose_source_runtime_capture_matrix\link_child_pose_source_runtime_capture_matrix_summary.json"
$workorderCsv = Join-Path $characterRoot "pose_source_runtime_capture_matrix\link_child_pose_source_runtime_capture_matrix.csv"
$packageSummaryPath = Join-Path $characterRoot "pose_source_risk_diagnostic_package\link_child_pose_source_risk_diagnostic_package_summary.json"
$runtimeParitySummaryPath = Join-Path $characterRoot "pose_source_risk_diagnostic_runtime_parity\link_child_pose_source_risk_diagnostic_runtime_parity_summary.json"
$runtimeConfigSummaryPath = Join-Path $characterRoot "pose_source_runtime_capture_config\link_child_pose_source_runtime_config_matrix_summary.json"
$closureSummaryPath = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix_summary.json"
$closureCsv = Join-Path $characterRoot "semantic_frontier_closure_matrix\link_child_semantic_frontier_closure_matrix.csv"
$runtimeCaptureCsv = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix.csv"

Require-Path $callsiteSummaryPath "Pose/source callsite summary"
Require-Path $callsiteCsv "Pose/source callsite CSV"
Require-Path $workorderSummaryPath "Pose/source runtime workorder summary"
Require-Path $workorderCsv "Pose/source runtime workorder CSV"
Require-Path $packageSummaryPath "Pose/source diagnostic package summary"
Require-Path $runtimeParitySummaryPath "Pose/source runtime parity summary"
Require-Path $runtimeConfigSummaryPath "Pose/source runtime config summary"
Require-Path $closureSummaryPath "Semantic frontier closure summary"
Require-Path $closureCsv "Semantic frontier closure CSV"
Require-Path $runtimeCaptureCsv "Runtime config capture matrix CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$callsiteSummary = Read-Json $callsiteSummaryPath
$workorderSummary = Read-Json $workorderSummaryPath
$packageSummary = Read-Json $packageSummaryPath
$runtimeParitySummary = Read-Json $runtimeParitySummaryPath
$runtimeConfigSummary = Read-Json $runtimeConfigSummaryPath
$closureSummary = Read-Json $closureSummaryPath

$callsiteRows = @(Import-Csv -LiteralPath $callsiteCsv)
$workorderRows = @(Import-Csv -LiteralPath $workorderCsv)
$closureRows = @(Import-Csv -LiteralPath $closureCsv)
$runtimeCaptureRows = @(Import-Csv -LiteralPath $runtimeCaptureCsv)

$callsiteByName = @{}
$workorderByName = @{}
$runtimeParityByName = @{}
$runtimeConfigByName = @{}
$runtimeCaptureByName = @{}
Add-ByName $callsiteByName $callsiteRows
Add-ByName $workorderByName $workorderRows
Add-ByName $runtimeParityByName @($runtimeParitySummary.records)
Add-ByName $runtimeConfigByName @($runtimeConfigSummary.records)
Add-ByName $runtimeCaptureByName $runtimeCaptureRows

$targetRows = @(
    $closureRows | Where-Object {
        [string]$_.frontier_gate -eq "pose_metric_or_source_identity" -and
        [string]$_.workorder_kind -eq "pose_source_runtime_capture" -and
        [string]$_.post_harness_blocker -in @(
            "pose_source_natural_context_capture_required",
            "runtime_geometry_harness_required",
            "semantic_acceptance_review_required"
        )
    }
)

$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$dependencyReadyCount = 0
$directCallsiteReadyCount = 0
$workorderReadyCount = 0
$packageReadyCount = 0
$offlineParityValidCount = 0
$runtimeConfigReadyCount = 0
$runtimeHarnessValidCount = 0
$installedRuntimeValidCount = 0
$runtimeValidEvidenceCount = 0
$naturalCaptureRequiredCount = 0
$outsideEnvelopeCount = 0
$directSourceReferenceTotal = 0
$directPlayerActorReferenceTotal = 0
$runtimeContextTriggerCount = 0
$statusCounts = @{}
$callsiteClassCounts = @{}
$nextEvidenceCounts = @{}
$captureStatusCounts = @{}
$captureScopeCounts = @{}
$forcedAnimationOnlyStatusCounts = @{}
$issueReasonCounts = @{}

foreach ($target in $targetRows) {
    $n64Name = [string]$target.n64_name
    $callsite = $callsiteByName[$n64Name]
    $workorder = $workorderByName[$n64Name]
    $runtimeParity = $runtimeParityByName[$n64Name]
    $runtimeConfig = $runtimeConfigByName[$n64Name]
    $runtimeCapture = $runtimeCaptureByName[$n64Name]
    $issues = New-Object "System.Collections.Generic.List[object]"

    if ([string]$target.frontier_gate -ne "pose_metric_or_source_identity") {
        $issues.Add([pscustomobject]@{ reason = "frontier_gate_mismatch"; detail = [string]$target.frontier_gate }) | Out-Null
    }
    if ([string]$target.workorder_kind -ne "pose_source_runtime_capture") {
        $issues.Add([pscustomobject]@{ reason = "workorder_kind_mismatch"; detail = [string]$target.workorder_kind }) | Out-Null
    }
    if ([string]$target.coverage_status -ne "covered_by_verified_workorder") {
        $issues.Add([pscustomobject]@{ reason = "coverage_status_mismatch"; detail = [string]$target.coverage_status }) | Out-Null
    }
    if ([string]$target.harness_evidence_status -notin @("harness_geometry_valid", "natural_installed_dump_valid", "natural_static_frame_dump_valid", "runtime_geometry_dump_missing")) {
        $issues.Add([pscustomobject]@{ reason = "closure_harness_evidence_status_mismatch"; detail = [string]$target.harness_evidence_status }) | Out-Null
    }

    if ($null -eq $callsite) {
        $issues.Add([pscustomobject]@{ reason = "missing_callsite_context_row"; detail = $n64Name }) | Out-Null
    }
    else {
        if ([string]$callsite.source_csab_name -ne [string]$target.source_csab_name) {
            $issues.Add([pscustomobject]@{ reason = "callsite_source_csab_mismatch"; detail = [string]$callsite.source_csab_name }) | Out-Null
        }
        if ([string]$callsite.semantic_resolution_status -ne "blocked_pose_metric_outside_reference_envelope") {
            $issues.Add([pscustomobject]@{ reason = "callsite_semantic_resolution_status_mismatch"; detail = [string]$callsite.semantic_resolution_status }) | Out-Null
        }
        if ([string]$callsite.reference_envelope_status -ne "outside_reference_envelope") {
            $issues.Add([pscustomobject]@{ reason = "callsite_reference_envelope_not_outside"; detail = [string]$callsite.reference_envelope_status }) | Out-Null
        }
        else {
            $outsideEnvelopeCount++
        }
        if ([string]$callsite.callsite_context_status -ne "direct_player_actor_callsite_found") {
            $issues.Add([pscustomobject]@{ reason = "callsite_context_status_mismatch"; detail = [string]$callsite.callsite_context_status }) | Out-Null
        }
        if ((Get-IntValue $callsite.direct_source_reference_count) -le 0) {
            $issues.Add([pscustomobject]@{ reason = "missing_direct_source_reference"; detail = $n64Name }) | Out-Null
        }
        if ((Get-IntValue $callsite.direct_player_actor_source_reference_count) -le 0) {
            $issues.Add([pscustomobject]@{ reason = "missing_direct_player_actor_reference"; detail = $n64Name }) | Out-Null
        }
        if ([string]::IsNullOrWhiteSpace([string]$callsite.sample_callsite_contexts)) {
            $issues.Add([pscustomobject]@{ reason = "missing_sample_callsite_context"; detail = $n64Name }) | Out-Null
        }
        $directSourceReferenceTotal += (Get-IntValue $callsite.direct_source_reference_count)
        $directPlayerActorReferenceTotal += (Get-IntValue $callsite.direct_player_actor_source_reference_count)
        Add-Count $callsiteClassCounts ([string]$callsite.callsite_review_class)
        Add-Count $nextEvidenceCounts ([string]$callsite.next_evidence)
    }

    if ($null -eq $workorder) {
        $issues.Add([pscustomobject]@{ reason = "missing_pose_source_workorder_row"; detail = $n64Name }) | Out-Null
    }
    else {
        if ([string]$workorder.source_csab_name -ne [string]$target.source_csab_name) {
            $issues.Add([pscustomobject]@{ reason = "workorder_source_csab_mismatch"; detail = [string]$workorder.source_csab_name }) | Out-Null
        }
        if ($null -ne $callsite -and [string]$workorder.callsite_review_class -ne [string]$callsite.callsite_review_class) {
            $issues.Add([pscustomobject]@{ reason = "workorder_callsite_class_mismatch"; detail = [string]$workorder.callsite_review_class }) | Out-Null
        }
        if ($null -ne $callsite -and [string]$workorder.next_evidence -ne [string]$callsite.next_evidence) {
            $issues.Add([pscustomobject]@{ reason = "workorder_next_evidence_mismatch"; detail = [string]$workorder.next_evidence }) | Out-Null
        }
        if ([string]::IsNullOrWhiteSpace([string]$workorder.capture_status)) {
            $issues.Add([pscustomobject]@{ reason = "workorder_capture_status_missing"; detail = $n64Name }) | Out-Null
        }
        if ([string]::IsNullOrWhiteSpace([string]$workorder.capture_scope)) {
            $issues.Add([pscustomobject]@{ reason = "workorder_capture_scope_missing"; detail = $n64Name }) | Out-Null
        }
        if ([string]::IsNullOrWhiteSpace([string]$workorder.runtime_trigger)) {
            $issues.Add([pscustomobject]@{ reason = "workorder_runtime_trigger_missing"; detail = $n64Name }) | Out-Null
        }
        else {
            $runtimeContextTriggerCount++
        }
        if ([string]$workorder.forced_animation_only_status -notlike "insufficient_without_*") {
            $issues.Add([pscustomobject]@{ reason = "forced_animation_status_not_context_blocked"; detail = [string]$workorder.forced_animation_only_status }) | Out-Null
        }
        if ((Get-IntValue $workorder.issue_count) -ne 0) {
            $issues.Add([pscustomobject]@{ reason = "workorder_issue_count"; detail = [string]$workorder.issue_count }) | Out-Null
        }
        Add-Count $captureStatusCounts ([string]$workorder.capture_status)
        Add-Count $captureScopeCounts ([string]$workorder.capture_scope)
        Add-Count $forcedAnimationOnlyStatusCounts ([string]$workorder.forced_animation_only_status)
    }

    $packageReady = (
        [string]$packageSummary.status -eq "package_ready_pending_runtime_skinned_parity" -and
        (Get-IntValue $packageSummary.exported) -eq 16 -and
        (Get-IntValue $packageSummary.failed) -eq 0 -and
        (Get-IntValue $packageSummary.package_audit_counts.issue_counts.total) -eq 0
    )
    if ($packageReady) {
        $packageReadyCount++
    }
    else {
        $issues.Add([pscustomobject]@{ reason = "pose_source_package_not_ready"; detail = [string]$packageSummary.status }) | Out-Null
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
    $installedStaticFrameValid = if ($null -eq $runtimeCapture) { $false } else { Get-BoolValue $runtimeCapture.installed_static_frame_dump_valid }
    if ($runtimeHarnessValid) {
        $runtimeHarnessValidCount++
        $runtimeValidEvidenceCount++
    }
    if ($installedRuntimeValid) {
        $installedRuntimeValidCount++
        if (-not $runtimeHarnessValid) {
            $runtimeValidEvidenceCount++
        }
    }
    elseif ($installedStaticFrameValid -and -not $runtimeHarnessValid) {
        $runtimeValidEvidenceCount++
    }
    if (-not $installedRuntimeValid) {
        $naturalCaptureRequiredCount++
    }

    if ($null -ne $callsite) {
        $directCallsiteReadyCount++
    }
    if ($null -ne $workorder) {
        $workorderReadyCount++
    }

    $dependencyStatus = if ($issues.Count -ne 0) {
        "pose_source_direct_callsite_dependency_has_issues"
    }
    elseif ($installedRuntimeValid) {
        "pose_source_direct_callsite_dependency_ready_with_natural_capture_pending_source_identity"
    }
    else {
        "pose_source_direct_callsite_dependency_ready_pending_source_identity_or_natural_capture"
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
        source_csab_name = [string]$target.source_csab_name
        output_csab_name = if ($null -eq $workorder) { "" } else { [string]$workorder.output_csab_name }
        callsite_review_class = if ($null -eq $callsite) { "" } else { [string]$callsite.callsite_review_class }
        callsite_context_status = if ($null -eq $callsite) { "" } else { [string]$callsite.callsite_context_status }
        sample_callsite_contexts = if ($null -eq $callsite) { "" } else { [string]$callsite.sample_callsite_contexts }
        direct_source_reference_count = if ($null -eq $callsite) { 0 } else { Get-IntValue $callsite.direct_source_reference_count }
        direct_player_actor_source_reference_count = if ($null -eq $callsite) { 0 } else { Get-IntValue $callsite.direct_player_actor_source_reference_count }
        reference_envelope_status = if ($null -eq $callsite) { "" } else { [string]$callsite.reference_envelope_status }
        next_evidence = if ($null -eq $callsite) { "" } else { [string]$callsite.next_evidence }
        capture_status = if ($null -eq $workorder) { "" } else { [string]$workorder.capture_status }
        capture_scope = if ($null -eq $workorder) { "" } else { [string]$workorder.capture_scope }
        runtime_trigger = if ($null -eq $workorder) { "" } else { [string]$workorder.runtime_trigger }
        forced_animation_only_status = if ($null -eq $workorder) { "" } else { [string]$workorder.forced_animation_only_status }
        offline_runtime_parity_status = if ($null -eq $runtimeParity) { "missing" } else { [string]$runtimeParity.status }
        runtime_config_status = if ($null -eq $runtimeConfig) { "missing" } else { [string]$runtimeConfig.status }
        runtime_harness_dump_valid = $runtimeHarnessValid
        installed_runtime_dump_valid = $installedRuntimeValid
        installed_static_frame_dump_valid = $installedStaticFrameValid
        dependency_decision_status = $dependencyStatus
        source_identity_acceptance_status = if ($installedRuntimeValid) { "ready_for_source_identity_review_after_natural_capture" } else { "blocked_pending_source_identity_or_natural_capture" }
        semantic_acceptance_status = if ($installedRuntimeValid) { "ready_for_semantic_review_after_natural_capture" } else { "blocked_pending_source_identity_or_natural_capture" }
        mapping_promotion_allowed = $false
        required_next_step = if ($null -eq $callsite) { "" } else { [string]$callsite.required_next_step }
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray())
$summaryPath = Join-Path $OutputRoot "link_child_pose_source_direct_callsite_dependency_policy_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_pose_source_direct_callsite_dependency_policy.csv"
$status = if ($issueCount -eq 0) { "pose_source_direct_callsite_dependency_ready" } else { "pose_source_direct_callsite_dependency_has_issues" }

$summary = [pscustomobject]@{
    format = "oot3d_link_child_pose_source_direct_callsite_dependency_policy_v1"
    status = $status
    policy = [ordered]@{
        scope = "Link child residual rows with direct N64 player-actor callsite context but outside-envelope pose/source risk"
        semantic_effect = "proves direct-callsite dependency readiness and installed natural-draw review evidence only; no source identities, semantic acceptances, or mapping promotions are accepted"
        acceptance_policy = "each row still requires accepted source identity/route policy or a non-harness installed runtime draw before promotion"
    }
    callsite_context_summary = (Resolve-Path -LiteralPath $callsiteSummaryPath).Path
    pose_source_runtime_workorder_summary = (Resolve-Path -LiteralPath $workorderSummaryPath).Path
    pose_source_package_summary = (Resolve-Path -LiteralPath $packageSummaryPath).Path
    pose_source_runtime_parity_summary = (Resolve-Path -LiteralPath $runtimeParitySummaryPath).Path
    pose_source_runtime_config_summary = (Resolve-Path -LiteralPath $runtimeConfigSummaryPath).Path
    semantic_frontier_closure_summary = (Resolve-Path -LiteralPath $closureSummaryPath).Path
    runtime_config_capture_csv = (Resolve-Path -LiteralPath $runtimeCaptureCsv).Path
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    record_count = $recordArray.Count
    closure_runtime_harness_dump_valid_count = Get-IntValue (Get-JsonValue $closureSummary "runtime_harness_dump_valid_count")
    closure_installed_runtime_dump_valid_count = Get-IntValue (Get-JsonValue $closureSummary "installed_runtime_dump_valid_count")
    dependency_ready_count = $dependencyReadyCount
    direct_callsite_ready_count = $directCallsiteReadyCount
    workorder_ready_count = $workorderReadyCount
    package_ready_count = $packageReadyCount
    offline_runtime_parity_valid_count = $offlineParityValidCount
    runtime_config_ready_count = $runtimeConfigReadyCount
    runtime_harness_dump_valid_count = $runtimeHarnessValidCount
    installed_runtime_dump_valid_count = $installedRuntimeValidCount
    installed_static_frame_dump_valid_count = @($recordArray | Where-Object { Get-BoolValue $_.installed_static_frame_dump_valid }).Count
    runtime_valid_evidence_count = $runtimeValidEvidenceCount
    natural_capture_required_count = $naturalCaptureRequiredCount
    outside_reference_envelope_count = $outsideEnvelopeCount
    direct_source_reference_count = $directSourceReferenceTotal
    direct_player_actor_source_reference_count = $directPlayerActorReferenceTotal
    runtime_context_trigger_count = $runtimeContextTriggerCount
    source_identity_accepted_count = 0
    semantic_accepted_count = 0
    mapping_promotion_allowed_count = 0
    issue_count = $issueCount
    dependency_decision_status_counts = $statusCounts
    callsite_review_class_counts = $callsiteClassCounts
    next_evidence_counts = $nextEvidenceCounts
    capture_status_counts = $captureStatusCounts
    capture_scope_counts = $captureScopeCounts
    forced_animation_only_status_counts = $forcedAnimationOnlyStatusCounts
    issue_reason_counts = $issueReasonCounts
    upstream_counts = [ordered]@{
        callsite_record_count = Get-IntValue $callsiteSummary.row_count
        workorder_record_count = Get-IntValue $workorderSummary.record_count
        package_exported_count = Get-IntValue $packageSummary.exported
        runtime_parity_exported_count = Get-IntValue $runtimeParitySummary.exported
        runtime_config_record_count = Get-IntValue $runtimeConfigSummary.record_count
    }
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,output_csab_name,callsite_review_class,callsite_context_status,sample_callsite_contexts,direct_source_reference_count,direct_player_actor_source_reference_count,reference_envelope_status,next_evidence,capture_status,capture_scope,runtime_trigger,forced_animation_only_status,offline_runtime_parity_status,runtime_config_status,runtime_harness_dump_valid,installed_runtime_dump_valid,installed_static_frame_dump_valid,dependency_decision_status,source_identity_acceptance_status,semantic_acceptance_status,mapping_promotion_allowed,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_pose_source_direct_callsite_dependency_policy_v1") "unexpected dependency summary format"
    Assert-Condition ($summary.status -eq "pose_source_direct_callsite_dependency_ready") "expected pose/source direct-callsite dependency policy to be ready"
    Assert-Condition ([int]$summary.record_count -eq 16) "expected 16 pose/source direct-callsite rows"
    Assert-Condition ([int]$summary.dependency_ready_count -eq 16) "expected all pose/source direct-callsite rows dependency-ready"
    Assert-Condition ([int]$summary.direct_callsite_ready_count -eq 16) "expected 16 direct callsite rows"
    Assert-Condition ([int]$summary.workorder_ready_count -eq 16) "expected 16 pose/source runtime workorders"
    Assert-Condition ([int]$summary.package_ready_count -eq 16) "expected package readiness for all 16 rows"
    Assert-Condition ([int]$summary.offline_runtime_parity_valid_count -eq 16) "expected offline runtime parity for all 16 rows"
    Assert-Condition ([int]$summary.runtime_config_ready_count -eq 16) "expected runtime config readiness for all 16 rows"
    Assert-Condition ([int]$summary.runtime_valid_evidence_count -eq 16) "expected runtime-valid evidence for all 16 direct-callsite rows"
    Assert-Condition ([int]$summary.runtime_harness_dump_valid_count -eq 15) "expected harness-valid dumps for the 15 uncaptured rows"
    Assert-Condition ([int]$summary.installed_runtime_dump_valid_count -eq 1) "expected one dynamic natural installed dump under direct-callsite policy"
    Assert-Condition ([int]$summary.installed_static_frame_dump_valid_count -eq 0) "expected no static-frame natural draw under direct-callsite policy"
    Assert-Condition ([int]$summary.natural_capture_required_count -eq 15) "expected 15 natural/context captures still required"
    Assert-Condition ([int]$summary.outside_reference_envelope_count -eq 16) "expected all 16 rows outside reference envelope"
    Assert-Condition ([int]$summary.direct_source_reference_count -eq 35) "expected 35 direct source references"
    Assert-Condition ([int]$summary.direct_player_actor_source_reference_count -eq 35) "expected 35 direct player-actor source references"
    Assert-Condition ([int]$summary.runtime_context_trigger_count -eq 16) "expected 16 runtime context triggers"
    Assert-Condition ([int]$summary.source_identity_accepted_count -eq 0) "expected no source identity acceptances"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected no semantic acceptances"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected no mapping promotions"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero dependency issues"
    Assert-Condition ((Get-JsonValue $summary.dependency_decision_status_counts "pose_source_direct_callsite_dependency_ready_pending_source_identity_or_natural_capture") -eq 15) "expected 15 direct-callsite dependencies pending source identity or natural capture"
    Assert-Condition ((Get-JsonValue $summary.dependency_decision_status_counts "pose_source_direct_callsite_dependency_ready_with_natural_capture_pending_source_identity") -eq 1) "expected one direct-callsite dependency with installed natural capture pending source identity"
    Assert-Condition ((Get-JsonValue $summary.callsite_review_class_counts "player_cutscene_action_table") -eq 7) "expected seven cutscene action-table rows"
    Assert-Condition ((Get-JsonValue $summary.callsite_review_class_counts "player_cutscene_wait_function_with_sfx") -eq 1) "expected one cutscene wait/SFX row"
    Assert-Condition ((Get-JsonValue $summary.callsite_review_class_counts "player_magic_spell_stage_table") -eq 1) "expected one magic spell stage-table row"
    Assert-Condition ((Get-JsonValue $summary.callsite_review_class_counts "player_model_anim_group_climb_hold_variant") -eq 2) "expected two climb-hold model-group rows"
    Assert-Condition ((Get-JsonValue $summary.callsite_review_class_counts "player_model_anim_group_slope_slip_variant") -eq 3) "expected three slope-slip model-group rows"
    Assert-Condition ((Get-JsonValue $summary.callsite_review_class_counts "player_swim_state_runtime_action") -eq 1) "expected one swim-state row"
    Assert-Condition ((Get-JsonValue $summary.callsite_review_class_counts "player_water_ledge_climb_runtime_action") -eq 1) "expected one water ledge-climb row"
    Assert-Condition ((Get-JsonValue $summary.next_evidence_counts "cutscene_actor_cue_runtime_capture") -eq 8) "expected eight cutscene actor-cue next-evidence rows"
    Assert-Condition ((Get-JsonValue $summary.next_evidence_counts "model_anim_type_and_equipment_variant_capture") -eq 5) "expected five model/equipment next-evidence rows"
    Assert-Condition ((Get-JsonValue $summary.next_evidence_counts "stateful_player_runtime_transform_capture") -eq 2) "expected two stateful runtime transform rows"
    Assert-Condition ((Get-JsonValue $summary.next_evidence_counts "magic_spell_sequence_runtime_capture") -eq 1) "expected one magic sequence row"
    Require-Path $csvPath "Pose/source direct-callsite dependency CSV"
}

Write-Host "OOT3D Link child pose/source direct-callsite dependency policy: $summaryPath"
Write-Host "status=$($summary.status) rows=$($summary.record_count) dependencyReady=$($summary.dependency_ready_count) directRefs=$($summary.direct_player_actor_source_reference_count) runtimeValid=$($summary.runtime_valid_evidence_count) harnessValid=$($summary.runtime_harness_dump_valid_count) installedValid=$($summary.installed_runtime_dump_valid_count) naturalRequired=$($summary.natural_capture_required_count) issues=$($summary.issue_count)"
