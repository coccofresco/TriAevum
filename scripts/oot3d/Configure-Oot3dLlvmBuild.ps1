[CmdletBinding()]
param(
    [string]$LlvmRoot = "I:\oot3dre_tools\llvm-22.1.6",
    [string]$BuildDirectory = "I:\oot3dre_work\build-llvm-22.1.6",
    [string]$CodeBin = "",
    [switch]$RebuildWholeAot,
    [switch]$Force,
    [ValidateRange(1, 64)]
    [int]$WholeAotParallelCompiles = 4
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $RebuildWholeAot.IsPresent) {
    throw @"
The isolated LLVM development build cannot consume the historical unaudited
whole-AOT prebuilt after the generated/runtime ABI change. Pass
-RebuildWholeAot to regenerate and compile the shards in this build tree. For
the authoritative content-addressed Windows product, run
Build-Oot3dWholeAotProduct.ps1 followed by
Configure-Oot3dWholeAotProduct.ps1.
"@
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$llvmRootPath = [System.IO.Path]::GetFullPath($LlvmRoot)
$buildPath = [System.IO.Path]::GetFullPath($BuildDirectory)
$toolchain = Join-Path $repoRoot "cmake\Oot3dLlvmClToolchain.cmake"
$vcpkgRoot = Join-Path $repoRoot "build-codex\vcpkg"
$vcpkgToolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
$validatedA32Generated = Join-Path $repoRoot "build-codex\oot3d_a32_generated"
$manifest = Join-Path $repoRoot "tools\oot3d\operational_inputs.json"
$inputs = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
$cmake = [string]$inputs.variables.cmakeExe
$vsDevShell = [string]$inputs.variables.vsDevShell
$clangCl = Join-Path $llvmRootPath "bin\clang-cl.exe"

$operationalVariables = @{}
foreach ($property in $inputs.variables.PSObject.Properties) {
    $operationalVariables[$property.Name] = [string]$property.Value
}
$operationalVariables["repoRoot"] = $repoRoot

function Expand-Oot3dOperationalPath {
    param([string]$Path)

    $expanded = $Path
    for ($pass = 0; $pass -le $operationalVariables.Count; $pass++) {
        $previous = $expanded
        foreach ($key in $operationalVariables.Keys) {
            $expanded = $expanded.Replace(
                ('${' + $key + '}'),
                [string]$operationalVariables[$key])
        }
        if ($expanded -eq $previous) {
            return [Environment]::ExpandEnvironmentVariables($expanded)
        }
    }
    throw "Operational input variables contain a substitution cycle: $Path"
}

if ([string]::IsNullOrWhiteSpace($CodeBin)) {
    $codeEntry = @($inputs.entries | Where-Object {
            [string]$_.id -eq "oot3d_code_bin"
        } | Select-Object -First 1)
    if ($codeEntry.Count -ne 1) {
        throw "Operational input oot3d_code_bin is missing"
    }
    $CodeBin = Expand-Oot3dOperationalPath ([string]$codeEntry[0].path)
}
$codeBinPath = [System.IO.Path]::GetFullPath($CodeBin)

foreach ($required in @(
        $cmake, $vsDevShell, $clangCl, $toolchain, $vcpkgToolchain)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required LLVM build input is missing: $required"
    }
}
if (-not (Test-Path -LiteralPath $validatedA32Generated -PathType Container)) {
    throw "Validated A32 generated directory is missing: $validatedA32Generated"
}

if (-not (Test-Path -LiteralPath $codeBinPath -PathType Leaf)) {
    throw "Required LLVM build input is missing: $codeBinPath"
}
$wholeAotGenerated = Join-Path $buildPath "oot3d_whole_aot_cpp"
$generator = Join-Path $repoRoot `
    "tools\oot3d\native_a32_runtime\generate_whole_aot_cpp.py"
& python $generator `
    --program (Join-Path $repoRoot "build-codex\oot3d_whole_aot\aot_program.json") `
    --selection (Join-Path $repoRoot `
        "tools\oot3d\native_a32_runtime\whole_aot_functions.json") `
    --code $codeBinPath `
    --output $wholeAotGenerated `
    --shards 256 `
    --shard-strategy affinity
if ($LASTEXITCODE -ne 0) {
    throw "Unable to generate isolated LLVM whole-AOT shards"
}

$cache = Join-Path $buildPath "CMakeCache.txt"
if (Test-Path -LiteralPath $cache) {
    $compilerLine = Select-String -LiteralPath $cache `
        -Pattern '^CMAKE_CXX_COMPILER:FILEPATH=' | Select-Object -First 1
    if ($null -eq $compilerLine -or
        $compilerLine.Line -notlike "*$($clangCl.Replace('\', '/'))*") {
        throw "Build tree is not owned by the pinned LLVM toolchain: $buildPath"
    }
    $chainloadLine = Select-String -LiteralPath $cache `
        -Pattern '^VCPKG_CHAINLOAD_TOOLCHAIN_FILE:(?:FILEPATH|UNINITIALIZED)=' |
        Select-Object -First 1
    $cachedChainload = if ($null -ne $chainloadLine) {
        [System.IO.Path]::GetFullPath($chainloadLine.Line.Split('=', 2)[1])
    } else {
        ""
    }
    if ($cachedChainload -ne [System.IO.Path]::GetFullPath($toolchain)) {
        throw "Build tree does not use the pinned LLVM vcpkg chainload: $buildPath"
    }
}

$arguments = @(
    "-S", $repoRoot,
    "-B", $buildPath,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain",
    "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_ROOT=$vcpkgRoot",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
    "-DVCPKG_HOST_TRIPLET=x64-windows-static",
    "-DOOT3D_LLVM_ROOT=$llvmRootPath",
    "-DOOT3D_A32_AOT_GENERATED_DIR=$validatedA32Generated",
    "-DOOT3D_WHOLE_AOT_GENERATED_DIR=$wholeAotGenerated",
    "-DOOT3D_WHOLE_AOT_CODE_BIN=$codeBinPath",
    "-DOOT3D_WHOLE_AOT_PREBUILT_LIBRARY=",
    "-DOOT3D_REBUILD_WHOLE_AOT=$($RebuildWholeAot.IsPresent.ToString().ToUpperInvariant())",
    "-DOOT3D_REGENERATE_WHOLE_AOT=OFF",
    "-DOOT3D_REQUIRE_WHOLE_AOT=ON",
    "-DOOT3D_WHOLE_AOT_PRODUCT_MODE=OFF",
    "-DOOT3D_WHOLE_AOT_MAX_PARALLEL_COMPILES=$WholeAotParallelCompiles",
    "-DTHREE_DS_RECOMP_ENABLE_DX11=OFF",
    "-DOOT3D_FAST_DEV_LINK=ON",
    "-DTHREE_DS_RECOMP_FAST_DEV_LINK=ON",
    "-DAUTOMATE_VCPKG_UPDATE=OFF"
)

function Test-CacheSetting {
    param([string]$Pattern)
    return (Test-Path -LiteralPath $cache) -and
        [bool](Select-String -LiteralPath $cache -Pattern $Pattern -Quiet)
}

$wholeAotValue = if ($RebuildWholeAot) { "TRUE" } else { "FALSE" }
$configureRequired = $Force -or
    -not (Test-Path -LiteralPath $cache) -or
    -not (Test-CacheSetting '^VCPKG_TARGET_TRIPLET:STRING=x64-windows-static$') -or
    -not (Test-CacheSetting '^OOT3D_WHOLE_AOT_PREBUILT_LIBRARY:FILEPATH=$') -or
    -not (Test-CacheSetting "^OOT3D_REBUILD_WHOLE_AOT:BOOL=$wholeAotValue$") -or
    -not (Test-CacheSetting '^OOT3D_REGENERATE_WHOLE_AOT:BOOL=OFF$') -or
    -not (Test-CacheSetting '^OOT3D_REQUIRE_WHOLE_AOT:BOOL=ON$') -or
    -not (Test-CacheSetting '^OOT3D_WHOLE_AOT_PRODUCT_MODE:BOOL=OFF$') -or
    -not (Test-CacheSetting '^THREE_DS_RECOMP_ENABLE_DX11:BOOL=OFF$') -or
    -not (Test-CacheSetting '^OOT3D_FAST_DEV_LINK:BOOL=ON$') -or
    -not (Test-CacheSetting '^THREE_DS_RECOMP_FAST_DEV_LINK:BOOL=ON$') -or
    -not (Test-CacheSetting '^AUTOMATE_VCPKG_UPDATE:BOOL=OFF$') -or
    -not (Test-CacheSetting '^CMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON$')

if ($configureRequired) {
    & $vsDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $env:OOT3D_LLVM_ROOT = $llvmRootPath
    $env:VCPKG_ROOT = $vcpkgRoot
    & $cmake @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "LLVM CMake configure failed with exit code $LASTEXITCODE"
    }
}

[pscustomobject]@{
    status = if ($configureRequired) { "configured" } else { "already_configured" }
    source = $repoRoot
    build = $buildPath
    llvm_root = $llvmRootPath
    compiler = $clangCl
    linker = (Join-Path $llvmRootPath "bin\lld-link.exe")
    whole_aot_rebuild = $RebuildWholeAot.IsPresent
    whole_aot_code = $codeBinPath
    build_command = ".\scripts\oot3d\Build-Oot3dLlvm.ps1 -BuildDirectory `"$buildPath`" -Target oot3d_native_game"
}
