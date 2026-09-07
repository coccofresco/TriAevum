[CmdletBinding()]
param(
    [string]$Output = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$analysisRoot = Join-Path $repoRoot "tools\oot3d\decomp_support\analysis"
$generator = Join-Path $repoRoot `
    "tools\oot3d\native_a32_runtime\generate_structural_scenario_catalog.py"
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $repoRoot `
        "tools\oot3d\native_a32_runtime\oot3d_structural_scenarios.json"
}
$inputs = [ordered]@{
    entrances = Join-Path $analysisRoot "scene_global_entrance_table.json"
    setups = Join-Path $analysisRoot "scene_setup_source_table.json"
    actors = Join-Path $analysisRoot "scene_actor_source_table.json"
    lights = Join-Path $analysisRoot "scene_light_source_table.json"
    cutscenes = Join-Path $analysisRoot "scene_cutscene_source_table.json"
    transition_contract = Join-Path $analysisRoot `
        "transition_request_helper_semantics.json"
}
foreach ($required in @($generator) + @($inputs.Values)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Structural scenario evidence is missing: $required"
    }
}

& python $generator `
    --entrances $inputs.entrances `
    --setups $inputs.setups `
    --actors $inputs.actors `
    --lights $inputs.lights `
    --cutscenes $inputs.cutscenes `
    --transition-contract $inputs.transition_contract `
    --output ([System.IO.Path]::GetFullPath($Output))
if ($LASTEXITCODE -ne 0) {
    throw "Structural scenario catalog generation failed: $LASTEXITCODE"
}

$catalog = Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json
if ([string]$catalog.format -ne "oot3d_structural_scenario_catalog_v2" -or
    [int]$catalog.summary.recipe_count -ne @($catalog.recipes).Count) {
    throw "Generated structural scenario catalog failed validation"
}
Write-Output ([ordered]@{
    output = [System.IO.Path]::GetFullPath($Output)
    recipes = [int]$catalog.summary.recipe_count
    scenes = [int]$catalog.summary.unique_scene_count
} | ConvertTo-Json -Compress)
