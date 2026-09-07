[CmdletBinding()]
param(
    [ValidateSet("none", "generate", "use", "sample-use")]
    [string]$ProfileMode = "none",
    [string]$ProfileFile = "",
    [string]$OrderFile = "",
    [string]$BuildRoot = "I:\oot3dre_work\build-aot-llvm",
    [string]$LlvmRoot = "I:\oot3dre_tools\llvm-22.1.6",
    [string]$WholeAotGeneratedDir =
        "I:\oot3dre_work\whole_aot_optimization\scalar_full",
    [string]$A32AotGeneratedDir =
        "I:\oot3dre-vulkan\build-codex\oot3d_a32_generated",
    [string]$WholeAotProgram =
        "I:\oot3dre-vulkan\build-codex\oot3d_whole_aot\aot_program.json",
    [string]$VcpkgRoot = "I:\oot3dre-vulkan\build-codex\vcpkg",
    [ValidateRange(1, 64)]
    [int]$Parallel = 12,
    [switch]$ConfigureOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$buildDirectory = Join-Path ([System.IO.Path]::GetFullPath($BuildRoot)) $ProfileMode
$clangCl = Join-Path $LlvmRoot "bin\clang-cl.exe"
$lldLink = Join-Path $LlvmRoot "bin\lld-link.exe"
$cmake = "C:\Program Files\CMake\bin\cmake.exe"
$ninja = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$resourceCompiler =
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe"

$requiredInputs = @(
    $cmake,
    $ninja,
    $clangCl,
    $lldLink,
    $resourceCompiler,
    $VcpkgRoot,
    (Join-Path $VcpkgRoot "installed"),
    (Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"),
    $WholeAotGeneratedDir,
    $A32AotGeneratedDir,
    $WholeAotProgram
)
foreach ($path in $requiredInputs) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required LLVM whole-AOT input is missing: $path"
    }
}
if (($ProfileMode -eq "use" -or $ProfileMode -eq "sample-use") -and
    ([string]::IsNullOrWhiteSpace($ProfileFile) -or
     -not (Test-Path -LiteralPath $ProfileFile))) {
    throw "ProfileMode '$ProfileMode' requires an existing -ProfileFile"
}
if (-not [string]::IsNullOrWhiteSpace($OrderFile) -and
    -not (Test-Path -LiteralPath $OrderFile)) {
    throw "Order file does not exist: $OrderFile"
}

$programDestination = Join-Path $buildDirectory "oot3d_whole_aot\aot_program.json"
New-Item -ItemType Directory -Force (Split-Path $programDestination) | Out-Null
Copy-Item -LiteralPath $WholeAotProgram -Destination $programDestination -Force

function ConvertTo-CMakePath {
    param([string]$Path)
    return $Path.Replace('\', '/')
}

$configureArguments = @(
    "-S", (ConvertTo-CMakePath $repoRoot),
    "-B", (ConvertTo-CMakePath $buildDirectory),
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$(ConvertTo-CMakePath $ninja)",
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
    "-DCMAKE_C_COMPILER=$(ConvertTo-CMakePath $clangCl)",
    "-DCMAKE_CXX_COMPILER=$(ConvertTo-CMakePath $clangCl)",
    "-DCMAKE_LINKER=$(ConvertTo-CMakePath $lldLink)",
    "-DCMAKE_RC_COMPILER=$(ConvertTo-CMakePath $resourceCompiler)",
    "-DCMAKE_TOOLCHAIN_FILE=$(ConvertTo-CMakePath (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'))",
    "-DVCPKG_FALLBACK_ROOT=$(ConvertTo-CMakePath $VcpkgRoot)",
    "-DVCPKG_INSTALLED_DIR=$(ConvertTo-CMakePath (Join-Path $VcpkgRoot 'installed'))",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
    "-DAUTOMATE_VCPKG_UPDATE=OFF",
    "-DOOT3D_ENABLE_VULKAN_RENDERER=ON",
    "-DOOT3D_REBUILD_WHOLE_AOT=ON",
    "-DOOT3D_WHOLE_AOT_THINLTO=ON",
    "-DOOT3D_WHOLE_AOT_PROFILE_MODE=$ProfileMode",
    "-DOOT3D_WHOLE_AOT_PROFILE_FILE=$(ConvertTo-CMakePath $ProfileFile)",
    "-DOOT3D_WHOLE_AOT_ORDER_FILE=$(ConvertTo-CMakePath $OrderFile)",
    "-DOOT3D_WHOLE_AOT_MAX_PARALLEL_COMPILES=8",
    "-DOOT3D_WHOLE_AOT_GENERATED_DIR=$(ConvertTo-CMakePath $WholeAotGeneratedDir)",
    "-DOOT3D_A32_AOT_GENERATED_DIR=$(ConvertTo-CMakePath $A32AotGeneratedDir)"
)

& $cmake @configureArguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
if (-not $ConfigureOnly) {
    & $cmake --build $buildDirectory --target oot3d_native_game --parallel $Parallel
    exit $LASTEXITCODE
}

Write-Output $buildDirectory
