param(
    [Parameter(Mandatory = $true)]
    [string[]]$Screenshot,
    [string]$PaletteCsv = "I:\oot3dre_work\spot04\labeled_uv_overlay\spot04_labeled_uv_palette.csv",
    [string]$OutputRoot = "I:\oot3dre_work\spot04\labeled_uv_overlay\screenshot_decode",
    [ValidateRange(0, 3)]
    [int]$Tolerance5 = 1,
    [ValidateRange(1, 16)]
    [int]$SamplingStep = 1,
    [ValidateRange(1, 1000000)]
    [int]$MinPixelCount = 24
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Convert-Rgb8ToRgb5([int]$Value) {
    return [Math]::Min(31, [Math]::Max(0, [int][Math]::Round($Value * 31.0 / 255.0)))
}

function Convert-Rgb5ToRgb8([int]$Value) {
    return (($Value -shl 3) -bor ($Value -shr 2)) -band 0xFF
}

function Get-Rgb5Key([int]$R5, [int]$G5, [int]$B5) {
    return "$R5,$G5,$B5"
}

function Add-LookupValue($Lookup, [string]$Key, $Row) {
    if (-not $Lookup.ContainsKey($Key)) {
        $Lookup[$Key] = $Row
        return
    }
    if ($Lookup[$Key] -eq $script:AmbiguousMarker) {
        return
    }
    if ([string]$Lookup[$Key].palette_key -ne [string]$Row.palette_key) {
        $Lookup[$Key] = $script:AmbiguousMarker
    }
}

function New-Bitmap32([string]$Path) {
    $image = [System.Drawing.Image]::FromFile($Path)
    try {
        $bitmap = New-Object System.Drawing.Bitmap $image.Width, $image.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.DrawImage($image, 0, 0, $image.Width, $image.Height)
        }
        finally {
            $graphics.Dispose()
        }
        return $bitmap
    }
    finally {
        $image.Dispose()
    }
}

Add-Type -AssemblyName System.Drawing
Require-Path $PaletteCsv "Machine UV palette"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$rawPaletteRows = @(
    Import-Csv -LiteralPath $PaletteCsv | Where-Object {
        [string]$_.diagnostic_mode -eq "Machine" -and -not [string]::IsNullOrWhiteSpace([string]$_.machine_code)
    }
)
if ($rawPaletteRows.Count -eq 0) {
    throw "Palette has no machine-readable rows: $PaletteCsv"
}

$paletteRows = @(
    $rawPaletteRows | ForEach-Object {
        $r5 = [int]$_.r5
        $g5 = [int]$_.g5
        $b5 = [int]$_.b5
        [pscustomobject]([ordered]@{
            palette_key = "{0}:{1}" -f $_.texture_debug_id, $_.cell_id
            rgb5_key = Get-Rgb5Key -R5 $r5 -G5 $g5 -B5 $b5
            texture_debug_id = [string]$_.texture_debug_id
            texture_debug_ordinal = [int]$_.texture_debug_ordinal
            label = [string]$_.label
            cell_id = [string]$_.cell_id
            cell_ordinal = [int]$_.cell_ordinal
            cell_x = [int]$_.cell_x
            cell_y = [int]$_.cell_y
            grid = "{0}x{1}" -f $_.grid_cells_x, $_.grid_cells_y
            machine_code = [int]$_.machine_code
            machine_code_hex = [string]$_.machine_code_hex
            r5 = $r5
            g5 = $g5
            b5 = $b5
            r = [int]$_.r
            g = [int]$_.g
            b = [int]$_.b
            room = [int]$_.room
            material_hex = [string]$_.material_hex
            material_index = [int]$_.material_index
            stage = [string]$_.stage
            original_texture_name = [string]$_.original_texture_name
            original_texture_path = [string]$_.original_texture_path
            material_display_list = [string]$_.material_display_list
            mesh_indices = [string]$_.mesh_indices
            shape_indices = [string]$_.shape_indices
        })
    }
)

$script:AmbiguousMarker = "__AMBIGUOUS__"
$exactLookup = @{}
$tolerantLookup = @{}
foreach ($row in $paletteRows) {
    Add-LookupValue -Lookup $exactLookup -Key $row.rgb5_key -Row $row
    for ($dr = -$Tolerance5; $dr -le $Tolerance5; $dr++) {
        for ($dg = -$Tolerance5; $dg -le $Tolerance5; $dg++) {
            for ($db = -$Tolerance5; $db -le $Tolerance5; $db++) {
                $r5 = [Math]::Min(31, [Math]::Max(0, $row.r5 + $dr))
                $g5 = [Math]::Min(31, [Math]::Max(0, $row.g5 + $dg))
                $b5 = [Math]::Min(31, [Math]::Max(0, $row.b5 + $db))
                Add-LookupValue -Lookup $tolerantLookup -Key (Get-Rgb5Key -R5 $r5 -G5 $g5 -B5 $b5) -Row $row
            }
        }
    }
}

$allSummaries = New-Object "System.Collections.Generic.List[object]"

foreach ($screenshotPath in $Screenshot) {
    Require-Path $screenshotPath "Screenshot"
    $bitmap = New-Bitmap32 -Path $screenshotPath
    $mask = New-Object System.Drawing.Bitmap $bitmap.Width, $bitmap.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $bitmap.Width, $bitmap.Height
    $sourceData = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $maskData = $mask.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $sourceStride = [Math]::Abs($sourceData.Stride)
        $maskStride = [Math]::Abs($maskData.Stride)
        $sourceBytes = New-Object byte[] ($sourceStride * $bitmap.Height)
        $maskBytes = New-Object byte[] ($maskStride * $bitmap.Height)
        [System.Runtime.InteropServices.Marshal]::Copy($sourceData.Scan0, $sourceBytes, 0, $sourceBytes.Length)

        $statsByKey = @{}
        $sampleCount = 0
        $recognizedCount = 0
        $exactCount = 0
        $tolerantCount = 0

        for ($y = 0; $y -lt $bitmap.Height; $y += $SamplingStep) {
            $sourceRow = $y * $sourceStride
            $maskRow = $y * $maskStride
            for ($x = 0; $x -lt $bitmap.Width; $x += $SamplingStep) {
                $sampleCount += 1
                $sourceOffset = $sourceRow + ($x * 4)
                $b = [int]$sourceBytes[$sourceOffset]
                $g = [int]$sourceBytes[$sourceOffset + 1]
                $r = [int]$sourceBytes[$sourceOffset + 2]
                $a = [int]$sourceBytes[$sourceOffset + 3]
                if ($a -lt 16) {
                    continue
                }

                $key = Get-Rgb5Key -R5 (Convert-Rgb8ToRgb5 $r) -G5 (Convert-Rgb8ToRgb5 $g) -B5 (Convert-Rgb8ToRgb5 $b)
                $row = $null
                if ($exactLookup.ContainsKey($key) -and $exactLookup[$key] -ne $script:AmbiguousMarker) {
                    $row = $exactLookup[$key]
                    $exactCount += 1
                }
                elseif ($tolerantLookup.ContainsKey($key) -and $tolerantLookup[$key] -ne $script:AmbiguousMarker) {
                    $row = $tolerantLookup[$key]
                    $tolerantCount += 1
                }
                if ($null -eq $row) {
                    continue
                }

                $recognizedCount += 1
                $statKey = [string]$row.palette_key
                if (-not $statsByKey.ContainsKey($statKey)) {
                    $statsByKey[$statKey] = [pscustomobject]([ordered]@{
                        row = $row
                        count = 0
                        min_x = $bitmap.Width
                        min_y = $bitmap.Height
                        max_x = 0
                        max_y = 0
                    })
                }
                $stat = $statsByKey[$statKey]
                $stat.count = [int]$stat.count + 1
                $stat.min_x = [Math]::Min([int]$stat.min_x, $x)
                $stat.min_y = [Math]::Min([int]$stat.min_y, $y)
                $stat.max_x = [Math]::Max([int]$stat.max_x, $x)
                $stat.max_y = [Math]::Max([int]$stat.max_y, $y)

                $maskOffset = $maskRow + ($x * 4)
                $maskBytes[$maskOffset] = [byte]$row.b
                $maskBytes[$maskOffset + 1] = [byte]$row.g
                $maskBytes[$maskOffset + 2] = [byte]$row.r
                $maskBytes[$maskOffset + 3] = 255
            }
        }

        [System.Runtime.InteropServices.Marshal]::Copy($maskBytes, 0, $maskData.Scan0, $maskBytes.Length)
    }
    finally {
        $bitmap.UnlockBits($sourceData)
        $mask.UnlockBits($maskData)
    }

    $safeBase = [Regex]::Replace([System.IO.Path]::GetFileNameWithoutExtension($screenshotPath), "[^A-Za-z0-9_.-]", "_")
    $csvOutput = Join-Path $OutputRoot "$safeBase`_decoded_cells.csv"
    $jsonOutput = Join-Path $OutputRoot "$safeBase`_decoded_cells.json"
    $maskOutput = Join-Path $OutputRoot "$safeBase`_decoded_mask.png"
    $summaryOutput = Join-Path $OutputRoot "$safeBase`_decode_summary.json"

    $records = @(
        $statsByKey.Values |
            Where-Object { [int]$_.count -ge $MinPixelCount } |
            Sort-Object @{ Expression = { [int]$_.count }; Descending = $true } |
            ForEach-Object {
                $row = $_.row
                [pscustomobject]([ordered]@{
                    texture_debug_id = $row.texture_debug_id
                    cell_id = $row.cell_id
                    count = [int]$_.count
                    coverage_percent = [Math]::Round((100.0 * [int]$_.count / [Math]::Max(1, $sampleCount)), 4)
                    bbox = "{0},{1},{2},{3}" -f $_.min_x, $_.min_y, $_.max_x, $_.max_y
                    grid = $row.grid
                    machine_code_hex = $row.machine_code_hex
                    room = $row.room
                    material_hex = $row.material_hex
                    material_index = $row.material_index
                    stage = $row.stage
                    original_texture_name = $row.original_texture_name
                    material_display_list = $row.material_display_list
                    mesh_indices = $row.mesh_indices
                    shape_indices = $row.shape_indices
                    cell_hit_command = ".\scripts\oot3d\Get-Oot3dLabeledUvCellHits.ps1 -TextureDebugId $($row.texture_debug_id) -Cell $($row.cell_id)"
                })
            }
    )

    $records | Export-Csv -LiteralPath $csvOutput -NoTypeInformation -Encoding utf8
    $records | ConvertTo-Json -Depth 6 | Out-File -LiteralPath $jsonOutput -Encoding utf8
    $mask.Save($maskOutput, [System.Drawing.Imaging.ImageFormat]::Png)

    $summary = [ordered]@{
        created_at = (Get-Date).ToString("o")
        screenshot = $screenshotPath
        palette_csv = $PaletteCsv
        width = $bitmap.Width
        height = $bitmap.Height
        sampling_step = $SamplingStep
        tolerance5 = $Tolerance5
        min_pixel_count = $MinPixelCount
        sample_count = $sampleCount
        recognized_sample_count = $recognizedCount
        exact_match_count = $exactCount
        tolerant_match_count = $tolerantCount
        recognized_percent = [Math]::Round((100.0 * $recognizedCount / [Math]::Max(1, $sampleCount)), 4)
        decoded_cell_count = $records.Count
        csv = $csvOutput
        json = $jsonOutput
        decoded_mask = $maskOutput
    }
    $summary | ConvertTo-Json -Depth 6 | Out-File -LiteralPath $summaryOutput -Encoding utf8
    $allSummaries.Add([pscustomobject]$summary) | Out-Null

    $mask.Dispose()
    $bitmap.Dispose()

    Write-Host "Decoded screenshot: $screenshotPath"
    Write-Host "Recognized samples: $recognizedCount / $sampleCount ($($summary.recognized_percent)%)"
    Write-Host "Decoded cells: $($records.Count)"
    Write-Host "CSV: $csvOutput"
    Write-Host "Mask: $maskOutput"
    Write-Host "Summary: $summaryOutput"
}

$batchSummary = Join-Path $OutputRoot "decode_batch_summary.json"
$allSummaries | ConvertTo-Json -Depth 6 | Out-File -LiteralPath $batchSummary -Encoding utf8
Write-Host "Batch summary: $batchSummary"
