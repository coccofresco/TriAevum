[CmdletBinding()]
param(
    [string]$BuildDirectory = "I:\oot3dre_work\source-overlay-fast",
    [string]$DeployDirectory =
        "I:\oot3dre_work\native_game\source_overlays",
    [string]$LlvmPassBuildDirectory =
        "I:\oot3dre_work\source-overlay-llvm-lowering",
    [ValidateRange(1, 4)]
    [int]$Parallel = 2,
    [switch]$Reconfigure,
    [switch]$RunTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$totalTimer = [System.Diagnostics.Stopwatch]::StartNew()

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$sourceRoot = Join-Path $repoRoot "tools\oot3d\source_overlay"
$migrationValidator = Join-Path $sourceRoot `
    "validate_hybrid_migration_manifest.py"
$buildPath = [System.IO.Path]::GetFullPath($BuildDirectory)
$deployPath = [System.IO.Path]::GetFullPath($DeployDirectory)
$inputs = Get-Content -LiteralPath (
    Join-Path $repoRoot "tools\oot3d\operational_inputs.json") -Raw |
    ConvertFrom-Json
$cmake = [string]$inputs.variables.cmakeExe
$vsDevShell = [string]$inputs.variables.vsDevShell
$toolchain = Join-Path $repoRoot "CMake\Oot3dLlvmClToolchain.cmake"
$llvmRoot = [string]$inputs.variables.llvmRoot
$llvmPassScript = Join-Path $PSScriptRoot `
    "Build-Oot3dSourceOverlayLlvmPass.ps1"
$llvmPassBuildPath = [System.IO.Path]::GetFullPath($LlvmPassBuildDirectory)
$llvmPass = Join-Path $llvmPassBuildPath "Oot3dGuestMemoryLowering.exe"
$clang = Join-Path $llvmRoot "bin\clang.exe"
$opt = Join-Path $llvmRoot "bin\opt.exe"

foreach ($requiredFile in @(
        $cmake, $vsDevShell, $toolchain, $llvmPassScript, $clang, $opt,
        $migrationValidator)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required source-overlay build input is missing: $requiredFile"
    }
}

& python $migrationValidator
if ($LASTEXITCODE -ne 0) {
    throw "Hybrid migration manifest validation failed with exit code $LASTEXITCODE"
}

& $llvmPassScript -BuildDirectory $llvmPassBuildPath -Parallel $Parallel
if (-not (Test-Path -LiteralPath $llvmPass -PathType Leaf)) {
    throw "Source-overlay LLVM lowering executable is missing: $llvmPass"
}

$cache = Join-Path $buildPath "CMakeCache.txt"
$environmentCache = Join-Path $buildPath "oot3d-vsdev-environment.json"
$needsConfigure = $Reconfigure -or
    -not (Test-Path -LiteralPath $cache -PathType Leaf)
$hostPatchEnabled = $false
if (Test-Path -LiteralPath $cache -PathType Leaf) {
    $hostPatchEnabled = Select-String -LiteralPath $cache -Quiet `
        -SimpleMatch "OOT3D_SOURCE_OVERLAY_BUILD_HOST_PATCH:BOOL=ON"
}
if (-not $needsConfigure -and $hostPatchEnabled) {
    $needsConfigure = $true
}
if (-not $needsConfigure) {
    $needsConfigure = -not (Select-String -LiteralPath $cache -Quiet `
        -SimpleMatch "OOT3D_SOURCE_OVERLAY_GUEST_MEMORY_LOWERING:FILEPATH=")
}
if ($needsConfigure -or
    -not (Test-Path -LiteralPath $environmentCache -PathType Leaf)) {
    & $vsDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation |
        Out-Null
    $environmentNames = @(
        "PATH", "INCLUDE", "LIB", "LIBPATH", "VCToolsInstallDir",
        "WindowsSdkDir", "WindowsSDKVersion", "UniversalCRTSdkDir",
        "UCRTVersion", "VSINSTALLDIR", "VCINSTALLDIR"
    )
    $capturedEnvironment = [ordered]@{}
    foreach ($name in $environmentNames) {
        $value = [Environment]::GetEnvironmentVariable($name, "Process")
        if ($null -ne $value) {
            $capturedEnvironment[$name] = $value
        }
    }
    New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
    $capturedEnvironment | ConvertTo-Json | Set-Content `
        -LiteralPath $environmentCache -Encoding utf8
} else {
    $capturedEnvironment = Get-Content -LiteralPath $environmentCache -Raw |
        ConvertFrom-Json
    foreach ($property in $capturedEnvironment.PSObject.Properties) {
        [Environment]::SetEnvironmentVariable(
            $property.Name, [string]$property.Value, "Process")
    }
}

if ($needsConfigure) {
    New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
    & $cmake -S $sourceRoot -B $buildPath -G Ninja `
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo" `
        "-DOOT3D_SOURCE_OVERLAY_BUILD_HOST_PATCH=OFF" `
        "-DOOT3D_SOURCE_OVERLAY_BUILD_DECOMP_OWNER=ON" `
        "-DOOT3D_SOURCE_OVERLAY_CLANG=$($clang.Replace('\', '/'))" `
        "-DOOT3D_SOURCE_OVERLAY_OPT=$($opt.Replace('\', '/'))" `
        "-DOOT3D_SOURCE_OVERLAY_GUEST_MEMORY_LOWERING=$($llvmPass.Replace('\', '/'))" `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
    if ($LASTEXITCODE -ne 0) {
        throw "Source-overlay CMake configure failed with exit code $LASTEXITCODE"
    }
}

$timer = [System.Diagnostics.Stopwatch]::StartNew()
& $cmake --build $buildPath --target oot3d_source_overlay_smoke `
    --parallel $Parallel
$buildExitCode = $LASTEXITCODE
$timer.Stop()
if ($buildExitCode -ne 0) {
    throw "Source-overlay build failed with exit code $buildExitCode"
}

$dll = Join-Path $buildPath "oot3d_source_overlay.dll"
if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) {
    throw "Source-overlay DLL was not produced: $dll"
}
New-Item -ItemType Directory -Path $deployPath -Force | Out-Null
$deployedDll = Join-Path $deployPath "oot3d_source_overlay.dll"
$deployRequired = -not (
    Test-Path -LiteralPath $deployedDll -PathType Leaf)
if (-not $deployRequired) {
    $sourceInfo = Get-Item -LiteralPath $dll
    $deployedInfo = Get-Item -LiteralPath $deployedDll
    $deployRequired =
        $sourceInfo.Length -ne $deployedInfo.Length -or
        $sourceInfo.LastWriteTimeUtc -ne $deployedInfo.LastWriteTimeUtc
}
if ($deployRequired) {
    Copy-Item -LiteralPath $dll -Destination $deployedDll -Force
}

if ($RunTests) {
    & $cmake --build $buildPath --target oot3d_source_overlay_loader_tests `
        --parallel $Parallel
    if ($LASTEXITCODE -ne 0) {
        throw "Source-overlay test build failed with exit code $LASTEXITCODE"
    }
    & (Join-Path $buildPath "oot3d_source_overlay_loader_tests.exe") $dll
    if ($LASTEXITCODE -ne 0) {
        throw "Source-overlay tests failed with exit code $LASTEXITCODE"
    }
}

$totalTimer.Stop()
[ordered]@{
    format = "oot3d_source_overlay_build_v1"
    build_seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
    total_seconds = [Math]::Round($totalTimer.Elapsed.TotalSeconds, 3)
    deployed = $deployRequired
    dll = $deployedDll
    environment = "OOT3D_SOURCE_OVERLAY=$deployedDll"
} | ConvertTo-Json
