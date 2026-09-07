param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$AnonymousN64TableContext = "",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($AnonymousN64TableContext)) {
    $AnonymousN64TableContext = Join-Path $WorkRoot "character_conversion\anonymous_n64_table_context\link_child_anonymous_n64_table_context_summary.json"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\anonymous_owner_capture_matrix"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child anonymous owner capture matrix verification failed: $Message"
    }
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
        return "anonymous_owner_capture"
    }
    return $safe
}

function Parse-SymbolList([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return @()
    }
    return @($Value -split ";" | ForEach-Object { $_.Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Get-CapturePlan([string]$N64Name) {
    switch ($N64Name) {
        "gPlayerAnim_002578" {
            return [pscustomobject]@{
                owner_context_class = "player_model_anim_group_candidate_owner"
                capture_status = "needs_model_anim_type_equipment_capture"
                capture_scope = "PLAYER_ANIMGROUP_side_walkL modelAnimType candidate owner"
                runtime_trigger = "D_80853914 PLAYER_ANIMGROUP_side_walkL with anchor modelAnimType"
                acceptance_gate = "anonymous table identity plus modelAnimType/equipment draw capture and source-owner policy"
                required_next_step = "capture side-walkL modelAnimType selection and compare anonymous derivative against the named anchor owner"
            }
        }
        "gPlayerAnim_0025A8" {
            return [pscustomobject]@{
                owner_context_class = "player_wait_defense_transition_candidate_owner"
                capture_status = "needs_wait_defense_transition_table_capture"
                capture_scope = "D_808543A4 waitR2defense transition"
                runtime_trigger = "D_808543A4 anchor waitR2defense selector"
                acceptance_gate = "anonymous table identity plus wait-defense transition capture and source-owner policy"
                required_next_step = "capture waitR2defense transition timing and decide whether the numeric row is an alias or a distinct baked derivative"
            }
        }
        "gPlayerAnim_0025B8" {
            return [pscustomobject]@{
                owner_context_class = "player_wait_defense_transition_candidate_owner"
                capture_status = "needs_wait_defense_transition_table_capture"
                capture_scope = "D_808543A4 waitR2defense transition"
                runtime_trigger = "D_808543A4 anchor waitR2defense selector"
                acceptance_gate = "anonymous table identity plus wait-defense transition capture and source-owner policy"
                required_next_step = "capture waitR2defense transition timing and decide whether the second numeric row is an alias, a temporal variant, or a distinct derivative"
            }
        }
        "gPlayerAnim_0025E8" {
            return [pscustomobject]@{
                owner_context_class = "player_boomerang_upper_action_branch_candidate_owner"
                capture_status = "needs_boomerang_upper_action_branch_capture"
                capture_scope = "func_808335B0 boomerang side-walkR branch"
                runtime_trigger = "func_808334B4 true branch returning gPlayerAnim_link_boom_throw_side_walkR"
                acceptance_gate = "promoted owner reuse policy plus boomerang upper-action installed draw capture"
                required_next_step = "capture the boomerang side-walkR branch and decide whether the anonymous row can reuse the promoted boomerang owner"
            }
        }
        "gPlayerAnim_002840" {
            return [pscustomobject]@{
                owner_context_class = "player_cutscene_action_table_candidate_owner"
                capture_status = "needs_cutscene_actor_cue_runtime_capture"
                capture_scope = "PLAYER_CSACTION_99 zeldamiru wait"
                runtime_trigger = "PLAYER_CSACTION_99"
                acceptance_gate = "anonymous table identity plus cutscene actor-cue installed draw capture and owner policy"
                required_next_step = "bind PLAYER_CSACTION_99 to the cutscene cue and capture the installed draw before accepting zeldamiru_wait reuse"
            }
        }
        "gPlayerAnim_002A78" {
            return [pscustomobject]@{
                owner_context_class = "player_model_anim_group_candidate_owner"
                capture_status = "needs_model_anim_type_equipment_capture"
                capture_scope = "PLAYER_ANIMGROUP_heavy_run modelAnimType candidate owner"
                runtime_trigger = "D_80853914 PLAYER_ANIMGROUP_heavy_run with fighter-long modelAnimType"
                acceptance_gate = "anonymous table identity plus modelAnimType/equipment draw capture and source-owner policy"
                required_next_step = "capture heavy-run modelAnimType selection and decide whether same-frame source reuse is intentional"
            }
        }
        "gPlayerAnim_002AD0" {
            return [pscustomobject]@{
                owner_context_class = "player_power_kiru_table_and_cutscene_candidate_owner"
                capture_status = "needs_power_kiru_table_and_cutscene_capture"
                capture_scope = "D_80854350 power-kiru start plus PLAYER_CSACTION_63"
                runtime_trigger = "D_80854350 power-kiru start and PLAYER_CSACTION_63"
                acceptance_gate = "anonymous table identity plus power-kiru/cutscene installed draw capture and source-owner policy"
                required_next_step = "capture both the power-kiru table path and PLAYER_CSACTION_63 path to disambiguate owner reuse"
            }
        }
        "gPlayerAnim_002B48" {
            return [pscustomobject]@{
                owner_context_class = "player_model_anim_group_candidate_owner"
                capture_status = "needs_model_anim_type_equipment_capture"
                capture_scope = "PLAYER_ANIMGROUP_walk modelAnimType candidate owner"
                runtime_trigger = "D_80853914 PLAYER_ANIMGROUP_walk with fighter-long modelAnimType"
                acceptance_gate = "anonymous table identity plus modelAnimType/equipment draw capture and source-owner policy"
                required_next_step = "capture fighter walk modelAnimType selection and decide whether child ft_walk_long reuse is intentional"
            }
        }
        "gPlayerAnim_002B50" {
            return [pscustomobject]@{
                owner_context_class = "player_model_anim_group_candidate_owner"
                capture_status = "needs_model_anim_type_equipment_capture"
                capture_scope = "PLAYER_ANIMGROUP_run modelAnimType candidate owner"
                runtime_trigger = "D_80853914 PLAYER_ANIMGROUP_run with fighter-long modelAnimType"
                acceptance_gate = "anonymous table identity plus modelAnimType/equipment draw capture and source-owner policy"
                required_next_step = "capture fighter run modelAnimType selection and decide whether same-frame child ft_run_long reuse is intentional"
            }
        }
        "gPlayerAnim_002BD8" {
            return [pscustomobject]@{
                owner_context_class = "player_model_anim_group_candidate_owner"
                capture_status = "needs_model_anim_type_equipment_capture"
                capture_scope = "PLAYER_ANIMGROUP_walk/wait-neighbor fighter table candidate owner"
                runtime_trigger = "D_80853914 fighter walk-long owner near wait transition region"
                acceptance_gate = "anonymous table identity plus modelAnimType/equipment draw capture and source-owner policy"
                required_next_step = "capture the fighter walk owner in the wait-transition neighborhood and decide whether this numeric row is a temporal variant"
            }
        }
        "gPlayerAnim_002D50" {
            return [pscustomobject]@{
                owner_context_class = "player_climb_side_table_candidate_owner"
                capture_status = "needs_climb_side_table_capture"
                capture_scope = "Player age/climb info table unk_BC side-climb pair"
                runtime_trigger = "unk_BC climb-side pair selecting gPlayerAnim_link_normal_Fclimb_sideR"
                acceptance_gate = "promoted owner reuse policy plus climb-side installed draw capture"
                required_next_step = "capture the climb-side state using the promoted Fclimb_sideR owner before accepting reuse"
            }
        }
    }
    return [pscustomobject]@{
        owner_context_class = "manual_anonymous_owner_capture_review"
        capture_status = "needs_manual_capture_design"
        capture_scope = "manual"
        runtime_trigger = "manual owner callsite review"
        acceptance_gate = "manual_review"
        required_next_step = "inspect owner callsite context manually"
    }
}

Require-Path $AnonymousN64TableContext "Anonymous N64 table context summary"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$context = Get-Content -LiteralPath $AnonymousN64TableContext -Raw | ConvertFrom-Json
$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = 0
$ownerRefPresentCount = 0
$directAnonymousRefCount = 0
$candidateOwnerRefCount = 0
$captureStatusCounts = @{}
$ownerContextClassCounts = @{}
$acceptanceGateCounts = @{}
$sourceContractCounts = @{}

foreach ($row in @($context.records)) {
    $n64Name = [string](Get-JsonValue $row "n64_name")
    $plan = Get-CapturePlan $n64Name
    $directRefs = [int](Get-JsonValue $row "direct_anonymous_symbol_reference_count")
    $ownerRefs = [int](Get-JsonValue $row "candidate_owner_symbol_reference_count")
    $issues = New-Object "System.Collections.Generic.List[object]"
    if ($ownerRefs -le 0) {
        $issues.Add([pscustomobject]@{ reason = "missing_candidate_owner_source_reference"; detail = $n64Name }) | Out-Null
    }
    if ($directRefs -gt 0) {
        $issues.Add([pscustomobject]@{ reason = "unexpected_direct_anonymous_source_reference"; detail = $n64Name }) | Out-Null
    }
    if ([string]$plan.capture_status -eq "needs_manual_capture_design") {
        $issues.Add([pscustomobject]@{ reason = "manual_capture_status"; detail = $n64Name }) | Out-Null
    }

    $issueCount += $issues.Count
    $directAnonymousRefCount += $directRefs
    $candidateOwnerRefCount += $ownerRefs
    if ($ownerRefs -gt 0) {
        $ownerRefPresentCount++
    }
    Add-Count $captureStatusCounts ([string]$plan.capture_status)
    Add-Count $ownerContextClassCounts ([string]$plan.owner_context_class)
    Add-Count $acceptanceGateCounts ([string]$plan.acceptance_gate)
    Add-Count $sourceContractCounts ([string](Get-JsonValue $row "source_contract_kind"))

    $candidateOwners = @(
        Parse-SymbolList ([string](Get-JsonValue $row "candidate_csab_collision_n64_names"))
        Parse-SymbolList ([string](Get-JsonValue $row "runtime_csab_collision_n64_names"))
    ) | Select-Object -Unique

    $records.Add([pscustomobject]@{
        n64_name = $n64Name
        n64_table_index = Get-JsonValue $row "n64_table_index"
        n64_table_offset = [string](Get-JsonValue $row "n64_table_offset")
        n64_data_name = [string](Get-JsonValue $row "n64_data_name")
        n64_frame_count = Get-JsonValue $row "n64_frame_count"
        source_csab_name = [string](Get-JsonValue $row "source_csab_name")
        source_contract_kind = [string](Get-JsonValue $row "source_contract_kind")
        candidate_owner_symbols = ($candidateOwners -join ";")
        candidate_owner_symbol_reference_count = $ownerRefs
        candidate_owner_symbol_references = [string](Get-JsonValue $row "candidate_owner_symbol_references")
        direct_anonymous_symbol_reference_count = $directRefs
        previous_named_context = [string](Get-JsonValue $row "previous_named_context")
        next_named_context = [string](Get-JsonValue $row "next_named_context")
        owner_context_class = [string]$plan.owner_context_class
        capture_status = [string]$plan.capture_status
        capture_scope = [string]$plan.capture_scope
        runtime_trigger = [string]$plan.runtime_trigger
        acceptance_gate = [string]$plan.acceptance_gate
        expected_dump_path = "debug/oot3d_link_child_anonymous_owner_capture_" + (Safe-PathPart $n64Name) + ".json"
        runtime_acceptance_status = "anonymous_owner_capture_pending_installed_runtime_draw"
        semantic_effect = "owner capture workorder only; no anonymous identity, ownership, reuse, or mapping promotion"
        required_next_step = [string]$plan.required_next_step
        issue_count = [int]$issues.Count
        issues = @($issues.ToArray())
    }) | Out-Null
}

$recordArray = @($records.ToArray())
$summaryOutput = Join-Path $OutputRoot "link_child_anonymous_owner_capture_matrix_summary.json"
$csvOutput = Join-Path $OutputRoot "link_child_anonymous_owner_capture_matrix.csv"
$status = if ($issueCount -eq 0 -and $recordArray.Count -gt 0) {
    "anonymous_owner_capture_workorders_ready"
}
elseif ($recordArray.Count -eq 0) {
    "complete_no_anonymous_owner_rows"
}
else {
    "anonymous_owner_capture_workorders_have_issues"
}

$summary = [pscustomobject]@{
    format = "oot3d_link_child_anonymous_owner_capture_matrix_v1"
    status = $status
    policy = [ordered]@{
        scope = "installed runtime capture workorders for Link child anonymous numeric rows using named candidate-owner callsite context"
        semantic_effect = "capture planning only; rows remain blocked until installed runtime draw evidence and ownership/reuse policy are accepted"
        source_context_policy = "anonymous numeric symbols have no direct player-actor refs; capture is designed from the named owner/collision symbol callsite"
    }
    anonymous_n64_table_context = (Resolve-Path -LiteralPath $AnonymousN64TableContext).Path
    output_root = $OutputRoot
    record_count = $recordArray.Count
    anonymous_context_record_count = [int](Get-JsonValue $context "record_count")
    direct_anonymous_symbol_reference_count = $directAnonymousRefCount
    candidate_owner_symbol_reference_count = $candidateOwnerRefCount
    candidate_owner_reference_present_count = $ownerRefPresentCount
    accepted_identity_count = 0
    accepted_ownership_count = 0
    accepted_reuse_policy_count = 0
    installed_runtime_dump_valid_count = 0
    issue_count = $issueCount
    capture_status_counts = $captureStatusCounts
    owner_context_class_counts = $ownerContextClassCounts
    acceptance_gate_counts = $acceptanceGateCounts
    source_contract_counts = $sourceContractCounts
    records = $recordArray
}
$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryOutput -Encoding utf8

$recordArray |
    Select-Object n64_name,source_csab_name,source_contract_kind,candidate_owner_symbols,candidate_owner_symbol_reference_count,direct_anonymous_symbol_reference_count,owner_context_class,capture_status,capture_scope,runtime_trigger,acceptance_gate,runtime_acceptance_status,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvOutput -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_anonymous_owner_capture_matrix_v1") "unexpected owner capture matrix format"
    Assert-Condition ($summary.status -eq "anonymous_owner_capture_workorders_ready") "expected ready anonymous owner capture workorders"
    Assert-Condition ([int]$summary.record_count -eq 11) "expected 11 anonymous owner capture workorders"
    Assert-Condition ([int]$summary.anonymous_context_record_count -eq 11) "expected 11 input anonymous N64 context rows"
    Assert-Condition ([int]$summary.direct_anonymous_symbol_reference_count -eq 0) "expected no direct anonymous symbol refs"
    Assert-Condition ([int]$summary.candidate_owner_symbol_reference_count -eq 14) "expected 14 candidate-owner symbol refs"
    Assert-Condition ([int]$summary.candidate_owner_reference_present_count -eq 11) "expected owner refs for every anonymous row"
    Assert-Condition ([int]$summary.accepted_identity_count -eq 0) "expected no accepted anonymous identities"
    Assert-Condition ([int]$summary.accepted_ownership_count -eq 0) "expected no accepted anonymous ownership decisions"
    Assert-Condition ([int]$summary.accepted_reuse_policy_count -eq 0) "expected no accepted anonymous reuse policies"
    Assert-Condition ([int]$summary.installed_runtime_dump_valid_count -eq 0) "expected no installed anonymous owner capture dumps"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected zero anonymous owner capture issues"
    Assert-Condition ((Get-JsonValue $summary.capture_status_counts "needs_model_anim_type_equipment_capture") -eq 5) "expected five model/equipment anonymous owner captures"
    Assert-Condition ((Get-JsonValue $summary.capture_status_counts "needs_wait_defense_transition_table_capture") -eq 2) "expected two wait-defense transition captures"
    Assert-Condition ((Get-JsonValue $summary.capture_status_counts "needs_boomerang_upper_action_branch_capture") -eq 1) "expected one boomerang branch capture"
    Assert-Condition ((Get-JsonValue $summary.capture_status_counts "needs_cutscene_actor_cue_runtime_capture") -eq 1) "expected one cutscene actor cue capture"
    Assert-Condition ((Get-JsonValue $summary.capture_status_counts "needs_power_kiru_table_and_cutscene_capture") -eq 1) "expected one power-kiru table/cutscene capture"
    Assert-Condition ((Get-JsonValue $summary.capture_status_counts "needs_climb_side_table_capture") -eq 1) "expected one climb-side table capture"
    Assert-Condition (Test-Path -LiteralPath $csvOutput) "expected anonymous owner capture CSV output"
}

Write-Host "OOT3D Link child anonymous owner capture matrix: $summaryOutput"
Write-Host "status=$($summary.status) rows=$($summary.record_count) ownerRefsPresent=$($summary.candidate_owner_reference_present_count) directAnonymousRefs=$($summary.direct_anonymous_symbol_reference_count) issues=$($summary.issue_count)"
