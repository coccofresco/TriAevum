[CmdletBinding()]
param(
    [string]$LlvmRoot = "I:\oot3dre_tools\llvm-22.1.6",
    [string]$ExpectedVersion = "22.1.6",
    [switch]$KeepArtifacts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$llvmRootPath = [System.IO.Path]::GetFullPath($LlvmRoot)
$bin = Join-Path $llvmRootPath "bin"
$tools = @(
    "clang-cl.exe",
    "clang.exe",
    "lld-link.exe",
    "llvm-config.exe",
    "opt.exe",
    "llc.exe",
    "llvm-lib.exe",
    "llvm-profdata.exe"
)

foreach ($tool in $tools) {
    $path = Join-Path $bin $tool
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required LLVM tool is missing: $path"
    }
}

$llvmConfig = Join-Path $bin "llvm-config.exe"
$version = (& $llvmConfig --version).Trim()
if ($LASTEXITCODE -ne 0 -or $version -ne $ExpectedVersion) {
    throw "Expected LLVM $ExpectedVersion, found '$version' at $llvmRootPath"
}

$targets = (& $llvmConfig --targets-built).Trim().Split(
    " ", [System.StringSplitOptions]::RemoveEmptyEntries)
foreach ($target in @("ARM", "AArch64", "X86")) {
    if ($targets -notcontains $target) {
        throw "LLVM target '$target' is not available: $($targets -join ', ')"
    }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$manifest = Join-Path $repoRoot "tools\oot3d\operational_inputs.json"
$inputs = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
$vsDevShell = [string]$inputs.variables.vsDevShell
if (-not (Test-Path -LiteralPath $vsDevShell -PathType Leaf)) {
    throw "Visual Studio developer environment is missing: $vsDevShell"
}

$artifactRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "oot3d-llvm-smoke-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $artifactRoot | Out-Null

$source = Join-Path $artifactRoot "smoke.cpp"
$ir = Join-Path $artifactRoot "smoke.ll"
$optimizedIr = Join-Path $artifactRoot "smoke.optimized.ll"
$object = Join-Path $artifactRoot "smoke.obj"
$executable = Join-Path $artifactRoot "smoke.exe"

$sourceText = @'
#include <cstdint>

extern "C" std::uint32_t oot3d_llvm_smoke(std::uint32_t value) {
    return ((value << 5U) ^ (value >> 3U)) + 0x3D3D3D3DU;
}

int main() {
    return oot3d_llvm_smoke(0x12345678U) == 0x8209830CU ? 0 : 1;
}
'@
[System.IO.File]::WriteAllText($source, $sourceText,
    [System.Text.UTF8Encoding]::new($false))

try {
    $clang = Join-Path $bin "clang.exe"
    $opt = Join-Path $bin "opt.exe"
    $llc = Join-Path $bin "llc.exe"
    $clangCl = Join-Path $bin "clang-cl.exe"

    & $clang --target=x86_64-pc-windows-msvc -O2 -S -emit-llvm $source -o $ir
    if ($LASTEXITCODE -ne 0) {
        throw "clang failed to emit LLVM IR"
    }

    & $opt '-passes=default<O2>' -S $ir -o $optimizedIr
    if ($LASTEXITCODE -ne 0) {
        throw "opt failed to optimize LLVM IR"
    }

    & $llc --filetype=obj --mtriple=x86_64-pc-windows-msvc `
        $optimizedIr -o $object
    if ($LASTEXITCODE -ne 0) {
        throw "llc failed to emit a COFF object"
    }

    & $vsDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    & $clangCl /nologo /O2 /EHsc /std:c++20 $source `
        "/Fe:$executable" -fuse-ld=lld
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $executable)) {
        throw "clang-cl/lld-link failed to produce the smoke executable"
    }

    & $executable
    if ($LASTEXITCODE -ne 0) {
        throw "LLVM smoke executable returned $LASTEXITCODE"
    }

    [pscustomobject]@{
        status = "ok"
        llvm_root = $llvmRootPath
        version = $version
        targets = $targets
        clang_cl = $clangCl
        lld_link = (Join-Path $bin "lld-link.exe")
        llvm_config = $llvmConfig
        ir_bytes = (Get-Item -LiteralPath $optimizedIr).Length
        object_bytes = (Get-Item -LiteralPath $object).Length
        executable_bytes = (Get-Item -LiteralPath $executable).Length
        artifacts = if ($KeepArtifacts) { $artifactRoot } else { $null }
    }
} finally {
    if (-not $KeepArtifacts -and (Test-Path -LiteralPath $artifactRoot)) {
        Remove-Item -LiteralPath $artifactRoot -Recurse -Force
    }
}
