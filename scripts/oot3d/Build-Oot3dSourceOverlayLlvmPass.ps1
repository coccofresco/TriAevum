[CmdletBinding()]
param(
    [string]$BuildDirectory =
        "I:\oot3dre_work\source-overlay-llvm-lowering",
    [ValidateRange(1, 4)]
    [int]$Parallel = 2,
    [switch]$Reconfigure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$timer = [System.Diagnostics.Stopwatch]::StartNew()

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$sourceRoot = Join-Path $repoRoot "tools\oot3d\source_overlay\llvm_pass"
$buildPath = [System.IO.Path]::GetFullPath($BuildDirectory)
$inputs = Get-Content -LiteralPath (
    Join-Path $repoRoot "tools\oot3d\operational_inputs.json") -Raw |
    ConvertFrom-Json
$cmake = [string]$inputs.variables.cmakeExe
$vsDevShell = [string]$inputs.variables.vsDevShell
$llvmRoot = [string]$inputs.variables.llvmRoot
$executable = Join-Path $buildPath "Oot3dGuestMemoryLowering.exe"
$sources = @(
    (Join-Path $sourceRoot "CMakeLists.txt"),
    (Join-Path $sourceRoot "Oot3dGuestMemoryLowering.cpp")
)

foreach ($required in @(
        $cmake, $vsDevShell,
        (Join-Path $llvmRoot "bin\clang++.exe"),
        (Join-Path $llvmRoot "bin\llvm-rc.exe"),
        (Join-Path $llvmRoot "lib\cmake\llvm\LLVMConfig.cmake")) + $sources) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required LLVM lowering input is missing: $required"
    }
}

$needsBuild = $Reconfigure -or
    -not (Test-Path -LiteralPath $executable -PathType Leaf)
if (-not $needsBuild) {
    $outputTime = (Get-Item -LiteralPath $executable).LastWriteTimeUtc
    $needsBuild = $null -ne ($sources | Where-Object {
        (Get-Item -LiteralPath $_).LastWriteTimeUtc -gt $outputTime
    } | Select-Object -First 1)
}

if ($needsBuild) {
    & $vsDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation |
        Out-Null
    New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
    $cache = Join-Path $buildPath "CMakeCache.txt"
    if ($Reconfigure -or -not (Test-Path -LiteralPath $cache -PathType Leaf)) {
        $cmakeSource = $sourceRoot.Replace('\', '/')
        $cmakeBuild = $buildPath.Replace('\', '/')
        $clang = (Join-Path $llvmRoot "bin\clang++.exe").Replace('\', '/')
        $resourceCompiler =
            (Join-Path $llvmRoot "bin\llvm-rc.exe").Replace('\', '/')
        $llvmConfig =
            (Join-Path $llvmRoot "lib\cmake\llvm").Replace('\', '/')
        & $cmake -S $cmakeSource -B $cmakeBuild -G Ninja `
            "-DCMAKE_BUILD_TYPE=Release" `
            "-DCMAKE_CXX_COMPILER=$clang" `
            "-DCMAKE_RC_COMPILER=$resourceCompiler" `
            "-DLLVM_DIR=$llvmConfig"
        if ($LASTEXITCODE -ne 0) {
            throw "LLVM lowering configure failed with exit code $LASTEXITCODE"
        }
    }
    & $cmake --build $buildPath --target Oot3dGuestMemoryLowering `
        --parallel $Parallel
    if ($LASTEXITCODE -ne 0) {
        throw "LLVM lowering build failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "LLVM lowering executable was not produced: $executable"
}
$timer.Stop()
[ordered]@{
    format = "oot3d_guest_memory_lowering_build_v1"
    rebuilt = $needsBuild
    seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
    executable = $executable
} | ConvertTo-Json -Compress
