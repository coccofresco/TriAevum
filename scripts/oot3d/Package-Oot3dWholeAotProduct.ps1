[CmdletBinding()]
param(
    [string]$ProductExecutable =
        "I:\oot3dre_work\whole-aot-product-consumer\oot3d_native_game.exe",
    [string]$ProductBuildDirectory =
        "I:\oot3dre_work\whole-aot-product-consumer",
    [string]$ProcessManifest =
        "I:\oot3dre_work\native_game\oot3d_native_process_manifest.json",
    [string]$GraphicsConfig =
        "I:\oot3dre_work\native_game\oot3d_native_game.json",
    [string]$OutputRoot =
        "I:\oot3dre_work\whole-aot-product-packages",
    [string]$CacheRoot =
        "I:\oot3dre_work\oot3d-whole-aot-product-cache",
    [string]$PicaAotShaderPack = "",
    [string]$PackageName = "",
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$runtimeRoot = Join-Path $repoRoot "tools\oot3d\native_a32_runtime"
$productManifest = Join-Path $runtimeRoot "whole_aot_product_manifest.json"
$goldenContract = Join-Path $runtimeRoot "whole_aot_product_golden.json"
$scenarioCatalog = Join-Path $runtimeRoot "whole_aot_product_scenarios.json"
$launcher = Join-Path $PSScriptRoot "Start-Oot3dWholeAotProduct.ps1"
$shaderc = Join-Path $ProductBuildDirectory "shaderc_shared.dll"
$controllerDb = Join-Path $ProductBuildDirectory "gamecontrollerdb.txt"
$resourceRoot = Join-Path $repoRoot "runtime/three_ds_recomp\src\fast"
$controls = Join-Path $repoRoot "config\oot3d_controls.example.json"
$topScreen = Join-Path $repoRoot "config\topscreen_ui.example.json"
$topScreenOverrides = Join-Path $repoRoot `
    "tools\oot3d\ui_topscreen\assets\atlas_overrides.o3tu"
$documentation = Join-Path $repoRoot "docs\OOT3D_WHOLE_AOT_PRODUCT.md"
$guestMapTool = Join-Path $runtimeRoot "generate_whole_aot_guest_map.py"
$crashResolver = Join-Path $runtimeRoot "resolve_whole_aot_minidump.py"
$crashResolverLauncher = Join-Path $PSScriptRoot `
    "Resolve-Oot3dWholeAotCrash.ps1"
$nativePdb = Join-Path $ProductBuildDirectory "symbols\oot3d_native_game.pdb"
$nativeLinkMap = Join-Path $ProductBuildDirectory `
    "symbols\oot3d_native_game.map"
if([string]::IsNullOrWhiteSpace($PicaAotShaderPack)) {
    $PicaAotShaderPack = Join-Path $ProductBuildDirectory `
        "oot3d_pica_default.o3ps"
}
$PicaAotShaderPack = [System.IO.Path]::GetFullPath($PicaAotShaderPack)

foreach($required in @(
        $ProductExecutable,
        $ProcessManifest,
        $GraphicsConfig,
        $productManifest,
        $goldenContract,
        $scenarioCatalog,
        $launcher,
        $shaderc,
        $controllerDb,
        $resourceRoot,
        $controls,
        $topScreen,
        $topScreenOverrides,
        $documentation,
        $guestMapTool,
        $crashResolver,
        $crashResolverLauncher,
        $nativePdb,
        $nativeLinkMap,
        $PicaAotShaderPack)) {
    if(-not (Test-Path -LiteralPath $required)) {
        throw "Required whole-AOT package input is missing: $required"
    }
}

$keyExpression = @"
import sys
from pathlib import Path
sys.path.insert(0, str(Path(r'$runtimeRoot')))
from prepare_whole_aot_product import product_cache_key
print(product_cache_key(Path(r'$productManifest')))
"@
$cacheKey = (& python -c $keyExpression).Trim()
if($LASTEXITCODE -ne 0 -or $cacheKey -notmatch '^[0-9a-f]{64}$') {
    throw "Unable to calculate the whole-AOT product cache key"
}
if([string]::IsNullOrWhiteSpace($PackageName)) {
    $PackageName = "oot3d-whole-aot-$($cacheKey.Substring(0, 12))"
}
if($PackageName.IndexOfAny([System.IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    throw "PackageName contains invalid path characters"
}

$resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$output = [System.IO.Path]::GetFullPath(
    (Join-Path $resolvedOutputRoot $PackageName))
$outputPrefix = $resolvedOutputRoot.TrimEnd('\', '/') +
    [System.IO.Path]::DirectorySeparatorChar
if(-not $output.StartsWith(
        $outputPrefix,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Package output escapes OutputRoot: $output"
}
if($output -eq [System.IO.Path]::GetPathRoot($output)) {
    throw "Refusing to package into a volume root"
}
if(Test-Path -LiteralPath $output) {
    if(-not $Force.IsPresent) {
        throw "Package already exists; pass -Force to replace it: $output"
    }
    [System.IO.Directory]::Delete($output, $true)
}

$staging = "$output.staging-$PID"
if(Test-Path -LiteralPath $staging) {
    if(-not $staging.StartsWith(
            $outputPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Package staging path escapes OutputRoot: $staging"
    }
    [System.IO.Directory]::Delete($staging, $true)
}
foreach($directory in @(
        $staging,
        (Join-Path $staging "config"),
        (Join-Path $staging "game"),
        (Join-Path $staging "metadata"),
        (Join-Path $staging "savedata"),
        (Join-Path $staging "symbols"))) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

$sourceManifest = Get-Content -LiteralPath $ProcessManifest -Raw |
    ConvertFrom-Json
$gameInputs = [ordered]@{
    code_bin = [string]$sourceManifest.source.code_bin_path
    exheader = [string]$sourceManifest.source.exheader_path
    romfs = [string]$sourceManifest.source.romfs_image_path
}
foreach($entry in $gameInputs.GetEnumerator()) {
    if(-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
        throw "Original game input is missing: $($entry.Value)"
    }
}

Copy-Item -LiteralPath $ProductExecutable -Destination (
    Join-Path $staging "oot3d_native_game.exe") -Force
Copy-Item -LiteralPath $shaderc -Destination $staging -Force
Copy-Item -LiteralPath $controllerDb -Destination $staging -Force
Copy-Item -LiteralPath $launcher -Destination (
    Join-Path $staging "Start-Oot3d.ps1") -Force
Copy-Item -LiteralPath $PicaAotShaderPack -Destination (
    Join-Path $staging "oot3d_pica_default.o3ps") -Force
Copy-Item -LiteralPath $crashResolverLauncher -Destination (
    Join-Path $staging "Resolve-Crash.ps1") -Force
Copy-Item -LiteralPath $resourceRoot -Destination (
    Join-Path $staging "resources") -Recurse -Force
Copy-Item -LiteralPath $GraphicsConfig -Destination (
    Join-Path $staging "config\oot3d_native_game.json") -Force
Copy-Item -LiteralPath $controls -Destination (
    Join-Path $staging "config\oot3d_controls.json") -Force
Copy-Item -LiteralPath $topScreen -Destination (
    Join-Path $staging "config\topscreen_ui.json") -Force
Copy-Item -LiteralPath $topScreenOverrides -Destination (
    Join-Path $staging "config\atlas_overrides.o3tu") -Force
Copy-Item -LiteralPath $documentation -Destination (
    Join-Path $staging "README.md") -Force
Copy-Item -LiteralPath $productManifest -Destination (
    Join-Path $staging "metadata\whole_aot_product_manifest.json") -Force
Copy-Item -LiteralPath $goldenContract -Destination (
    Join-Path $staging "metadata\whole_aot_product_golden.json") -Force
Copy-Item -LiteralPath $scenarioCatalog -Destination (
    Join-Path $staging "metadata\whole_aot_product_scenarios.json") -Force

$artifactRoot = Join-Path $CacheRoot $cacheKey
$archiveMetadata = Join-Path $artifactRoot "whole_aot_archive.json"
$aotProgram = Join-Path $artifactRoot "aot_program.json"
$generatedManifest = Join-Path $artifactRoot `
    "cpp\whole_aot_cpp_manifest.json"
foreach($artifact in @($archiveMetadata, $aotProgram, $generatedManifest)) {
    if(-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
        throw "Whole-AOT package metadata is missing: $artifact"
    }
}
Copy-Item -LiteralPath $archiveMetadata -Destination (
    Join-Path $staging "metadata\whole_aot_archive.json") -Force
$pdbArchive = Join-Path $staging "symbols\oot3d_native_game.pdb.zip"
$linkMapArchive = Join-Path $staging "symbols\oot3d_native_game.map.zip"
Compress-Archive -LiteralPath $nativePdb -DestinationPath $pdbArchive `
    -CompressionLevel Optimal -Force
Compress-Archive -LiteralPath $nativeLinkMap -DestinationPath $linkMapArchive `
    -CompressionLevel Optimal -Force
Copy-Item -LiteralPath $crashResolver -Destination (
    Join-Path $staging "symbols\resolve_whole_aot_minidump.py") -Force
$guestMapPath = Join-Path $staging "symbols\whole_aot_guest_map.json"
& python $guestMapTool --program $aotProgram `
    --generated-manifest $generatedManifest `
    --link-map $nativeLinkMap `
    --executable $ProductExecutable `
    --output $guestMapPath
if($LASTEXITCODE -ne 0) {
    throw "Unable to generate the whole-AOT guest address map"
}
$symbolMetadata = [ordered]@{
    format = "oot3d_whole_aot_symbols_v1"
    executable = [ordered]@{
        bytes = (Get-Item -LiteralPath $ProductExecutable).Length
        sha256 = (Get-FileHash -LiteralPath $ProductExecutable `
            -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    pdb = [ordered]@{
        archive = "oot3d_native_game.pdb.zip"
        entry = "oot3d_native_game.pdb"
        bytes = (Get-Item -LiteralPath $nativePdb).Length
        sha256 = (Get-FileHash -LiteralPath $nativePdb `
            -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    link_map = [ordered]@{
        archive = "oot3d_native_game.map.zip"
        entry = "oot3d_native_game.map"
        bytes = (Get-Item -LiteralPath $nativeLinkMap).Length
        sha256 = (Get-FileHash -LiteralPath $nativeLinkMap `
            -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    guest_map = [ordered]@{
        file = "whole_aot_guest_map.json"
        bytes = (Get-Item -LiteralPath $guestMapPath).Length
        sha256 = (Get-FileHash -LiteralPath $guestMapPath `
            -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$symbolMetadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (
    Join-Path $staging "symbols\symbols.json") -Encoding utf8

Copy-Item -LiteralPath $gameInputs.code_bin -Destination (
    Join-Path $staging "game\code.bin") -Force
Copy-Item -LiteralPath $gameInputs.exheader -Destination (
    Join-Path $staging "game\exheader.bin") -Force
Copy-Item -LiteralPath $gameInputs.romfs -Destination (
    Join-Path $staging "game\romfs.bin") -Force

$sourceManifest.source.code_bin_path = "game\code.bin"
$sourceManifest.source.exheader_path = "game\exheader.bin"
$sourceManifest.source.romfs_image_path = "game\romfs.bin"
$sourceManifest | ConvertTo-Json -Depth 30 |
    Set-Content -LiteralPath (
        Join-Path $staging "game\oot3d_native_process_manifest.json") `
        -Encoding utf8

$inventory = @()
Get-ChildItem -LiteralPath $staging -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $relative = [System.IO.Path]::GetRelativePath(
            $staging, $_.FullName).Replace('\', '/')
        $inventory += [ordered]@{
            path = $relative
            bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName `
                -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
$packageMetadata = [ordered]@{
    format = "oot3d_whole_aot_local_package_v1"
    created_utc = [DateTime]::UtcNow.ToString("O")
    cache_key = $cacheKey
    original_game_data_included = $true
    redistribution_allowed = $false
    executable_sha256 =
        (Get-FileHash -LiteralPath $ProductExecutable `
            -Algorithm SHA256).Hash.ToLowerInvariant()
    file_count = $inventory.Count
    package_bytes =
        [uint64](($inventory | ForEach-Object { $_['bytes'] } |
            Measure-Object -Sum).Sum)
    files = $inventory
}
$packageMetadataPath = Join-Path $staging "metadata\package.json"
$packageMetadata | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $packageMetadataPath -Encoding utf8

Move-Item -LiteralPath $staging -Destination $output
[ordered]@{
    status = "packaged"
    package = $output
    cache_key = $cacheKey
    files = $packageMetadata.file_count + 1
    bytes = $packageMetadata.package_bytes +
        (Get-Item -LiteralPath (
            Join-Path $output "metadata\package.json")).Length
    launcher = Join-Path $output "Start-Oot3d.ps1"
} | ConvertTo-Json -Depth 4
