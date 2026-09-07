[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GeneratedDirectory,
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [string]$LlvmRoot = "I:\oot3dre_tools\llvm-22.1.6",
    [int]$Parallel = 12,
    [switch]$ThinLto
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$sourceDirectory = Join-Path $PSScriptRoot "corpus_build"
$runtimeRoot = $PSScriptRoot
$cmake = "C:\Program Files\CMake\bin\cmake.exe"
$ninja = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$clang = Join-Path $LlvmRoot "bin\clang-cl.exe"
$linker = Join-Path $LlvmRoot "bin\lld-link.exe"
$archiver = Join-Path $LlvmRoot "bin\llvm-lib.exe"
$resourceCompiler = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe"

foreach ($path in @($cmake, $ninja, $clang, $linker, $archiver, $resourceCompiler)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required build tool does not exist: $path"
    }
}

$generated = [System.IO.Path]::GetFullPath($GeneratedDirectory)
$build = [System.IO.Path]::GetFullPath($BuildDirectory)
$resourceCompiler = $resourceCompiler.Replace('\', '/')
$thinLtoValue = if ($ThinLto) { "ON" } else { "OFF" }

& $cmake -S $sourceDirectory -B $build -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_CXX_COMPILER=$clang" `
    "-DCMAKE_LINKER=$linker" `
    "-DCMAKE_AR=$archiver" `
    "-DCMAKE_RC_COMPILER=$resourceCompiler" `
    "-DOOT3D_MASS_AOT_GENERATED_DIR=$generated" `
    "-DOOT3D_MASS_AOT_RUNTIME_ROOT=$runtimeRoot" `
    "-DOOT3D_MASS_AOT_ENABLE_THINLTO=$thinLtoValue"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $cmake --build $build --target oot3d_mass_aot_corpus --parallel $Parallel
exit $LASTEXITCODE
