param(
    [string]$GhidraRoot = "..\tools\ghidra\ghidra_12.1.2_PUBLIC",
    [string]$JdkRoot = "..\tools\jdk21",
    [string]$ProjectDir = ".\work\ghidra_project",
    [string]$ProjectName = "oot3d_code",
    [string]$ExportDir = ".\ghidra_export"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $ProjectDir) -and (Test-Path -LiteralPath "..\work\ghidra_project")) {
    $ProjectDir = "..\work\ghidra_project"
}

$GhidraRoot = (Resolve-Path $GhidraRoot).Path
$JdkRoot = (Resolve-Path $JdkRoot).Path
$ProjectDir = (Resolve-Path $ProjectDir).Path
$ExportDir = (New-Item -ItemType Directory -Force -Path $ExportDir).FullName

Remove-Item (Join-Path $ExportDir "functions.csv") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $ExportDir "disassembly.txt") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $ExportDir "decompiled\*.c") -Force -ErrorAction SilentlyContinue

$env:JAVA_HOME = $JdkRoot
$env:PATH = "$env:JAVA_HOME\bin;$env:PATH"

$headless = Join-Path $GhidraRoot "support\analyzeHeadless.bat"
& $headless `
    $ProjectDir `
    $ProjectName `
    -process code.bin `
    -noanalysis `
    -scriptPath ".\ghidra_scripts" `
    -postScript ExportOot3d.java $ExportDir
