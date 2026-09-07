param(
    [string]$OutDir = "build/matched",
    [string]$SourceList = "metadata/matched_sources.txt",
    [string[]]$Source,
    [ValidateSet("gcc", "armcc", "auto")]
    [string]$Compiler = "gcc",
    [string]$Optimization = "-O2",
    [string]$ArmccOptimization = "-Otime",
    [string]$ArmccPath,
    [ValidateSet("system", "all")]
    [string]$ArmccSearch = "system",
    [string[]]$ExtraCFlag,
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

function Find-UnprefixedTool {
    param(
        [string]$Name,
        [string[]]$Roots
    )

    $names = @($Name)
    if (-not $Name.EndsWith(".exe", [StringComparison]::OrdinalIgnoreCase)) {
        $names += "$Name.exe"
    }

    foreach ($root in $Roots) {
        if (-not $root) {
            continue
        }
        foreach ($base in @($root, (Join-Path $root "bin"), (Join-Path $root "bin64"), (Join-Path $root "win_32-pentium"))) {
            foreach ($candidateName in $names) {
                $candidate = Join-Path $base $candidateName
                if (Test-Path -LiteralPath $candidate) {
                    return $candidate
                }
            }
        }
    }

    foreach ($candidateName in $names) {
        $cmd = Get-Command $candidateName -ErrorAction SilentlyContinue
        if ($cmd) {
            return $cmd.Source
        }
    }

    throw "Could not find $Name."
}

function Find-ArmccTool {
    param(
        [string]$Name,
        [string]$ArmccPath,
        [string]$Search
    )

    $roots = @()
    if ($ArmccPath) {
        $roots += $ArmccPath
    }
    if ($env:ARMCC_PATH) {
        $roots += $env:ARMCC_PATH
    }
    $roots += @(
        "C:\Program Files (x86)\ARM_Compiler_5.06u7",
        "C:\Program Files\ARM_Compiler_5.06u7",
        "C:\Program Files (x86)\Arm Compiler 5.06u7",
        "C:\Program Files\Arm Compiler 5.06u7",
        "C:\Keil_v5\ARM\ARMCC",
        "C:\Keil\ARM\ARMCC"
    )
    if ($env:LOCALAPPDATA) {
        $roots += (Join-Path $env:LOCALAPPDATA "Keil_v5\ARM\ARMCC")
    }
    if ($Search -eq "all") {
        $roots += (Join-Path $repoRoot "..\tools\rvct40_591")
    }

    return Find-UnprefixedTool -Name $Name -Roots $roots
}

function Test-ArmccLicense {
    param([string]$Armcc)

    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $Armcc --vsn 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
    if ($exitCode -eq 0) {
        $oldErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            $cpuOutput = & $Armcc --cpu=list 2>&1
            $cpuExitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $oldErrorActionPreference
        }
        if ($cpuExitCode -ne 0 -or -not (($cpuOutput | Out-String) -match "(?i)\bMPCore\b")) {
            throw "ARMCC is license-ready but target-incompatible: --cpu=MPCore is not supported by $Armcc."
        }
        return $true
    }

    $nonEmptyOutput = @($output | Where-Object { $_ -and $_.ToString().Trim().Length -gt 0 })
    $message = ($nonEmptyOutput | Where-Object {
        $_.ToString() -match "(?i)(C\d+E|license|cannot obtain|failed to check out)"
    } | Select-Object -First 1)
    if (-not $message) {
        $message = ($nonEmptyOutput | Select-Object -First 1)
    }
    if (-not $message) {
        $message = "armcc --vsn failed with exit code $exitCode"
    }
    throw "ARMCC is installed but not license-ready: $message. Set ARMLMD_LICENSE_FILE or LM_LICENSE_FILE to a valid ARMCC/RVCT license."
}

function Read-SourceList {
    param(
        [string]$RepoRoot,
        [string]$SourceList,
        [string[]]$Source
    )

    if ($Source -and $Source.Count -gt 0) {
        return $Source
    }

    $sourceListPath = Join-Path $RepoRoot $SourceList
    if (-not (Test-Path -LiteralPath $sourceListPath)) {
        throw "Source list not found: $sourceListPath"
    }

    $sources = @()
    foreach ($line in Get-Content -LiteralPath $sourceListPath) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#")) {
            continue
        }
        $sources += $trimmed
    }

    if ($sources.Count -eq 0) {
        throw "No sources found in $sourceListPath"
    }

    return $sources
}

function Get-ObjectStem {
    param([string]$Source)

    $withoutExtension = $Source -replace '\.[^\\/\.]+$', ''
    return ($withoutExtension -replace '[\\/:\s]+', '_')
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outPath = Join-Path $repoRoot $OutDir
New-Item -ItemType Directory -Force -Path $outPath | Out-Null

$objdump = Find-Tool -Name "objdump" -ToolPrefix $ToolPrefix -ToolRoot $ToolRoot
$requestedCompiler = $Compiler
$compilerExe = $null
$compilerKind = $Compiler
$armccProbeError = $null

if ($Compiler -eq "armcc" -or $Compiler -eq "auto") {
    try {
        $armcc = Find-ArmccTool -Name "armcc" -ArmccPath $ArmccPath -Search $ArmccSearch
        Test-ArmccLicense -Armcc $armcc | Out-Null
        $compilerKind = "armcc"
        $compilerExe = $armcc
    } catch {
        $armccProbeError = $_.Exception.Message
        if ($Compiler -eq "armcc") {
            throw
        }
    }
}

if (-not $compilerExe) {
    $compilerKind = "gcc"
    $compilerExe = Find-Tool -Name "gcc" -ToolPrefix $ToolPrefix -ToolRoot $ToolRoot
}

$toolDirs = @(
    [IO.Path]::GetDirectoryName($compilerExe),
    [IO.Path]::GetDirectoryName($objdump)
) | Select-Object -Unique
foreach ($toolDir in $toolDirs) {
    if ($toolDir -and ($env:Path -notlike "*$toolDir*")) {
        $env:Path = "$toolDir;$env:Path"
    }
}

if ($compilerKind -eq "armcc") {
    $commonFlags = @(
        "--apcs=//interwork",
        "--cpu=MPCore",
        "--fpmode=fast",
        "--c99",
        "--arm",
        "--signed_chars",
        "--multibyte-chars",
        "--locale=japanese",
        $ArmccOptimization,
        "--data-reorder",
        "--split_sections",
        "-D__3DS__",
        "-I", (Join-Path $repoRoot "include")
    )
} else {
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
}
if ($ExtraCFlag -and $ExtraCFlag.Count -gt 0) {
    $commonFlags += $ExtraCFlag
}

function Add-FlatString {
    param(
        [object]$Value,
        [System.Collections.ArrayList]$Output
    )

    if ($null -eq $Value) {
        return
    }
    if ($Value -is [System.Array]) {
        foreach ($nestedValue in $Value) {
            Add-FlatString -Value $nestedValue -Output $Output
        }
        return
    }
    [void]$Output.Add([string]$Value)
}

$rawSources = Read-SourceList -RepoRoot $repoRoot -SourceList $SourceList -Source $Source
$normalizedSources = [System.Collections.ArrayList]::new()
Add-FlatString -Value $rawSources -Output $normalizedSources
$sources = @($normalizedSources.ToArray())

$objects = @()
foreach ($sourceEntry in $sources) {
    $srcPath = Join-Path $repoRoot $sourceEntry
    if (-not (Test-Path -LiteralPath $srcPath)) {
        throw "Source not found: $srcPath"
    }

    $stem = Get-ObjectStem -Source $sourceEntry
    $objPath = Join-Path $outPath "$stem.o"
    $dumpPath = Join-Path $outPath "$stem.dump"

    & $compilerExe @commonFlags -c $srcPath -o $objPath
    if ($LASTEXITCODE -ne 0) {
        throw "$compilerKind failed for $sourceEntry with exit code $LASTEXITCODE"
    }
    if (-not (Test-Path -LiteralPath $objPath)) {
        throw "$compilerKind did not create expected object $objPath"
    }

    & $objdump -dr $objPath | Set-Content -LiteralPath $dumpPath -Encoding ascii
    if ($LASTEXITCODE -ne 0) {
        throw "objdump failed for $objPath with exit code $LASTEXITCODE"
    }
    $objects += $objPath
}

$manifest = [ordered]@{
    compiler = $compilerKind
    requested_compiler = $requestedCompiler
    compiler_executable = $compilerExe
    armcc_probe_error = $armccProbeError
    armcc_search = $ArmccSearch
    tool_root = $ToolRoot
    tool_prefix = $ToolPrefix
    objdump = $objdump
    optimization = if ($compilerKind -eq "armcc") { $ArmccOptimization } else { $Optimization }
    flags = $commonFlags
    source_list = $SourceList
    sources = $sources
    objects = $objects
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outPath "manifest.json") -Encoding ascii

if (-not $NoCompare) {
    python (Join-Path $PSScriptRoot "compare_runtime_objects.py") `
        --target-disassembly (Join-Path $repoRoot "ghidra_export/disassembly.txt") `
        --compiled-dir $outPath `
        --manual-symbols (Join-Path $repoRoot "symbols/manual_symbols.csv") `
        --out-json (Join-Path $outPath "compare_matched_objects.json") `
        --out-md (Join-Path $outPath "compare_matched_objects.md") `
        --title "Maintained Object Comparison"
}

Write-Host "Built matched objects in $outPath using $compilerKind"
