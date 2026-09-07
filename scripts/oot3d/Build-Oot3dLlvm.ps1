[CmdletBinding()]
param(
    [string]$BuildDirectory = "I:\oot3dre_work\build-llvm-22.1.6",
    [string]$Target = "oot3d_native_game",
    [ValidateRange(1, 64)]
    [int]$Parallel = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$buildPath = [System.IO.Path]::GetFullPath($BuildDirectory)
$cache = Join-Path $buildPath "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) {
    throw "LLVM build is not configured: $buildPath"
}

$compiler = Select-String -LiteralPath $cache `
    -Pattern '^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING|UNINITIALIZED)=(.+)$' |
    Select-Object -First 1
if ($null -eq $compiler -or $compiler.Matches[0].Groups[1].Value -notmatch
        '/llvm-22\.1\.6/bin/clang-cl\.exe$') {
    throw "Build tree is not owned by the pinned LLVM compiler: $buildPath"
}

$ninjaSetting = Select-String -LiteralPath $cache `
    -Pattern '^CMAKE_MAKE_PROGRAM:(?:FILEPATH|UNINITIALIZED)=(.+)$' |
    Select-Object -First 1
if ($null -eq $ninjaSetting) {
    throw "CMAKE_MAKE_PROGRAM is missing from $cache"
}
$ninja = $ninjaSetting.Matches[0].Groups[1].Value
if (-not (Test-Path -LiteralPath $ninja -PathType Leaf)) {
    throw "Configured Ninja executable is missing: $ninja"
}

$manifest = Join-Path $repoRoot "tools\oot3d\operational_inputs.json"
$inputs = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
$vsDevShell = [string]$inputs.variables.vsDevShell
if (-not (Test-Path -LiteralPath $vsDevShell -PathType Leaf)) {
    throw "Visual Studio developer environment is missing: $vsDevShell"
}

& $vsDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

# Launch-VsDevShell points VCPKG_ROOT at Visual Studio's bundled client,
# which is not a full checkout accepted by automate-vcpkg.cmake. Preserve the
# checkout that owns this build tree so automatic CMake regeneration remains
# deterministic.
$vcpkgToolchainSetting = Select-String -LiteralPath $cache `
    -Pattern '^CMAKE_TOOLCHAIN_FILE:(?:FILEPATH|STRING|UNINITIALIZED)=(.+)$' |
    Select-Object -First 1
if ($null -ne $vcpkgToolchainSetting) {
    $vcpkgToolchain =
        $vcpkgToolchainSetting.Matches[0].Groups[1].Value
    if (Test-Path -LiteralPath $vcpkgToolchain -PathType Leaf) {
        $vcpkgRoot = Split-Path -Parent (
            Split-Path -Parent (
                Split-Path -Parent $vcpkgToolchain))
        if (Test-Path -LiteralPath (Join-Path $vcpkgRoot "README.md") `
                -PathType Leaf) {
            $env:VCPKG_ROOT = $vcpkgRoot
        }
    }
}

# ShaderMake may be built by MinGW even though the consumer uses clang-cl.
# Resolve its matching C++ runtime ahead of unrelated MinGW DLLs on PATH.
$shaderMakeCache = Join-Path $buildPath `
    "_deps\shadermake-build\build\CMakeCache.txt"
if (Test-Path -LiteralPath $shaderMakeCache -PathType Leaf) {
    $shaderMakeCompiler = Select-String -LiteralPath $shaderMakeCache `
        -Pattern '^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING|UNINITIALIZED)=(.+)$' |
        Select-Object -First 1
    if ($null -ne $shaderMakeCompiler) {
        $shaderMakeRuntime = Split-Path -Parent `
            $shaderMakeCompiler.Matches[0].Groups[1].Value
        $requiredRuntimeFiles = @(
            "libgcc_s_seh-1.dll",
            "libstdc++-6.dll",
            "libwinpthread-1.dll"
        )
        $hasMatchingRuntime = @($requiredRuntimeFiles | Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $shaderMakeRuntime $_) `
                -PathType Leaf)
        }).Count -eq 0
        if ($hasMatchingRuntime) {
            $env:PATH = "$shaderMakeRuntime;$env:PATH"
        }
    }
}

$timer = [System.Diagnostics.Stopwatch]::StartNew()
& $ninja -C $buildPath -j $Parallel $Target
$exitCode = $LASTEXITCODE
$timer.Stop()

Write-Host ("LLVM build target {0}: {1:N2} s (exit {2})" -f
    $Target, $timer.Elapsed.TotalSeconds, $exitCode)
if ($exitCode -ne 0) {
    throw "LLVM build failed with exit code $exitCode"
}
