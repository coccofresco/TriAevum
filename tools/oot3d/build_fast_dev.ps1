[CmdletBinding()]
param(
    [string]$Target = "oot3d_native_game",
    [int]$Parallel = 12,
    [string]$BuildDirectory = "",
    [switch]$Configure,
    [int]$MaxDefaultCompileActions = 24,
    [int]$MaxDefaultBuildActions = 96,
    [int]$MaxNativeGameCompileActions = 48,
    [int]$MaxNativeGameBuildActions = 160,
    [switch]$PreflightOnly,
    [switch]$AllowBroadRebuild
)

$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repoRoot "build-codex"
}
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null

$buildLockPath = Join-Path $BuildDirectory ".oot3d-build.lock"
try {
    $buildLock = [System.IO.File]::Open(
        $buildLockPath,
        [System.IO.FileMode]::OpenOrCreate,
        [System.IO.FileAccess]::ReadWrite,
        [System.IO.FileShare]::None)
} catch [System.IO.IOException] {
    throw "Another OOT3D build already owns $BuildDirectory. Wait for it to finish instead of starting overlapping Ninja processes."
}

try {
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$devShell = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1"
if (-not (Test-Path -LiteralPath $cmake)) {
    throw "Visual Studio CMake was not found at $cmake"
}
if (-not (Test-Path -LiteralPath $devShell)) {
    throw "Visual Studio developer shell was not found at $devShell"
}

& $devShell -Arch amd64 -SkipAutomaticLocation
$env:CL = "-EHsc"
$localVcpkg = Join-Path $BuildDirectory "vcpkg"
if (Test-Path -LiteralPath $localVcpkg) {
    $env:VCPKG_ROOT = $localVcpkg
}

$cachePath = Join-Path $BuildDirectory "CMakeCache.txt"
function Get-CacheValue([string]$Name) {
    if (-not (Test-Path -LiteralPath $cachePath)) {
        return $null
    }
    $setting = Select-String -LiteralPath $cachePath `
        -Pattern ("^{0}:[^=]+=(.*)$" -f [regex]::Escape($Name)) |
        Select-Object -First 1
    if ($null -eq $setting) {
        return $null
    }
    return $setting.Matches[0].Groups[1].Value
}

$a32Targets = @(
    "oot3d_native_game",
    "oot3d_native_a32_boot_probe",
    "oot3d_native_a32_execution_tests",
    "oot3d_native_a32_ctr_host_tests",
    "oot3d_native_a32_process_tests",
    "oot3d_native_actor_runtime_tests",
    "oot3d_native_closure_catalog_tests",
    "oot3d_native_a32_aot"
)
$needsA32Aot = $a32Targets -contains $Target
$requiresWholeAot = $Target -eq "oot3d_native_game"
$needsRuntimeTests = $Target -eq "oot3d_graphics_foundation_tests"
if ($needsA32Aot) {
    $a32Generator = Join-Path $repoRoot "tools\oot3d\native_a32_runtime\generate_aot.py"
    $a32Output = Get-CacheValue "OOT3D_A32_AOT_GENERATED_DIR"
    if ([string]::IsNullOrWhiteSpace($a32Output)) {
        $a32Output = Join-Path $BuildDirectory "oot3d_a32_generated"
    }
    & python $a32Generator --output $a32Output
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    foreach ($required in @(
        "oot3d_a32_generated.h",
        "oot3d_a32_true_aot_generated.h",
        "oot3d_a32_true_aot_generated.cpp",
        "registry.cpp",
        "manifest.json"
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $a32Output $required) -PathType Leaf)) {
            throw "A32 AOT generator did not produce $required"
        }
    }
    $wholeAotOutput = Get-CacheValue "OOT3D_WHOLE_AOT_GENERATED_DIR"
    if ([string]::IsNullOrWhiteSpace($wholeAotOutput)) {
        $wholeAotOutput = Join-Path $BuildDirectory "oot3d_whole_aot_cpp"
    }
    $rebuildWholeAot = (Get-CacheValue "OOT3D_REBUILD_WHOLE_AOT") -eq "ON"
    $wholeAotPrebuilt = Get-CacheValue "OOT3D_WHOLE_AOT_PREBUILT_LIBRARY"
    if ($requiresWholeAot -and -not $rebuildWholeAot -and
        -not [string]::IsNullOrWhiteSpace($wholeAotPrebuilt) -and
        -not (Test-Path -LiteralPath $wholeAotPrebuilt -PathType Leaf)) {
        throw "oot3d_native_game requires whole-AOT, but the configured archive is missing: $wholeAotPrebuilt"
    }
    if ($rebuildWholeAot) {
        $wholeAotGenerator = Join-Path $repoRoot `
            "tools\oot3d\native_a32_runtime\generate_whole_aot_cpp.py"
        & python $wholeAotGenerator --output $wholeAotOutput
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
    if ($rebuildWholeAot -or
        (-not [string]::IsNullOrWhiteSpace($wholeAotPrebuilt) -and
         (Test-Path -LiteralPath $wholeAotPrebuilt -PathType Leaf))) {
        foreach ($required in @(
            "oot3d_whole_aot_generated.h",
            "oot3d_whole_aot_generated.cpp",
            "whole_aot_cpp_manifest.json"
        )) {
            if (-not (Test-Path -LiteralPath `
                    (Join-Path $wholeAotOutput $required) -PathType Leaf)) {
                throw "Configured whole-AOT input is incomplete: $wholeAotOutput is missing $required"
            }
        }
    }
}
if ($Target -eq "oot3d_native_a32_boot_probe") {
    $processManifestBuilder = Join-Path $repoRoot "tools\oot3d\native_a32_runtime\build_process_manifest.py"
    $processManifest = Join-Path $BuildDirectory "oot3d_native_process_manifest.json"
    & python $processManifestBuilder --output $processManifest
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$thinLtoCache = [System.IO.Path]::GetFullPath(
    (Join-Path $BuildDirectory "thinlto-cache")) -replace '\\', '/'
function Test-CacheSetting([string]$Pattern) {
    return (Test-Path -LiteralPath $cachePath) -and
        [bool](Select-String -LiteralPath $cachePath -Pattern $Pattern -Quiet)
}

$wholeAotRequirementMissing = $requiresWholeAot -and
    -not (Test-CacheSetting '^OOT3D_REQUIRE_WHOLE_AOT:BOOL=ON$')
$runtimeTestsMissing = $needsRuntimeTests -and
    -not (Test-CacheSetting '^THREE_DS_RECOMP_BUILD_TESTS:BOOL=ON$')
$configureRequired = $Configure -or
    -not (Test-Path -LiteralPath $cachePath) -or
    -not (Test-CacheSetting '^OOT3D_FAST_DEV_LINK:BOOL=ON$') -or
    -not (Test-CacheSetting '^THREE_DS_RECOMP_FAST_DEV_LINK:BOOL=ON$') -or
    -not (Test-CacheSetting '^THREE_DS_RECOMP_ENABLE_DX11:BOOL=OFF$') -or
    -not (Test-CacheSetting '^AUTOMATE_VCPKG_UPDATE:BOOL=OFF$') -or
    $wholeAotRequirementMissing -or
    $runtimeTestsMissing -or
    -not (Test-CacheSetting (
        '^OOT3D_WHOLE_AOT_THINLTO_CACHE_DIR:PATH=' +
        [regex]::Escape($thinLtoCache) + '$')) -or
    ($needsA32Aot -and
     -not (Test-Path -LiteralPath (Join-Path $BuildDirectory "build.ninja") -PathType Leaf)) -or
    ($needsA32Aot -and
     -not (Select-String -LiteralPath (Join-Path $BuildDirectory "build.ninja") `
         -Pattern 'oot3d_native_a32_aot' -Quiet -ErrorAction SilentlyContinue)) -or
    ($needsA32Aot -and $rebuildWholeAot -and
     -not (Select-String -LiteralPath (Join-Path $BuildDirectory "build.ninja") `
         -Pattern 'oot3d_whole_aot_generated.cpp' -Quiet -ErrorAction SilentlyContinue))

if ($configureRequired) {
    $cmakeArguments = @(
        "-S", $repoRoot,
        "-B", $BuildDirectory,
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        "-DOOT3D_FAST_DEV_LINK=ON",
        "-DTHREE_DS_RECOMP_FAST_DEV_LINK=ON",
        "-DTHREE_DS_RECOMP_ENABLE_DX11=OFF",
        "-DAUTOMATE_VCPKG_UPDATE=OFF",
        "-DOOT3D_WHOLE_AOT_THINLTO_CACHE_DIR=$thinLtoCache"
    )
    if ($requiresWholeAot) {
        $cmakeArguments += "-DOOT3D_REQUIRE_WHOLE_AOT=ON"
    }
    if ($needsRuntimeTests) {
        $cmakeArguments += "-DTHREE_DS_RECOMP_BUILD_TESTS=ON"
    }
    & $cmake @cmakeArguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$makeProgramSetting = Select-String -LiteralPath $cachePath `
    -Pattern '^CMAKE_MAKE_PROGRAM:(?:FILEPATH|UNINITIALIZED)=(.+)$' |
    Select-Object -First 1
if ($null -eq $makeProgramSetting) {
    throw "CMAKE_MAKE_PROGRAM is missing from $cachePath"
}
$ninja = $makeProgramSetting.Matches[0].Groups[1].Value
if (-not (Test-Path -LiteralPath $ninja)) {
    throw "Configured Ninja executable is missing: $ninja"
}

$metadataOutput = @(& $ninja -C $BuildDirectory -t recompact 2>&1)
if ($LASTEXITCODE -ne 0) {
    $metadataOutput | Write-Host
    throw "Ninja metadata in $BuildDirectory is corrupt and could not be repaired; refusing to start a build."
}
$metadataErrorPath = Join-Path $BuildDirectory ".ninja-deps-check.stderr"
try {
    & $ninja -C $BuildDirectory -t deps 1>$null 2>$metadataErrorPath
    $metadataExitCode = $LASTEXITCODE
    $metadataErrors = if (Test-Path -LiteralPath $metadataErrorPath) {
        @(Get-Content -LiteralPath $metadataErrorPath)
    } else {
        @()
    }
} finally {
    Remove-Item -LiteralPath $metadataErrorPath -Force -ErrorAction SilentlyContinue
}
if ($metadataExitCode -ne 0 -or $metadataErrors -match 'premature end of file') {
    $metadataErrors | Write-Host
    throw "Ninja dependency metadata in $BuildDirectory remains corrupt; refusing to trigger an uncontrolled rebuild."
}

$compileLimit = $null
$buildLimit = $null
if ($Target -eq "oot3d_native_game") {
    $compileLimit = $MaxNativeGameCompileActions
    $buildLimit = $MaxNativeGameBuildActions
} else {
    $compileLimit = $MaxDefaultCompileActions
    $buildLimit = $MaxDefaultBuildActions
}

$frozenBuildManifest = $null
if ($null -ne $compileLimit -and -not $AllowBroadRebuild -and $compileLimit -ge 0) {

    & $ninja -C $BuildDirectory build.ninja
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to refresh the Ninja build graph (exit $LASTEXITCODE)."
    }

    $frozenBuildManifest = Join-Path $BuildDirectory (".fast-dev-{0}.ninja" -f $Target)
    Copy-Item -LiteralPath (Join-Path $BuildDirectory "build.ninja") -Destination $frozenBuildManifest -Force
    try {
        $dryRunOutput = @(& $ninja -C $BuildDirectory -f (Split-Path $frozenBuildManifest -Leaf) -n $Target 2>&1)
        $dryRunExitCode = $LASTEXITCODE
        if ($dryRunExitCode -ne 0) {
            $dryRunOutput | Write-Host
            throw "Unable to inspect the pending $Target build actions (exit $dryRunExitCode)."
        }

        $buildActions = @($dryRunOutput | Where-Object {
            [string]$_ -match '^\[\d+/\d+\] '
        })
        $compileActions = @($buildActions | Where-Object {
            [string]$_ -match ' Building (?:C|CXX|OBJCXX) object '
        })
        $noWork = $dryRunOutput -match 'ninja: no work to do\.'
        if ($buildActions.Count -eq 0 -and -not $noWork) {
            $dryRunOutput | Select-Object -First 20 | Write-Host
            throw "Refusing $Target build because the Ninja preflight output could not be interpreted."
        }
        if ($compileActions.Count -gt $compileLimit -or
            $buildActions.Count -gt $buildLimit) {
            $buildActions | Select-Object -First 12 | Write-Host
            $message = "Refusing broad {0} rebuild: {1} compile and {2} total actions exceed the fast-dev limits " +
                       "({3} compile, {4} total). Fix the dependency fan-out, choose a narrower target, or explicitly " +
                       "rerun with -AllowBroadRebuild for an intentional full rebuild."
            throw ($message -f $Target, $compileActions.Count, $buildActions.Count,
                   $compileLimit, $buildLimit)
        }
        $message = "Fast-dev preflight for {0}: {1} compile and {2} total pending action(s) " +
                   "(limits {3}/{4})"
        Write-Host ($message -f $Target, $compileActions.Count, $buildActions.Count,
                    $compileLimit, $buildLimit)
    } catch {
        Remove-Item -LiteralPath $frozenBuildManifest -Force -ErrorAction SilentlyContinue
        $frozenBuildManifest = $null
        throw
    }
}

if ($PreflightOnly) {
    if ($null -ne $frozenBuildManifest) {
        Remove-Item -LiteralPath $frozenBuildManifest -Force -ErrorAction SilentlyContinue
    }
    Write-Host "Preflight completed; no compilation was started."
    exit 0
}

$buildItem = Get-Item -LiteralPath $BuildDirectory -Force
if ($buildItem.LinkType) {
    Write-Host "Build tree: $BuildDirectory -> $($buildItem.Target)"
} else {
    Write-Host "Build tree: $BuildDirectory"
}

$timer = [System.Diagnostics.Stopwatch]::StartNew()
try {
    if ($null -ne $frozenBuildManifest) {
        & $ninja -C $BuildDirectory -f (Split-Path $frozenBuildManifest -Leaf) -j $Parallel $Target
    } else {
        & $ninja -C $BuildDirectory -j $Parallel $Target
    }
    $buildExitCode = $LASTEXITCODE
} finally {
    if ($null -ne $frozenBuildManifest) {
        Remove-Item -LiteralPath $frozenBuildManifest -Force -ErrorAction SilentlyContinue
    }
}
$timer.Stop()
Write-Host ("Build target {0}: {1:N2} s (exit {2})" -f $Target, $timer.Elapsed.TotalSeconds, $buildExitCode)
exit $buildExitCode
} finally {
    $buildLock.Dispose()
}
