param(
    [string]$OutDir = "build/runtime",
    [string]$Optimization = "-O2",
    [string]$ToolRoot,
    [string]$ToolPrefix = "arm-none-eabi",
    [switch]$NoCompare
)

$ErrorActionPreference = "Stop"

function Find-Tool {
    param(
        [string]$Name,
        [string]$ToolPrefix,
        [string]$ToolRoot
    )

    if ($ToolRoot) {
        $candidate = Join-Path $ToolRoot "bin\$ToolPrefix-$Name.exe"
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
        throw "Could not find $ToolPrefix-$Name under ToolRoot: $ToolRoot"
    }

    if ($env:DEVKITARM) {
        $candidate = Join-Path $env:DEVKITARM "bin\$ToolPrefix-$Name.exe"
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $devkitProCandidate = "C:\devkitPro\devkitARM\bin\$ToolPrefix-$Name.exe"
    if (Test-Path -LiteralPath $devkitProCandidate) {
        return $devkitProCandidate
    }

    $cmd = Get-Command "$ToolPrefix-$Name" -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    foreach ($msysPrefix in @("mingw64", "ucrt64", "clang64")) {
        $msysCandidate = "C:\msys64\$msysPrefix\bin\$ToolPrefix-$Name.exe"
        if (Test-Path -LiteralPath $msysCandidate) {
            return $msysCandidate
        }
    }

    throw "Could not find $ToolPrefix-$Name. Install devkitPro/devkitARM, set DEVKITARM, or pass -ToolRoot."
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outPath = Join-Path $repoRoot $OutDir
New-Item -ItemType Directory -Force -Path $outPath | Out-Null

$gcc = Find-Tool -Name "gcc" -ToolPrefix $ToolPrefix -ToolRoot $ToolRoot
$objdump = Find-Tool -Name "objdump" -ToolPrefix $ToolPrefix -ToolRoot $ToolRoot
$toolDirs = @(
    [IO.Path]::GetDirectoryName($gcc),
    [IO.Path]::GetDirectoryName($objdump)
) | Select-Object -Unique
foreach ($toolDir in $toolDirs) {
    if ($toolDir -and ($env:Path -notlike "*$toolDir*")) {
        $env:Path = "$toolDir;$env:Path"
    }
}

$archFlags = @(
    "-march=armv6k",
    "-mtune=mpcore",
    "-mfpu=vfp",
    "-mfloat-abi=hard",
    "-mtp=soft"
)

$commonFlags = @(
    "-std=gnu99",
    $Optimization,
    "-g",
    "-Wall",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-common",
    "-mword-relocations",
    "-ffunction-sections",
    "-fdata-sections",
    "-D__3DS__",
    "-I", (Join-Path $repoRoot "include")
) + $archFlags

$sources = @(
    "src/startup/clear_bss.c",
    "src/runtime/runtime_helpers.c",
    "src/runtime/matrix_helpers.c"
)

$objects = @()
foreach ($source in $sources) {
    $srcPath = Join-Path $repoRoot $source
    $stem = [IO.Path]::GetFileNameWithoutExtension($source)
    $objPath = Join-Path $outPath "$stem.o"
    $dumpPath = Join-Path $outPath "$stem.dump"

    & $gcc @commonFlags -c $srcPath -o $objPath
    if ($LASTEXITCODE -ne 0) {
        throw "gcc failed for $source with exit code $LASTEXITCODE"
    }
    if (-not (Test-Path -LiteralPath $objPath)) {
        throw "gcc did not create expected object $objPath"
    }

    & $objdump -dr $objPath | Set-Content -LiteralPath $dumpPath -Encoding ascii
    if ($LASTEXITCODE -ne 0) {
        throw "objdump failed for $objPath with exit code $LASTEXITCODE"
    }
    $objects += $objPath
}

$manifest = [ordered]@{
    gcc = $gcc
    objdump = $objdump
    tool_root = $ToolRoot
    tool_prefix = $ToolPrefix
    optimization = $Optimization
    flags = $commonFlags
    sources = $sources
    objects = $objects
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outPath "manifest.json") -Encoding ascii

if (-not $NoCompare) {
    python (Join-Path $PSScriptRoot "compare_runtime_objects.py") `
        --target-disassembly (Join-Path $repoRoot "ghidra_export/disassembly.txt") `
        --compiled-dir $outPath `
        --manual-symbols (Join-Path $repoRoot "symbols/manual_symbols.csv") `
        --out-json (Join-Path $outPath "compare_runtime_objects.json") `
        --out-md (Join-Path $outPath "compare_runtime_objects.md")
}

Write-Host "Built runtime objects in $outPath"
