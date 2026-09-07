param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\installed_draw_proof_queue"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child installed-draw proof queue verification failed: $Message"
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

function Add-BySessionKey([hashtable]$Map, [object[]]$Rows) {
    foreach ($row in @($Rows)) {
        $Map[[string]$row.session_key] = $row
    }
}

function Get-ProofWave([string]$Bucket) {
    switch ($Bucket) {
        "exact_direct_n64_route_proven" { return "stage_1_direct_route_installed_draw" }
        "direct_n64_player_callsite_context" { return "stage_2_direct_callsite_source_identity_draw" }
        "family_or_sibling_n64_route_only" { return "stage_3_family_route_policy_draw" }
        "n64_table_identity_candidate_owner_context" { return "stage_4_anonymous_owner_identity_draw" }
        "promoted_owner_route_delegated_alias" { return "stage_5_alias_owner_policy_draw" }
        "route_absent_owner_policy_only" { return "stage_6_route_absent_owner_policy_draw" }
        "source_risk_no_route_proof" { return "stage_7_alternate_source_draw" }
        default { return "stage_99_unclassified" }
    }
}

function Get-TriggerProofRequirement([string]$Bucket) {
    switch ($Bucket) {
        "exact_direct_n64_route_proven" { return "drive the known N64 route context without runtime harness" }
        "direct_n64_player_callsite_context" { return "bind source identity, then drive direct player callsite without runtime harness" }
        "family_or_sibling_n64_route_only" { return "accept family-route policy or prove exact route, then drive route without runtime harness" }
        "n64_table_identity_candidate_owner_context" { return "accept anonymous owner identity, then drive owner context without runtime harness" }
        "promoted_owner_route_delegated_alias" { return "accept alias-owner policy, then drive alias context without runtime harness" }
        "route_absent_owner_policy_only" { return "prove owner/reuse policy and find a natural route before replacement" }
        "source_risk_no_route_proof" { return "resolve alternate source or installed runtime/skinned parity before replacement" }
        default { return "classify route/source proof before installed draw capture" }
    }
}

$characterRoot = Join-Path $WorkRoot "character_conversion"
$gateSummaryPath = Join-Path $characterRoot "in_game_replacement_gate\link_child_in_game_replacement_gate_summary.json"
$gateCsv = Join-Path $characterRoot "in_game_replacement_gate\link_child_in_game_replacement_gate.csv"
$campaignSummaryPath = Join-Path $characterRoot "runtime_capture_campaign\link_child_runtime_capture_campaign_summary.json"
$campaignCsv = Join-Path $characterRoot "runtime_capture_campaign\link_child_runtime_capture_campaign.csv"
$runbookSummaryPath = Join-Path $characterRoot "runtime_capture_runbook\link_child_runtime_capture_runbook_summary.json"
$runbookCsv = Join-Path $characterRoot "runtime_capture_runbook\link_child_runtime_capture_runbook.csv"
$runtimeCaptureMatrixSummaryPath = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix_summary.json"
$runtimeCaptureMatrixCsv = Join-Path $characterRoot "runtime_config_capture_matrix\link_child_runtime_config_capture_matrix.csv"
$replaySeedMatrixSummaryPath = Join-Path $characterRoot "replay_trigger_seed_matrix\link_child_replay_trigger_seed_matrix_summary.json"
$replaySeedMatrixCsv = Join-Path $characterRoot "replay_trigger_seed_matrix\link_child_replay_trigger_seed_matrix.csv"

Require-Path $gateSummaryPath "In-game replacement gate summary"
Require-Path $gateCsv "In-game replacement gate CSV"
Require-Path $campaignSummaryPath "Runtime capture campaign summary"
Require-Path $campaignCsv "Runtime capture campaign CSV"
Require-Path $runbookSummaryPath "Runtime capture runbook summary"
Require-Path $runbookCsv "Runtime capture runbook CSV"
Require-Path $runtimeCaptureMatrixSummaryPath "Runtime config capture matrix summary"
Require-Path $runtimeCaptureMatrixCsv "Runtime config capture matrix CSV"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$gateSummary = Read-Json $gateSummaryPath
$campaignSummary = Read-Json $campaignSummaryPath
$runbookSummary = Read-Json $runbookSummaryPath
$runtimeCaptureMatrixSummary = Read-Json $runtimeCaptureMatrixSummaryPath
$replaySeedMatrixAvailable = (Test-Path -LiteralPath $replaySeedMatrixSummaryPath) -and (Test-Path -LiteralPath $replaySeedMatrixCsv)
$replaySeedMatrixSummary = $null
if ($replaySeedMatrixAvailable) {
    $replaySeedMatrixSummary = Read-Json $replaySeedMatrixSummaryPath
}
$gateRows = @(Import-Csv -LiteralPath $gateCsv)
$campaignRows = @(Import-Csv -LiteralPath $campaignCsv)
$runbookRows = @(Import-Csv -LiteralPath $runbookCsv)
$runtimeCaptureRows = @(Import-Csv -LiteralPath $runtimeCaptureMatrixCsv)
$replaySeedRows = if ($replaySeedMatrixAvailable) { @(Import-Csv -LiteralPath $replaySeedMatrixCsv) } else { @() }

$campaignByName = @{}
$runbookBySession = @{}
$runtimeCaptureByName = @{}
$replaySeedBySession = @{}
Add-ByName $campaignByName $campaignRows
Add-BySessionKey $runbookBySession $runbookRows
Add-ByName $runtimeCaptureByName $runtimeCaptureRows
Add-BySessionKey $replaySeedBySession $replaySeedRows

$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$rowToCampaignLinkedCount = 0
$rowToRunbookLinkedCount = 0
$rowCaptureRunnerReadyCount = 0
$sessionCaptureRunnerReadyRowCount = 0
$preflightReadyCount = 0
$noLaunchPreflightReadyCount = 0
$nonHarnessCaptureCommandCount = 0
$harnessCaptureCommandCount = 0
$expectedDumpPathCount = 0
$expectedContextTracePathCount = 0
$installedDrawValidCount = 0
$installedStaticFrameDumpValidCount = 0
$harnessOnlyCount = 0
$replacementReadyCount = 0
$triggerRequiredCount = 0
$externalTriggerRequiredCount = 0
$automaticReplayReadyCount = 0
$firstWaveAutomaticReplayReadyCount = 0
$dynamicPlaybackTraceReadyCount = 0
$dynamicPlaybackTraceIncompleteCount = 0
$dynamicPlaybackTraceMissingCount = 0
$firstWaveDynamicPlaybackTraceReadyCount = 0
$firstWaveDynamicPlaybackTraceIncompleteCount = 0
$sessionKeySet = @{}
$firstWaveSessionSet = @{}
$proofWaveCounts = @{}
$campaignStageCounts = @{}
$sessionKindCounts = @{}
$blockingKindCounts = @{}
$routeBucketCounts = @{}
$proofStatusCounts = @{}
$automationStatusCounts = @{}
$issueReasonCounts = @{}

foreach ($gate in $gateRows) {
    $n64Name = [string]$gate.n64_name
    $campaign = $campaignByName[$n64Name]
    $runtimeCapture = $runtimeCaptureByName[$n64Name]
    $replaySeed = $null
    $issues = New-Object "System.Collections.Generic.List[object]"
    if ($null -eq $campaign) {
        $issues.Add([pscustomobject]@{ reason = "missing_runtime_campaign_row"; detail = $n64Name }) | Out-Null
    }
    if ($null -eq $runtimeCapture) {
        $issues.Add([pscustomobject]@{ reason = "missing_runtime_capture_matrix_row"; detail = $n64Name }) | Out-Null
    }

    $runbook = $null
    if ($null -ne $campaign) {
        $runbook = $runbookBySession[[string]$campaign.session_key]
        if ($null -eq $runbook) {
            $issues.Add([pscustomobject]@{ reason = "missing_runtime_runbook_session"; detail = [string]$campaign.session_key }) | Out-Null
        }
        if ($replaySeedMatrixAvailable) {
            $replaySeed = $replaySeedBySession[[string]$campaign.session_key]
            if ($null -eq $replaySeed) {
                $issues.Add([pscustomobject]@{ reason = "missing_replay_seed_matrix_row"; detail = [string]$campaign.session_key }) | Out-Null
            }
        }
    }

    $routeIssueCount = Get-IntValue $gate.issue_count
    if ($routeIssueCount -ne 0) {
        $issues.Add([pscustomobject]@{ reason = "replacement_gate_issue"; detail = [string]$gate.issue_count }) | Out-Null
    }

    $rowCaptureCommand = if ($null -eq $campaign) { "" } else { [string]$campaign.capture_command }
    $harnessCaptureCommand = if ($null -eq $campaign) { "" } else { [string]$campaign.harness_capture_command }
    $preflightCommand = if ($null -eq $campaign) { "" } else { [string]$campaign.preflight_command }
    $noLaunchRunnerCommand = if ($null -eq $campaign) { "" } else { [string]$campaign.no_launch_runner_command }
    $sessionCaptureCommand = if ($null -eq $runbook) { "" } else { [string]$runbook.capture_command }
    $sessionPreflightCommand = if ($null -eq $runbook) { "" } else { [string]$runbook.preflight_command }
    $expectedDumpPath = if ($null -eq $campaign) { "" } else { [string]$campaign.expected_dump_path }
    $expectedContextTracePath = if ($null -eq $campaign) { "" } else { [string]$campaign.expected_context_trace_path }

    if ([string]::IsNullOrWhiteSpace($rowCaptureCommand)) {
        $issues.Add([pscustomobject]@{ reason = "missing_non_harness_row_capture_command"; detail = $n64Name }) | Out-Null
    }
    elseif ($rowCaptureCommand -match "UseRuntimeHarness") {
        $issues.Add([pscustomobject]@{ reason = "row_capture_command_uses_harness"; detail = $rowCaptureCommand }) | Out-Null
    }
    if ([string]::IsNullOrWhiteSpace($harnessCaptureCommand)) {
        $issues.Add([pscustomobject]@{ reason = "missing_harness_capture_command"; detail = $n64Name }) | Out-Null
    }
    elseif ($harnessCaptureCommand -notmatch "UseRuntimeHarness") {
        $issues.Add([pscustomobject]@{ reason = "harness_capture_command_missing_harness_flag"; detail = $harnessCaptureCommand }) | Out-Null
    }
    if ([string]::IsNullOrWhiteSpace($preflightCommand)) {
        $issues.Add([pscustomobject]@{ reason = "missing_preflight_command"; detail = $n64Name }) | Out-Null
    }
    if ([string]::IsNullOrWhiteSpace($noLaunchRunnerCommand) -or $noLaunchRunnerCommand -notmatch "NoLaunch") {
        $issues.Add([pscustomobject]@{ reason = "missing_no_launch_preflight_command"; detail = $n64Name }) | Out-Null
    }
    if ([string]::IsNullOrWhiteSpace($sessionCaptureCommand) -and $null -ne $campaign) {
        $issues.Add([pscustomobject]@{ reason = "missing_session_capture_command"; detail = [string]$campaign.session_key }) | Out-Null
    }
    if ([string]::IsNullOrWhiteSpace($expectedDumpPath)) {
        $issues.Add([pscustomobject]@{ reason = "missing_expected_dump_path"; detail = $n64Name }) | Out-Null
    }
    if ([string]::IsNullOrWhiteSpace($expectedContextTracePath)) {
        $issues.Add([pscustomobject]@{ reason = "missing_expected_context_trace_path"; detail = $n64Name }) | Out-Null
    }

    $bucket = [string]$gate.route_proof_bucket
    $proofWave = Get-ProofWave $bucket
    $installedDrawValid = Get-BoolValue $gate.installed_runtime_dump_valid
    $installedStaticFrameValid = Get-BoolValue $gate.installed_static_frame_dump_valid
    $harnessOnly = ([string]$gate.installed_draw_gate_status -eq "harness_only_installed_draw_missing")
    $replacementReady = Get-BoolValue $gate.replacement_ready
    $runtimeCaptureStatus = if ($null -eq $runtimeCapture) { "" } else { [string]$runtimeCapture.capture_status }
    $dumpUsesDynamicFrame = if ($null -eq $runtimeCapture) { $false } else { Get-BoolValue $runtimeCapture.dump_uses_dynamic_frame }
    $contextTraceDynamicPlaybackReady = if ($null -eq $runtimeCapture) { $false } else { Get-BoolValue $runtimeCapture.context_trace_dynamic_playback_ready }
    $contextTraceTargetRowCount = if ($null -eq $runtimeCapture) { 0 } else { Get-IntValue $runtimeCapture.context_trace_target_row_count }
    $contextTraceTargetUniqueSelectedFrameCount = if ($null -eq $runtimeCapture) { 0 } else { Get-IntValue $runtimeCapture.context_trace_target_unique_selected_frame_count }
    $contextTraceTargetForcedStaticFrameRowCount = if ($null -eq $runtimeCapture) { 0 } else { Get-IntValue $runtimeCapture.context_trace_target_forced_static_frame_row_count }
    $replaySeedStatus = if ($null -eq $replaySeed) { "" } else { [string]$replaySeed.seed_status }
    $replaySessionSeedPresent = if ($null -eq $replaySeed) { $false } else { Get-BoolValue $replaySeed.session_replay_seed_present }
    $replayRunnerConsumableSeedPresent = if ($null -eq $replaySeed) { $false } else { Get-BoolValue $replaySeed.runner_consumable_replay_seed_present }
    $replayInputMacroPresent = if ($null -eq $replaySeed) { $false } else { Get-BoolValue $replaySeed.input_macro_present }
    $replaySeedAutomaticReplayReady = if ($null -eq $replaySeed) { $false } else { Get-BoolValue $replaySeed.automatic_replay_ready }
    $replaySeedExternalTriggerRequired = if ($null -eq $replaySeed) { $true } else { Get-BoolValue $replaySeed.external_trigger_required }
    $matchingRunnerConsumableReplaySeedPaths = if ($null -eq $replaySeed) { "" } else { [string]$replaySeed.matching_runner_consumable_replay_seed_paths }
    $matchingInputMacroPaths = if ($null -eq $replaySeed) { "" } else { [string]$replaySeed.matching_input_macro_paths }
    $dynamicPlaybackGate = if ($installedDrawValid -and $contextTraceDynamicPlaybackReady) {
        "dynamic_installed_draw_trace_ready"
    }
    elseif ($dumpUsesDynamicFrame) {
        "dynamic_frame_dump_trace_incomplete"
    }
    elseif ($installedStaticFrameValid) {
        "static_frame_dump_dynamic_playback_missing"
    }
    else {
        "dynamic_installed_draw_missing"
    }
    $rowRunnerReady = (
        [int]$issues.Count -eq 0 -and
        -not [string]::IsNullOrWhiteSpace($rowCaptureCommand) -and
        -not [string]::IsNullOrWhiteSpace($preflightCommand) -and
        -not [string]::IsNullOrWhiteSpace($expectedDumpPath) -and
        -not [string]::IsNullOrWhiteSpace($expectedContextTracePath)
    )
    $sessionRunnerReady = (
        [int]$issues.Count -eq 0 -and
        -not [string]::IsNullOrWhiteSpace($sessionCaptureCommand) -and
        -not [string]::IsNullOrWhiteSpace($sessionPreflightCommand)
    )
    $automaticReplayReady = (-not $installedDrawValid -and $rowRunnerReady -and $sessionRunnerReady -and $replaySeedAutomaticReplayReady)
    $externalTriggerRequired = (-not $installedDrawValid -and -not $automaticReplayReady)
    $proofStatus = if ([int]$issues.Count -ne 0) {
        "installed_draw_proof_queue_has_issues"
    }
    elseif ($replacementReady) {
        "installed_draw_proof_already_replacement_ready"
    }
    elseif ($installedDrawValid) {
        "installed_draw_valid_pending_policy_or_promotion"
    }
    elseif ($installedStaticFrameValid) {
        "static_frame_draw_valid_pending_dynamic_playback"
    }
    else {
        "blocked_pending_non_harness_installed_draw"
    }
    $automationStatus = if ([int]$issues.Count -ne 0) {
        "automation_has_issues"
    }
    elseif ($installedDrawValid) {
        "installed_draw_valid_no_capture_needed"
    }
    elseif ($automaticReplayReady) {
        "runner_ready_automatic_replay_ready"
    }
    elseif ($rowRunnerReady -and $sessionRunnerReady) {
        "runner_ready_external_trigger_required"
    }
    else {
        "runner_not_ready"
    }

    $record = [pscustomobject]@{
        n64_name = $n64Name
        source_csab_name = [string]$gate.source_csab_name
        route_proof_bucket = $bucket
        proof_wave = $proofWave
        route_proof_status = [string]$gate.route_proof_status
        next_replacement_blocker = [string]$gate.next_replacement_blocker
        proof_status = $proofStatus
        automation_status = $automationStatus
        trigger_proof_requirement = Get-TriggerProofRequirement $bucket
        campaign_priority = if ($null -eq $campaign) { 0 } else { Get-IntValue $campaign.priority }
        campaign_stage = if ($null -eq $campaign) { "" } else { [string]$campaign.campaign_stage }
        session_kind = if ($null -eq $campaign) { "" } else { [string]$campaign.session_kind }
        blocking_kind = if ($null -eq $campaign) { "" } else { [string]$campaign.blocking_kind }
        session_key = if ($null -eq $campaign) { "" } else { [string]$campaign.session_key }
        session_seed = if ($null -eq $runbook) { "" } else { [string]$runbook.session_seed }
        session_row_count = if ($null -eq $runbook) { 0 } else { Get-IntValue $runbook.row_count }
        runtime_trigger = if ($null -eq $campaign) { [string]$gate.runtime_trigger } else { [string]$campaign.runtime_trigger }
        capture_scope = if ($null -eq $campaign) { "" } else { [string]$campaign.capture_scope }
        acceptance_gate = if ($null -eq $campaign) { "" } else { [string]$campaign.acceptance_gate }
        installed_draw_gate_status = [string]$gate.installed_draw_gate_status
        installed_runtime_dump_valid = $installedDrawValid
        installed_static_frame_dump_valid = $installedStaticFrameValid
        runtime_capture_status = $runtimeCaptureStatus
        dump_uses_dynamic_frame = $dumpUsesDynamicFrame
        dynamic_playback_gate = $dynamicPlaybackGate
        context_trace_dynamic_playback_ready = $contextTraceDynamicPlaybackReady
        context_trace_target_row_count = $contextTraceTargetRowCount
        context_trace_target_unique_selected_frame_count = $contextTraceTargetUniqueSelectedFrameCount
        context_trace_target_forced_static_frame_row_count = $contextTraceTargetForcedStaticFrameRowCount
        replay_seed_status = $replaySeedStatus
        replay_session_seed_present = $replaySessionSeedPresent
        replay_runner_consumable_seed_present = $replayRunnerConsumableSeedPresent
        replay_input_macro_present = $replayInputMacroPresent
        replay_seed_automatic_replay_ready = $replaySeedAutomaticReplayReady
        replay_seed_external_trigger_required = $replaySeedExternalTriggerRequired
        matching_runner_consumable_replay_seed_paths = $matchingRunnerConsumableReplaySeedPaths
        matching_input_macro_paths = $matchingInputMacroPaths
        runtime_harness_dump_valid = Get-BoolValue $gate.runtime_harness_dump_valid
        harness_only = $harnessOnly
        semantic_accepted = Get-BoolValue $gate.semantic_accepted
        mapping_promotion_allowed = Get-BoolValue $gate.mapping_promotion_allowed
        replacement_ready = $replacementReady
        expected_dump_path = $expectedDumpPath
        expected_context_trace_path = $expectedContextTracePath
        row_preflight_command = $preflightCommand
        row_no_launch_runner_command = $noLaunchRunnerCommand
        row_capture_command = $rowCaptureCommand
        session_preflight_command = $sessionPreflightCommand
        session_capture_command = $sessionCaptureCommand
        harness_capture_command = $harnessCaptureCommand
        row_capture_runner_ready = $rowRunnerReady
        session_capture_runner_ready = $sessionRunnerReady
        external_trigger_required = $externalTriggerRequired
        automatic_replay_ready = $automaticReplayReady
        required_next_step = [string]$gate.required_next_step
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }

    if ($null -ne $campaign) {
        $rowToCampaignLinkedCount++
        $sessionKeySet[[string]$campaign.session_key] = $true
        if ($proofWave -eq "stage_1_direct_route_installed_draw") {
            $firstWaveSessionSet[[string]$campaign.session_key] = $true
        }
    }
    if ($null -ne $runbook) {
        $rowToRunbookLinkedCount++
    }
    if ([bool]$record.row_capture_runner_ready) {
        $rowCaptureRunnerReadyCount++
    }
    if ([bool]$record.session_capture_runner_ready) {
        $sessionCaptureRunnerReadyRowCount++
    }
    if (-not [string]::IsNullOrWhiteSpace($preflightCommand)) {
        $preflightReadyCount++
    }
    if (-not [string]::IsNullOrWhiteSpace($noLaunchRunnerCommand) -and $noLaunchRunnerCommand -match "NoLaunch") {
        $noLaunchPreflightReadyCount++
    }
    if (-not [string]::IsNullOrWhiteSpace($rowCaptureCommand) -and $rowCaptureCommand -notmatch "UseRuntimeHarness") {
        $nonHarnessCaptureCommandCount++
    }
    if (-not [string]::IsNullOrWhiteSpace($harnessCaptureCommand) -and $harnessCaptureCommand -match "UseRuntimeHarness") {
        $harnessCaptureCommandCount++
    }
    if (-not [string]::IsNullOrWhiteSpace($expectedDumpPath)) {
        $expectedDumpPathCount++
    }
    if (-not [string]::IsNullOrWhiteSpace($expectedContextTracePath)) {
        $expectedContextTracePathCount++
    }
    if ($installedDrawValid) {
        $installedDrawValidCount++
    }
    if ($installedStaticFrameValid) {
        $installedStaticFrameDumpValidCount++
    }
    if ($harnessOnly) {
        $harnessOnlyCount++
    }
    if ($replacementReady) {
        $replacementReadyCount++
    }
    if ([bool]$record.external_trigger_required) {
        $triggerRequiredCount++
        $externalTriggerRequiredCount++
    }
    if ([bool]$record.automatic_replay_ready) {
        $automaticReplayReadyCount++
        if ($proofWave -eq "stage_1_direct_route_installed_draw") {
            $firstWaveAutomaticReplayReadyCount++
        }
    }
    if ([bool]$record.context_trace_dynamic_playback_ready) {
        $dynamicPlaybackTraceReadyCount++
        if ($proofWave -eq "stage_1_direct_route_installed_draw") {
            $firstWaveDynamicPlaybackTraceReadyCount++
        }
    }
    elseif ([bool]$record.dump_uses_dynamic_frame) {
        $dynamicPlaybackTraceIncompleteCount++
        if ($proofWave -eq "stage_1_direct_route_installed_draw") {
            $firstWaveDynamicPlaybackTraceIncompleteCount++
        }
    }
    else {
        $dynamicPlaybackTraceMissingCount++
    }
    $issueCount += [int]$record.issue_count
    Add-Count $proofWaveCounts $proofWave
    Add-Count $campaignStageCounts ([string]$record.campaign_stage)
    Add-Count $sessionKindCounts ([string]$record.session_kind)
    Add-Count $blockingKindCounts ([string]$record.blocking_kind)
    Add-Count $routeBucketCounts $bucket
    Add-Count $proofStatusCounts $proofStatus
    Add-Count $automationStatusCounts $automationStatus
    foreach ($issue in @($record.issues)) {
        Add-Count $issueReasonCounts ([string]$issue.reason)
    }
    $records.Add($record) | Out-Null
}

$recordArray = @($records.ToArray() | Sort-Object campaign_priority,proof_wave,n64_name)
$summaryPath = Join-Path $OutputRoot "link_child_installed_draw_proof_queue_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_installed_draw_proof_queue.csv"
$status = if ($issueCount -eq 0) { "installed_draw_proof_queue_ready" } else { "installed_draw_proof_queue_has_issues" }

$summary = [pscustomobject]@{
    format = "oot3d_link_child_installed_draw_proof_queue_v1"
    status = $status
    policy = [ordered]@{
        scope = "Non-harness installed runtime draw proof queue for all Link child residual rows"
        semantic_effect = "does not accept semantics or promote mappings; only binds each blocked row to the next non-harness proof command and trigger requirement"
        replacement_gate = "a row remains blocked until the non-harness installed draw is valid and the semantic/policy gates are accepted"
    }
    in_game_replacement_gate_summary = (Resolve-Path -LiteralPath $gateSummaryPath).Path
    runtime_capture_campaign_summary = (Resolve-Path -LiteralPath $campaignSummaryPath).Path
    runtime_capture_runbook_summary = (Resolve-Path -LiteralPath $runbookSummaryPath).Path
    runtime_config_capture_matrix_summary = (Resolve-Path -LiteralPath $runtimeCaptureMatrixSummaryPath).Path
    replay_trigger_seed_matrix_summary = if ($replaySeedMatrixAvailable) { (Resolve-Path -LiteralPath $replaySeedMatrixSummaryPath).Path } else { "" }
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    replay_seed_matrix_available = $replaySeedMatrixAvailable
    record_count = $recordArray.Count
    replacement_gate_row_count = Get-IntValue (Get-JsonValue $gateSummary "record_count")
    campaign_row_count = Get-IntValue (Get-JsonValue $campaignSummary "record_count")
    campaign_session_count = Get-IntValue (Get-JsonValue $campaignSummary "session_count")
    runbook_row_count = Get-IntValue (Get-JsonValue $runbookSummary "record_count")
    runbook_session_count = Get-IntValue (Get-JsonValue $runbookSummary "session_count")
    linked_session_count = $sessionKeySet.Count
    first_wave_direct_route_session_count = $firstWaveSessionSet.Count
    row_to_campaign_linked_count = $rowToCampaignLinkedCount
    row_to_runbook_linked_count = $rowToRunbookLinkedCount
    row_capture_runner_ready_count = $rowCaptureRunnerReadyCount
    session_capture_runner_ready_row_count = $sessionCaptureRunnerReadyRowCount
    preflight_ready_count = $preflightReadyCount
    no_launch_preflight_ready_count = $noLaunchPreflightReadyCount
    non_harness_capture_command_count = $nonHarnessCaptureCommandCount
    harness_capture_command_count = $harnessCaptureCommandCount
    expected_dump_path_count = $expectedDumpPathCount
    expected_context_trace_path_count = $expectedContextTracePathCount
    installed_draw_valid_count = $installedDrawValidCount
    installed_static_frame_dump_valid_count = $installedStaticFrameDumpValidCount
    harness_only_count = $harnessOnlyCount
    replacement_ready_count = $replacementReadyCount
    trigger_required_count = $triggerRequiredCount
    external_trigger_required_count = $externalTriggerRequiredCount
    automatic_replay_ready_count = $automaticReplayReadyCount
    replay_seed_matrix_automatic_replay_ready_session_count = if ($null -eq $replaySeedMatrixSummary) { 0 } else { Get-IntValue (Get-JsonValue $replaySeedMatrixSummary "automatic_replay_ready_count") }
    replay_seed_matrix_external_trigger_required_session_count = if ($null -eq $replaySeedMatrixSummary) { 0 } else { Get-IntValue (Get-JsonValue $replaySeedMatrixSummary "external_trigger_required_count") }
    first_wave_automatic_replay_ready_count = $firstWaveAutomaticReplayReadyCount
    runtime_config_dynamic_playback_trace_ready_count = Get-IntValue (Get-JsonValue $runtimeCaptureMatrixSummary "dynamic_playback_trace_ready_count")
    runtime_config_dynamic_playback_trace_incomplete_count = Get-IntValue (Get-JsonValue $runtimeCaptureMatrixSummary "dynamic_playback_trace_incomplete_count")
    dynamic_playback_trace_ready_count = $dynamicPlaybackTraceReadyCount
    dynamic_playback_trace_incomplete_count = $dynamicPlaybackTraceIncompleteCount
    dynamic_playback_trace_missing_count = $dynamicPlaybackTraceMissingCount
    first_wave_dynamic_playback_trace_ready_count = $firstWaveDynamicPlaybackTraceReadyCount
    first_wave_dynamic_playback_trace_incomplete_count = $firstWaveDynamicPlaybackTraceIncompleteCount
    first_wave_direct_route_row_count = Get-JsonValue $proofWaveCounts "stage_1_direct_route_installed_draw"
    direct_callsite_source_identity_row_count = Get-JsonValue $proofWaveCounts "stage_2_direct_callsite_source_identity_draw"
    family_route_policy_row_count = Get-JsonValue $proofWaveCounts "stage_3_family_route_policy_draw"
    anonymous_owner_identity_row_count = Get-JsonValue $proofWaveCounts "stage_4_anonymous_owner_identity_draw"
    alias_owner_policy_row_count = Get-JsonValue $proofWaveCounts "stage_5_alias_owner_policy_draw"
    route_absent_owner_policy_row_count = Get-JsonValue $proofWaveCounts "stage_6_route_absent_owner_policy_draw"
    alternate_source_row_count = Get-JsonValue $proofWaveCounts "stage_7_alternate_source_draw"
    issue_count = $issueCount
    proof_wave_counts = $proofWaveCounts
    campaign_stage_counts = $campaignStageCounts
    session_kind_counts = $sessionKindCounts
    blocking_kind_counts = $blockingKindCounts
    route_proof_bucket_counts = $routeBucketCounts
    proof_status_counts = $proofStatusCounts
    automation_status_counts = $automationStatusCounts
    issue_reason_counts = $issueReasonCounts
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,route_proof_bucket,proof_wave,route_proof_status,next_replacement_blocker,proof_status,automation_status,trigger_proof_requirement,campaign_priority,campaign_stage,session_kind,blocking_kind,session_key,session_seed,session_row_count,runtime_trigger,capture_scope,acceptance_gate,installed_draw_gate_status,installed_runtime_dump_valid,installed_static_frame_dump_valid,runtime_capture_status,dump_uses_dynamic_frame,dynamic_playback_gate,context_trace_dynamic_playback_ready,context_trace_target_row_count,context_trace_target_unique_selected_frame_count,context_trace_target_forced_static_frame_row_count,replay_seed_status,replay_session_seed_present,replay_runner_consumable_seed_present,replay_input_macro_present,replay_seed_automatic_replay_ready,replay_seed_external_trigger_required,matching_runner_consumable_replay_seed_paths,matching_input_macro_paths,runtime_harness_dump_valid,harness_only,semantic_accepted,mapping_promotion_allowed,replacement_ready,expected_dump_path,expected_context_trace_path,row_preflight_command,row_no_launch_runner_command,row_capture_command,session_preflight_command,session_capture_command,harness_capture_command,row_capture_runner_ready,session_capture_runner_ready,external_trigger_required,automatic_replay_ready,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_installed_draw_proof_queue_v1") "unexpected installed-draw proof queue format"
    Assert-Condition ($summary.status -eq "installed_draw_proof_queue_ready") "expected installed-draw proof queue ready"
    Assert-Condition ([int]$summary.record_count -eq 63) "expected 63 installed-draw proof rows"
    Assert-Condition ([int]$summary.replacement_gate_row_count -eq 63) "expected 63 replacement gate rows"
    Assert-Condition ([int]$summary.campaign_row_count -eq 63) "expected 63 campaign rows"
    Assert-Condition ([int]$summary.campaign_session_count -eq 55) "expected 55 campaign sessions"
    Assert-Condition ([int]$summary.runbook_row_count -eq 63) "expected 63 runbook rows"
    Assert-Condition ([int]$summary.runbook_session_count -eq 55) "expected 55 runbook sessions"
    Assert-Condition ([bool]$summary.replay_seed_matrix_available) "expected replay seed matrix to be available"
    Assert-Condition ([int]$summary.linked_session_count -eq 55) "expected 55 linked sessions"
    Assert-Condition ([int]$summary.first_wave_direct_route_session_count -eq 4) "expected four first-wave direct-route sessions"
    Assert-Condition ([int]$summary.row_to_campaign_linked_count -eq 63) "expected all rows linked to campaign"
    Assert-Condition ([int]$summary.row_to_runbook_linked_count -eq 63) "expected all rows linked to runbook"
    Assert-Condition ([int]$summary.row_capture_runner_ready_count -eq 63) "expected all rows to have row capture runners"
    Assert-Condition ([int]$summary.session_capture_runner_ready_row_count -eq 63) "expected all rows to have session capture runners"
    Assert-Condition ([int]$summary.preflight_ready_count -eq 63) "expected all rows to have preflight commands"
    Assert-Condition ([int]$summary.no_launch_preflight_ready_count -eq 63) "expected all rows to have no-launch preflight commands"
    Assert-Condition ([int]$summary.non_harness_capture_command_count -eq 63) "expected all rows to have non-harness capture commands"
    Assert-Condition ([int]$summary.harness_capture_command_count -eq 63) "expected all rows to have harness capture commands"
    Assert-Condition ([int]$summary.expected_dump_path_count -eq 63) "expected all rows to have expected dump paths"
    Assert-Condition ([int]$summary.expected_context_trace_path_count -eq 63) "expected all rows to have expected context trace paths"
    Assert-Condition ([int]$summary.installed_draw_valid_count -eq 1) "expected one dynamic installed draw proof"
    Assert-Condition ([int]$summary.installed_static_frame_dump_valid_count -eq 0) "expected no static-frame installed draw proofs"
    Assert-Condition ([int]$summary.harness_only_count -eq 62) "expected 62 harness-only rows"
    Assert-Condition ([int]$summary.replacement_ready_count -eq 0) "expected zero replacement-ready rows"
    Assert-Condition ([int]$summary.trigger_required_count -eq 61) "expected 61 rows to require an external trigger"
    Assert-Condition ([int]$summary.external_trigger_required_count -eq 61) "expected 61 rows to require external trigger"
    Assert-Condition ([int]$summary.automatic_replay_ready_count -eq 1) "expected one automatic replay-ready row"
    Assert-Condition ([int]$summary.replay_seed_matrix_automatic_replay_ready_session_count -eq 1) "expected one replay matrix automatic replay-ready session"
    Assert-Condition ([int]$summary.replay_seed_matrix_external_trigger_required_session_count -eq 54) "expected 54 replay matrix external-trigger sessions"
    Assert-Condition ([int]$summary.first_wave_automatic_replay_ready_count -eq 1) "expected one first-wave automatic replay-ready row"
    Assert-Condition ([int]$summary.runtime_config_dynamic_playback_trace_ready_count -eq 1) "expected one runtime matrix dynamic playback trace proof"
    Assert-Condition ([int]$summary.runtime_config_dynamic_playback_trace_incomplete_count -eq 0) "expected zero runtime matrix incomplete dynamic playback traces"
    Assert-Condition ([int]$summary.dynamic_playback_trace_ready_count -eq 1) "expected one installed-draw dynamic playback trace proof"
    Assert-Condition ([int]$summary.dynamic_playback_trace_incomplete_count -eq 0) "expected zero installed-draw incomplete dynamic playback traces"
    Assert-Condition ([int]$summary.dynamic_playback_trace_missing_count -eq 62) "expected 62 installed-draw rows to miss dynamic playback traces"
    Assert-Condition ([int]$summary.first_wave_dynamic_playback_trace_ready_count -eq 0) "expected zero first-wave dynamic playback trace proofs"
    Assert-Condition ([int]$summary.first_wave_dynamic_playback_trace_incomplete_count -eq 0) "expected zero first-wave incomplete dynamic playback traces"
    Assert-Condition ([int]$summary.first_wave_direct_route_row_count -eq 5) "expected five first-wave direct-route rows"
    Assert-Condition ([int]$summary.direct_callsite_source_identity_row_count -eq 16) "expected sixteen direct-callsite rows"
    Assert-Condition ([int]$summary.family_route_policy_row_count -eq 8) "expected eight family-route rows"
    Assert-Condition ([int]$summary.anonymous_owner_identity_row_count -eq 11) "expected eleven anonymous-owner rows"
    Assert-Condition ([int]$summary.alias_owner_policy_row_count -eq 7) "expected seven alias-owner rows"
    Assert-Condition ([int]$summary.route_absent_owner_policy_row_count -eq 14) "expected fourteen route-absent owner-policy rows"
    Assert-Condition ([int]$summary.alternate_source_row_count -eq 2) "expected two alternate-source rows"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero installed-draw proof queue issues"
    Assert-Condition ((Get-JsonValue $summary.proof_status_counts "blocked_pending_non_harness_installed_draw") -eq 62) "expected 62 rows blocked pending non-harness installed draw"
    Assert-Condition ((Get-JsonValue $summary.proof_status_counts "installed_draw_valid_pending_policy_or_promotion") -eq 1) "expected one installed draw proof pending policy or promotion"
    Assert-Condition ((Get-IntValue (Get-JsonValue $summary.proof_status_counts "static_frame_draw_valid_pending_dynamic_playback")) -eq 0) "expected no static-frame draw proof pending dynamic playback"
    Assert-Condition ((Get-JsonValue $summary.automation_status_counts "runner_ready_external_trigger_required") -eq 61) "expected 61 rows runner-ready but trigger-required"
    Assert-Condition ((Get-JsonValue $summary.automation_status_counts "runner_ready_automatic_replay_ready") -eq 1) "expected one row runner-ready for automatic replay"
    Assert-Condition ((Get-JsonValue $summary.automation_status_counts "installed_draw_valid_no_capture_needed") -eq 1) "expected one installed-draw row to need no recapture"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "exact_direct_n64_route_proven") -eq 5) "expected five exact direct route rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "direct_n64_player_callsite_context") -eq 16) "expected sixteen direct callsite rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "family_or_sibling_n64_route_only") -eq 8) "expected eight family/sibling route rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "n64_table_identity_candidate_owner_context") -eq 11) "expected eleven anonymous owner rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "promoted_owner_route_delegated_alias") -eq 7) "expected seven promoted alias rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "route_absent_owner_policy_only") -eq 14) "expected fourteen route-absent rows"
    Assert-Condition ((Get-JsonValue $summary.route_proof_bucket_counts "source_risk_no_route_proof") -eq 2) "expected two source-risk rows"
    Require-Path $csvPath "Installed-draw proof queue CSV"
}

Write-Host "OOT3D Link child installed-draw proof queue: $summaryPath"
Write-Host "status=$($summary.status) rows=$($summary.record_count) sessions=$($summary.linked_session_count) rowRunners=$($summary.row_capture_runner_ready_count) installedDraw=$($summary.installed_draw_valid_count) triggerRequired=$($summary.trigger_required_count) issues=$($summary.issue_count)"
