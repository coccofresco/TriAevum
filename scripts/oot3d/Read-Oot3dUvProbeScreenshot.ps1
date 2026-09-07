param(
    [Parameter(Mandatory = $true)]
    [string[]]$Screenshot,
    [string]$PaletteCsv = "I:\oot3dre_work\spot04\labeled_uv_overlay\spot04_labeled_uv_palette.csv",
    [string]$LegendCsv = "I:\oot3dre_work\spot04\labeled_uv_overlay\spot04_labeled_uv_legend.csv",
    [string]$SceneManifest = "I:\oot3dre_work\spot04\converted_scene\scene_manifest.json",
    [string]$OutputRoot = "I:\oot3dre_work\spot04\uv_probe_decode",
    [ValidateRange(0, 3)]
    [int]$Tolerance5 = 1,
    [ValidateRange(1, 16)]
    [int]$SamplingStep = 1,
    [ValidateRange(1, 1000000)]
    [int]$MinPixelCount = 24,
    [ValidateRange(1, 512)]
    [int]$MaxDecodedCells = 64,
    [ValidateRange(1, 128)]
    [int]$MaxHitRowsPerCell = 16,
    [ValidateSet("Auto", "Normal", "FlipX", "FlipY", "FlipXY")]
    [string]$RenderedTextureOrientation = "Auto"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Read-JsonArray([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return @()
    }
    $text = Get-Content -LiteralPath $Path -Raw
    if ([string]::IsNullOrWhiteSpace($text)) {
        return @()
    }
    $value = $text | ConvertFrom-Json
    if ($value -is [System.Array]) {
        return @($value)
    }
    return @($value)
}

function Write-JsonArray([object[]]$Rows, [string]$Path) {
    if ($Rows.Count -eq 0) {
        "[]" | Out-File -LiteralPath $Path -Encoding utf8
        return
    }
    $Rows | ConvertTo-Json -Depth 8 | Out-File -LiteralPath $Path -Encoding utf8
}

function Convert-ToInt([object]$Value, [int]$Fallback = 0) {
    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) {
        return $Fallback
    }
    return [int]$Value
}

function Convert-ToDoubleValue([object]$Value, [double]$Fallback = 0.0) {
    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) {
        return $Fallback
    }

    $text = ([string]$Value).Trim()
    $style = [System.Globalization.NumberStyles]::Float -bor [System.Globalization.NumberStyles]::AllowThousands
    $invariant = [System.Globalization.CultureInfo]::InvariantCulture
    $current = [System.Globalization.CultureInfo]::CurrentCulture
    $parsed = 0.0

    if ($text.Contains(",") -and -not $text.Contains(".")) {
        $decimalCommaText = $text.Replace(",", ".")
        if ([double]::TryParse($decimalCommaText, $style, $invariant, [ref]$parsed)) {
            return $parsed
        }
    }

    if ([double]::TryParse($text, $style, $invariant, [ref]$parsed)) {
        return $parsed
    }
    if ([double]::TryParse($text, $style, $current, [ref]$parsed)) {
        return $parsed
    }

    return $Fallback
}

function Get-JsonValue($Object, [string]$Name) {
    if ($null -eq $Object) {
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

function Normalize-TextureOrientationName([string]$Value) {
    $normalized = $Value.Trim().ToLowerInvariant().Replace("-", "_")
    switch ($normalized) {
        "normal" { return "Normal" }
        "none" { return "Normal" }
        "flipx" { return "FlipX" }
        "flip_x" { return "FlipX" }
        "flipy" { return "FlipY" }
        "flip_y" { return "FlipY" }
        "flipxy" { return "FlipXY" }
        "flip_x_y" { return "FlipXY" }
        "flip_xy" { return "FlipXY" }
        default { return "FlipXY" }
    }
}

function Resolve-RenderedTextureOrientation([string]$Requested, [string]$ManifestPath) {
    if ($Requested -ne "Auto") {
        return $Requested
    }

    $sceneManifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    foreach ($record in @($sceneManifest.records)) {
        $roomManifestPath = [string](Get-JsonValue $record "manifest")
        if ([string]::IsNullOrWhiteSpace($roomManifestPath) -or -not (Test-Path -LiteralPath $roomManifestPath)) {
            continue
        }

        $roomManifest = Get-Content -LiteralPath $roomManifestPath -Raw | ConvertFrom-Json
        $orientation = [string](Get-JsonValue $roomManifest "texture_orientation")
        if (-not [string]::IsNullOrWhiteSpace($orientation)) {
            return Normalize-TextureOrientationName $orientation
        }
    }

    return "FlipXY"
}

function Convert-CellForOrientation([string]$CellId, [string]$Grid, [string]$TextureOrientation) {
    $gridMatch = [Regex]::Match($Grid, "^(\d+)x(\d+)$")
    $cellMatch = [Regex]::Match($CellId, "^C([0-9A-Fa-f]+)$")
    if (-not $gridMatch.Success -or -not $cellMatch.Success) {
        return $CellId
    }

    $cellsX = [int]$gridMatch.Groups[1].Value
    $cellsY = [int]$gridMatch.Groups[2].Value
    $ordinal = [Convert]::ToInt32($cellMatch.Groups[1].Value, 16)
    if ($ordinal -lt 0 -or $ordinal -ge ($cellsX * $cellsY)) {
        return $CellId
    }

    $x = $ordinal % $cellsX
    $y = [int][Math]::Floor($ordinal / $cellsX)
    if ($TextureOrientation -eq "FlipX" -or $TextureOrientation -eq "FlipXY") {
        $x = ($cellsX - 1) - $x
    }
    if ($TextureOrientation -eq "FlipY" -or $TextureOrientation -eq "FlipXY") {
        $y = ($cellsY - 1) - $y
    }

    $sourceOrdinal = ($y * $cellsX) + $x
    return "C{0}" -f $sourceOrdinal.ToString("X2")
}

function Convert-CellSetForOrientation([string]$Cells, [string]$Grid, [string]$TextureOrientation) {
    if ([string]::IsNullOrWhiteSpace($Cells)) {
        return ""
    }
    $converted = @(
        [string]$Cells -split "\|" |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object { Convert-CellForOrientation -CellId $_ -Grid $Grid -TextureOrientation $TextureOrientation } |
            Sort-Object -Unique
    )
    return ($converted -join "|")
}

$scriptRoot = Split-Path -Parent $PSCommandPath
$decodeScript = Join-Path $scriptRoot "Read-Oot3dUvOverlayScreenshot.ps1"
$hitsScript = Join-Path $scriptRoot "Get-Oot3dLabeledUvCellHits.ps1"

Require-Path $decodeScript "UV screenshot decoder"
Require-Path $hitsScript "UV cell hit mapper"
Require-Path $PaletteCsv "Machine UV palette"
Require-Path $LegendCsv "Labeled UV legend"
Require-Path $SceneManifest "Scene manifest"

$resolvedRenderedTextureOrientation = Resolve-RenderedTextureOrientation `
    -Requested $RenderedTextureOrientation `
    -ManifestPath $SceneManifest

$decodeRoot = Join-Path $OutputRoot "decoded_cells"
$hitRoot = Join-Path $OutputRoot "cell_hits"
New-Item -ItemType Directory -Force -Path $decodeRoot | Out-Null
New-Item -ItemType Directory -Force -Path $hitRoot | Out-Null

& $decodeScript `
    -Screenshot $Screenshot `
    -PaletteCsv $PaletteCsv `
    -OutputRoot $decodeRoot `
    -Tolerance5 $Tolerance5 `
    -SamplingStep $SamplingStep `
    -MinPixelCount $MinPixelCount

$batchSummaryPath = Join-Path $decodeRoot "decode_batch_summary.json"
$decodeSummaries = @(Read-JsonArray -Path $batchSummaryPath)
$decodedRows = New-Object "System.Collections.Generic.List[object]"

foreach ($summary in $decodeSummaries) {
    $csvPath = [string]$summary.csv
    if ([string]::IsNullOrWhiteSpace($csvPath) -or -not (Test-Path -LiteralPath $csvPath)) {
        continue
    }
    foreach ($row in @(Import-Csv -LiteralPath $csvPath)) {
        $decodedRows.Add([pscustomobject]([ordered]@{
            screenshot = [string]$summary.screenshot
            texture_debug_id = [string]$row.texture_debug_id
            cell_id = [string]$row.cell_id
            count = Convert-ToInt $row.count
            coverage_percent = Convert-ToDoubleValue $row.coverage_percent
            bbox = [string]$row.bbox
            grid = [string]$row.grid
            machine_code_hex = [string]$row.machine_code_hex
            room = Convert-ToInt $row.room
            material_hex = [string]$row.material_hex
            material_index = Convert-ToInt $row.material_index
            stage = [string]$row.stage
            original_texture_name = [string]$row.original_texture_name
            material_display_list = [string]$row.material_display_list
            mesh_indices = [string]$row.mesh_indices
            shape_indices = [string]$row.shape_indices
        })) | Out-Null
    }
}

$selectedDecodedRows = @(
    $decodedRows |
        Sort-Object @{ Expression = { [int]$_.count }; Descending = $true } |
        Select-Object -First $MaxDecodedCells
)

$reportRows = New-Object "System.Collections.Generic.List[object]"
$expandedRows = New-Object "System.Collections.Generic.List[object]"

foreach ($decoded in $selectedDecodedRows) {
    & $hitsScript `
        -TextureDebugId ([string]$decoded.texture_debug_id) `
        -Cell ([string]$decoded.cell_id) `
        -LegendCsv $LegendCsv `
        -SceneManifest $SceneManifest `
        -OutputRoot $hitRoot

    $hitJson = Join-Path $hitRoot ("cell_hits_{0}_{1}.json" -f $decoded.texture_debug_id, $decoded.cell_id)
    $hits = @(Read-JsonArray -Path $hitJson)
    $topHits = @($hits | Select-Object -First $MaxHitRowsPerCell)
    $sourceCellId = Convert-CellForOrientation `
        -CellId ([string]$decoded.cell_id) `
        -Grid ([string]$decoded.grid) `
        -TextureOrientation $resolvedRenderedTextureOrientation
    $candidateMeshes = @($hits | ForEach-Object { [int]$_.mesh_index } | Sort-Object -Unique)
    $candidateOrientations = @($hits | ForEach-Object { [string]$_.uv_orientation } | Sort-Object -Unique)
    $candidatePreview = @(
        $topHits | ForEach-Object {
            $sourceTouchedCells = Convert-CellSetForOrientation `
                -Cells ([string]$_.touched_cells) `
                -Grid ([string]$_.grid) `
                -TextureOrientation $resolvedRenderedTextureOrientation
            "m{0}:b{1}:t{2}:{3}:{4}->{5}" -f $_.mesh_index, $_.batch_ordinal, $_.triangle_ordinal, $_.uv_orientation, $_.touched_cells, $sourceTouchedCells
        }
    ) -join ";"

    if ($hits.Count -eq 0) {
        $status = "decoded_cell_without_expected_triangle_hit"
    } else {
        $status = "decoded_cell_mapped_to_expected_triangles"
    }

    $reportRows.Add([pscustomobject]([ordered]@{
        screenshot = [string]$decoded.screenshot
        texture_debug_id = [string]$decoded.texture_debug_id
        cell_id = [string]$decoded.cell_id
        source_cell_id = $sourceCellId
        rendered_texture_orientation = $resolvedRenderedTextureOrientation
        observed_pixels = [int]$decoded.count
        observed_coverage_percent = Convert-ToDoubleValue $decoded.coverage_percent
        observed_bbox = [string]$decoded.bbox
        grid = [string]$decoded.grid
        room = [int]$decoded.room
        material_hex = [string]$decoded.material_hex
        material_index = [int]$decoded.material_index
        stage = [string]$decoded.stage
        original_texture_name = [string]$decoded.original_texture_name
        material_display_list = [string]$decoded.material_display_list
        declared_mesh_indices = [string]$decoded.mesh_indices
        declared_shape_indices = [string]$decoded.shape_indices
        expected_hit_count = $hits.Count
        candidate_mesh_indices = ($candidateMeshes -join "|")
        candidate_uv_orientations = ($candidateOrientations -join "|")
        candidate_preview = $candidatePreview
        status = $status
    })) | Out-Null

    foreach ($hit in $topHits) {
        $sourceTouchedCells = Convert-CellSetForOrientation `
            -Cells ([string]$hit.touched_cells) `
            -Grid ([string]$hit.grid) `
            -TextureOrientation $resolvedRenderedTextureOrientation
        $sourceVertexCells = Convert-CellSetForOrientation `
            -Cells ([string]$hit.vertex_cells) `
            -Grid ([string]$hit.grid) `
            -TextureOrientation $resolvedRenderedTextureOrientation
        $expandedRows.Add([pscustomobject]([ordered]@{
            screenshot = [string]$decoded.screenshot
            texture_debug_id = [string]$decoded.texture_debug_id
            cell_id = [string]$decoded.cell_id
            source_cell_id = $sourceCellId
            rendered_texture_orientation = $resolvedRenderedTextureOrientation
            observed_pixels = [int]$decoded.count
            observed_bbox = [string]$decoded.bbox
            room = [int]$hit.room
            material_index = [int]$hit.material_index
            stage = [string]$hit.stage
            original_texture_name = [string]$hit.original_texture_name
            mesh_index = [int]$hit.mesh_index
            batch_ordinal = [int]$hit.batch_ordinal
            triangle_ordinal = [int]$hit.triangle_ordinal
            touched_cells = [string]$hit.touched_cells
            source_touched_cells = $sourceTouchedCells
            vertex_cells = [string]$hit.vertex_cells
            source_vertex_cells = $sourceVertexCells
            uv_orientation = [string]$hit.uv_orientation
            uv_area = Convert-ToDoubleValue $hit.uv_area
            uvs = [string]$hit.uvs
            positions = [string]$hit.positions
            batch_path = [string]$hit.batch_path
            vertex_path = [string]$hit.vertex_path
        })) | Out-Null
    }
}

$reportCsv = Join-Path $OutputRoot "uv_probe_report.csv"
$reportJson = Join-Path $OutputRoot "uv_probe_report.json"
$expandedCsv = Join-Path $OutputRoot "uv_probe_expanded_hits.csv"
$expandedJson = Join-Path $OutputRoot "uv_probe_expanded_hits.json"
$summaryPath = Join-Path $OutputRoot "uv_probe_summary.json"

$reportArray = @($reportRows.ToArray())
$expandedArray = @($expandedRows.ToArray())

$reportArray | Export-Csv -LiteralPath $reportCsv -NoTypeInformation -Encoding utf8
Write-JsonArray -Rows $reportArray -Path $reportJson
$expandedArray | Export-Csv -LiteralPath $expandedCsv -NoTypeInformation -Encoding utf8
Write-JsonArray -Rows $expandedArray -Path $expandedJson

$summary = [ordered]@{
    created_at = (Get-Date).ToString("o")
    screenshots = $Screenshot
    palette_csv = $PaletteCsv
    legend_csv = $LegendCsv
    scene_manifest = $SceneManifest
    output_root = $OutputRoot
    decode_root = $decodeRoot
    hit_root = $hitRoot
    decoded_cell_count = $decodedRows.Count
    reported_cell_count = $reportRows.Count
    expanded_hit_count = $expandedRows.Count
    max_decoded_cells = $MaxDecodedCells
    max_hit_rows_per_cell = $MaxHitRowsPerCell
    rendered_texture_orientation = $resolvedRenderedTextureOrientation
    report_csv = $reportCsv
    report_json = $reportJson
    expanded_hits_csv = $expandedCsv
    expanded_hits_json = $expandedJson
    decode_batch_summary = $batchSummaryPath
}
$summary | ConvertTo-Json -Depth 8 | Out-File -LiteralPath $summaryPath -Encoding utf8

Write-Host "UV probe report: $reportCsv"
Write-Host "Expanded hits: $expandedCsv"
Write-Host "Summary: $summaryPath"
