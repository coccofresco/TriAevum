param(
    [string]$GhidraRoot = "..\tools\ghidra\ghidra_12.1.2_PUBLIC",
    [string]$JdkRoot = "..\tools\jdk21",
    [string]$ProjectDir = ".\work\ghidra_project",
    [string]$ProjectName = "oot3d_code",
    [string]$SymbolsCsv = ".\symbols\manual_symbols.csv",
    [switch]$Reexport
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $ProjectDir) -and (Test-Path -LiteralPath "..\work\ghidra_project")) {
    $ProjectDir = "..\work\ghidra_project"
}

$GhidraRoot = (Resolve-Path $GhidraRoot).Path
$JdkRoot = (Resolve-Path $JdkRoot).Path
$ProjectDir = (Resolve-Path $ProjectDir).Path
$SymbolsCsv = (Resolve-Path $SymbolsCsv).Path

$env:JAVA_HOME = $JdkRoot
$env:PATH = "$env:JAVA_HOME\bin;$env:PATH"

$headless = Join-Path $GhidraRoot "support\analyzeHeadless.bat"
& $headless `
    $ProjectDir `
    $ProjectName `
    -process code.bin `
    -noanalysis `
    -scriptPath ".\ghidra_scripts" `
    -postScript ApplyManualSymbols.java $SymbolsCsv

if ($Reexport) {
    .\scripts\ghidra-reexport.ps1 -GhidraRoot $GhidraRoot -JdkRoot $JdkRoot -ProjectDir $ProjectDir -ProjectName $ProjectName
}
