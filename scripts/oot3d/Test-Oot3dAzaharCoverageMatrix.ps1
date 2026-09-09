param(
    [ValidateSet("smoke", "scene_representative", "local_entrance_representative",
        "setup_variant", "exhaustive", "all")]
    [string]$Mode = "smoke",
    [string]$CatalogPath = (Join-Path $PSScriptRoot "../../tools/oot3d/native_a32_runtime/oot3d_azahar_coverage_scenarios.json"),
    [string]$LauncherPath = (Join-Path $PSScriptRoot "Invoke-Oot3dAzaharCoverageScenario.ps1"),
    [string]$AzaharExe = "I:\oot3dre_work\azahar-oot3d-coverage-build\bin\Release\azahar.exe",
    [string]$RomPath = "E:\ppssppvr\oot3d_decomp\oot3d.cci",
    [string]$SeedSavestatePath = "",
    [string]$OutputRoot = "I:\oot3dre_work\azahar-coverage-matrices",
    [string]$RunDirectory = "",
    [string]$ScenarioListPath = "",
    [ValidateSet("opengl", "vulkan")]
    [string]$Backend = "vulkan",
    [int]$Slot = 5,
    [int]$StartIndex = 0,
    [int]$MaxScenarios = 0,
    [int]$SettleMilliseconds = -1,
    [int]$TimeoutSeconds = 90,
    [int]$MaxVerticesPerDraw = 1,
    [int]$CaptureFramesOverride = 0,
    [switch]$SkipFramebuffer,
    [switch]$CompactEvidence,
    [switch]$ShaderSeed,
    [switch]$RetryFailures,
    [switch]$FailFast
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-FileExists {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
}

function Add-SetValues {
    param(
        [System.Collections.Generic.HashSet[string]]$Set,
        [System.Collections.Specialized.OrderedDictionary]$FirstObservedIn,
        [object[]]$Values,
        [string]$ScenarioId
    )
    foreach ($value in $Values) {
        if ($null -ne $value) {
            $identity = [string]$value
            if ($Set.Add($identity)) {
                $FirstObservedIn[$identity] = $ScenarioId
            }
        }
    }
}

function Write-MatrixState {
    param(
        [string]$Path,
        [object]$Catalog,
        [object[]]$Selected,
        [System.Collections.ArrayList]$Results
    )

    $latestByScenario = [ordered]@{}
    foreach ($result in $Results) {
        $latestByScenario[[string]$result.scenario_id] = $result
    }
    $latest = @($latestByScenario.Values)
    $passed = @($latest | Where-Object status -eq "passed")
    $failed = @($latest | Where-Object status -eq "failed")
    $unavailable = @($latest | Where-Object status -eq "unavailable")
    $sceneIds = [System.Collections.Generic.HashSet[string]]::new()
    $vertexPrograms = [System.Collections.Generic.HashSet[string]]::new()
    $geometryPrograms = [System.Collections.Generic.HashSet[string]]::new()
    $fragmentConfigs = [System.Collections.Generic.HashSet[string]]::new()
    $pipelines = [System.Collections.Generic.HashSet[string]]::new()
    $textureFormats = [System.Collections.Generic.HashSet[string]]::new()
    $shadowStates = [System.Collections.Generic.HashSet[string]]::new()
    $vertexFirstObservedIn = [ordered]@{}
    $geometryFirstObservedIn = [ordered]@{}
    $fragmentFirstObservedIn = [ordered]@{}
    $pipelineFirstObservedIn = [ordered]@{}
    $textureFirstObservedIn = [ordered]@{}
    $shadowFirstObservedIn = [ordered]@{}
    $draws = 0L
    foreach ($result in $passed) {
        $scenarioId = [string]$result.scenario_id
        $null = $sceneIds.Add([string]$result.scene_id)
        $draws += [long]$result.shader_counts.draws
        Add-SetValues $vertexPrograms $vertexFirstObservedIn @($result.vertex_program_ids) $scenarioId
        Add-SetValues $geometryPrograms $geometryFirstObservedIn @($result.geometry_program_ids) $scenarioId
        Add-SetValues $fragmentConfigs $fragmentFirstObservedIn @($result.fragment_config_ids) $scenarioId
        Add-SetValues $pipelines $pipelineFirstObservedIn @($result.pipeline_ids) $scenarioId
        Add-SetValues $textureFormats $textureFirstObservedIn @($result.texture_format_ids) $scenarioId
        Add-SetValues $shadowStates $shadowFirstObservedIn @($result.shadow_state_ids) $scenarioId
    }

    $document = [pscustomobject][ordered]@{
        format = "oot3d_azahar_coverage_matrix_v1"
        updated_at = (Get-Date).ToString("o")
        evidence_role = if ($ShaderSeed.IsPresent) { "offline_shader_preparation_not_gameplay_replay" } else { "validation_only_not_runtime_input" }
        mode = $Mode
        backend = $Backend
        selection_source = if ([string]::IsNullOrWhiteSpace($ScenarioListPath)) {
            "catalog_tag:$Mode"
        } else {
            (Resolve-Path -LiteralPath $ScenarioListPath).Path
        }
        capture_policy = [ordered]@{
            capture_frames_override = $CaptureFramesOverride
            framebuffer_capture_enabled = -not $SkipFramebuffer.IsPresent
            detailed_pica_evidence_retained = -not $CompactEvidence.IsPresent -or $ShaderSeed.IsPresent
            shader_seed_capture = $ShaderSeed.IsPresent
            max_vertices_per_draw = $MaxVerticesPerDraw
        }
        catalog_path = (Resolve-Path -LiteralPath $CatalogPath).Path
        catalog_decomp_provenance = $Catalog.provenance.decomp
        selected_scenario_count = $Selected.Count
        attempt_count = $Results.Count
        coverage = [ordered]@{
            classified_scenarios = $latest.Count
            attempted_scenarios = $passed.Count + $failed.Count
            passed_scenarios = $passed.Count
            failed_scenarios = $failed.Count
            unavailable_scenarios = $unavailable.Count
            remaining_scenarios = [Math]::Max(0, $Selected.Count - $latest.Count)
            covered_scenes = $sceneIds.Count
            draws = $draws
            unique_vertex_programs = $vertexPrograms.Count
            unique_geometry_programs = $geometryPrograms.Count
            unique_fragment_configs = $fragmentConfigs.Count
            unique_pipelines = $pipelines.Count
            unique_enabled_texture_format_types = $textureFormats.Count
            unique_shadow_states = $shadowStates.Count
        }
        identities = [ordered]@{
            vertex_programs = @($vertexPrograms | Sort-Object)
            geometry_programs = @($geometryPrograms | Sort-Object)
            fragment_configs = @($fragmentConfigs | Sort-Object)
            pipelines = @($pipelines | Sort-Object)
            enabled_texture_format_types = @($textureFormats | Sort-Object)
            shadow_states = @($shadowStates | Sort-Object)
        }
        identity_first_observed_in = [ordered]@{
            vertex_programs = $vertexFirstObservedIn
            geometry_programs = $geometryFirstObservedIn
            fragment_configs = $fragmentFirstObservedIn
            pipelines = $pipelineFirstObservedIn
            enabled_texture_format_types = $textureFirstObservedIn
            shadow_states = $shadowFirstObservedIn
        }
        results = @($latest)
    }
    $temporary = $Path + ".tmp"
    $document | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $temporary -Encoding ascii
    Move-Item -LiteralPath $temporary -Destination $Path -Force
    return $document
}

Assert-FileExists $CatalogPath "Azahar coverage catalog"
Assert-FileExists $LauncherPath "Azahar scenario launcher"
Assert-FileExists $AzaharExe "Instrumented Azahar executable"
Assert-FileExists $RomPath "OOT3D game image"
if (-not [string]::IsNullOrWhiteSpace($SeedSavestatePath)) {
    Assert-FileExists $SeedSavestatePath "Seed savestate"
}

$catalog = Get-Content -LiteralPath $CatalogPath -Raw | ConvertFrom-Json
if ([string]$catalog.format -ne "oot3d_azahar_coverage_catalog_v1") {
    throw "Unsupported Azahar coverage catalog format: $($catalog.format)"
}
$selected = if (-not [string]::IsNullOrWhiteSpace($ScenarioListPath)) {
    Assert-FileExists $ScenarioListPath "Scenario list"
    $selection = Get-Content -LiteralPath $ScenarioListPath -Raw | ConvertFrom-Json
    $requestedIds = @($selection.scenario_ids | ForEach-Object { [string]$_ })
    if ($requestedIds.Count -eq 0) {
        throw "Scenario list contains no scenario_ids: $ScenarioListPath"
    }
    $catalogById = @{}
    foreach ($scenario in $catalog.scenarios) {
        $catalogById[[string]$scenario.id] = $scenario
    }
    @($requestedIds | ForEach-Object {
        if (-not $catalogById.ContainsKey($_)) {
            throw "Scenario list references unknown catalog id: $_"
        }
        $catalogById[$_]
    })
} elseif ($Mode -eq "all") {
    @($catalog.scenarios)
} else {
    @($catalog.scenarios | Where-Object { @($_.selection_tags) -contains $Mode })
}
if ($StartIndex -lt 0 -or $StartIndex -ge $selected.Count) {
    throw "StartIndex $StartIndex is outside the selected $($selected.Count)-scenario matrix."
}
$selected = @($selected | Select-Object -Skip $StartIndex)
if ($MaxScenarios -gt 0) {
    $selected = @($selected | Select-Object -First $MaxScenarios)
}

if ([string]::IsNullOrWhiteSpace($RunDirectory)) {
    $RunDirectory = Join-Path $OutputRoot ("{0}_{1}_{2}" -f $Mode, $Backend,
        (Get-Date).ToString("yyyyMMdd_HHmmss"))
}
$captureRoot = Join-Path $RunDirectory "captures"
$logRoot = Join-Path $RunDirectory "logs"
$statePath = Join-Path $RunDirectory "coverage_matrix.json"
New-Item -ItemType Directory -Force -Path $RunDirectory, $captureRoot, $logRoot | Out-Null

$results = [System.Collections.ArrayList]::new()
if (Test-Path -LiteralPath $statePath -PathType Leaf) {
    $existing = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    if ([string]$existing.format -ne "oot3d_azahar_coverage_matrix_v1") {
        throw "Unsupported existing matrix state: $statePath"
    }
    $seedProperty = $existing.capture_policy.PSObject.Properties["shader_seed_capture"]
    $existingSeed = $null -ne $seedProperty -and [bool]$seedProperty.Value
    if ($existingSeed -ne $ShaderSeed.IsPresent) {
        throw "Cannot resume a matrix with a different shader-seed capture policy. Use a new RunDirectory."
    }
    foreach ($result in @($existing.results)) {
        $null = $results.Add($result)
    }
}

$skip = @{}
foreach ($result in $results) {
    if ([string]$result.status -in @("passed", "unavailable") -or -not $RetryFailures.IsPresent) {
        $skip[[string]$result.scenario_id] = $true
    }
}

foreach ($scenario in $selected) {
    $scenarioId = [string]$scenario.id
    if ($skip.ContainsKey($scenarioId)) {
        continue
    }
    $safeId = $scenarioId -replace '[^A-Za-z0-9_.-]+', '_'
    $scenarioCaptureRoot = Join-Path $captureRoot $safeId
    $logPath = Join-Path $logRoot ($safeId + ".log")
    New-Item -ItemType Directory -Force -Path $scenarioCaptureRoot | Out-Null
    $started = Get-Date
    if ($null -ne $scenario.runtime_availability -and
        -not [bool]$scenario.runtime_availability.launchable) {
        $result = [ordered]@{
            scenario_id = $scenarioId
            status = "unavailable"
            started_at = $started.ToString("o")
            elapsed_seconds = 0.0
            scene_id = [int]$scenario.scene_id
            entrance_index = [int]$scenario.entrance_index
            coverage_summary_path = ""
            shader_coverage_path = ""
            shader_counts = [ordered]@{ draws = 0 }
            vertex_program_ids = @()
            geometry_program_ids = @()
            fragment_config_ids = @()
            pipeline_ids = @()
            texture_format_ids = @()
            shadow_state_ids = @()
            error = [string]$scenario.runtime_availability.reason
            availability_classification = [string]$scenario.runtime_availability.classification
        }
        $null = $results.Add([pscustomobject]$result)
        $matrix = Write-MatrixState $statePath $catalog $selected $results
        Write-Host ("[{0}/{1}] {2}: unavailable ({3})" -f
            $matrix.coverage.classified_scenarios, $selected.Count, $scenarioId,
            $result.availability_classification)
        continue
    }
    try {
        $arguments = @{
            ScenarioId = $scenarioId
            CatalogPath = $CatalogPath
            AzaharExe = $AzaharExe
            RomPath = $RomPath
            OutputRoot = $scenarioCaptureRoot
            Slot = $Slot
            SeedSavestatePath = $SeedSavestatePath
            Backend = $Backend
            TimeoutSeconds = $TimeoutSeconds
            MaxVerticesPerDraw = $MaxVerticesPerDraw
        }
        if ($CaptureFramesOverride -gt 0) {
            $arguments.CaptureFramesOverride = $CaptureFramesOverride
        }
        if ($SkipFramebuffer.IsPresent) {
            $arguments.SkipFramebuffer = $true
        }
        if ($CompactEvidence.IsPresent) {
            $arguments.CompactEvidence = $true
        }
        if ($ShaderSeed.IsPresent) {
            $arguments.ShaderSeed = $true
        }
        if ($SettleMilliseconds -ge 0) {
            $arguments.SettleMilliseconds = $SettleMilliseconds
        }
        & $LauncherPath @arguments *> $logPath
        $summaryFile = Get-ChildItem -LiteralPath $scenarioCaptureRoot -Filter "coverage_summary.json" -File -Recurse | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($null -eq $summaryFile) {
            throw "Scenario launcher returned without a coverage summary."
        }
        $summary = Get-Content -LiteralPath $summaryFile.FullName -Raw | ConvertFrom-Json
        $shader = $summary.shader_coverage
        if ($null -eq $shader -or [string]$shader.format -ne "oot3d_azahar_shader_coverage_v1") {
            throw "Scenario coverage summary lacks shader coverage."
        }
        $textureIds = @($shader.enabled_texture_format_types | ForEach-Object {
            "format_$($_.format)_type_$($_.type)"
        })
        $result = [ordered]@{
            scenario_id = $scenarioId
            status = "passed"
            started_at = $started.ToString("o")
            elapsed_seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 3)
            scene_id = [int]$scenario.scene_id
            entrance_index = [int]$scenario.entrance_index
            coverage_summary_path = $summaryFile.FullName
            shader_coverage_path = [string]$summary.shader_coverage_path
            shader_counts = $shader.counts
            vertex_program_ids = @($shader.vertex_programs | ForEach-Object { [string]$_.id })
            geometry_program_ids = @($shader.geometry_programs | ForEach-Object { [string]$_.id })
            fragment_config_ids = @($shader.fragment_configs | ForEach-Object { [string]$_.id })
            pipeline_ids = @($shader.pipelines | ForEach-Object { [string]$_.id })
            texture_format_ids = $textureIds
            shadow_state_ids = @($shader.shadow_states | ForEach-Object { [string]$_.id })
            error = ""
        }
    } catch {
        $result = [ordered]@{
            scenario_id = $scenarioId
            status = "failed"
            started_at = $started.ToString("o")
            elapsed_seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 3)
            scene_id = [int]$scenario.scene_id
            entrance_index = [int]$scenario.entrance_index
            coverage_summary_path = ""
            shader_coverage_path = ""
            shader_counts = [ordered]@{ draws = 0 }
            vertex_program_ids = @()
            geometry_program_ids = @()
            fragment_config_ids = @()
            pipeline_ids = @()
            texture_format_ids = @()
            shadow_state_ids = @()
            error = $_.Exception.Message
        }
    }
    $null = $results.Add([pscustomobject]$result)
    $matrix = Write-MatrixState $statePath $catalog $selected $results
    Write-Host ("[{0}/{1}] {2}: {3}; scenes={4}, pipelines={5}" -f
        $matrix.coverage.classified_scenarios, $selected.Count, $scenarioId, $result.status,
        $matrix.coverage.covered_scenes, $matrix.coverage.unique_pipelines)
    if ([string]$result.status -eq "failed" -and $FailFast.IsPresent) {
        throw "Azahar coverage scenario failed: $scenarioId - $($result.error)"
    }
}

$final = Write-MatrixState $statePath $catalog $selected $results
$final | Select-Object format, mode, backend, selected_scenario_count, coverage | Format-List
Write-Host "Azahar coverage matrix: $statePath"
if ($ShaderSeed.IsPresent) {
    $recovery = Join-Path $PSScriptRoot "../../tools/oot3d/native_a32_runtime/recover_pica_capture_corpus.py"
    & python $recovery --matrix $statePath --output-root (Join-Path $RunDirectory "shader-corpus")
    if ($LASTEXITCODE -ne 0) { throw "Shader corpus recovery failed: $statePath" }
}
