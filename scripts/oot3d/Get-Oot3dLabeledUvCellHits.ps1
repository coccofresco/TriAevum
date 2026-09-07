param(
    [string[]]$TextureDebugId = @(),
    [string[]]$Cell = @("C32", "C31", "C3A"),
    [string]$LegendCsv = "I:\oot3dre_work\spot04\labeled_uv_overlay\spot04_labeled_uv_legend.csv",
    [string]$SceneManifest = "I:\oot3dre_work\spot04\converted_scene\scene_manifest.json",
    [string]$OutputRoot = "I:\oot3dre_work\spot04\labeled_uv_overlay\cell_hits",
    [switch]$IncludeAllTriangles
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
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

function Get-JsonArray($Object, [string]$Name) {
    $value = Get-JsonValue $Object $Name
    if ($null -eq $value) {
        return @()
    }
    if ($value -is [System.Array]) {
        return $value
    }
    return @($value)
}

function Normalize-HexId([string]$Value, [string]$Prefix, [int]$Digits) {
    $text = $Value.Trim().ToUpperInvariant()
    if ($text.StartsWith($Prefix, [System.StringComparison]::Ordinal)) {
        $text = $text.Substring($Prefix.Length)
    }
    $number = [Convert]::ToInt32($text, 16)
    return "{0}{1}" -f $Prefix, $number.ToString("X$Digits")
}

function New-OrdinalSet() {
    return New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::Ordinal)
}

function Get-ResourceMap([string]$ManifestPath) {
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    $map = @{}
    foreach ($record in @(Get-JsonArray $manifest "records")) {
        foreach ($resource in @(Get-JsonArray $record "resources")) {
            $path = [string](Get-JsonValue $resource "path")
            $file = [string](Get-JsonValue $resource "file")
            if (-not [string]::IsNullOrWhiteSpace($path) -and -not [string]::IsNullOrWhiteSpace($file)) {
                $map[$path] = $file
            }
        }
    }
    return $map
}

function Get-Xml([string]$Path) {
    [xml](Get-Content -LiteralPath $Path -Raw)
}

function Get-Attribute($Node, [string]$Name) {
    return $Node.GetAttribute($Name)
}

function Get-VertexMap([string]$VertexPath) {
    $xml = Get-Xml -Path $VertexPath
    $vertices = @()
    $index = 0
    foreach ($node in @($xml.Vertex.Vtx)) {
        $vertices += [pscustomobject]@{
            index = $index
            x = [int](Get-Attribute $node "X")
            y = [int](Get-Attribute $node "Y")
            z = [int](Get-Attribute $node "Z")
            s = [int](Get-Attribute $node "S")
            t = [int](Get-Attribute $node "T")
        }
        $index += 1
    }
    return $vertices
}

function Get-Triangles([string]$TrianglePath) {
    $xml = Get-Xml -Path $TrianglePath
    $triangles = @()
    $vertexPath = [string](Get-Attribute $xml.DisplayList.LoadVertices "Path")
    foreach ($node in @($xml.DisplayList.ChildNodes)) {
        if ($node.Name -eq "Triangle1") {
            $triangles += [pscustomobject]@{
                vertex_path = $vertexPath
                indices = @(
                    [int](Get-Attribute $node "V00"),
                    [int](Get-Attribute $node "V01"),
                    [int](Get-Attribute $node "V02")
                )
            }
        }
        elseif ($node.Name -eq "Triangles2") {
            $triangles += [pscustomobject]@{
                vertex_path = $vertexPath
                indices = @(
                    [int](Get-Attribute $node "V00"),
                    [int](Get-Attribute $node "V01"),
                    [int](Get-Attribute $node "V02")
                )
            }
            $triangles += [pscustomobject]@{
                vertex_path = $vertexPath
                indices = @(
                    [int](Get-Attribute $node "V10"),
                    [int](Get-Attribute $node "V11"),
                    [int](Get-Attribute $node "V12")
                )
            }
        }
    }
    return $triangles
}

function Get-Frac([double]$Value) {
    $result = $Value - [Math]::Floor($Value)
    if ($result -lt 0.0) {
        return $result + 1.0
    }
    return $result
}

function Get-CellId([double]$U, [double]$V, [int]$CellsX, [int]$CellsY) {
    $x = [Math]::Min($CellsX - 1, [Math]::Max(0, [int][Math]::Floor((Get-Frac $U) * $CellsX)))
    $y = [Math]::Min($CellsY - 1, [Math]::Max(0, [int][Math]::Floor((Get-Frac $V) * $CellsY)))
    $index = ($y * $CellsX) + $x
    return [pscustomobject]@{
        id = "C{0}" -f $index.ToString("X2")
        x = $x
        y = $y
        ordinal = $index
    }
}

function Get-LegendInt($Object, [string]$Name, [int]$Fallback) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or [string]::IsNullOrWhiteSpace([string]$property.Value)) {
        return $Fallback
    }
    return [int]$property.Value
}

function Get-CellsForTriangle($Uvs, [int]$CellsX, [int]$CellsY) {
    $set = New-OrdinalSet
    foreach ($uv in $Uvs) {
        [void]$set.Add((Get-CellId -U $uv.u -V $uv.v -CellsX $CellsX -CellsY $CellsY).id)
    }
    $centroidU = (($Uvs[0].u + $Uvs[1].u + $Uvs[2].u) / 3.0)
    $centroidV = (($Uvs[0].v + $Uvs[1].v + $Uvs[2].v) / 3.0)
    [void]$set.Add((Get-CellId -U $centroidU -V $centroidV -CellsX $CellsX -CellsY $CellsY).id)

    $minU = [Math]::Min([double]$Uvs[0].u, [Math]::Min([double]$Uvs[1].u, [double]$Uvs[2].u))
    $maxU = [Math]::Max([double]$Uvs[0].u, [Math]::Max([double]$Uvs[1].u, [double]$Uvs[2].u))
    $minV = [Math]::Min([double]$Uvs[0].v, [Math]::Min([double]$Uvs[1].v, [double]$Uvs[2].v))
    $maxV = [Math]::Max([double]$Uvs[0].v, [Math]::Max([double]$Uvs[1].v, [double]$Uvs[2].v))

    $startU = [int][Math]::Floor($minU) - 1
    $endU = [int][Math]::Ceiling($maxU) + 1
    $startV = [int][Math]::Floor($minV) - 1
    $endV = [int][Math]::Ceiling($maxV) + 1
    $candidateCount = ($endU - $startU + 1) * ($endV - $startV + 1) * $CellsX * $CellsY

    if ($candidateCount -le 20000) {
        for ($cellY = 0; $cellY -lt $CellsY; $cellY++) {
            for ($cellX = 0; $cellX -lt $CellsX; $cellX++) {
                $localU = ($cellX + 0.5) / $CellsX
                $localV = ($cellY + 0.5) / $CellsY
                $matched = $false
                for ($repeatV = $startV; $repeatV -le $endV -and -not $matched; $repeatV++) {
                    for ($repeatU = $startU; $repeatU -le $endU -and -not $matched; $repeatU++) {
                        $u = $repeatU + $localU
                        $v = $repeatV + $localV
                        if (
                            $u -ge ($minU - 0.000001) -and $u -le ($maxU + 0.000001) -and
                            $v -ge ($minV - 0.000001) -and $v -le ($maxV + 0.000001) -and
                            (Test-PointInTriangleUv -U $u -V $v -Uvs $Uvs)
                        ) {
                            [void]$set.Add((Get-CellId -U $u -V $v -CellsX $CellsX -CellsY $CellsY).id)
                            $matched = $true
                        }
                    }
                }
            }
        }
    }

    return @($set | Sort-Object)
}

function Get-TriangleUvArea($Uvs) {
    $u0 = [double]$Uvs[0].u
    $v0 = [double]$Uvs[0].v
    $u1 = [double]$Uvs[1].u
    $v1 = [double]$Uvs[1].v
    $u2 = [double]$Uvs[2].u
    $v2 = [double]$Uvs[2].v
    return (($u1 - $u0) * ($v2 - $v0)) - (($v1 - $v0) * ($u2 - $u0))
}

function Test-PointInTriangleUv([double]$U, [double]$V, $Uvs) {
    $u0 = [double]$Uvs[0].u
    $v0 = [double]$Uvs[0].v
    $u1 = [double]$Uvs[1].u
    $v1 = [double]$Uvs[1].v
    $u2 = [double]$Uvs[2].u
    $v2 = [double]$Uvs[2].v

    $edge0u = $u2 - $u0
    $edge0v = $v2 - $v0
    $edge1u = $u1 - $u0
    $edge1v = $v1 - $v0
    $pointU = $U - $u0
    $pointV = $V - $v0

    $dot00 = ($edge0u * $edge0u) + ($edge0v * $edge0v)
    $dot01 = ($edge0u * $edge1u) + ($edge0v * $edge1v)
    $dot02 = ($edge0u * $pointU) + ($edge0v * $pointV)
    $dot11 = ($edge1u * $edge1u) + ($edge1v * $edge1v)
    $dot12 = ($edge1u * $pointU) + ($edge1v * $pointV)
    $denominator = ($dot00 * $dot11) - ($dot01 * $dot01)
    if ([Math]::Abs($denominator) -lt 0.0000000001) {
        return $false
    }

    $invDenominator = 1.0 / $denominator
    $a = (($dot11 * $dot02) - ($dot01 * $dot12)) * $invDenominator
    $b = (($dot00 * $dot12) - ($dot01 * $dot02)) * $invDenominator
    return $a -ge -0.000001 -and $b -ge -0.000001 -and ($a + $b) -le 1.000001
}

Require-Path $LegendCsv "Labeled UV legend"
Require-Path $SceneManifest "Scene manifest"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$resourceMap = Get-ResourceMap -ManifestPath $SceneManifest
$legendRows = @(Import-Csv -LiteralPath $LegendCsv)

$targetTextureIds = New-OrdinalSet
foreach ($id in $TextureDebugId) {
    if (-not [string]::IsNullOrWhiteSpace($id)) {
        [void]$targetTextureIds.Add((Normalize-HexId -Value $id -Prefix "T" -Digits 2))
    }
}

$targetCells = New-OrdinalSet
foreach ($id in $Cell) {
    if (-not [string]::IsNullOrWhiteSpace($id)) {
        [void]$targetCells.Add((Normalize-HexId -Value $id -Prefix "C" -Digits 2))
    }
}

if ($targetCells.Count -eq 0 -and -not $IncludeAllTriangles) {
    throw "At least one -Cell value is required unless -IncludeAllTriangles is passed"
}

$selectedRows = @(
    $legendRows | Where-Object {
        $targetTextureIds.Count -eq 0 -or $targetTextureIds.Contains([string]$_.texture_debug_id)
    }
)

$hits = New-Object "System.Collections.Generic.List[object]"
$vertexCache = @{}
$triangleCache = @{}

foreach ($legend in $selectedRows) {
    $width = [int]$legend.width
    $height = [int]$legend.height
    $fallbackCellsX = [Math]::Max(2, [Math]::Min(8, [int][Math]::Floor($width / 32)))
    $fallbackCellsY = [Math]::Max(2, [Math]::Min(8, [int][Math]::Floor($height / 32)))
    $cellsX = Get-LegendInt -Object $legend -Name "grid_cells_x" -Fallback $fallbackCellsX
    $cellsY = Get-LegendInt -Object $legend -Name "grid_cells_y" -Fallback $fallbackCellsY
    $meshIndices = @([string]$legend.mesh_indices -split "\|" | Where-Object { $_ -ne "" } | ForEach-Object { [int]$_ })

    foreach ($meshIndex in $meshIndices) {
        $meshPath = "scenes/overworld/spot04/oot3d/spot04_room_$($legend.room)/gOot3dSpot04Room$($legend.room)_mesh_$meshIndex"
        if (-not $resourceMap.ContainsKey($meshPath)) {
            continue
        }
        $meshXml = Get-Xml -Path $resourceMap[$meshPath]
        $batchOrdinal = 0
        foreach ($call in @($meshXml.DisplayList.CallDisplayList)) {
            $batchPath = [string](Get-Attribute $call "Path")
            if ($batchPath -notlike "*_tri") {
                continue
            }
            if (-not $resourceMap.ContainsKey($batchPath)) {
                continue
            }
            if (-not $triangleCache.ContainsKey($batchPath)) {
                $triangleCache[$batchPath] = @(Get-Triangles -TrianglePath $resourceMap[$batchPath])
            }
            $triangles = @($triangleCache[$batchPath])
            $triangleOrdinal = 0
            foreach ($triangle in $triangles) {
                $vertexPath = [string]$triangle.vertex_path
                if (-not $resourceMap.ContainsKey($vertexPath)) {
                    $triangleOrdinal += 1
                    continue
                }
                if (-not $vertexCache.ContainsKey($vertexPath)) {
                    $vertexCache[$vertexPath] = @(Get-VertexMap -VertexPath $resourceMap[$vertexPath])
                }
                $vertices = @($vertexCache[$vertexPath])
                $triangleVertices = @()
                foreach ($index in @($triangle.indices)) {
                    if ($index -ge 0 -and $index -lt $vertices.Count) {
                        $triangleVertices += $vertices[$index]
                    }
                }
                if ($triangleVertices.Count -ne 3) {
                    $triangleOrdinal += 1
                    continue
                }

                $uvs = @(
                    [pscustomobject]@{ u = ([double]$triangleVertices[0].s / ($width * 32.0)); v = ([double]$triangleVertices[0].t / ($height * 32.0)) },
                    [pscustomobject]@{ u = ([double]$triangleVertices[1].s / ($width * 32.0)); v = ([double]$triangleVertices[1].t / ($height * 32.0)) },
                    [pscustomobject]@{ u = ([double]$triangleVertices[2].s / ($width * 32.0)); v = ([double]$triangleVertices[2].t / ($height * 32.0)) }
                )
                $triangleCells = @(Get-CellsForTriangle -Uvs $uvs -CellsX $cellsX -CellsY $cellsY)
                $matches = @()
                foreach ($cellId in $triangleCells) {
                    if ($targetCells.Contains($cellId)) {
                        $matches += $cellId
                    }
                }
                if ($matches.Count -eq 0 -and -not $IncludeAllTriangles) {
                    $triangleOrdinal += 1
                    continue
                }

                $uvArea = Get-TriangleUvArea -Uvs $uvs
                $orientation = if ([Math]::Abs($uvArea) -lt 0.0000001) {
                    "degenerate"
                }
                elseif ($uvArea -gt 0.0) {
                    "uv_ccw"
                }
                else {
                    "uv_cw"
                }

                $hits.Add([pscustomobject]([ordered]@{
                    texture_debug_id = [string]$legend.texture_debug_id
                    label = [string]$legend.label
                    matched_cells = ($matches -join "|")
                    touched_cells = ($triangleCells -join "|")
                    room = [int]$legend.room
                    material_hex = [string]$legend.material_hex
                    material_index = [int]$legend.material_index
                    stage = [string]$legend.stage
                    original_texture_name = [string]$legend.original_texture_name
                    width = $width
                    height = $height
                    grid = "$($cellsX)x$($cellsY)"
                    mesh_index = $meshIndex
                    batch_ordinal = $batchOrdinal
                    triangle_ordinal = $triangleOrdinal
                    batch_path = $batchPath
                    vertex_path = $vertexPath
                    vertex_indices = (@($triangle.indices) -join "|")
                    vertex_cells = (@($uvs | ForEach-Object { (Get-CellId -U $_.u -V $_.v -CellsX $cellsX -CellsY $cellsY).id }) -join "|")
                    uv_area = $uvArea
                    uv_orientation = $orientation
                    uvs = (@($uvs | ForEach-Object { "{0:F5},{1:F5}" -f $_.u, $_.v }) -join "|")
                    positions = (@($triangleVertices | ForEach-Object { "$($_.x),$($_.y),$($_.z)" }) -join "|")
                })) | Out-Null
                $triangleOrdinal += 1
            }
            $batchOrdinal += 1
        }
    }
}

$textureSuffix = if ($targetTextureIds.Count -eq 0) { "allT" } else { (@($targetTextureIds | Sort-Object) -join "_") }
$cellSuffix = if ($targetCells.Count -eq 0) { "allC" } else { (@($targetCells | Sort-Object) -join "_") }
$csvOutput = Join-Path $OutputRoot "cell_hits_${textureSuffix}_${cellSuffix}.csv"
$jsonOutput = Join-Path $OutputRoot "cell_hits_${textureSuffix}_${cellSuffix}.json"
$summaryOutput = Join-Path $OutputRoot "cell_hits_${textureSuffix}_${cellSuffix}_summary.json"

$hits | Export-Csv -LiteralPath $csvOutput -NoTypeInformation -Encoding utf8
$hits | ConvertTo-Json -Depth 6 | Out-File -LiteralPath $jsonOutput -Encoding utf8

$summary = [ordered]@{
    created_at = (Get-Date).ToString("o")
    texture_debug_ids = @($targetTextureIds | Sort-Object)
    cells = @($targetCells | Sort-Object)
    legend_csv = $LegendCsv
    scene_manifest = $SceneManifest
    selected_texture_count = $selectedRows.Count
    hit_count = $hits.Count
    hit_orientation_counts = @($hits | Group-Object uv_orientation | Sort-Object Name | ForEach-Object { [pscustomobject]@{ name = $_.Name; count = $_.Count } })
    csv = $csvOutput
    json = $jsonOutput
}
$summary | ConvertTo-Json -Depth 6 | Out-File -LiteralPath $summaryOutput -Encoding utf8

Write-Host "Cell hit CSV: $csvOutput"
Write-Host "Cell hit JSON: $jsonOutput"
Write-Host "Summary: $summaryOutput"
Write-Host "Hits: $($hits.Count)"
