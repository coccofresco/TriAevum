[CmdletBinding()]
param(
    [string]$DecompRoot = "I:\oot3decomp",
    [string]$LlvmRoot = "I:\oot3dre_tools\llvm-22.1.6",
    [string]$ProcessManifest = "I:\oot3dre_work\native_game\oot3d_native_process_manifest.json",
    [string]$CodeBin = "",
    [string]$BuildDir = "build-source-native",
    [string]$VcpkgRoot = "C:\vcpkg",
    [switch]$ReplaceSnapshot,
    [switch]$Build,
    [switch]$BuildSurface,
    [switch]$BuildLoweredSurface,
    [switch]$BuildLlvmLegalizedSurface
)

$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$snapshotBuildRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$buildRoot = Join-Path $snapshotBuildRoot "host-clean"
$profile = Join-Path $repoRoot "tools\oot3d\source_native_runtime\decomp_profile.json"
$syncTool = Join-Path $repoRoot "tools\oot3d\source_native_runtime\sync_decomp_snapshot.py"
$snapshotRoot = Join-Path $snapshotBuildRoot "decomp-snapshot"
$processProfileRoot = Join-Path $snapshotBuildRoot "process-image"

$arguments = @(
    $syncTool,
    "--decomp-root", ([System.IO.Path]::GetFullPath($DecompRoot)),
    "--profile", $profile,
    "--output-root", $snapshotRoot
)
if ($ReplaceSnapshot) {
    $arguments += "--replace"
}

& python @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$processProfileTool = Join-Path $repoRoot `
    "tools\oot3d\source_native_runtime\prepare_process_image_profile.py"
$processArguments = @(
    $processProfileTool,
    "--manifest", ([System.IO.Path]::GetFullPath($ProcessManifest)),
    "--consumer-profile", $profile,
    "--output-root", $processProfileRoot
)
if (-not [string]::IsNullOrWhiteSpace($CodeBin)) {
    $processArguments += @("--code-bin", ([System.IO.Path]::GetFullPath($CodeBin)))
}
& python @processArguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($Build) {
    $windowsCmake = "C:\Program Files\CMake\bin\cmake.exe"
    if (-not (Test-Path -LiteralPath $windowsCmake)) {
        $windowsCmake = (Get-Command cmake.exe -ErrorAction Stop).Source
    }
    $sourceRoot = Join-Path $repoRoot "tools\oot3d\source_native_runtime"
    & $windowsCmake -S $sourceRoot -B $buildRoot -G Ninja `
        "-DOOT3D_DECOMP_ROOT=$([System.IO.Path]::GetFullPath($DecompRoot))" `
        "-DOOT3D_SOURCE_SNAPSHOT_ROOT=$snapshotRoot" `
        "-DOOT3D_SOURCE_PROCESS_PROFILE_ROOT=$processProfileRoot" `
        "-DOOT3D_SOURCE_LEGALIZED_ARCHIVE=$snapshotBuildRoot/llvm-legalized-surface/liboot3d_source_llvm_legalized_surface.a" `
        "-DOOT3D_VCPKG_ROOT=$([System.IO.Path]::GetFullPath($VcpkgRoot))"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    if (-not $BuildLlvmLegalizedSurface) {
        & $windowsCmake --build $buildRoot --config RelWithDebInfo --target `
            oot3d_source_game oot3d_source_runtime_tests
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
    if ($BuildSurface -or $BuildLoweredSurface -or $BuildLlvmLegalizedSurface) {
        $clangBuildRoot = Join-Path $snapshotBuildRoot "clang"
        $clang = Join-Path $LlvmRoot "bin\clang.exe"
        $clangxx = Join-Path $LlvmRoot "bin\clang++.exe"
        $llvmNm = Join-Path $LlvmRoot "bin\llvm-nm.exe"
        $llvmAr = Join-Path $LlvmRoot "bin\llvm-ar.exe"
        $llvmRc = Join-Path $LlvmRoot "bin\llvm-rc.exe"
        foreach ($required in @($clang, $clangxx, $llvmNm, $llvmAr, $llvmRc)) {
            if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
                throw "Required source-surface LLVM tool is missing: $required"
            }
        }
        $clangCmake = $clang.Replace('\', '/')
        $clangxxCmake = $clangxx.Replace('\', '/')
        $llvmRcCmake = $llvmRc.Replace('\', '/')
        & $windowsCmake -S $sourceRoot -B $clangBuildRoot -G Ninja `
            -DCMAKE_BUILD_TYPE=RelWithDebInfo `
            "-DCMAKE_C_COMPILER=$clangCmake" `
            "-DCMAKE_CXX_COMPILER=$clangxxCmake" `
            "-DCMAKE_RC_COMPILER=$llvmRcCmake" `
            -DCMAKE_C_COMPILER_TARGET=x86_64-w64-windows-gnu `
            -DCMAKE_CXX_COMPILER_TARGET=x86_64-w64-windows-gnu `
            "-DOOT3D_DECOMP_ROOT=$([System.IO.Path]::GetFullPath($DecompRoot))" `
            "-DOOT3D_SOURCE_SNAPSHOT_ROOT=$snapshotRoot" `
            "-DOOT3D_SOURCE_PROCESS_PROFILE_ROOT=$processProfileRoot" `
            "-DOOT3D_VCPKG_ROOT=$([System.IO.Path]::GetFullPath($VcpkgRoot))"
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
        & $windowsCmake --build $clangBuildRoot --target `
            oot3d_source_surface_archive --parallel
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
        & python (Join-Path $sourceRoot "analyze_source_surface.py") `
            --llvm-nm $llvmNm `
            --archive (Join-Path $clangBuildRoot "liboot3d_source_surface_archive.a") `
            --snapshot-manifest (Join-Path $snapshotRoot "source_snapshot_manifest.json") `
            --process-manifest ([System.IO.Path]::GetFullPath($ProcessManifest)) `
            --output (Join-Path $clangBuildRoot "source_surface_contracts.json")
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }

        if ($BuildLoweredSurface -or $BuildLlvmLegalizedSurface) {
            $surfaceContracts = Join-Path $clangBuildRoot "source_surface_contracts.json"
            $declarationReport = Join-Path $clangBuildRoot "target_data_declarations.json"
            $loweredRoot = Join-Path $snapshotBuildRoot "lowered-decomp-snapshot"
            & python (Join-Path $sourceRoot "analyze_target_data_declarations.py") `
                --clang $clang `
                --snapshot-root $snapshotRoot `
                --snapshot-manifest (Join-Path $snapshotRoot "source_snapshot_manifest.json") `
                --surface-contracts $surfaceContracts `
                --output $declarationReport `
                --assembly-output (Join-Path $clangBuildRoot "absolute_target_data_symbols.s") `
                --lowered-assembly-output (Join-Path $clangBuildRoot "lowered_absolute_target_data_symbols.s") `
                --jobs 8
            if ($LASTEXITCODE -ne 0) {
                exit $LASTEXITCODE
            }
            & python (Join-Path $sourceRoot "lower_target_word_globals.py") `
                --snapshot-root $snapshotRoot `
                --snapshot-manifest (Join-Path $snapshotRoot "source_snapshot_manifest.json") `
                --declaration-report $declarationReport `
                --surface-contracts $surfaceContracts `
                --process-manifest ([System.IO.Path]::GetFullPath($ProcessManifest)) `
                --output-root $loweredRoot `
                --replace
            if ($LASTEXITCODE -ne 0) {
                exit $LASTEXITCODE
            }
            & $windowsCmake -S $sourceRoot -B $clangBuildRoot -G Ninja `
                "-DOOT3D_SOURCE_LOWERED_ROOT=$loweredRoot"
            if ($LASTEXITCODE -ne 0) {
                exit $LASTEXITCODE
            }
            & $windowsCmake --build $clangBuildRoot --target `
                oot3d_source_lowered_surface_archive --parallel
            if ($LASTEXITCODE -ne 0) {
                exit $LASTEXITCODE
            }
            & python (Join-Path $sourceRoot "analyze_source_surface.py") `
                --llvm-nm $llvmNm `
                --archive (Join-Path $clangBuildRoot "liboot3d_source_lowered_surface_archive.a") `
                --snapshot-manifest (Join-Path $snapshotRoot "source_snapshot_manifest.json") `
                --process-manifest ([System.IO.Path]::GetFullPath($ProcessManifest)) `
                --output (Join-Path $clangBuildRoot "lowered_source_surface_contracts.json")
            if ($LASTEXITCODE -ne 0) {
                exit $LASTEXITCODE
            }

            if ($BuildLlvmLegalizedSurface) {
                $passSource = Join-Path $sourceRoot "llvm_pass"
                $passBuild = Join-Path $snapshotBuildRoot "llvm-pass"
                $llvmDir = Join-Path $LlvmRoot "lib\cmake\llvm"
                & $windowsCmake -S $passSource -B $passBuild -G Ninja `
                    -DCMAKE_BUILD_TYPE=Release `
                    "-DCMAKE_CXX_COMPILER=$clangxxCmake" `
                    "-DCMAKE_RC_COMPILER=$llvmRcCmake" `
                    "-DLLVM_DIR=$llvmDir"
                if ($LASTEXITCODE -ne 0) {
                    exit $LASTEXITCODE
                }
                & $windowsCmake --build $passBuild --parallel
                if ($LASTEXITCODE -ne 0) {
                    exit $LASTEXITCODE
                }
                $legalizedRoot = Join-Path $snapshotBuildRoot "llvm-legalized-surface"
                $legalizer = Join-Path $passBuild "Oot3dGuestStorageLegalizer.exe"
                & python (Join-Path $sourceRoot "build_llvm_legalized_surface.py") `
                    --clang $clang `
                    --llvm-ar $llvmAr `
                    --legalizer $legalizer `
                    --source-root $loweredRoot `
                    --snapshot-manifest (Join-Path $snapshotRoot "source_snapshot_manifest.json") `
                    --declaration-report $declarationReport `
                    --surface-contracts $surfaceContracts `
                    --process-manifest ([System.IO.Path]::GetFullPath($ProcessManifest)) `
                    --output-root $legalizedRoot `
                    --jobs 12
                if ($LASTEXITCODE -ne 0) {
                    exit $LASTEXITCODE
                }
                & python (Join-Path $sourceRoot "analyze_source_surface.py") `
                    --llvm-nm $llvmNm `
                    --archive (Join-Path $legalizedRoot "liboot3d_source_llvm_legalized_surface.a") `
                    --snapshot-manifest (Join-Path $snapshotRoot "source_snapshot_manifest.json") `
                    --process-manifest ([System.IO.Path]::GetFullPath($ProcessManifest)) `
                    --output (Join-Path $legalizedRoot "source_surface_contracts.json")
                if ($LASTEXITCODE -ne 0) {
                    exit $LASTEXITCODE
                }
                & python (Join-Path $sourceRoot "validate_host_import_contract.py") `
                    --surface-contracts (Join-Path $legalizedRoot "source_surface_contracts.json") `
                    --host-contract (Join-Path $sourceRoot "host_import_contract.json")
                if ($LASTEXITCODE -ne 0) {
                    exit $LASTEXITCODE
                }
                & $windowsCmake --build $buildRoot --config RelWithDebInfo --target `
                    oot3d_source_game oot3d_source_runtime_tests
            }
        }
    }
    exit $LASTEXITCODE
}

Write-Host "OOT3D source snapshot prepared at $snapshotRoot"
