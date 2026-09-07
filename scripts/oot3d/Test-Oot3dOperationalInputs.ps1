param(
    [string]$Manifest = "",
    [switch]$IncludeOptional,
    [switch]$AsJson
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($Manifest)) {
    $Manifest = Join-Path $repoRoot "tools\oot3d\operational_inputs.json"
}

if (-not (Test-Path -LiteralPath $Manifest)) {
    throw "OOT3D operational input manifest not found: $Manifest"
}

$config = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
$vars = @{}
foreach ($property in $config.variables.PSObject.Properties) {
    $vars[$property.Name] = [string]$property.Value
}
$vars["repoRoot"] = [string]$repoRoot

function Expand-Oot3dOperationalPath {
    param([string]$Path)

    $expanded = $Path
    for ($pass = 0; $pass -le $vars.Count; $pass++) {
        $previous = $expanded
        foreach ($key in $vars.Keys) {
            $expanded = $expanded.Replace(('${' + $key + '}'), [string]$vars[$key])
        }
        if ($expanded -eq $previous) {
            return [Environment]::ExpandEnvironmentVariables($expanded)
        }
    }
    throw "Operational input variables contain a substitution cycle: $Path"
}

$rows = New-Object System.Collections.Generic.List[object]

foreach ($entry in @($config.entries)) {
    $required = [bool]$entry.required
    if (-not $required -and -not $IncludeOptional) {
        continue
    }

    $path = Expand-Oot3dOperationalPath ([string]$entry.path)
    $exists = Test-Path -LiteralPath $path
    $kindOk = $false
    $lengthOk = $true
    $countOk = $true
    $length = $null
    $fileCount = $null
    $status = "ok"

    if ($exists) {
        $item = Get-Item -LiteralPath $path
        if ([string]$entry.kind -eq "directory") {
            $kindOk = $item.PSIsContainer
            if ($entry.PSObject.Properties.Name -contains "min_file_count") {
                $fileCount = @(Get-ChildItem -LiteralPath $path -File -ErrorAction SilentlyContinue).Count
                $countOk = $fileCount -ge [int64]$entry.min_file_count
            }
        } elseif ([string]$entry.kind -eq "file") {
            $kindOk = -not $item.PSIsContainer
            if ($kindOk) {
                $length = [int64]$item.Length
                if ($entry.PSObject.Properties.Name -contains "min_length") {
                    $lengthOk = $length -ge [int64]$entry.min_length
                }
            }
        } else {
            $kindOk = $true
        }
    }

    if (-not $exists) {
        $status = "missing"
    } elseif (-not $kindOk) {
        $status = "wrong_kind"
    } elseif (-not $lengthOk) {
        $status = "too_small"
    } elseif (-not $countOk) {
        $status = "too_few_files"
    }

    $rows.Add([pscustomobject]@{
        id = [string]$entry.id
        required = $required
        kind = [string]$entry.kind
        status = $status
        path = $path
        length = $length
        file_count = $fileCount
        purpose = [string]$entry.purpose
    })
}

$missingRequired = @($rows | Where-Object { $_.required -and $_.status -ne "ok" })

if ($AsJson) {
    [pscustomobject]@{
        format = "oot3d_operational_inputs_check_v1"
        manifest = (Resolve-Path -LiteralPath $Manifest).Path
        status = if ($missingRequired.Count -eq 0) { "ok" } else { "failed" }
        missing_required_count = $missingRequired.Count
        rows = $rows
    } | ConvertTo-Json -Depth 6
} else {
    $rows | Sort-Object required, id -Descending | Format-Table id, required, kind, status, path -AutoSize
}

if ($missingRequired.Count -gt 0) {
    $ids = ($missingRequired | ForEach-Object { $_.id }) -join ", "
    throw "OOT3D operational input check failed for required entries: $ids"
}
