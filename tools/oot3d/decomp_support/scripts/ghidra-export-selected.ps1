param(
    [string]$GhidraRoot = "..\tools\ghidra\ghidra_12.1.2_PUBLIC",
    [string]$JdkRoot = "..\tools\jdk21",
    [string]$ProjectDir = ".\work\ghidra_project",
    [string]$ProjectName = "oot3d_code",
    [string]$ExportDir = ".\ghidra_export",
    [string]$SymbolsCsv = ".\symbols\manual_symbols.csv",
    [string[]]$Entries = @(),
    [string]$EntriesFile = "",
    [switch]$PendingManualSymbols,
    [switch]$IncludeCallers,
    [switch]$IncludeCallees,
    [switch]$ApplyManualSymbols,
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
    $ExportDir = (New-Item -ItemType Directory -Force -Path $ExportDir).FullName
    $SymbolsCsv = (Resolve-Path $SymbolsCsv).Path

    $resolvedEntriesFile = $EntriesFile
    $createdEntriesFile = $false
    if ($PendingManualSymbols) {
        & python scripts\build_symbol_overlay.py | Out-Host
        $overlay = Get-Content analysis\symbol_overlay.json -Raw | ConvertFrom-Json
        $pendingEntries = @($overlay.rows | Where-Object { $_.pending_ghidra_export } | ForEach-Object { $_.entry })
        if ($pendingEntries.Count -eq 0) {
            Write-Host "No pending manual symbols need selective Ghidra export."
            return
        }
        $resolvedEntriesFile = "analysis\selected_export_entries.txt"
        Set-Content -LiteralPath $resolvedEntriesFile -Value $pendingEntries -Encoding UTF8
        $createdEntriesFile = $true
    }
    elseif ($Entries.Count -gt 0) {
        $resolvedEntriesFile = "analysis\selected_export_entries.txt"
        Set-Content -LiteralPath $resolvedEntriesFile -Value $Entries -Encoding UTF8
        $createdEntriesFile = $true
    }
    elseif (-not $EntriesFile) {
        throw "Provide -Entries, -EntriesFile, or -PendingManualSymbols."
    }

    $resolvedEntriesFile = (Resolve-Path $resolvedEntriesFile).Path

    $env:JAVA_HOME = $JdkRoot
    $env:PATH = "$env:JAVA_HOME\bin;$env:PATH"

    $headless = Join-Path $GhidraRoot "support\analyzeHeadless.bat"
    if ($ApplyManualSymbols) {
        & $headless `
            $ProjectDir `
            $ProjectName `
            -process code.bin `
            -noanalysis `
            -scriptPath ".\ghidra_scripts" `
            -postScript ApplyManualSymbols.java $SymbolsCsv
    }

    Remove-Item (Join-Path $ExportDir "functions_selected.csv") -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $ExportDir "disassembly_selected.txt") -Force -ErrorAction SilentlyContinue

    & $headless `
        $ProjectDir `
        $ProjectName `
        -process code.bin `
        -noanalysis `
        -scriptPath ".\ghidra_scripts" `
        -postScript ExportOot3dSelected.java $ExportDir $resolvedEntriesFile $IncludeCallers.IsPresent $IncludeCallees.IsPresent

    $selectedCsv = Join-Path $ExportDir "functions_selected.csv"
    if (-not (Test-Path -LiteralPath $selectedCsv)) {
        throw "Selective Ghidra export did not produce $selectedCsv. Check the headless log above for script errors."
    }

    $fullFunctionsCsv = Join-Path $ExportDir "functions.csv"
    if (Test-Path -LiteralPath $fullFunctionsCsv) {
        & python scripts\merge_selected_ghidra_export.py --export-dir $ExportDir
        if (-not $KeepSelectedArtifacts) {
            Remove-Item -LiteralPath $selectedCsv -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath (Join-Path $ExportDir "disassembly_selected.txt") -Force -ErrorAction SilentlyContinue
        }
    }

    if ($createdEntriesFile -and -not $KeepSelectedArtifacts) {
        Remove-Item -LiteralPath $resolvedEntriesFile -Force -ErrorAction SilentlyContinue
    }
}
finally {
    Pop-Location
}
