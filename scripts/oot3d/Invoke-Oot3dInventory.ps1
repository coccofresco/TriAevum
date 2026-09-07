param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
    [switch]$Shallow,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Invoke-Oot3dTool([string[]]$Arguments) {
    Write-Host "python -m oot3d_asset_tool $($Arguments -join ' ')"
    & python -m oot3d_asset_tool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "oot3d_asset_tool failed with exit code $LASTEXITCODE"
    }
}

function Get-JsonValue($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return 0
    }
    return $property.Value
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D inventory verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $RomFs "OOT3D RomFS"

$outputRoot = Join-Path $WorkRoot "inventory"
$inventoryOutput = Join-Path $outputRoot "oot3d_romfs_inventory.json"
$summaryOutput = Join-Path $outputRoot "oot3d_romfs_inventory_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $inventoryArgs = @(
        "inventory-romfs",
        $RomFs,
        "--output",
        $inventoryOutput
    )
    if ($Shallow) {
        $inventoryArgs += "--shallow"
    }
    Invoke-Oot3dTool -Arguments $inventoryArgs
}
finally {
    Pop-Location
}

$inventory = Get-Content -LiteralPath $inventoryOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    romfs = (Resolve-Path $RomFs).Path
    deep = [bool]$inventory.deep
    inventory = $inventoryOutput
    file_count = $inventory.file_count
    total_bytes = $inventory.total_bytes
    top_level_file_counts = $inventory.top_level_file_counts
    extension_counts = [ordered]@{
        zsi = Get-JsonValue $inventory.extension_counts ".zsi"
        zar = Get-JsonValue $inventory.extension_counts ".zar"
        ctxb = Get-JsonValue $inventory.extension_counts ".ctxb"
        moflex = Get-JsonValue $inventory.extension_counts ".moflex"
        cmb = Get-JsonValue $inventory.extension_counts ".cmb"
        bcstm = Get-JsonValue $inventory.extension_counts ".bcstm"
    }
    container_counts = $inventory.container_counts
    model_counts = $inventory.model_counts
    conversion_readiness = $inventory.conversion_readiness
    parse_error_count = $inventory.parse_error_count
}

$summary | ConvertTo-Json -Depth 8 | Out-File -LiteralPath $summaryOutput -Encoding utf8

if ($Verify) {
    Assert-Condition ($summary.file_count -gt 0) "RomFS contains no files"
    Assert-Condition ($summary.extension_counts.zsi -gt 0) "RomFS contains no ZSI files"
    Assert-Condition ($summary.extension_counts.zar -gt 0) "RomFS contains no ZAR archives"
    Assert-Condition ($summary.extension_counts.ctxb -gt 0) "RomFS contains no CTXB files"
    if (-not $Shallow) {
        Assert-Condition ($summary.model_counts.discovered -gt 0) "deep inventory discovered no CMB models"
        Assert-Condition (
            $summary.model_counts.parsed -eq ($summary.model_counts.static_candidate + $summary.model_counts.nonstatic_candidate)
        ) "parsed model count does not match static + nonstatic counts"
        Assert-Condition ($summary.file_count -eq 1974) "expected 1974 RomFS files"
        Assert-Condition ($summary.extension_counts.zsi -eq 724) "expected 724 ZSI files"
        Assert-Condition ($summary.extension_counts.zar -eq 461) "expected 461 ZAR archives"
        Assert-Condition ($summary.extension_counts.ctxb -eq 588) "expected 588 CTXB files"
        Assert-Condition ($summary.extension_counts.moflex -eq 138) "expected 138 Moflex files"
        Assert-Condition ($summary.model_counts.discovered -eq 1998) "expected 1998 discovered CMB models"
        Assert-Condition ($summary.model_counts.parsed -eq 1998) "expected 1998 parsed CMB models"
        Assert-Condition ($summary.model_counts.static_candidate -eq 1508) "expected 1508 static CMB candidates"
        Assert-Condition ($summary.model_counts.nonstatic_candidate -eq 490) "expected 490 nonstatic CMB candidates"
        Assert-Condition ($summary.model_counts.rigid_export_candidate -eq 1796) "expected 1796 rigid export candidate CMB models"
        Assert-Condition ($summary.model_counts.rigid_multibone_candidate -eq 288) "expected 288 rigid multi-bone CMB models"
        Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 known parse errors"
    }
}

Write-Host "OOT3D inventory: $inventoryOutput"
Write-Host "OOT3D inventory summary: $summaryOutput"
