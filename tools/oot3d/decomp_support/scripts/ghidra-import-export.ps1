param(
    [string]$CodeBin = ".\work\extract\exefs\code.bin",
    [string]$GhidraRoot = "..\tools\ghidra\ghidra_12.1.2_PUBLIC",
    [string]$JdkRoot = "..\tools\jdk21",
    [string]$ProjectDir = ".\work\ghidra_project",
    [string]$ProjectName = "oot3d_code",
    [string]$ExportDir = ".\ghidra_export"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $CodeBin) -and (Test-Path -LiteralPath "..\work\extract\exefs\code.bin")) {
    $CodeBin = "..\work\extract\exefs\code.bin"
}
if (-not (Test-Path -LiteralPath $ProjectDir) -and (Test-Path -LiteralPath "..\work\ghidra_project")) {
    $ProjectDir = "..\work\ghidra_project"
}

$CodeBin = (Resolve-Path $CodeBin).Path
$GhidraRoot = (Resolve-Path $GhidraRoot).Path
$JdkRoot = (Resolve-Path $JdkRoot).Path
$ProjectDir = (New-Item -ItemType Directory -Force -Path $ProjectDir).FullName
$ExportDir = (New-Item -ItemType Directory -Force -Path $ExportDir).FullName

$env:JAVA_HOME = $JdkRoot
$env:PATH = "$env:JAVA_HOME\bin;$env:PATH"

$headless = Join-Path $GhidraRoot "support\analyzeHeadless.bat"
& $headless `
    $ProjectDir `
    $ProjectName `
    -import $CodeBin `
    -processor ARM:LE:32:v6 `
    -cspec default `
    -loader BinaryLoader `
    -loader-baseAddr 0x00100000 `
    -analysisTimeoutPerFile 1800 `
    -scriptPath ".\ghidra_scripts" `
    -postScript ExportOot3d.java $ExportDir
