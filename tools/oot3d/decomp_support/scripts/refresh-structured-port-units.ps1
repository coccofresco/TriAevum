param(
    [string]$Units = "metadata/structured_port_units.csv",
    [string[]]$Unit,
    [string[]]$PortFile,
    [switch]$SkipDefaultBuild,
    [switch]$SkipIndex
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$unitsPath = Join-Path $repoRoot $Units
if (-not (Test-Path -LiteralPath $unitsPath)) {
    throw "Structured port units file not found: $unitsPath"
}

Push-Location $repoRoot
try {
    python scripts\extract_n64_port_units.py
    if (-not $SkipDefaultBuild) {
        .\scripts\build-matched-objects.ps1
    }

    $rows = Import-Csv -LiteralPath $unitsPath
    if ($Unit -and $Unit.Count -gt 0) {
        $selectedUnits = @{}
        foreach ($item in $Unit) {
            $selectedUnits[$item] = $true
        }
        $rows = @($rows | Where-Object { $selectedUnits.ContainsKey($_.unit) })
    }
    if ($PortFile -and $PortFile.Count -gt 0) {
        $selectedPortFiles = @{}
        foreach ($item in $PortFile) {
            $selectedPortFiles[$item] = $true
        }
        $rows = @($rows | Where-Object { $selectedPortFiles.ContainsKey($_.port_file) })
    }
    if ($rows.Count -eq 0) {
        throw "No structured port units selected."
    }

    foreach ($row in $rows) {
        $params = @{
            Source = @($row.source)
            OutDir = $row.out_dir
        }
        if ($row.extra_cflag) {
            $params.ExtraCFlag = @($row.extra_cflag -split '\s+' | Where-Object { $_ })
        }
        .\scripts\build-matched-objects.ps1 @params

        python scripts\build_structured_port_status.py `
            --port-file $row.port_file `
            --structured-compare (Join-Path $row.out_dir "compare_matched_objects.json") `
            --out $row.status_report `
            --title $row.title `
            --n64-extracts $row.n64_extracts `
            --extra-cflag $row.extra_cflag
    }

    if (-not $SkipIndex) {
        python scripts\build_structured_port_index.py --units $Units
    }
}
finally {
    Pop-Location
}

Write-Host "Structured port units refreshed."
