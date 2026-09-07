param(
    [string]$GhidraRoot = "..\tools\ghidra\ghidra_12.1.2_PUBLIC",
    [string]$JdkRoot = "..\tools\jdk21",
    [string]$ProjectDir = ".\work\ghidra_project",
    [string]$ProjectName = "oot3d_code",
    [string]$SplitsCsv = ".\analysis\direct_target_split_symbol_candidates.csv",
    [switch]$ExportSelected,
    [switch]$IncludeCallers,
    [switch]$IncludeCallees,
    [switch]$KeepSelectedArtifacts
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Push-Location $repoRoot
try {
    if (-not (Test-Path -LiteralPath $ProjectDir) -and (Test-Path -LiteralPath "..\work\ghidra_project")) {
        $ProjectDir = "..\work\ghidra_project"
    }

    $GhidraRoot = (Resolve-Path $GhidraRoot).Path
    $JdkRoot = (Resolve-Path $JdkRoot).Path
    $ProjectDir = (Resolve-Path $ProjectDir).Path
    $SplitsCsv = (Resolve-Path $SplitsCsv).Path

    $splitRows = @(Import-Csv -LiteralPath $SplitsCsv | Where-Object {
        $_.entry -and $_.new_name -and ((-not $_.approved) -or $_.approved -match '^(1|true|yes|y|approved|promote)$')
    })
    if ($splitRows.Count -eq 0) {
        Write-Host "No approved function split rows in $SplitsCsv."
        return
    }

    $env:JAVA_HOME = $JdkRoot
    $env:PATH = "$env:JAVA_HOME\bin;$env:PATH"

    $headless = Join-Path $GhidraRoot "support\analyzeHeadless.bat"
    & $headless `
        $ProjectDir `
        $ProjectName `
        -process code.bin `
        -noanalysis `
        -scriptPath ".\ghidra_scripts" `
        -postScript ApplyFunctionSplits.java $SplitsCsv

    if ($ExportSelected) {
        $entriesFile = "analysis\selected_function_split_entries.txt"
        Set-Content -LiteralPath $entriesFile -Value @($splitRows | ForEach-Object { $_.entry }) -Encoding UTF8
        .\scripts\ghidra-export-selected.ps1 `
            -GhidraRoot $GhidraRoot `
            -JdkRoot $JdkRoot `
            -ProjectDir $ProjectDir `
            -ProjectName $ProjectName `
            -EntriesFile $entriesFile `
            -IncludeCallers:$IncludeCallers.IsPresent `
            -IncludeCallees:$IncludeCallees.IsPresent `
            -KeepSelectedArtifacts:$KeepSelectedArtifacts.IsPresent
    }
}
finally {
    Pop-Location
}
