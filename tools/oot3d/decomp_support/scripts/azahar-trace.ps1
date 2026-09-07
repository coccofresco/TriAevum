param(
    [string]$AzaharRoot = (Resolve-Path "$PSScriptRoot\..\..\azahar_instrumented").Path,
    [string]$TracePath = (Join-Path (Resolve-Path "$PSScriptRoot\..").Path "traces\oot3d-first-run.jsonl"),
    [string]$Allowlist = "0x00375bcc,0x003731e0,0x003679b4,0x0036ae14,0x002cfca0,0x00250ad0,0x00416608,0x002e2e60,0x0044e2d0,0x002c5ba0",
    [int]$MaxEvents = 200000,
    [int]$Sample = 1,
    [switch]$Configure,
    [switch]$Build,
    [switch]$Run
)

$ErrorActionPreference = "Stop"

$msysBash = "C:\msys64\usr\bin\bash.exe"
$mingwBin = "C:\msys64\mingw64\bin"
$buildDir = Join-Path $AzaharRoot "build-oot3d-trace-mingw-nolto"
$exePath = Join-Path $buildDir "bin\Release\azahar.exe"

if (-not (Test-Path $msysBash)) {
    throw "MSYS2 bash was not found at $msysBash"
}

if (-not (Test-Path $mingwBin)) {
    throw "MSYS2 MinGW bin directory was not found at $mingwBin"
}

$azaharRootUnix = ($AzaharRoot -replace "\\", "/") -replace "^([A-Za-z]):", { "/" + $_.Groups[1].Value.ToLower() }

if ($Configure) {
    & $msysBash -lc "export PATH=/mingw64/bin:/usr/bin:`$PATH; cd '$azaharRootUnix' && cmake -S . -B build-oot3d-trace-mingw-nolto -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_LTO=OFF -DENABLE_QT=ON -DENABLE_QT_TRANSLATION=OFF -DENABLE_SDL2=ON -DENABLE_QT_UPDATE_CHECKER=OFF -DENABLE_WEB_SERVICE=OFF -DENABLE_SCRIPTING=OFF -DENABLE_TESTS=OFF"
    if ($LASTEXITCODE -ne 0) {
        throw "Azahar configure failed with exit code $LASTEXITCODE"
    }
}

if ($Build) {
    & $msysBash -lc "export PATH=/mingw64/bin:/usr/bin:`$PATH; cd '$azaharRootUnix' && cmake --build build-oot3d-trace-mingw-nolto --target citra_meta --parallel 8"
    if ($LASTEXITCODE -ne 0) {
        throw "Azahar build failed with exit code $LASTEXITCODE"
    }
}

if ($Run) {
    if (-not (Test-Path $exePath)) {
        throw "Azahar executable was not found at $exePath. Run with -Configure -Build first."
    }

    $traceDir = Split-Path -Parent $TracePath
    if (-not (Test-Path $traceDir)) {
        New-Item -ItemType Directory -Path $traceDir | Out-Null
    }

    $env:PATH = "$mingwBin;$env:PATH"
    $env:OOT3D_TRACE_PATH = $TracePath
    $env:OOT3D_TRACE_ALLOWLIST = $Allowlist
    $env:OOT3D_TRACE_MAX_EVENTS = "$MaxEvents"
    $env:OOT3D_TRACE_SAMPLE = "$Sample"

    Write-Host "Starting Azahar with OOT3D_TRACE_PATH=$TracePath"
    Write-Host "Disable CPU JIT in Azahar settings before collecting trace data."
    & $exePath
}

if (-not ($Configure -or $Build -or $Run)) {
    Write-Host "Usage examples:"
    Write-Host "  .\scripts\azahar-trace.ps1 -Configure -Build"
    Write-Host "  .\scripts\azahar-trace.ps1 -Run"
    Write-Host "  .\scripts\azahar-trace.ps1 -Run -Sample 100"
}
