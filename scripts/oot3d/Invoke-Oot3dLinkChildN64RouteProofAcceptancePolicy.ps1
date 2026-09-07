param(
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\n64_route_proof_acceptance_policy"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "Link child N64 route proof acceptance policy failed: $Message"
    }
}

function Get-BoolValue([object]$Value) {
    if ($null -eq $Value) {
        return $false
    }
    if ($Value -is [bool]) {
        return [bool]$Value
    }
    $text = [string]$Value
    return ($text -eq "True" -or $text -eq "true" -or $text -eq "1")
}

function Get-IntValue([object]$Value) {
    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) {
        return 0
    }
    return [int]$Value
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

function Add-Count([hashtable]$Map, [string]$Key) {
    if ([string]::IsNullOrWhiteSpace($Key)) {
        $Key = "unknown"
    }
    if (-not $Map.ContainsKey($Key)) {
        $Map[$Key] = 0
    }
    $Map[$Key] = [int]$Map[$Key] + 1
}

function Get-AcceptanceRecord([object]$RouteRecord, [object]$RoutePolicyRecord, [object]$SourceIdentityRecord) {
    $bucket = [string]$RouteRecord.route_proof_bucket
    $status = ""
    $n64RouteAccepted = $false
    $routeOrOwnerPolicyReady = $false
    $directCallsiteContextReady = $false
    $familyRoutePolicyRequired = $false
    $anonymousOwnerIdentityRequired = $false
    $promotedOwnerAliasPolicyRequired = $false
    $routeAbsentOwnerPolicyRequired = $false
    $alternateSourceRequired = $false
    $policyRequiredBeforePromotion = $true
    $requiredPolicyGate = ""
    $issueList = New-Object "System.Collections.Generic.List[object]"
    $installedRuntimeDumpValid = Get-BoolValue $RouteRecord.installed_runtime_dump_valid
    $installedStaticFrameDumpValid = Get-BoolValue $RouteRecord.installed_static_frame_dump_valid
    $sourceIdentityReviewStatus = "not_applicable"
    $sourceIdentityAccepted = $false
    $sourceIdentityReviewIssueCount = 0
    if ($bucket -eq "direct_n64_player_callsite_context") {
        if ($null -eq $SourceIdentityRecord) {
            $sourceIdentityReviewStatus = "missing_source_identity_review"
            $issueList.Add([pscustomobject]@{
                reason = "missing_source_identity_review_record"
                detail = [string]$RouteRecord.n64_name
            }) | Out-Null
        }
        else {
            $sourceIdentityReviewStatus = [string]$SourceIdentityRecord.source_identity_review_status
            $sourceIdentityAccepted = Get-BoolValue $SourceIdentityRecord.source_identity_accepted
            $sourceIdentityReviewIssueCount = Get-IntValue $SourceIdentityRecord.issue_count
            if ($sourceIdentityReviewIssueCount -ne 0) {
                $issueList.Add([pscustomobject]@{
                    reason = "source_identity_review_issue"
                    detail = [string]$SourceIdentityRecord.issue_count
                }) | Out-Null
            }
        }
    }

    switch ($bucket) {
        "exact_direct_n64_route_proven" {
            $n64RouteAccepted = $true
            if (
                $null -ne $RoutePolicyRecord -and
                [string]$RoutePolicyRecord.policy_decision_status -like "route_callsite_policy_ready*" -and
                (Get-IntValue $RoutePolicyRecord.issue_count) -eq 0
            ) {
                if ($installedRuntimeDumpValid) {
                    $status = "n64_route_callsite_policy_and_installed_draw_ready_pending_semantic_acceptance"
                    $requiredPolicyGate = "semantic_acceptance_review"
                }
                else {
                    $status = "n64_route_and_callsite_policy_ready_pending_installed_draw"
                    $requiredPolicyGate = "installed_draw_only"
                }
                $routeOrOwnerPolicyReady = $true
            }
            else {
                $status = "n64_route_proof_accepted_pending_installed_draw_and_owner_policy"
                $requiredPolicyGate = "installed_draw_and_owner_policy"
                if ($null -eq $RoutePolicyRecord) {
                    $issueList.Add([pscustomobject]@{
                        reason = "accepted_route_missing_route_policy_record"
                        detail = [string]$RouteRecord.n64_name
                    }) | Out-Null
                }
            }
            if ((Get-IntValue $RouteRecord.direct_player_actor_reference_count) -le 0) {
                $issueList.Add([pscustomobject]@{
                    reason = "accepted_route_without_direct_player_actor_reference"
                    detail = [string]$RouteRecord.n64_name
                }) | Out-Null
            }
        }
        "direct_n64_player_callsite_context" {
            $directCallsiteContextReady = $true
            if ($installedRuntimeDumpValid) {
                if ($sourceIdentityAccepted) {
                    $status = "direct_callsite_context_installed_draw_source_identity_ready_pending_semantic_acceptance"
                    $requiredPolicyGate = "semantic_acceptance_review"
                }
                else {
                    $status = "direct_callsite_context_ready_with_installed_draw_pending_source_identity"
                    $requiredPolicyGate = "source_identity_review"
                }
            }
            else {
                $status = "direct_callsite_context_ready_pending_source_identity_or_installed_draw"
                $requiredPolicyGate = "source_identity_or_installed_draw"
            }
        }
        "family_or_sibling_n64_route_only" {
            $status = "family_route_policy_required_before_n64_route_acceptance"
            $familyRoutePolicyRequired = $true
            $requiredPolicyGate = "exact_route_policy_or_family_route_acceptance"
        }
        "n64_table_identity_candidate_owner_context" {
            $status = "anonymous_owner_identity_policy_required_before_acceptance"
            $anonymousOwnerIdentityRequired = $true
            $requiredPolicyGate = "anonymous_owner_identity_policy"
        }
        "promoted_owner_route_delegated_alias" {
            $status = "promoted_owner_alias_policy_required_before_acceptance"
            $promotedOwnerAliasPolicyRequired = $true
            $requiredPolicyGate = "alias_owner_policy"
        }
        "route_absent_owner_policy_only" {
            $status = "route_absent_owner_policy_required_before_acceptance"
            $routeAbsentOwnerPolicyRequired = $true
            $requiredPolicyGate = "route_absent_owner_policy"
        }
        "source_risk_no_route_proof" {
            $status = "alternate_source_required_before_acceptance"
            $alternateSourceRequired = $true
            $requiredPolicyGate = "alternate_source"
        }
        default {
            $status = "route_acceptance_unclassified"
            $requiredPolicyGate = "route_acceptance_classification"
            $issueList.Add([pscustomobject]@{
                reason = "unknown_route_proof_bucket"
                detail = $bucket
            }) | Out-Null
        }
    }

    $semanticAcceptanceStatus = [string]$RouteRecord.semantic_acceptance_status
    if ($sourceIdentityAccepted) {
        $semanticAcceptanceStatus = [string]$SourceIdentityRecord.semantic_acceptance_status
    }
    elseif ($installedRuntimeDumpValid -and $bucket -eq "direct_n64_player_callsite_context") {
        $semanticAcceptanceStatus = "blocked_pending_source_identity_or_semantic_review"
    }
    $requiredNextStep = [string]$RouteRecord.required_next_step
    if ($sourceIdentityAccepted) {
        $requiredNextStep = "semantic acceptance review using installed dynamic draw and accepted source identity"
    }

    return [pscustomobject]@{
        n64_name = [string]$RouteRecord.n64_name
        source_csab_name = [string]$RouteRecord.source_csab_name
        frontier_class = [string]$RouteRecord.frontier_class
        workorder_kind = [string]$RouteRecord.workorder_kind
        route_proof_bucket = $bucket
        route_proof_status = [string]$RouteRecord.route_proof_status
        n64_route_acceptance_status = $status
        n64_route_accepted = $n64RouteAccepted
        route_or_owner_policy_ready = $routeOrOwnerPolicyReady
        direct_callsite_context_ready = $directCallsiteContextReady
        family_route_policy_required = $familyRoutePolicyRequired
        anonymous_owner_identity_required = $anonymousOwnerIdentityRequired
        promoted_owner_alias_policy_required = $promotedOwnerAliasPolicyRequired
        route_absent_owner_policy_required = $routeAbsentOwnerPolicyRequired
        alternate_source_required = $alternateSourceRequired
        policy_required_before_promotion = $policyRequiredBeforePromotion
        required_policy_gate = $requiredPolicyGate
        installed_runtime_draw_required = (-not $installedRuntimeDumpValid)
        installed_runtime_dump_valid = $installedRuntimeDumpValid
        installed_static_frame_dump_valid = $installedStaticFrameDumpValid
        runtime_harness_dump_valid = Get-BoolValue $RouteRecord.runtime_harness_dump_valid
        source_identity_review_status = $sourceIdentityReviewStatus
        source_identity_accepted = $sourceIdentityAccepted
        source_identity_review_issue_count = $sourceIdentityReviewIssueCount
        semantic_acceptance_status = $semanticAcceptanceStatus
        mapping_promotion_allowed = Get-BoolValue $RouteRecord.mapping_promotion_allowed
        required_next_step = $requiredNextStep
        issue_count = $issueList.Count
        issues = @($issueList.ToArray())
    }
}

$routeSummaryPath = Join-Path $WorkRoot "character_conversion\n64_route_proof_priority_matrix\link_child_n64_route_proof_priority_matrix_summary.json"
$routePolicyCsvPath = Join-Path $WorkRoot "character_conversion\route_proven_ownership_policy\link_child_route_proven_ownership_policy.csv"
$sourceIdentitySummaryPath = Join-Path $WorkRoot "character_conversion\direct_callsite_source_identity_review\link_child_direct_callsite_source_identity_review_summary.json"
$sourceIdentityCsvPath = Join-Path $WorkRoot "character_conversion\direct_callsite_source_identity_review\link_child_direct_callsite_source_identity_review.csv"
Require-Path $routeSummaryPath "N64 route proof priority matrix summary"
Require-Path $routePolicyCsvPath "Route-proven ownership policy CSV"
Require-Path $sourceIdentitySummaryPath "Direct-callsite source identity review summary"
Require-Path $sourceIdentityCsvPath "Direct-callsite source identity review CSV"
$routeSummary = Get-Content -LiteralPath $routeSummaryPath -Raw | ConvertFrom-Json
$routeRecords = @($routeSummary.records)
$routePolicyRows = @(Import-Csv -LiteralPath $routePolicyCsvPath)
$sourceIdentitySummary = Get-Content -LiteralPath $sourceIdentitySummaryPath -Raw | ConvertFrom-Json
$sourceIdentityRows = @(Import-Csv -LiteralPath $sourceIdentityCsvPath)
$routePolicyByName = @{}
foreach ($routePolicyRow in $routePolicyRows) {
    $routePolicyByName[[string]$routePolicyRow.n64_name] = $routePolicyRow
}
$sourceIdentityByName = @{}
foreach ($sourceIdentityRow in $sourceIdentityRows) {
    $sourceIdentityByName[[string]$sourceIdentityRow.n64_name] = $sourceIdentityRow
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$records = New-Object "System.Collections.Generic.List[object]"
$issueCount = Get-IntValue $routeSummary.issue_count
$n64RouteAcceptedCount = 0
$routeOrOwnerPolicyReadyCount = 0
$directCallsiteContextReadyCount = 0
$familyRoutePolicyRequiredCount = 0
$anonymousOwnerIdentityRequiredCount = 0
$promotedOwnerAliasPolicyRequiredCount = 0
$routeAbsentOwnerPolicyRequiredCount = 0
$alternateSourceRequiredCount = 0
$installedRuntimeDrawRequiredCount = 0
$installedRuntimeDumpValidCount = 0
$installedStaticFrameDumpValidCount = 0
$runtimeHarnessDumpValidCount = 0
$sourceIdentityAcceptedCount = 0
$sourceIdentityReviewIssueCount = 0
$semanticAcceptedCount = 0
$mappingPromotionAllowedCount = 0
$statusCounts = @{}
$bucketCounts = @{}
$policyGateCounts = @{}
$sourceIdentityReviewStatusCounts = @{}
$issueReasonCounts = @{}

foreach ($routeRecord in $routeRecords) {
    $record = Get-AcceptanceRecord $routeRecord $routePolicyByName[[string]$routeRecord.n64_name] $sourceIdentityByName[[string]$routeRecord.n64_name]
    if ([bool]$record.n64_route_accepted) {
        $n64RouteAcceptedCount++
    }
    if ([bool]$record.route_or_owner_policy_ready) {
        $routeOrOwnerPolicyReadyCount++
    }
    if ([bool]$record.direct_callsite_context_ready) {
        $directCallsiteContextReadyCount++
    }
    if ([bool]$record.family_route_policy_required) {
        $familyRoutePolicyRequiredCount++
    }
    if ([bool]$record.anonymous_owner_identity_required) {
        $anonymousOwnerIdentityRequiredCount++
    }
    if ([bool]$record.promoted_owner_alias_policy_required) {
        $promotedOwnerAliasPolicyRequiredCount++
    }
    if ([bool]$record.route_absent_owner_policy_required) {
        $routeAbsentOwnerPolicyRequiredCount++
    }
    if ([bool]$record.alternate_source_required) {
        $alternateSourceRequiredCount++
    }
    if ([bool]$record.installed_runtime_draw_required) {
        $installedRuntimeDrawRequiredCount++
    }
    if ([bool]$record.installed_runtime_dump_valid) {
        $installedRuntimeDumpValidCount++
    }
    if ([bool]$record.installed_static_frame_dump_valid) {
        $installedStaticFrameDumpValidCount++
    }
    if ([bool]$record.runtime_harness_dump_valid) {
        $runtimeHarnessDumpValidCount++
    }
    if ([bool]$record.source_identity_accepted) {
        $sourceIdentityAcceptedCount++
    }
    $sourceIdentityReviewIssueCount += Get-IntValue $record.source_identity_review_issue_count
    if ([string]$record.semantic_acceptance_status -eq "semantic_accepted") {
        $semanticAcceptedCount++
    }
    if ([bool]$record.mapping_promotion_allowed) {
        $mappingPromotionAllowedCount++
    }
    Add-Count $statusCounts ([string]$record.n64_route_acceptance_status)
    Add-Count $bucketCounts ([string]$record.route_proof_bucket)
    Add-Count $policyGateCounts ([string]$record.required_policy_gate)
    Add-Count $sourceIdentityReviewStatusCounts ([string]$record.source_identity_review_status)
    foreach ($issue in @($record.issues)) {
        Add-Count $issueReasonCounts ([string]$issue.reason)
    }
    $issueCount += [int]$record.issue_count
    $records.Add($record) | Out-Null
}

$recordArray = @($records.ToArray() | Sort-Object route_proof_bucket,n64_name)
$summaryPath = Join-Path $OutputRoot "link_child_n64_route_proof_acceptance_policy_summary.json"
$csvPath = Join-Path $OutputRoot "link_child_n64_route_proof_acceptance_policy.csv"
$status = if ($issueCount -eq 0) { "n64_route_proof_acceptance_policy_ready" } else { "n64_route_proof_acceptance_policy_has_issues" }

$summary = [pscustomobject]@{
    format = "oot3d_link_child_n64_route_proof_acceptance_policy_v1"
    status = $status
    policy = [ordered]@{
        scope = "Conservative acceptance policy over Link child residual N64 PlayerAnimation route evidence"
        accepted_route_rule = "Only exact direct N64 PlayerAnimation route proof is accepted as route evidence."
        semantic_effect = "Consumes direct-callsite source identity review evidence; no final semantic acceptance or mapping promotion is granted by this policy."
        replacement_gate = "Every residual row still requires non-harness installed runtime draw proof before in-game replacement."
    }
    n64_route_proof_priority_matrix_summary = (Resolve-Path -LiteralPath $routeSummaryPath).Path
    route_proven_ownership_policy_csv = (Resolve-Path -LiteralPath $routePolicyCsvPath).Path
    direct_callsite_source_identity_review_summary = (Resolve-Path -LiteralPath $sourceIdentitySummaryPath).Path
    output_root = (Resolve-Path -LiteralPath $OutputRoot).Path
    record_count = $recordArray.Count
    route_priority_issue_count = Get-IntValue $routeSummary.issue_count
    source_identity_review_row_count = Get-IntValue $sourceIdentitySummary.record_count
    n64_route_accepted_count = $n64RouteAcceptedCount
    route_or_owner_policy_ready_count = $routeOrOwnerPolicyReadyCount
    direct_callsite_context_ready_count = $directCallsiteContextReadyCount
    accepted_route_or_direct_context_count = $n64RouteAcceptedCount + $directCallsiteContextReadyCount
    family_route_policy_required_count = $familyRoutePolicyRequiredCount
    anonymous_owner_identity_required_count = $anonymousOwnerIdentityRequiredCount
    promoted_owner_alias_policy_required_count = $promotedOwnerAliasPolicyRequiredCount
    route_absent_owner_policy_required_count = $routeAbsentOwnerPolicyRequiredCount
    alternate_source_required_count = $alternateSourceRequiredCount
    installed_runtime_draw_required_count = $installedRuntimeDrawRequiredCount
    installed_runtime_dump_valid_count = $installedRuntimeDumpValidCount
    installed_static_frame_dump_valid_count = $installedStaticFrameDumpValidCount
    runtime_harness_dump_valid_count = $runtimeHarnessDumpValidCount
    source_identity_accepted_count = $sourceIdentityAcceptedCount
    source_identity_review_issue_count = $sourceIdentityReviewIssueCount
    semantic_accepted_count = $semanticAcceptedCount
    mapping_promotion_allowed_count = $mappingPromotionAllowedCount
    issue_count = $issueCount
    n64_route_acceptance_status_counts = $statusCounts
    route_proof_bucket_counts = $bucketCounts
    required_policy_gate_counts = $policyGateCounts
    source_identity_review_status_counts = $sourceIdentityReviewStatusCounts
    issue_reason_counts = $issueReasonCounts
    records = $recordArray
}

$summary | ConvertTo-Json -Depth 20 | Out-File -LiteralPath $summaryPath -Encoding utf8
$recordArray |
    Select-Object n64_name,source_csab_name,frontier_class,workorder_kind,route_proof_bucket,route_proof_status,n64_route_acceptance_status,n64_route_accepted,route_or_owner_policy_ready,direct_callsite_context_ready,family_route_policy_required,anonymous_owner_identity_required,promoted_owner_alias_policy_required,route_absent_owner_policy_required,alternate_source_required,policy_required_before_promotion,required_policy_gate,installed_runtime_draw_required,installed_runtime_dump_valid,installed_static_frame_dump_valid,runtime_harness_dump_valid,source_identity_review_status,source_identity_accepted,source_identity_review_issue_count,semantic_acceptance_status,mapping_promotion_allowed,required_next_step,issue_count |
    Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_link_child_n64_route_proof_acceptance_policy_v1") "unexpected acceptance policy format"
    Assert-Condition ($summary.status -eq "n64_route_proof_acceptance_policy_ready") "expected acceptance policy ready"
    Assert-Condition ([int]$summary.record_count -eq 63) "expected 63 residual route records"
    Assert-Condition ([int]$summary.route_priority_issue_count -eq 0) "expected route priority input without issues"
    Assert-Condition ([int]$summary.source_identity_review_row_count -eq 16) "expected 16 direct-callsite source identity review rows"
    Assert-Condition ([int]$summary.n64_route_accepted_count -eq 5) "expected five exact direct N64 routes accepted"
    Assert-Condition ([int]$summary.route_or_owner_policy_ready_count -eq 5) "expected five route-proven callsite policies ready"
    Assert-Condition ([int]$summary.direct_callsite_context_ready_count -eq 16) "expected sixteen direct callsite context rows"
    Assert-Condition ([int]$summary.accepted_route_or_direct_context_count -eq 21) "expected 21 accepted route or direct-context rows"
    Assert-Condition ([int]$summary.family_route_policy_required_count -eq 8) "expected eight family-route policy rows"
    Assert-Condition ([int]$summary.anonymous_owner_identity_required_count -eq 11) "expected eleven anonymous-owner policy rows"
    Assert-Condition ([int]$summary.promoted_owner_alias_policy_required_count -eq 7) "expected seven promoted-owner alias rows"
    Assert-Condition ([int]$summary.route_absent_owner_policy_required_count -eq 14) "expected fourteen route-absent owner-policy rows"
    Assert-Condition ([int]$summary.alternate_source_required_count -eq 2) "expected two alternate-source rows"
    Assert-Condition ([int]$summary.installed_runtime_draw_required_count -eq 62) "expected 62 rows to still require installed runtime draw"
    Assert-Condition ([int]$summary.installed_runtime_dump_valid_count -eq 1) "expected one dynamic installed runtime draw"
    Assert-Condition ([int]$summary.installed_static_frame_dump_valid_count -eq 0) "expected no static-frame installed draws"
    Assert-Condition ([int]$summary.runtime_harness_dump_valid_count -eq 62) "expected 62 harness-valid rows"
    Assert-Condition ([int]$summary.source_identity_accepted_count -eq 1) "expected one accepted direct-callsite source identity"
    Assert-Condition ([int]$summary.source_identity_review_issue_count -eq 0) "expected zero source identity review issues"
    Assert-Condition ([int]$summary.semantic_accepted_count -eq 0) "expected no semantic acceptances"
    Assert-Condition ([int]$summary.mapping_promotion_allowed_count -eq 0) "expected no mapping promotions"
    Assert-Condition ([int]$summary.issue_count -eq 0) "expected no acceptance policy issues"
    Assert-Condition ((Get-JsonValue $summary.n64_route_acceptance_status_counts "n64_route_and_callsite_policy_ready_pending_installed_draw") -eq 5) "expected five route and callsite policy ready rows"
    Assert-Condition ((Get-JsonValue $summary.n64_route_acceptance_status_counts "direct_callsite_context_ready_pending_source_identity_or_installed_draw") -eq 15) "expected fifteen direct-context rows still pending source identity or installed draw"
    Assert-Condition ((Get-JsonValue $summary.n64_route_acceptance_status_counts "direct_callsite_context_installed_draw_source_identity_ready_pending_semantic_acceptance") -eq 1) "expected one direct-context row with installed draw and accepted source identity"
    Assert-Condition ((Get-JsonValue $summary.required_policy_gate_counts "installed_draw_only") -eq 5) "expected five installed-draw-only gates"
    Assert-Condition ((Get-JsonValue $summary.required_policy_gate_counts "semantic_acceptance_review") -eq 1) "expected one semantic acceptance review gate after source identity acceptance"
    Assert-Condition ((Get-JsonValue $summary.source_identity_review_status_counts "source_identity_accepted_pending_semantic_acceptance") -eq 1) "expected one accepted source identity review row"
}

Write-Host "status=$($summary.status) rows=$($summary.record_count) n64RouteAccepted=$($summary.n64_route_accepted_count) routePolicyReady=$($summary.route_or_owner_policy_ready_count) directContext=$($summary.direct_callsite_context_ready_count) installedDrawRequired=$($summary.installed_runtime_draw_required_count) semanticAccepted=$($summary.semantic_accepted_count) promotions=$($summary.mapping_promotion_allowed_count) issues=$($summary.issue_count)"
Write-Host "summary=$summaryPath"
Write-Host "csv=$csvPath"
