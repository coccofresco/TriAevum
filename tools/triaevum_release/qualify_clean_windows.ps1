param(
    [Parameter(Mandatory=$true)][string]$Package,
    [Parameter(Mandatory=$true)][string]$Rom,
    [Parameter(Mandatory=$true)][string]$Output
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Run inside the clean test machine, never against an existing installation.
$packagePath = (Resolve-Path -LiteralPath $Package).Path
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$outputPath = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $outputPath) { throw 'Output must not exist.' }
foreach ($source in @($packagePath)) {
    if ($outputPath.StartsWith($source.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Output must be outside the input directories.'
    }
}
foreach ($file in @('TriAevumForge.exe', 'TriAevum.exe', 'release-manifest.json', 'recipes/precompiled-titles.json')) {
    if (-not (Test-Path -LiteralPath (Join-Path $packagePath $file) -PathType Leaf)) {
        throw "Missing public package file: $file"
    }
}

$null = New-Item -ItemType Directory -Path $outputPath
$receipt = [ordered]@{
    format = 'triaevum_clean_windows_qualification_v1'
    status = 'started'
    scope = 'frozen_rom_import_precompiled_boot_no_sdk_or_build_cache'
    clean_environment_operator_attestation_required = $true
    started_utc = [DateTime]::UtcNow.ToString('o')
    os = [Environment]::OSVersion.VersionString
    machine = $env:COMPUTERNAME
    processors = [Environment]::ProcessorCount
    package_manifest_sha256 = (Get-FileHash -LiteralPath (Join-Path $packagePath 'release-manifest.json')).Hash
    rom_sha256 = (Get-FileHash -LiteralPath $romPath).Hash
    host_tools_found = @()
    stages = @()
}
foreach ($name in @('cl.exe', 'clang-cl.exe', 'msbuild.exe', 'python.exe', 'WindowsSandbox.exe')) {
    $receipt.host_tools_found += @(Get-Command $name -ErrorAction SilentlyContinue |
        Select-Object Name,Source)
}
# Inventory is evidence, not proof that an unlisted installation cannot exist.
$receipt.development_directories = @(
    @("${env:ProgramFiles}\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio",
      "${env:ProgramFiles(x86)}\Windows Kits") | Where-Object { Test-Path -LiteralPath $_ }
)
$installation = Join-Path $outputPath 'installation'

function Invoke-RecordedProcess([string]$Name, [string]$Executable, [string[]]$Arguments, [int]$TimeoutSeconds) {
    # All arguments originate here; reject quotes to avoid changing Windows argv.
    if (@($Arguments | Where-Object { $_.Contains('"') }).Count) { throw 'Quote in process argument.' }
    $commandLine = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $Executable -ArgumentList $commandLine -WorkingDirectory $installation `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $outputPath "$Name.stdout.log") `
        -RedirectStandardError (Join-Path $outputPath "$Name.stderr.log")
    try {
        $null = $process.Handle
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "$Name timed out; see its logs."
        }
        $process.Refresh()
        $script:receipt.stages += [ordered]@{ name=$Name; seconds=$timer.Elapsed.TotalSeconds; exit_code=$process.ExitCode }
        if ($process.ExitCode -ne 0) { throw "$Name failed ($($process.ExitCode)); see its logs." }
    } finally { $process.Dispose() }
}

try {
    Copy-Item -LiteralPath $packagePath -Destination $installation -Recurse
    $data = Join-Path $installation 'data'
    if (Test-Path -LiteralPath $data) { throw 'Package already contains private data.' }
    $null = New-Item -ItemType Directory -Path $data
    Invoke-RecordedProcess 'forge' (Join-Path $installation 'TriAevumForge.exe') @(
        '--install-worker', '--rom', $romPath, '--data-root', $data, '--owner-pid', "$PID") 180
    $events = @(Get-Content -LiteralPath (Join-Path $outputPath 'forge.stdout.log') |
        Where-Object { $_ -match '^\s*\{"forge_worker_event":' } |
        ForEach-Object { $_ | ConvertFrom-Json })
    if (-not @($events | Where-Object { $_.forge_worker_event -eq 'complete' }).Count) {
        throw 'Forge exited without a complete installation event.'
    }
    $complete = @($events | Where-Object { $_.forge_worker_event -eq 'complete' })[-1].payload
    if ($complete.build.install_model -ne 'precompiled_title_rom_import_v1' -or $complete.build.objects_compiled -ne 0 `
        -or (Test-Path -LiteralPath (Join-Path $data 'translator-cache')) `
        -or (Test-Path -LiteralPath (Join-Path $data 'toolchain'))) {
        throw 'User installation performed unexpected compilation or SDK preparation.'
    }
    $receipt.forge_install_seconds = $complete.install_wall_seconds
    $statePath = Join-Path $outputPath 'runtime.json'
    $capturePath = Join-Path $outputPath 'framebuffer.bmp'
    Invoke-RecordedProcess 'runtime' (Join-Path $installation 'TriAevum.exe') @(
        '--launch-profile', (Join-Path $installation 'TriAevum.launch.json'),
        '--frames', '0', '--max-seconds', '30', '--benchmark-warmup-frames', '60',
        '--output', $statePath, '--screenshot', $capturePath, '--screenshot-start-frame', '600') 90
    $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    if ($state.ui_profile -ne 'topscreen' -or -not $state.frame_rate.visual_interpolation_active `
        -or $state.frame_rate.visual_sample_multiplier -ne 2 -or -not (Test-Path -LiteralPath $capturePath)) {
        throw 'Runtime did not demonstrate the default TopScreen/x2 capture contract.'
    }
    $receipt.status = 'import_and_boot_passed_pending_visual_audio_and_environment_review'
    $receipt.benchmark_window = $state.benchmark_window
} catch {
    $receipt.status = 'failed'
    $receipt.error = $_.Exception.Message
    throw
} finally {
    $receipt.finished_utc = [DateTime]::UtcNow.ToString('o')
    $receipt | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $outputPath 'qualification.json') -Encoding UTF8
}
