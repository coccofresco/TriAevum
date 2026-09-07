[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,
    [ValidateRange(2, 1000000)]
    [int]$Frames = 301,
    [ValidateRange(1, 1000000)]
    [int]$ScreenshotFrame = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = [System.IO.Path]::GetFullPath($PackageDirectory)
$launcher = Join-Path $root "Start-Oot3d.ps1"
$metadataPath = Join-Path $root "metadata\package.json"
$processManifestPath =
    Join-Path $root "game\oot3d_native_process_manifest.json"
$pdbArchive = Join-Path $root "symbols\oot3d_native_game.pdb.zip"
$linkMapArchive = Join-Path $root "symbols\oot3d_native_game.map.zip"
$guestMapPath = Join-Path $root "symbols\whole_aot_guest_map.json"
$symbolMetadataPath = Join-Path $root "symbols\symbols.json"
$crashResolver = Join-Path $root "symbols\resolve_whole_aot_minidump.py"
$crashResolverLauncher = Join-Path $root "Resolve-Crash.ps1"
$productContractPath = Join-Path $root `
    "metadata\whole_aot_product_manifest.json"
$goldenContractPath = Join-Path $root `
    "metadata\whole_aot_product_golden.json"
$scenarioCatalogPath = Join-Path $root `
    "metadata\whole_aot_product_scenarios.json"
foreach($required in @(
        $launcher,
        $metadataPath,
        $processManifestPath,
        $pdbArchive,
        $linkMapArchive,
        $guestMapPath,
        $symbolMetadataPath,
        $crashResolver,
        $crashResolverLauncher,
        $productContractPath,
        $goldenContractPath,
        $scenarioCatalogPath)) {
    if(-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required packaged product file is missing: $required"
    }
}
if($ScreenshotFrame -ge $Frames) {
    throw "ScreenshotFrame must be lower than Frames"
}

$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
if([string]$metadata.format -ne "oot3d_whole_aot_local_package_v1") {
    throw "Unsupported whole-AOT package metadata: $metadataPath"
}
$inventoryFailures = @()
foreach($entry in @($metadata.files)) {
    $path = [System.IO.Path]::GetFullPath(
        (Join-Path $root ([string]$entry.path)))
    $rootPrefix = $root.TrimEnd('\', '/') +
        [System.IO.Path]::DirectorySeparatorChar
    if(-not $path.StartsWith(
            $rootPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        $inventoryFailures += "path_escape:$($entry.path)"
        continue
    }
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $inventoryFailures += "missing:$($entry.path)"
        continue
    }
    $item = Get-Item -LiteralPath $path
    if($item.Length -ne [long]$entry.bytes) {
        $inventoryFailures += "size:$($entry.path)"
        continue
    }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if($hash -ne [string]$entry.sha256) {
        $inventoryFailures += "sha256:$($entry.path)"
    }
}
if($inventoryFailures.Count -ne 0) {
    throw "Whole-AOT package inventory failed: $($inventoryFailures -join ', ')"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
function Test-SymbolArchive {
    param(
        [Parameter(Mandatory = $true)][string]$Archive,
        [Parameter(Mandatory = $true)][pscustomobject]$Expected
    )
    $zip = [System.IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $entries = @($zip.Entries | Where-Object {
            $_.FullName -eq [string]$Expected.entry
        })
        if($entries.Count -ne 1 -or
           [uint64]$entries[0].Length -ne [uint64]$Expected.bytes) {
            throw "Symbol archive content differs: $Archive"
        }
        $stream = $entries[0].Open()
        try {
            $sha = [System.Security.Cryptography.SHA256]::Create()
            try {
                $hash = [Convert]::ToHexString(
                    $sha.ComputeHash($stream)).ToLowerInvariant()
            } finally {
                $sha.Dispose()
            }
        } finally {
            $stream.Dispose()
        }
        if($hash -ne [string]$Expected.sha256) {
            throw "Symbol archive SHA-256 differs: $Archive"
        }
    } finally {
        $zip.Dispose()
    }
}

$symbolMetadata = Get-Content -LiteralPath $symbolMetadataPath -Raw |
    ConvertFrom-Json
if([string]$symbolMetadata.format -ne "oot3d_whole_aot_symbols_v1") {
    throw "Unsupported packaged symbol metadata"
}
$packagedExecutable = Join-Path $root "oot3d_native_game.exe"
if((Get-FileHash -LiteralPath $packagedExecutable -Algorithm SHA256).Hash -ne
   [string]$symbolMetadata.executable.sha256) {
    throw "Packaged symbols refer to a different executable"
}
Test-SymbolArchive -Archive $pdbArchive -Expected $symbolMetadata.pdb
Test-SymbolArchive -Archive $linkMapArchive -Expected $symbolMetadata.link_map
if((Get-FileHash -LiteralPath $guestMapPath -Algorithm SHA256).Hash -ne
   [string]$symbolMetadata.guest_map.sha256) {
    throw "Packaged guest map differs from symbol metadata"
}

$guestMap = Get-Content -LiteralPath $guestMapPath -Raw | ConvertFrom-Json
$productContract = Get-Content -LiteralPath $productContractPath -Raw |
    ConvertFrom-Json
$goldenContract = Get-Content -LiteralPath $goldenContractPath -Raw |
    ConvertFrom-Json
$scenarioCatalog = Get-Content -LiteralPath $scenarioCatalogPath -Raw |
    ConvertFrom-Json
if([string]$goldenContract.format -ne
   "oot3d_whole_aot_product_golden_v1" -or
   @($goldenContract.scenarios).Count -lt 3) {
    throw "Packaged whole-AOT golden contract is incomplete"
}
if([string]$scenarioCatalog.format -ne
       "oot3d_whole_aot_scenario_catalog_v1" -or
   @($scenarioCatalog.scenarios).Count -lt 8) {
    throw "Packaged whole-AOT scenario catalog is incomplete"
}
if([string]$guestMap.format -ne "oot3d_whole_aot_guest_map_v1" -or
   [uint64]$guestMap.function_count -ne
       [uint64]$productContract.closure.counts.selected_functions -or
   [uint64]$guestMap.dispatch_entry_count -ne
       [uint64]$productContract.closure.counts.dispatcher_entries -or
   [uint64]$guestMap.shard_count -ne
       [uint64]$productContract.codegen.shard_count -or
   [uint64]$guestMap.native_symbol_count -ne
       [uint64]$guestMap.function_count -or
   [uint64]$guestMap.native_extent_count -ne
       [uint64]$guestMap.function_count -or
   [uint64]$guestMap.native_image_size -eq 0 -or
   [uint64]$guestMap.native_image_timestamp -eq 0 -or
   @($guestMap.functions).Count -ne [uint64]$guestMap.function_count) {
    throw "Packaged whole-AOT guest address map is incomplete"
}
$processEntry = @($guestMap.functions | Where-Object {
    [uint64]$_.entry -eq 0x00100000
})
if($processEntry.Count -ne 1 -or
   [uint64]$processEntry[0].native_rva -eq 0 -or
   [string]$processEntry[0].native_symbol -notmatch
       'Execute_oot3d_process_entry_00100000$') {
    throw "Packaged guest map does not resolve the process entry"
}

$processManifest = Get-Content -LiteralPath $processManifestPath -Raw |
    ConvertFrom-Json
$gamePaths = @(
    [string]$processManifest.source.code_bin_path,
    [string]$processManifest.source.exheader_path,
    [string]$processManifest.source.romfs_image_path)
$absoluteGamePaths = @($gamePaths | Where-Object {
    [System.IO.Path]::IsPathRooted($_)
})
if($absoluteGamePaths.Count -ne 0) {
    throw "Packaged process manifest contains absolute game paths"
}
foreach($relativePath in $gamePaths) {
    $resolved = [System.IO.Path]::GetFullPath((Join-Path $root $relativePath))
    if(-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Packaged game input is missing: $resolved"
    }
}

$logs = Join-Path $root "logs\package-validation"
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$runtimePath = Join-Path $logs "runtime.json"
$screenshotPath = Join-Path $logs "frame.bmp"
& $launcher -Frames $Frames -FixedDeltaSeconds (1.0 / 60.0) `
    -PresentationRate free -DisableAudio `
    -Screenshot $screenshotPath -ScreenshotStartFrame $ScreenshotFrame `
    -Output $runtimePath
if($LASTEXITCODE -ne 0) {
    throw "Packaged whole-AOT runtime exited with code $LASTEXITCODE"
}

$runtime = Get-Content -LiteralPath $runtimePath -Raw | ConvertFrom-Json
$strictCounters = [ordered]@{
    retained_arm_fallbacks =
        [uint64]$runtime.compiled_functions.retained_arm_fallbacks
    whole_aot_memory_faults =
        [uint64]$runtime.compiled_functions.whole_aot_memory_faults
    whole_aot_unsupported_exits =
        [uint64]$runtime.compiled_functions.whole_aot_unsupported_exits
    mass_aot_block_limit_exits =
        [uint64]$runtime.compiled_functions.mass_aot_block_limit_exits
}
$strictFailures = @($strictCounters.GetEnumerator() | Where-Object {
    $_.Value -ne 0
})
if($strictFailures.Count -ne 0 -or
   -not [bool]$runtime.compiled_functions.whole_aot_available -or
   -not [bool]$runtime.compiled_functions.whole_aot_enabled) {
    throw "Packaged runtime violated the strict whole-AOT contract"
}
if([uint64]$runtime.run_frames -ne [uint64]$Frames) {
    throw "Packaged runtime did not complete all requested frames"
}

$screenshotBytes = [System.IO.File]::ReadAllBytes($screenshotPath)
$nonzeroFramebufferByte = $false
for($index = 54; $index -lt $screenshotBytes.Length; ++$index) {
    if($screenshotBytes[$index] -ne 0) {
        $nonzeroFramebufferByte = $true
        break
    }
}
if($screenshotBytes.Length -le 54 -or -not $nonzeroFramebufferByte) {
    throw "Packaged runtime produced an empty framebuffer checkpoint"
}

$summary = [ordered]@{
    format = "oot3d_whole_aot_local_package_validation_v1"
    passed = $true
    package = $root
    cache_key = [string]$metadata.cache_key
    inventory_files = @($metadata.files).Count
    inventory_bytes = [uint64]$metadata.package_bytes
    relative_game_paths = $gamePaths
    frames = [uint64]$runtime.run_frames
    screenshot_frame = $ScreenshotFrame
    screenshot_sha256 =
        (Get-FileHash -LiteralPath $screenshotPath `
            -Algorithm SHA256).Hash.ToLowerInvariant()
    strict_counters = $strictCounters
    whole_aot_calls = [uint64]$runtime.compiled_functions.whole_aot_calls
    direct_calls = [uint64]$runtime.compiled_functions.whole_aot_direct_calls
    indirect_calls =
        [uint64]$runtime.compiled_functions.whole_aot_indirect_calls
    external_calls =
        [uint64]$runtime.compiled_functions.whole_aot_external_calls
}
$summaryPath = Join-Path $logs "package-validation.json"
$summary | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $summaryPath -Encoding utf8
$summary | ConvertTo-Json -Depth 6
