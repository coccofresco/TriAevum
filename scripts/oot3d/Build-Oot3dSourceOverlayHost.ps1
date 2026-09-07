[CmdletBinding()]
param(
    [string]$QualifiedBuildDirectory =
        "F:\oot3dre_build\native-renderer-llvm",
    [string]$OverlayBuildDirectory =
        "I:\oot3dre_work\source-overlay-host-patch",
    [ValidateRange(1, 4)]
    [int]$Parallel = 2,
    [switch]$CompileOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$qualifiedBuild = [System.IO.Path]::GetFullPath($QualifiedBuildDirectory)
$overlayBuild = [System.IO.Path]::GetFullPath($OverlayBuildDirectory)
$sourceRoot = Join-Path $repoRoot "tools\oot3d\source_overlay"
$inputs = Get-Content -LiteralPath (
    Join-Path $repoRoot "tools\oot3d\operational_inputs.json") -Raw |
    ConvertFrom-Json
$cmake = [string]$inputs.variables.cmakeExe
$vsDevShell = [string]$inputs.variables.vsDevShell
$toolchain = Join-Path $repoRoot "CMake\Oot3dLlvmClToolchain.cmake"
$ninja = "C:\Users\xander\.local\bin\ninja.exe"
$environmentCache = Join-Path $overlayBuild "oot3d-vsdev-environment.json"

$qualifiedNinja = Join-Path $qualifiedBuild "build.ninja"
$qualifiedCache = Join-Path $qualifiedBuild "CMakeCache.txt"
foreach ($requiredFile in @(
        $cmake, $vsDevShell, $toolchain, $ninja, $qualifiedNinja,
        $qualifiedCache)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required source-overlay host input is missing: $requiredFile"
    }
}

if (-not (Test-Path -LiteralPath $environmentCache -PathType Leaf)) {
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
    New-Item -ItemType Directory -Path $overlayBuild -Force | Out-Null
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

$a32Generated =
    "I:\oot3dre_work\build-native-renderer-integration-v2\oot3d_a32_generated"
$wholeAotGenerated =
    "I:\oot3dre_work\whole_aot_optimization\scalar_full"
$vcpkgInclude =
    "I:\oot3dre_work\build-native-renderer-integration-v2\vcpkg\installed\x64-windows-static\include"

& $cmake -S $sourceRoot -B $overlayBuild -G Ninja `
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DOOT3D_SOURCE_OVERLAY_BUILD_HOST_PATCH=ON" `
    "-DOOT3D_SOURCE_OVERLAY_BUILD_DECOMP_OWNER=OFF" `
    "-DOOT3D_SOURCE_OVERLAY_A32_GENERATED_DIR=$a32Generated" `
    "-DOOT3D_SOURCE_OVERLAY_WHOLE_AOT_GENERATED_DIR=$wholeAotGenerated" `
    "-DOOT3D_SOURCE_OVERLAY_VCPKG_INCLUDE_DIR=$vcpkgInclude"
if ($LASTEXITCODE -ne 0) {
    throw "Source-overlay host configure failed with exit code $LASTEXITCODE"
}

$compileTimer = [System.Diagnostics.Stopwatch]::StartNew()
& $cmake --build $overlayBuild `
    --target oot3d_source_overlay_host_patch_objects `
    --parallel $Parallel
$compileExitCode = $LASTEXITCODE
$compileTimer.Stop()
if ($compileExitCode -ne 0) {
    throw "Source-overlay host compile failed with exit code $compileExitCode"
}

$objectRoot = Join-Path $overlayBuild `
    "CMakeFiles\oot3d_source_overlay_host_patch_objects.dir"
$patchObjects = @(Get-ChildItem -LiteralPath $objectRoot -Recurse `
    -Filter "*.obj" -File | Select-Object -ExpandProperty FullName)
if ($patchObjects.Count -ne 4) {
    throw "Expected four source-overlay host objects, found $($patchObjects.Count)"
}

if ($CompileOnly) {
    [ordered]@{
        format = "oot3d_source_overlay_host_compile_v1"
        compile_seconds = [Math]::Round($compileTimer.Elapsed.TotalSeconds, 3)
        object_count = $patchObjects.Count
        build_directory = $overlayBuild
    } | ConvertTo-Json
    return
}

$ninjaText = Get-Content -LiteralPath $qualifiedNinja -Raw
$targetPattern = '(?m)^build oot3d_native_game\.exe: .+$'
$targetMatch = [regex]::Match($ninjaText, $targetPattern)
if (-not $targetMatch.Success) {
    throw "Qualified build has no oot3d_native_game executable rule"
}
$targetLine = $targetMatch.Value
$dependencySeparator = $targetLine.IndexOf(" | ")
if ($dependencySeparator -lt 0) {
    throw "Qualified executable rule has no dependency separator"
}
$encodedObjects = @($patchObjects | ForEach-Object {
    $_.Replace("\", "/").Replace(":", "`$:")
}) -join " "
$patchedTargetLine =
    $targetLine.Substring(0, $dependencySeparator) + " " + $encodedObjects
$patchedTargetLine = [regex]::Replace(
    $patchedTargetLine,
    'CMakeFiles\\oot3d_native_game\.dir\\[^ ]+\.obj',
    {
        param($match)
        (Join-Path $qualifiedBuild $match.Value).Replace("\", "/").Replace(
            ":", "`$:")
    })
$patchedTargetLine = $patchedTargetLine.Replace(
    "build oot3d_native_game.exe:",
    "build oot3d_native_game_source_overlay.exe:")
$ninjaText = $ninjaText.Remove(
    $targetMatch.Index, $targetMatch.Length).Insert(
        $targetMatch.Index, $patchedTargetLine)

$targetBlockStart = $targetMatch.Index
$blockTail = $ninjaText.Substring($targetBlockStart)
$blankMatch = [regex]::Match($blockTail, '\r?\n\r?\n')
if (-not $blankMatch.Success) {
    throw "Qualified executable target block is unterminated"
}
$nextBlank = $targetBlockStart + $blankMatch.Index
$targetBlock = $ninjaText.Substring(
    $targetBlockStart, $nextBlank - $targetBlockStart)
$targetBlock = $targetBlock.Replace(
    "TARGET_FILE = oot3d_native_game.exe",
    "TARGET_FILE = oot3d_native_game_source_overlay.exe")
$targetBlock = $targetBlock.Replace(
    "TARGET_IMPLIB = oot3d_native_game.lib",
    "TARGET_IMPLIB = oot3d_native_game_source_overlay.lib")
$targetBlock = $targetBlock.Replace(
    "TARGET_PDB = oot3d_native_game.pdb",
    "TARGET_PDB = oot3d_native_game_source_overlay.pdb")
$targetBlock = $targetBlock.Replace(
    "native-renderer-llvm/oot3d_native_game.exe",
    "native-renderer-llvm/oot3d_native_game_source_overlay.exe")
$ninjaText = $ninjaText.Remove(
    $targetBlockStart, $nextBlank - $targetBlockStart).Insert(
        $targetBlockStart, $targetBlock)

$patchedNinja = Join-Path $qualifiedBuild `
    "oot3d_source_overlay_host.ninja"
Set-Content -LiteralPath $patchedNinja -Value $ninjaText -Encoding utf8

$linkTimer = [System.Diagnostics.Stopwatch]::StartNew()
& $ninja -C $qualifiedBuild -f $patchedNinja `
    -j 1 oot3d_native_game_source_overlay.exe
$linkExitCode = $LASTEXITCODE
$linkTimer.Stop()
if ($linkExitCode -ne 0) {
    throw "Source-overlay host link failed with exit code $linkExitCode"
}

$executable = Join-Path $qualifiedBuild `
    "oot3d_native_game_source_overlay.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Source-overlay host executable was not produced: $executable"
}

[ordered]@{
    format = "oot3d_source_overlay_host_build_v1"
    compile_seconds = [Math]::Round($compileTimer.Elapsed.TotalSeconds, 3)
    link_seconds = [Math]::Round($linkTimer.Elapsed.TotalSeconds, 3)
    executable = $executable
    baseline_build = $qualifiedBuild
} | ConvertTo-Json
