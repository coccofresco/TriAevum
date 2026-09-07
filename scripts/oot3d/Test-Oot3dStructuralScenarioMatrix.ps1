[CmdletBinding()]
param(
    [ValidateSet("smoke", "scenes", "all")]
    [string]$Selection = "smoke",
    [ValidateSet("discover", "strict", "full")]
    [string]$Mode = "full",
    [string[]]$Scenario = @(),
    [string]$ProductExecutable =
        "I:\oot3dre_work\whole-aot-product-consumer\oot3d_native_game.exe",
    [string]$PicaCompiler =
        "I:\oot3dre_work\whole-aot-product-consumer\oot3d_native_pica_aot_compiler.exe",
    [string]$ScenarioCatalog = "",
    [string]$BaselineInventory = "",
    [string]$PicaAotShaderPack = "",
    [string]$AnchorState =
        "I:\oot3dre_work\native_game\checkpoints\hud_minimap_visible_20260721.oot3dsav",
    [string]$GraphicsConfig =
        "I:\oot3dre_work\native_game\oot3d_native_game.json",
    [string]$SaveDataDirectory =
        "I:\oot3dre_work\native_game\oot3d_native_savedata",
    [string]$OutputRoot =
        "I:\oot3dre_work\structural-scenarios\matrices",
    [ValidateRange(1, 1000000)]
    [int]$MaximumFrames = 2000,
    [ValidateRange(15, 3600)]
    [double]$MaximumSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$invokeScript = Join-Path $PSScriptRoot "Invoke-Oot3dNativeGame.ps1"
if ([string]::IsNullOrWhiteSpace($ScenarioCatalog)) {
    $ScenarioCatalog = Join-Path $repoRoot `
        "tools\oot3d\native_a32_runtime\oot3d_structural_scenarios.json"
}
if ([string]::IsNullOrWhiteSpace($BaselineInventory)) {
    $BaselineInventory = Join-Path $repoRoot `
        "tools\oot3d\native_pica_frontend\profiles\oot3d_pica_boot_title_kokiri_effective.json"
}

function Resolve-RequiredFile {
    param([string]$Path, [string]$Description)
    $resolved = [System.IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "$Description is missing: $resolved"
    }
    return $resolved
}

function Get-FileIdentity {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        path = $item.FullName
        bytes = [uint64]$item.Length
        sha256 = (Get-FileHash -LiteralPath $item.FullName `
            -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$resolvedExecutable = Resolve-RequiredFile $ProductExecutable "Product executable"
$resolvedInvokeScript = Resolve-RequiredFile $invokeScript "Native-game launcher"
$resolvedCatalog = Resolve-RequiredFile $ScenarioCatalog "Scenario catalog"
$resolvedAnchor = Resolve-RequiredFile $AnchorState "Anchor savestate"
$resolvedGraphics = Resolve-RequiredFile $GraphicsConfig "Graphics configuration"
$resolvedBaselineInventory = Resolve-RequiredFile `
    $BaselineInventory "Baseline PICA inventory"
$resolvedCompiler = ""
if ($Mode -eq "full") {
    $resolvedCompiler = Resolve-RequiredFile $PicaCompiler "PICA AOT compiler"
}
$resolvedStrictPack = ""
if ($Mode -eq "strict") {
    $resolvedStrictPack = Resolve-RequiredFile `
        $PicaAotShaderPack "Strict PICA AOT shader pack"
}
$resolvedSaveData = [System.IO.Path]::GetFullPath($SaveDataDirectory)
if (-not (Test-Path -LiteralPath $resolvedSaveData -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $resolvedSaveData | Out-Null
}

$catalog = Get-Content -LiteralPath $resolvedCatalog -Raw | ConvertFrom-Json
if ([string]$catalog.format -ne "oot3d_structural_scenario_catalog_v2") {
    throw "Unsupported structural scenario catalog: $resolvedCatalog"
}
$allRecipes = @($catalog.recipes)
$byId = @{}
foreach ($recipe in $allRecipes) {
    $id = [string]$recipe.id
    if ($byId.ContainsKey($id)) {
        throw "Duplicate structural scenario id: $id"
    }
    $byId[$id] = $recipe
}

$selectedRecipes = @()
if ($Scenario.Count -ne 0) {
    foreach ($id in $Scenario) {
        if (-not $byId.ContainsKey($id)) {
            throw "Unknown structural scenario: $id"
        }
        $selectedRecipes += $byId[$id]
    }
} elseif ($Selection -eq "all") {
    $selectedRecipes = $allRecipes
} elseif ($Selection -eq "scenes") {
    $selectedRecipes = @($allRecipes |
        Group-Object { [int]$_.scene_id } |
        ForEach-Object {
            $_.Group | Sort-Object `
                @{ Expression = {
                    if ([string]$_.validation_status -eq
                        "native_kokiri_slot5_entrypoint_confirmed") { 0 } else { 1 }
                } },
                @{ Expression = { -[int]$_.reference_use_count } },
                @{ Expression = { [int]$_.entrance_index } } |
                Select-Object -First 1
        } | Sort-Object { [int]$_.scene_id })
} else {
    $smokeIds = @(
        "link_info_entry_00bb",
        "spot04_info_entry_0211",
        "spot00_info_entry_0185"
    )
    foreach ($id in $smokeIds) {
        if (-not $byId.ContainsKey($id)) {
            throw "Smoke scenario is absent from the catalog: $id"
        }
        $selectedRecipes += $byId[$id]
    }
}
if ($selectedRecipes.Count -eq 0) {
    throw "Structural scenario selection is empty"
}

$startedUtc = [DateTime]::UtcNow
$runId = $startedUtc.ToString("yyyyMMddTHHmmssZ")
$resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$runRoot = Join-Path $resolvedOutputRoot $runId
$discoveryRoot = Join-Path $runRoot "discovery"
$strictRoot = Join-Path $runRoot "strict"
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$hostPowerShell = (Get-Process -Id $PID).Path
$maximumSecondsText = $MaximumSeconds.ToString(
    "R", [Globalization.CultureInfo]::InvariantCulture)

function Invoke-ScenarioPass {
    param(
        [Parameter(Mandatory = $true)]$Recipe,
        [Parameter(Mandatory = $true)][ValidateSet("discovery", "strict")]
        [string]$Pass,
        [string]$InventoryPath = "",
        [string]$ShaderPack = ""
    )
    $passRoot = if ($Pass -eq "discovery") { $discoveryRoot } else { $strictRoot }
    New-Item -ItemType Directory -Force -Path $passRoot | Out-Null
    $id = [string]$Recipe.id
    $outputPath = Join-Path $passRoot "$id.run.json"
    $stdoutPath = Join-Path $passRoot "$id.stdout.log"
    $stderrPath = Join-Path $passRoot "$id.stderr.log"
    $arguments = @(
        "-NoLogo", "-NoProfile", "-File", $resolvedInvokeScript,
        "-SkipBuild",
        "-Executable", $resolvedExecutable,
        "-Renderer", "nri",
        "-UiProfile", "oot3d",
        "-Frames", [string]$MaximumFrames,
        "-MaxSeconds", $maximumSecondsText,
        "-FixedDeltaSeconds", "0.0166666666666667",
        "-SimulationRate", "30",
        "-PresentationRate", "free",
        "-Width", "1280",
        "-Height", "720",
        "-GraphicsConfig", $resolvedGraphics,
        "-SaveDataDirectory", $resolvedSaveData,
        "-LoadState", $resolvedAnchor,
        "-ScenarioCatalog", $resolvedCatalog,
        "-Scenario", $id,
        "-ScenarioStrict",
        "-ScenarioAutoExit",
        "-DisableTypedGameplay",
        "-DisableAudio",
        "-Output", $outputPath)
    if ($Pass -eq "discovery") {
        $arguments += @("-PicaEffectiveShaderInventory", $InventoryPath)
    } else {
        $arguments += @(
            "-PicaAotShaderPack", $ShaderPack,
            "-PicaAotShaderStrict")
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $hostPowerShell
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $arguments) {
        $startInfo.ArgumentList.Add($argument)
    }
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $outerTimeoutMs = [int][Math]::Ceiling(($MaximumSeconds + 30.0) * 1000.0)
    $timedOut = -not $process.WaitForExit($outerTimeoutMs)
    if ($timedOut) {
        try {
            $process.Kill($true)
            $process.WaitForExit(5000) | Out-Null
        } catch {
            Write-Warning "Could not terminate timed-out scenario $id"
        }
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $timer.Stop()
    Set-Content -LiteralPath $stdoutPath -Value $stdout -Encoding utf8
    Set-Content -LiteralPath $stderrPath -Value $stderr -Encoding utf8
    $exitCode = if ($timedOut) { 124 } else { $process.ExitCode }
    $run = $null
    if (Test-Path -LiteralPath $outputPath -PathType Leaf) {
        $run = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
    }
    $scenarioState = if ($null -ne $run) { $run.structural_scenario } else { $null }
    $shaderMatch = [regex]::Match(
        $stdout + "`n" + $stderr,
        "OOT3D_PICA_AOT_SHADER_RESOLUTION hits=(\d+) misses=(\d+) strict=(\d+) entries=(\d+)")
    $shaderResolution = if ($shaderMatch.Success) {
        [ordered]@{
            hits = [uint64]$shaderMatch.Groups[1].Value
            misses = [uint64]$shaderMatch.Groups[2].Value
            strict = [int]$shaderMatch.Groups[3].Value -ne 0
            entries = [uint64]$shaderMatch.Groups[4].Value
        }
    } else { $null }
    $validState = $null -ne $scenarioState -and
        [bool]$scenarioState.complete -and
        [bool]$scenarioState.target_identity_validated -and
        [int]$scenarioState.entrance_index -eq [int]$Recipe.entrance_index -and
        [int]$scenarioState.scene_id -eq [int]$Recipe.scene_id
    $validDraws = $null -ne $run -and
        [uint64]$run.draws_submitted -gt 0 -and
        [uint64]$run.refreshes_with_native_draws -gt 0
    $validInventory = $true
    if ($Pass -eq "discovery") {
        $validInventory = Test-Path -LiteralPath $InventoryPath -PathType Leaf
    }
    $validStrictShaders = $Pass -ne "strict" -or
        ($null -ne $shaderResolution -and
         [bool]$shaderResolution.strict -and
         [uint64]$shaderResolution.misses -eq 0)
    $passed = $exitCode -eq 0 -and $validState -and $validDraws -and
        $validInventory -and $validStrictShaders
    return [ordered]@{
        pass = $Pass
        passed = $passed
        exit_code = $exitCode
        timed_out = $timedOut
        elapsed_seconds = $timer.Elapsed.TotalSeconds
        output = if ($null -ne $run) { Get-FileIdentity $outputPath } else { $null }
        stdout = Get-FileIdentity $stdoutPath
        stderr = Get-FileIdentity $stderrPath
        inventory = if ($Pass -eq "discovery" -and $validInventory) {
            Get-FileIdentity $InventoryPath
        } else { $null }
        structural_scenario = $scenarioState
        draws_submitted = if ($null -ne $run) { [uint64]$run.draws_submitted } else { 0 }
        refreshes_with_native_draws = if ($null -ne $run) {
            [uint64]$run.refreshes_with_native_draws
        } else { 0 }
        shader_source_cache = if ($null -ne $run) { $run.shader_source_cache } else { $null }
        aot_shader_resolution = $shaderResolution
    }
}

function Invoke-PicaPackCompilation {
    param([string[]]$Inventories, [string]$PackPath)
    $manifestPath = Join-Path $runRoot "structural_scenarios.pica.manifest.json"
    $mergedPath = Join-Path $runRoot "structural_scenarios.pica.inventory.json"
    $stdoutPath = Join-Path $runRoot "pica_compile.stdout.log"
    $stderrPath = Join-Path $runRoot "pica_compile.stderr.log"
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $resolvedCompiler
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($inventory in @($resolvedBaselineInventory) + $Inventories) {
        $startInfo.ArgumentList.Add("--inventory")
        $startInfo.ArgumentList.Add($inventory)
    }
    foreach ($argument in @(
        "--pack", $PackPath,
        "--manifest", $manifestPath,
        "--merged-inventory", $mergedPath)) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    Set-Content -LiteralPath $stdoutPath `
        -Value $stdoutTask.GetAwaiter().GetResult() -Encoding utf8
    Set-Content -LiteralPath $stderrPath `
        -Value $stderrTask.GetAwaiter().GetResult() -Encoding utf8
    if ($process.ExitCode -ne 0) {
        throw "PICA AOT compilation failed; see $stderrPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    return [ordered]@{
        pack = Get-FileIdentity $PackPath
        manifest = Get-FileIdentity $manifestPath
        merged_inventory = Get-FileIdentity $mergedPath
        shader_count = [uint64]$manifest.shader_count
        stdout = Get-FileIdentity $stdoutPath
        stderr = Get-FileIdentity $stderrPath
    }
}

$records = [System.Collections.Generic.List[object]]::new()
$discoveredInventories = [System.Collections.Generic.List[string]]::new()
if ($Mode -in @("discover", "full")) {
    foreach ($recipe in $selectedRecipes) {
        $inventoryPath = Join-Path $discoveryRoot `
            "$([string]$recipe.id).inventory.json"
        Write-Host "[discover] $([string]$recipe.id)"
        $result = Invoke-ScenarioPass `
            -Recipe $recipe -Pass discovery -InventoryPath $inventoryPath
        $records.Add([ordered]@{
            id = [string]$recipe.id
            scene_id = [int]$recipe.scene_id
            semantic_scene = [string]$recipe.semantic_scene
            entrance_index = [int]$recipe.entrance_index
            declared_coverage = $recipe.coverage
            discovery = $result
            strict = $null
        }) | Out-Null
        if ([bool]$result.passed) {
            $discoveredInventories.Add($inventoryPath) | Out-Null
        }
    }
}

$compiledPack = $null
$activeStrictPack = $resolvedStrictPack
if ($Mode -eq "full") {
    if ($discoveredInventories.Count -eq 0) {
        Write-Warning "Skipping strict pass because no scenario completed discovery"
    } else {
        $activeStrictPack = Join-Path $runRoot "structural_scenarios.o3ps"
        $compiledPack = Invoke-PicaPackCompilation `
            -Inventories @($discoveredInventories) -PackPath $activeStrictPack
    }
}

if ($Mode -eq "strict") {
    foreach ($recipe in $selectedRecipes) {
        $records.Add([ordered]@{
            id = [string]$recipe.id
            scene_id = [int]$recipe.scene_id
            semantic_scene = [string]$recipe.semantic_scene
            entrance_index = [int]$recipe.entrance_index
            declared_coverage = $recipe.coverage
            discovery = $null
            strict = $null
        }) | Out-Null
    }
}
if ($Mode -eq "strict" -or
    ($Mode -eq "full" -and -not [string]::IsNullOrWhiteSpace($activeStrictPack))) {
    foreach ($record in $records) {
        if ($Mode -eq "full" -and -not [bool]$record.discovery.passed) {
            continue
        }
        $recipe = $byId[[string]$record.id]
        Write-Host "[strict] $([string]$recipe.id)"
        $record.strict = Invoke-ScenarioPass `
            -Recipe $recipe -Pass strict -ShaderPack $activeStrictPack
    }
}

$requiredPasses = if ($Mode -eq "discover") { @("discovery") } `
    elseif ($Mode -eq "strict") { @("strict") } else { @("discovery", "strict") }
$failed = @($records | Where-Object {
    $record = $_
    @($requiredPasses | Where-Object {
        $null -eq $record.$_ -or -not [bool]$record.$_.passed
    }).Count -ne 0
})
$completedUtc = [DateTime]::UtcNow
$report = [ordered]@{
    format = "oot3d_structural_scenario_matrix_v2"
    passed = $failed.Count -eq 0
    mode = $Mode
    selection = $Selection
    started_utc = $startedUtc.ToString("o")
    completed_utc = $completedUtc.ToString("o")
    elapsed_seconds = ($completedUtc - $startedUtc).TotalSeconds
    product = Get-FileIdentity $resolvedExecutable
    catalog = Get-FileIdentity $resolvedCatalog
    anchor = Get-FileIdentity $resolvedAnchor
    baseline_inventory = Get-FileIdentity $resolvedBaselineInventory
    compiled_pack = $compiledPack
    coverage = [ordered]@{
        catalog_recipes = [int]$catalog.summary.recipe_count
        catalog_unique_scenes = [int]$catalog.summary.unique_scene_count
        selected_recipes = $selectedRecipes.Count
        selected_unique_scenes = @($selectedRecipes.scene_id | Sort-Object -Unique).Count
        passed_recipes = $records.Count - $failed.Count
        failed_recipes = $failed.Count
        declared_actor_placements = [uint64](
            ($selectedRecipes | Measure-Object `
                -Property { [uint64]$_.coverage.actor_placement_count } -Sum).Sum)
        declared_light_records = [uint64](
            ($selectedRecipes | Measure-Object `
                -Property { [uint64]$_.coverage.decoded_light_record_count } -Sum).Sum)
        declared_cutscene_sources = [uint64](
            ($selectedRecipes | Measure-Object `
                -Property { [uint64]$_.coverage.cutscene_source_count } -Sum).Sum)
    }
    scenarios = @($records)
}
$reportPath = Join-Path $runRoot "structural_scenario_matrix.json"
$temporaryReport = "$reportPath.tmp"
$report | ConvertTo-Json -Depth 14 | Set-Content `
    -LiteralPath $temporaryReport -Encoding utf8
Move-Item -LiteralPath $temporaryReport -Destination $reportPath -Force
$latestPath = Join-Path $resolvedOutputRoot "latest.json"
New-Item -ItemType Directory -Force -Path $resolvedOutputRoot | Out-Null
[ordered]@{
    report = $reportPath
    passed = [bool]$report.passed
    completed_utc = $report.completed_utc
} | ConvertTo-Json | Set-Content -LiteralPath $latestPath -Encoding utf8

Write-Host "Structural scenario matrix: $reportPath"
Write-Host ("Result: {0}; {1}/{2} recipes passed across {3} scenes" -f `
    $(if ($report.passed) { "PASS" } else { "FAIL" }),
    $report.coverage.passed_recipes,
    $report.coverage.selected_recipes,
    $report.coverage.selected_unique_scenes)
if (-not [bool]$report.passed) {
    throw "Structural scenario matrix failed; see $reportPath"
}
