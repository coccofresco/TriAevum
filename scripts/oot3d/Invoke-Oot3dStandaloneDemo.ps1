param(
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$ToolRoot = "",
    [string]$ReferenceTrace = "",
    [int]$FrameStep = 4,
    [switch]$Verify,
    [switch]$NativeHost,
    [switch]$NativeReadiness,
    [switch]$RequireReferenceTrace,
    [switch]$LaunchNativeHostViewer,
    [switch]$LaunchViewer
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$script = Join-Path $repoRoot "tools\oot3d\standalone_demo\oot3d_demo.py"
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}

if (-not (Test-Path -LiteralPath $script)) {
    throw "Standalone demo script not found: $script"
}

$args = @(
    $script,
    "all",
    "--repo-root", $repoRoot,
    "--tool-root", $ToolRoot,
    "--romfs", $RomFs,
    "--work-root", $WorkRoot,
    "--frame-step", $FrameStep
)

& python @args
if ($LASTEXITCODE -ne 0) {
    throw "OOT3D standalone demo gate failed with exit code $LASTEXITCODE"
}

if ($Verify) {
    $summary = Join-Path $WorkRoot "standalone_demo\link_house\demo_verify_summary.json"
    if (-not (Test-Path -LiteralPath $summary)) {
        throw "Verify summary was not written: $summary"
    }
    $json = Get-Content -LiteralPath $summary -Raw | ConvertFrom-Json
    if ($json.status -ne "valid" -or $json.issue_count -ne 0) {
        throw "Verify summary is not valid: $summary"
    }
}

if ($NativeHost) {
    $manifest = Join-Path $WorkRoot "standalone_demo\link_house\demo_manifest.json"
    $nativeHostScript = Join-Path $repoRoot "scripts\oot3d\Invoke-Oot3dNativeDemoHost.ps1"
    $nativeResourceProbeScript = Join-Path $repoRoot "scripts\oot3d\Invoke-Oot3dNativeResourceProbe.ps1"
    $nativeRuntimeProbeScript = Join-Path $repoRoot "scripts\oot3d\Invoke-Oot3dNativeRuntimeProbe.ps1"
    $nativeHostArgs = @{
        Manifest = $manifest
        Verify = $true
    }
    if (-not [string]::IsNullOrWhiteSpace($ReferenceTrace)) {
        $nativeHostArgs.ReferenceTrace = $ReferenceTrace
    }
    if ($RequireReferenceTrace) {
        $nativeHostArgs.RequireReference = $true
    }
    if ($LaunchNativeHostViewer) {
        $nativeHostArgs.LaunchViewer = $true
    }
    & $nativeHostScript @nativeHostArgs
    if ($LASTEXITCODE -ne 0) {
        throw "OOT3D native demo host wrapper failed with exit code $LASTEXITCODE"
    }
    $nativeContract = Join-Path $WorkRoot "standalone_demo\link_house\contracts\native_resource_contract.json"
    & $nativeResourceProbeScript -Contract $nativeContract -Verify
    if ($LASTEXITCODE -ne 0) {
        throw "OOT3D runtime/three_ds_recomp native resource probe failed with exit code $LASTEXITCODE"
    }
    & $nativeRuntimeProbeScript -Manifest $manifest -Verify
    if ($LASTEXITCODE -ne 0) {
        throw "OOT3D runtime/three_ds_recomp native runtime probe failed with exit code $LASTEXITCODE"
    }
    & python $script native-readiness --manifest $manifest
    if ($LASTEXITCODE -ne 0) {
        throw "OOT3D native readiness refresh failed with exit code $LASTEXITCODE"
    }
}

if ($NativeReadiness) {
    $readiness = Join-Path $WorkRoot "standalone_demo\link_house\native_demo_readiness.json"
    if (-not (Test-Path -LiteralPath $readiness)) {
        throw "Native readiness summary was not written: $readiness"
    }
    $readinessJson = Get-Content -LiteralPath $readiness -Raw | ConvertFrom-Json
    Write-Host ("Native demo readiness: {0}; ready={1}; blockers={2}" -f `
        $readinessJson.status, $readinessJson.native_demo_ready, $readinessJson.blocker_count)
}

if ($LaunchViewer) {
    $manifest = Join-Path $WorkRoot "standalone_demo\link_house\demo_manifest.json"
    $viewer = Join-Path $repoRoot "tools\oot3d\standalone_demo\oot3d_viewer.py"
    & python $viewer --manifest $manifest
    if ($LASTEXITCODE -ne 0) {
        throw "OOT3D standalone demo viewer failed with exit code $LASTEXITCODE"
    }
}
