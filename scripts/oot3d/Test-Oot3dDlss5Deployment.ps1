[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [string]$TargetRoot,
    [string]$SourcePath = "",
    [string]$JsonReport = "",
    [switch]$Repair
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$expectedRuntimeVersion = "310.8.0.0"
$expectedRuntimeSha256 =
    "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E"

function Get-FileEvidence {
    param([Parameter(Mandatory = $true)][string]$Path)

    if(-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return [pscustomobject]@{
            Path = [System.IO.Path]::GetFullPath($Path)
            Exists = $false
            Version = ""
            Sha256 = ""
            SignatureStatus = "Missing"
            Signer = ""
        }
    }

    $item = Get-Item -LiteralPath $Path
    $signature = Get-AuthenticodeSignature -LiteralPath $item.FullName
    $signer = ""
    if($null -ne $signature.SignerCertificate) {
        $signer = [string]$signature.SignerCertificate.Subject
    }
    return [pscustomobject]@{
        Path = $item.FullName
        Exists = $true
        Version = [string]$item.VersionInfo.FileVersion
        Sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
        SignatureStatus = [string]$signature.Status
        Signer = $signer
    }
}

function Test-VerifiedNeuralRuntime {
    param([Parameter(Mandatory = $true)]$Evidence)

    return $Evidence.Exists -and
        $Evidence.Version.Replace(",", ".").Replace(" ", "") -eq
            $expectedRuntimeVersion -and
        $Evidence.Sha256 -eq $expectedRuntimeSha256 -and
        $Evidence.SignatureStatus -eq "Valid" -and
        $Evidence.Signer -match "NVIDIA"
}

function Find-VerifiedSourceRuntime {
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolved = [System.IO.Path]::GetFullPath($Path)
    $candidates = @()
    if(Test-Path -LiteralPath $resolved -PathType Leaf) {
        $candidates = @(Get-Item -LiteralPath $resolved)
    } elseif(Test-Path -LiteralPath $resolved -PathType Container) {
        $candidates = @(
            Get-ChildItem -LiteralPath $resolved -Filter "nvngx_dlssnr.dll" `
                -File -Recurse -ErrorAction SilentlyContinue)
    } else {
        throw "DLSSNR source path does not exist: $resolved"
    }

    foreach($candidate in $candidates) {
        $evidence = Get-FileEvidence -Path $candidate.FullName
        if(Test-VerifiedNeuralRuntime -Evidence $evidence) {
            return $evidence
        }
    }
    return $null
}

$resolvedTargetRoot = [System.IO.Path]::GetFullPath($TargetRoot)
if(-not (Test-Path -LiteralPath $resolvedTargetRoot -PathType Container)) {
    throw "DLSS5 target directory does not exist: $resolvedTargetRoot"
}

$runtimePath = Join-Path $resolvedTargetRoot "nvngx_dlssnr.dll"
$dlssPath = Join-Path $resolvedTargetRoot "nvngx_dlss.dll"
$addon = Get-ChildItem -LiteralPath $resolvedTargetRoot -File -ErrorAction Stop |
    Where-Object { $_.Name -match '^renodx-dlss5.*\.addon64$' } |
    Select-Object -First 1

$runtimeEvidence = Get-FileEvidence -Path $runtimePath
$dlssEvidence = Get-FileEvidence -Path $dlssPath
$addonEvidence = $null
if($null -ne $addon) {
    $addonEvidence = Get-FileEvidence -Path $addon.FullName
}

$sourceEvidence = $null
$backupPath = ""
$repairApplied = $false
if($Repair.IsPresent -and
   -not (Test-VerifiedNeuralRuntime -Evidence $runtimeEvidence)) {
    if([string]::IsNullOrWhiteSpace($SourcePath)) {
        throw "Repair requires -SourcePath pointing to a lawfully obtained NVIDIA-signed nvngx_dlssnr.dll"
    }

    $sourceEvidence = Find-VerifiedSourceRuntime -Path $SourcePath
    if($null -eq $sourceEvidence) {
        throw "Source does not contain the verified NVIDIA-signed DLSSNR 310.8 runtime"
    }

    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $backupPath = "$runtimePath.bad-signature-backup-$timestamp"
    if($PSCmdlet.ShouldProcess($runtimePath, "Back up and replace invalid DLSSNR runtime")) {
        if($runtimeEvidence.Exists) {
            Copy-Item -LiteralPath $runtimePath -Destination $backupPath
        }
        Copy-Item -LiteralPath $sourceEvidence.Path -Destination $runtimePath -Force
        $runtimeEvidence = Get-FileEvidence -Path $runtimePath
        if(-not (Test-VerifiedNeuralRuntime -Evidence $runtimeEvidence)) {
            throw "DLSSNR replacement failed post-copy verification"
        }
        $repairApplied = $true
    }
}

$reshadeLog = Join-Path $resolvedTargetRoot "ReShade.log"
$lastFeatureFailure = ""
if(Test-Path -LiteralPath $reshadeLog -PathType Leaf) {
    $failure = Select-String -LiteralPath $reshadeLog `
        -Pattern "feature 18 create failed" | Select-Object -Last 1
    if($null -ne $failure) {
        $lastFeatureFailure = $failure.Line.Trim()
    }
}

$runtimeVerified = Test-VerifiedNeuralRuntime -Evidence $runtimeEvidence
$result = [pscustomobject]@{
    Format = "oot3d_dlss5_deployment_report_v1"
    TargetRoot = $resolvedTargetRoot
    Addon = $addonEvidence
    DlssRuntime = $dlssEvidence
    NeuralRuntime = $runtimeEvidence
    NeuralRuntimeVerified = $runtimeVerified
    RepairApplied = $repairApplied
    BackupPath = $backupPath
    LastFeatureFailure = $lastFeatureFailure
    Diagnosis = if($runtimeVerified) {
        "DLSSNR runtime integrity is valid; continue with NGX/add-on contract diagnostics."
    } elseif($runtimeEvidence.SignatureStatus -eq "HashMismatch") {
        "DLSSNR runtime was modified after signing; NGX may return PlatformError (0xBAD00002)."
    } else {
        "DLSSNR runtime is missing or does not match the verified signed 310.8 build."
    }
}

if(-not [string]::IsNullOrWhiteSpace($JsonReport)) {
    $resolvedReport = [System.IO.Path]::GetFullPath($JsonReport)
    $reportParent = Split-Path -Parent $resolvedReport
    if(-not [string]::IsNullOrWhiteSpace($reportParent)) {
        New-Item -ItemType Directory -Force -Path $reportParent | Out-Null
    }
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resolvedReport
}

$result | ConvertTo-Json -Depth 5
if(-not $runtimeVerified) {
    exit 2
}

