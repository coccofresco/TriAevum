param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$CallsiteContext = "",
    [string]$RuntimeParitySummary = "",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($CallsiteContext)) {
    $CallsiteContext = Join-Path $WorkRoot "character_conversion\link_child_animation_pose_source_callsite_context.json"
}
if ([string]::IsNullOrWhiteSpace($RuntimeParitySummary)) {
    $RuntimeParitySummary = Join-Path $WorkRoot "character_conversion\pose_source_risk_diagnostic_runtime_parity\link_child_pose_source_risk_diagnostic_runtime_parity_summary.json"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\pose_source_runtime_capture_matrix"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child pose/source runtime capture matrix verification failed: $Message"
    }
}

function Get-JsonValue([object]$Object, [string]$Name) {
    if ($null -eq $Object) {
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

function Safe-PathPart([string]$Value) {
    $safe = $Value -replace "[^A-Za-z0-9_.-]", "_"
    if ([string]::IsNullOrWhiteSpace($safe)) {
        return "pose_source"
    }
    return $safe
}

function Select-RegexValue([string]$Value, [string]$Pattern) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return ""
    }
    if ($Value -match $Pattern) {
        return $Matches[1]
    }
    return ""
}

function Get-CapturePlan([string]$Class, [string]$SampleContexts, [string]$N64Name) {
    $cue = Select-RegexValue $SampleContexts "(PLAYER_CSACTION_\d+)"
    $group = Select-RegexValue $SampleContexts "(PLAYER_ANIMGROUP_[A-Za-z0-9_]+)"
    if ($Class -like "player_cutscene*") {
        return [pscustomobject]@{
            capture_status = "needs_cutscene_actor_cue_runtime_capture"
            capture_scope = "player_cutscene_actor_cue"
            runtime_trigger = if ($cue) { $cue } else { "PLAYER_CSACTION callback for $N64Name" }
            forced_animation_only_status = "insufficient_without_actor_cue_context"
            capture_runner_requirement = "cutscene actor cue selector plus action/wait pair draw dump"
        }
    }
    if ($Class -like "player_model_anim_group*") {
        return [pscustomobject]@{
            capture_status = "needs_model_anim_type_equipment_capture"
            capture_scope = "player_model_anim_group_variant"
            runtime_trigger = if ($group) { $group } else { "D_80853914 modelAnimType table entry" }
            forced_animation_only_status = "insufficient_without_modelAnimType_and_equipment_state"
            capture_runner_requirement = "modelAnimType/equipment selector plus player actor table draw dump"
        }
    }
    if ($Class -eq "player_magic_spell_stage_table") {
        return [pscustomobject]@{
            capture_status = "needs_magic_spell_sequence_capture"
            capture_scope = "player_magic_spell_sequence"
            runtime_trigger = "Player_Action_808507F4 D_80854A70 stage for $N64Name"
            forced_animation_only_status = "insufficient_without_magic_action_timing"
            capture_runner_requirement = "magic spell actionVar sequence and timed stage draw dump"
        }
    }
    if ($Class -eq "player_water_ledge_climb_runtime_action") {
        return [pscustomobject]@{
            capture_status = "needs_water_ledge_climb_state_capture"
            capture_scope = "player_water_ledge_climb_transform"
            runtime_trigger = "Player_ActionHandler_12 water ledge climb into Player_Action_80845668"
            forced_animation_only_status = "insufficient_without_world_position_and_shape_yOffset_adjustments"
            capture_runner_requirement = "water ledge-climb setup with wall normal, yDistToLedge, and shape offset"
        }
    }
    if ($Class -eq "player_swim_state_runtime_action") {
        return [pscustomobject]@{
            capture_status = "needs_swim_state_runtime_capture"
            capture_scope = "player_swim_state_transform"
            runtime_trigger = "func_80838F18 / Player_Action_8084D610 swim wait state"
            forced_animation_only_status = "insufficient_without_swim_velocity_morph_and_upper_body_state"
            capture_runner_requirement = "swim state draw dump after water, velocity, morph, and upper-body updates"
        }
    }
    return [pscustomobject]@{
        capture_status = "needs_manual_runtime_capture_design"
        capture_scope = "manual"
        runtime_trigger = "manual callsite review for $N64Name"
        forced_animation_only_status = "insufficient_without_context"
        capture_runner_requirement = "manual capture runner definition"
    }
}

Require-Path $CallsiteContext "Pose/source callsite context audit"
Require-Path $RuntimeParitySummary "Pose/source diagnostic runtime parity summary"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$callsite = Get-Content -LiteralPath $CallsiteContext -Raw | ConvertFrom-Json
$runtimeParity = Get-Content -LiteralPath $RuntimeParitySummary -Raw | ConvertFrom-Json

$parityByName = @{}
foreach ($record in @($runtimeParity.records | Where-Object { [string](Get-JsonValue $_ "status") -eq "valid" })) {
    $parityByName[[string](Get-JsonValue $record "n64_name")] = $record
}

$records = New-Object "System.Collections.Generic.List[object]"
$classCounts = @{}
$nextEvidenceCounts = @{}
$captureStatusCounts = @{}
$captureScopeCounts = @{}
$issueCount = 0

foreach ($row in @($callsite.rows)) {
    $n64Name = [string](Get-JsonValue $row "n64_name")
    $class = [string](Get-JsonValue $row "callsite_review_class")
    $nextEvidence = [string](Get-JsonValue $row "next_evidence")
    $sampleContexts = [string](Get-JsonValue $row "sample_callsite_contexts")
    $plan = Get-CapturePlan $class $sampleContexts $n64Name
    $parity = $parityByName[$n64Name]
    $issues = New-Object "System.Collections.Generic.List[object]"
    if ($null -eq $parity) {
        $issues.Add([pscustomobject]@{ reason = "missing_runtime_parity_record"; detail = $n64Name }) | Out-Null
    }
    if ([string]::IsNullOrWhiteSpace($class) -or $class -eq "direct_player_actor_callsite_unclassified") {
        $issues.Add([pscustomobject]@{ reason = "unclassified_callsite"; detail = $class }) | Out-Null
    }
    $issueCount += $issues.Count
    Add-Count $classCounts $class
    Add-Count $nextEvidenceCounts $nextEvidence
    Add-Count $captureStatusCounts ([string]$plan.capture_status)
    Add-Count $captureScopeCounts ([string]$plan.capture_scope)

    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        n64_data_name = [string](Get-JsonValue $row "n64_data_name")
        source_csab_name = [string](Get-JsonValue $row "source_csab_name")
        output_csab_name = if ($null -eq $parity) { "" } else { [string](Get-JsonValue $parity "output_csab_name") }
        track_export = if ($null -eq $parity) { "" } else { [string](Get-JsonValue $parity "track_export") }
        runtime_glb = if ($null -eq $parity) { "" } else { [string](Get-JsonValue $parity "runtime_glb") }
        runtime_frame_slot_count = if ($null -eq $parity) { $null } else { Get-JsonValue $parity "runtime_frame_slot_count" }
        callsite_review_class = $class
        next_evidence = $nextEvidence
        sample_callsite_contexts = $sampleContexts
        capture_status = [string]$plan.capture_status
        capture_scope = [string]$plan.capture_scope
        runtime_trigger = [string]$plan.runtime_trigger
        forced_animation_only_status = [string]$plan.forced_animation_only_status
        capture_runner_requirement = [string]$plan.capture_runner_requirement
        expected_dump_path = "debug/oot3d_link_child_pose_source_runtime_" + (Safe-PathPart $n64Name) + ".json"
        runtime_acceptance_status = "pending_installed_context_runtime_capture"
        semantic_effect = "workorder only; no source acceptance or mapping promotion"
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray())
$summaryOutput = Join-Path $OutputRoot "link_child_pose_source_runtime_capture_matrix_summary.json"
$csvOutput = Join-Path $OutputRoot "link_child_pose_source_runtime_capture_matrix.csv"
$status = if ($issueCount -eq 0 -and $recordArray.Count -gt 0) {
    "pose_source_runtime_capture_workorders_ready"
}
elseif ($recordArray.Count -eq 0) {
    "complete_no_pose_source_rows"
}
else {
    "pose_source_runtime_capture_workorders_have_issues"
}

$summary = [pscustomobject]@{
    format = "oot3d_link_child_pose_source_runtime_capture_matrix_v1"
    status = $status
    policy = [ordered]@{
        scope = "installed runtime context capture workorders for the 16 outside-envelope Link child pose/source rows"
        semantic_effect = "capture planning only; rows remain blocked until installed runtime draw evidence is collected and accepted"
        forced_animation_policy = "offline forced animation parity is insufficient for these rows without the N64 player actor context"
    }
    callsite_context = (Resolve-Path -LiteralPath $CallsiteContext).Path
    runtime_parity_summary = (Resolve-Path -LiteralPath $RuntimeParitySummary).Path
    output_root = $OutputRoot
    record_count = $recordArray.Count
    runtime_parity_record_count = $parityByName.Count
    issue_count = $issueCount
    class_counts = $classCounts
    next_evidence_counts = $nextEvidenceCounts
    capture_status_counts = $captureStatusCounts
    capture_scope_counts = $captureScopeCounts
    context_runner_implemented_count = 0
    installed_runtime_dump_valid_count = 0
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryOutput -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,output_csab_name,callsite_review_class,next_evidence,capture_status,capture_scope,runtime_trigger,forced_animation_only_status,capture_runner_requirement,expected_dump_path,issue_count |
    Export-Csv -LiteralPath $csvOutput -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_pose_source_runtime_capture_matrix_v1") "unexpected matrix format"
    Assert-Condition ($summary.status -eq "pose_source_runtime_capture_workorders_ready") "expected ready pose/source runtime capture workorders"
    Assert-Condition ([int]$summary.record_count -eq 16) "expected 16 pose/source runtime capture workorders"
    Assert-Condition ([int]$summary.runtime_parity_record_count -eq 16) "expected 16 valid runtime parity records"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero matrix issues"
    Assert-Condition ([int]$summary.context_runner_implemented_count -eq 0) "expected context runner to remain unimplemented"
    Assert-Condition ([int]$summary.installed_runtime_dump_valid_count -eq 0) "expected no installed runtime dumps to be accepted by this matrix"
    Assert-Condition ([int]$summary.class_counts.player_cutscene_action_table -eq 7) "expected seven cutscene action-table workorders"
    Assert-Condition ([int]$summary.class_counts.player_cutscene_wait_function_with_sfx -eq 1) "expected one cutscene wait/SFX workorder"
    Assert-Condition ([int]$summary.class_counts.player_magic_spell_stage_table -eq 1) "expected one magic spell workorder"
    Assert-Condition ([int]$summary.class_counts.player_model_anim_group_climb_hold_variant -eq 2) "expected two climb-hold variant workorders"
    Assert-Condition ([int]$summary.class_counts.player_model_anim_group_slope_slip_variant -eq 3) "expected three slope-slip variant workorders"
    Assert-Condition ([int]$summary.class_counts.player_swim_state_runtime_action -eq 1) "expected one swim state workorder"
    Assert-Condition ([int]$summary.class_counts.player_water_ledge_climb_runtime_action -eq 1) "expected one water ledge-climb workorder"
    Assert-Condition (Test-Path -LiteralPath $csvOutput) "expected CSV matrix output"
}

Write-Host "OOT3D Link child pose/source runtime capture matrix: $summaryOutput"
Write-Host "status=$($summary.status) records=$($summary.record_count) issues=$($summary.issue_count)"
