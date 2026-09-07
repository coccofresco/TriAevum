param(
    [string]$DemoJson = "I:\oot3dre_work\standalone_demo\kokiri_forest\native_host\comparison\kokiri_slot5\demo_slot5.json",
    [string]$OutputDir = "I:\oot3dre_work\standalone_demo\kokiri_forest\native_host\comparison\kokiri_slot5",
    [string]$PicaTraceJson = "I:\oot3dre\captures\azahar_pica\kokiri_slot5_20260704_101127\derived\oot3d_pica_frame_000015.native_pica_register_trace.json",
    [string]$VisualCompareSummary = "I:\oot3dre_work\standalone_demo\kokiri_forest\native_host\comparison\kokiri_slot5\visual_compare_summary.json"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-Prop {
    param([object]$Object, [string]$Name, [object]$Default = $null)
    if ($null -eq $Object) {
        return $Default
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $Default
    }
    return $property.Value
}

function Get-Num {
    param([object]$Value, [double]$Default = 0.0)
    if ($null -eq $Value) {
        return $Default
    }
    return [double]$Value
}

function Get-Int64 {
    param([object]$Value, [int64]$Default = 0)
    if ($null -eq $Value) {
        return $Default
    }
    return [int64]$Value
}

function Round4 {
    param([object]$Value)
    return [Math]::Round((Get-Num $Value), 4)
}

function Format-InvariantNumber {
    param([object]$Value)
    return ([double]$Value).ToString("0.####", [Globalization.CultureInfo]::InvariantCulture)
}

function Convert-ToRoundedVector4 {
    param([object[]]$Values)
    $rounded = @()
    foreach ($value in ($Values | Select-Object -First 4)) {
        $rounded += [Math]::Round([double]$value, 4)
    }
    return $rounded
}

function Convert-ToInvariantVectorKey {
    param([object[]]$Values)
    return (($Values | ForEach-Object { Format-InvariantNumber $_ }) -join ",")
}

function Get-ColorLuma {
    param([object]$Color)
    if ($null -eq $Color) {
        return 0.0
    }
    return 0.2126 * (Get-Num (Get-Prop $Color "r" 0)) +
           0.7152 * (Get-Num (Get-Prop $Color "g" 0)) +
           0.0722 * (Get-Num (Get-Prop $Color "b" 0))
}

function Get-ColorChannel {
    param([object]$Color, [string]$Channel)
    return [int](Get-Num (Get-Prop $Color $Channel 0))
}

function Get-VectorPart {
    param([object]$Vector, [string]$Part)
    return Round4 (Get-Prop $Vector $Part 0)
}

function New-BatchLightingRow {
    param([string]$ModelKey, [string]$ModelName, [object]$Batch)

    $diagnostics = Get-Prop $Batch "native_pica_lighting_diagnostics"
    if (-not [bool](Get-Prop $diagnostics "available" $false)) {
        return $null
    }

    $light0Dot = Get-Prop $diagnostics "light0_dot_stats"
    $light0Scale = Get-Prop $diagnostics "light0_diffuse_scale_stats"
    $light0OppositeScale = Get-Prop $diagnostics "light0_opposite_diffuse_scale_stats"
    $light1Dot = Get-Prop $diagnostics "light1_dot_stats"
    $light1Scale = Get-Prop $diagnostics "light1_diffuse_scale_stats"
    $baseStats = Get-Prop $diagnostics "base_color_stats"
    $modStats = Get-Prop $diagnostics "modulation_color_stats"
    $outStats = Get-Prop $diagnostics "output_color_stats"
    $vertexColorStats = Get-Prop $Batch "vertex_color_stats"
    $shaderPrimaryStats = Get-Prop $Batch "estimated_shader_primary_color_stats"
    $light0Vector = Get-Prop $diagnostics "light0_vector"
    $light1Vector = Get-Prop $diagnostics "light1_vector"
    $avgWorldNormal = Get-Prop $diagnostics "average_world_normal"
    $ambient = Get-Prop $diagnostics "ambient_color"
    $diffuse0 = Get-Prop $diagnostics "diffuse0_color"
    $diffuse1 = Get-Prop $diagnostics "diffuse1_color"
    $materialAmbient = Get-Prop $diagnostics "material_ambient_color"
    $materialDiffuse = Get-Prop $diagnostics "material_diffuse_color"
    $effectiveDiffuse = Get-Prop $diagnostics "effective_material_diffuse_color"

    $modMinLuma = Get-ColorLuma (Get-Prop $modStats "min")
    $modMaxLuma = Get-ColorLuma (Get-Prop $modStats "max")
    $outMinLuma = Get-ColorLuma (Get-Prop $outStats "min")
    $outMaxLuma = Get-ColorLuma (Get-Prop $outStats "max")
    $vertexColorMinLuma = Get-ColorLuma (Get-Prop $vertexColorStats "min")
    $vertexColorMaxLuma = Get-ColorLuma (Get-Prop $vertexColorStats "max")
    $shaderPrimaryMinLuma = Get-ColorLuma (Get-Prop $shaderPrimaryStats "min")
    $shaderPrimaryMaxLuma = Get-ColorLuma (Get-Prop $shaderPrimaryStats "max")
    $directionalCount = Get-Int64 (Get-Prop $diagnostics "directional_evaluated_vertex_count" 0)
    $positive0 = Get-Int64 (Get-Prop $light0Dot "positive_count" 0)
    $negative0 = Get-Int64 (Get-Prop $light0Dot "negative_count" 0)
    $effectiveDiffuseZero =
        (Get-ColorChannel $effectiveDiffuse "r") -eq 0 -and
        (Get-ColorChannel $effectiveDiffuse "g") -eq 0 -and
        (Get-ColorChannel $effectiveDiffuse "b") -eq 0
    $ambientOnlyLighting =
        $effectiveDiffuseZero -and
        (Get-ColorLuma $ambient) -gt 0.0 -and
        (Get-ColorLuma $materialAmbient) -gt 0.0

    return [pscustomobject][ordered]@{
        model_key = $ModelKey
        model_name = $ModelName
        batch_index = Get-Int64 (Get-Prop $Batch "batch_index" -1)
        mesh_index = Get-Int64 (Get-Prop $Batch "mesh_index" -1)
        shape_index = Get-Int64 (Get-Prop $Batch "shape_index" -1)
        material_index = Get-Int64 (Get-Prop $Batch "material_index" -1)
        texture_name = Get-Prop (Get-Prop $Batch "texture") "name" ""
        application = Get-Prop $Batch "native_pica_lighting_application" ""
        vertex_lighting = [bool](Get-Prop $Batch "native_pica_vertex_lighting_applied" $false)
        hemisphere_lighting = [bool](Get-Prop $Batch "native_pica_hemisphere_lighting_applied" $false)
        directional_lighting = [bool](Get-Prop $Batch "native_pica_directional_lighting_applied" $false)
        actor_vs_color_packet = [bool](Get-Prop $Batch "native_pica_vertex_hemisphere_actor_vs_color_packet_applied" $false)
        color_source = Get-Prop $Batch "native_pica_vertex_hemisphere_color_source" ""
        vector_source = Get-Prop $diagnostics "light0_vector_source" ""
        vertex_count = Get-Int64 (Get-Prop $diagnostics "vertex_count" 0)
        native_normal_vertex_count = Get-Int64 (Get-Prop $diagnostics "native_normal_vertex_count" 0)
        directional_evaluated_vertex_count = $directionalCount
        light0_x = Get-VectorPart $light0Vector "x"
        light0_y = Get-VectorPart $light0Vector "y"
        light0_z = Get-VectorPart $light0Vector "z"
        light1_x = Get-VectorPart $light1Vector "x"
        light1_y = Get-VectorPart $light1Vector "y"
        light1_z = Get-VectorPart $light1Vector "z"
        avg_world_normal_x = Get-VectorPart $avgWorldNormal "x"
        avg_world_normal_y = Get-VectorPart $avgWorldNormal "y"
        avg_world_normal_z = Get-VectorPart $avgWorldNormal "z"
        light0_dot_min = Round4 (Get-Prop $light0Dot "min" 0)
        light0_dot_max = Round4 (Get-Prop $light0Dot "max" 0)
        light0_dot_average = Round4 (Get-Prop $light0Dot "average" 0)
        light0_dot_negative_count = $negative0
        light0_dot_positive_count = $positive0
        light0_dot_positive_ratio = if ($directionalCount -gt 0) { Round4 ($positive0 / [double]$directionalCount) } else { 0 }
        light0_dot_negative_ratio = if ($directionalCount -gt 0) { Round4 ($negative0 / [double]$directionalCount) } else { 0 }
        light0_diffuse_scale_average = Round4 (Get-Prop $light0Scale "average" 0)
        light0_diffuse_scale_min = Round4 (Get-Prop $light0Scale "min" 0)
        light0_diffuse_scale_max = Round4 (Get-Prop $light0Scale "max" 0)
        light0_opposite_diffuse_scale_average = Round4 (Get-Prop $light0OppositeScale "average" 0)
        light1_dot_min = Round4 (Get-Prop $light1Dot "min" 0)
        light1_dot_max = Round4 (Get-Prop $light1Dot "max" 0)
        light1_dot_average = Round4 (Get-Prop $light1Dot "average" 0)
        light1_diffuse_scale_average = Round4 (Get-Prop $light1Scale "average" 0)
        ambient_r = Get-ColorChannel $ambient "r"
        ambient_g = Get-ColorChannel $ambient "g"
        ambient_b = Get-ColorChannel $ambient "b"
        diffuse0_r = Get-ColorChannel $diffuse0 "r"
        diffuse0_g = Get-ColorChannel $diffuse0 "g"
        diffuse0_b = Get-ColorChannel $diffuse0 "b"
        diffuse1_r = Get-ColorChannel $diffuse1 "r"
        diffuse1_g = Get-ColorChannel $diffuse1 "g"
        diffuse1_b = Get-ColorChannel $diffuse1 "b"
        material_ambient_r = Get-ColorChannel $materialAmbient "r"
        material_ambient_g = Get-ColorChannel $materialAmbient "g"
        material_ambient_b = Get-ColorChannel $materialAmbient "b"
        material_diffuse_r = Get-ColorChannel $materialDiffuse "r"
        material_diffuse_g = Get-ColorChannel $materialDiffuse "g"
        material_diffuse_b = Get-ColorChannel $materialDiffuse "b"
        effective_diffuse_r = Get-ColorChannel $effectiveDiffuse "r"
        effective_diffuse_g = Get-ColorChannel $effectiveDiffuse "g"
        effective_diffuse_b = Get-ColorChannel $effectiveDiffuse "b"
        effective_diffuse_zero = $effectiveDiffuseZero
        ambient_only_lighting = $ambientOnlyLighting
        base_luma_average = Round4 (Get-ColorLuma (Get-Prop $baseStats "average"))
        modulation_luma_min = Round4 $modMinLuma
        modulation_luma_max = Round4 $modMaxLuma
        modulation_luma_range = Round4 ($modMaxLuma - $modMinLuma)
        modulation_luma_average = Round4 (Get-ColorLuma (Get-Prop $modStats "average"))
        output_luma_min = Round4 $outMinLuma
        output_luma_max = Round4 $outMaxLuma
        output_luma_range = Round4 ($outMaxLuma - $outMinLuma)
        output_luma_average = Round4 (Get-ColorLuma (Get-Prop $outStats "average"))
        vertex_color_luma_min = Round4 $vertexColorMinLuma
        vertex_color_luma_max = Round4 $vertexColorMaxLuma
        vertex_color_luma_range = Round4 ($vertexColorMaxLuma - $vertexColorMinLuma)
        vertex_color_luma_average = Round4 (Get-ColorLuma (Get-Prop $vertexColorStats "average"))
        shader_primary_luma_min = Round4 $shaderPrimaryMinLuma
        shader_primary_luma_max = Round4 $shaderPrimaryMaxLuma
        shader_primary_luma_range = Round4 ($shaderPrimaryMaxLuma - $shaderPrimaryMinLuma)
        shader_primary_luma_average = Round4 (Get-ColorLuma (Get-Prop $shaderPrimaryStats "average"))
        sample_count = @(Get-Prop $diagnostics "samples" @()).Count
    }
}

function Add-ModelRows {
    param([System.Collections.Generic.List[object]]$Rows, [string]$ModelKey, [object]$Model)
    if ($null -eq $Model) {
        return
    }
    $batches = @(Get-Prop $Model "native_batches" @())
    $modelName = [string](Get-Prop $Model "name" $ModelKey)
    foreach ($batch in $batches) {
        $row = New-BatchLightingRow -ModelKey $ModelKey -ModelName $modelName -Batch $batch
        if ($null -ne $row) {
            $Rows.Add($row)
        }
    }
}

function New-ModelAggregate {
    param([string]$ModelKey, [object[]]$Rows)
    $directionalRows = @($Rows | Where-Object { $_.directional_evaluated_vertex_count -gt 0 })
    [int64]$directionalVertexCount = 0
    [double]$dotSum = 0.0
    [double]$scaleSum = 0.0
    [double]$oppositeScaleSum = 0.0
    [int64]$positive = 0
    [int64]$negative = 0
    [double]$dotMin = 0.0
    [double]$dotMax = 0.0
    [bool]$first = $true
    foreach ($row in $directionalRows) {
        $count = [int64]$row.directional_evaluated_vertex_count
        $directionalVertexCount += $count
        $dotSum += [double]$row.light0_dot_average * [double]$count
        $scaleSum += [double]$row.light0_diffuse_scale_average * [double]$count
        $oppositeScaleSum += [double]$row.light0_opposite_diffuse_scale_average * [double]$count
        $positive += [int64]$row.light0_dot_positive_count
        $negative += [int64]$row.light0_dot_negative_count
        if ($first) {
            $dotMin = [double]$row.light0_dot_min
            $dotMax = [double]$row.light0_dot_max
            $first = $false
        } else {
            $dotMin = [Math]::Min($dotMin, [double]$row.light0_dot_min)
            $dotMax = [Math]::Max($dotMax, [double]$row.light0_dot_max)
        }
    }
    $flatRows = @($Rows | Where-Object {
        $_.directional_evaluated_vertex_count -gt 0 -and [double]$_.modulation_luma_range -lt 8.0
    })
    $flatNonAmbientOnlyRows = @($flatRows | Where-Object {
        -not [bool]$_.ambient_only_lighting
    })
    $runtimeVectorRows = @($Rows | Where-Object {
        [string]$_.vector_source -like "*0045dd50_runtime_environment*"
    })
    $runtimeVectorRelevantRows = @($runtimeVectorRows | Where-Object {
        -not [bool]$_.effective_diffuse_zero
    })
    $ambientOnlyRows = @($Rows | Where-Object {
        [bool]$_.ambient_only_lighting
    })
    $flatShaderPrimaryRows = @($Rows | Where-Object {
        [double]$_.shader_primary_luma_range -lt 8.0
    })
    $ambientOnlyFlatShaderPrimaryRows = @($flatShaderPrimaryRows | Where-Object {
        [bool]$_.ambient_only_lighting
    })
    $ambientOnlyVariedShaderPrimaryRows = @($ambientOnlyRows | Where-Object {
        [double]$_.shader_primary_luma_range -ge 8.0
    })
    return [ordered]@{
        model_key = $ModelKey
        batch_count = $Rows.Count
        directional_batch_count = $directionalRows.Count
        directional_vertex_count = $directionalVertexCount
        light0_dot_min = Round4 $dotMin
        light0_dot_max = Round4 $dotMax
        light0_dot_average = if ($directionalVertexCount -gt 0) { Round4 ($dotSum / [double]$directionalVertexCount) } else { 0 }
        light0_diffuse_scale_average = if ($directionalVertexCount -gt 0) { Round4 ($scaleSum / [double]$directionalVertexCount) } else { 0 }
        light0_opposite_diffuse_scale_average = if ($directionalVertexCount -gt 0) { Round4 ($oppositeScaleSum / [double]$directionalVertexCount) } else { 0 }
        light0_dot_positive_ratio = if ($directionalVertexCount -gt 0) { Round4 ($positive / [double]$directionalVertexCount) } else { 0 }
        light0_dot_negative_ratio = if ($directionalVertexCount -gt 0) { Round4 ($negative / [double]$directionalVertexCount) } else { 0 }
        nearly_flat_modulation_batch_count = $flatRows.Count
        nearly_flat_modulation_nonambient_batch_count = $flatNonAmbientOnlyRows.Count
        ambient_only_lighting_batch_count = $ambientOnlyRows.Count
        nearly_flat_shader_primary_batch_count = $flatShaderPrimaryRows.Count
        ambient_only_flat_shader_primary_batch_count = $ambientOnlyFlatShaderPrimaryRows.Count
        ambient_only_varied_shader_primary_batch_count = $ambientOnlyVariedShaderPrimaryRows.Count
        runtime_environment_vector_batch_count = $runtimeVectorRows.Count
        runtime_environment_vector_relevant_batch_count = $runtimeVectorRelevantRows.Count
    }
}

function Get-PicaTraceSummary {
    param([string]$Path)
    $summary = [ordered]@{
        available = $false
        path = $Path
    }
    if (-not (Test-Path -LiteralPath $Path)) {
        return $summary
    }
    $trace = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $uploads = Get-Prop $trace "shader_uniform_uploads"
    $fogState = Get-Prop $trace "fog_state"
    $frameCapture = Get-Prop $trace "frame_capture"
    $drawSummary = Get-Prop $frameCapture "draw_summary"
    $cmbDrawTrace = Get-Prop $frameCapture "cmb_vertex_lighting_uniform_draw_trace"
    $blocks = @(Get-Prop $uploads "vertex_hemisphere_blocks" @())
    $candidateBlocks = @($blocks | Where-Object { [bool](Get-Prop $_ "vertex_hemisphere_candidate" $false) })
    $unique = @{}
    foreach ($block in $candidateBlocks) {
        $xyzw = @(Get-Prop $block "xyzw" @())
        if ($xyzw.Count -lt 4) {
            continue
        }
        $rounded = Convert-ToRoundedVector4 -Values $xyzw
        $key = Convert-ToInvariantVectorKey -Values $rounded
        if (-not $unique.ContainsKey($key)) {
            $unique[$key] = [ordered]@{
                count = 0
                xyzw = $rounded
            }
        }
        ++$unique[$key].count
    }
    $uniqueVectors = @(
        $unique.GetEnumerator() |
            Sort-Object -Property { $_.Value.count } -Descending |
            Select-Object -First 16 |
            ForEach-Object {
                [ordered]@{
                    xyzw = @($_.Value.xyzw)
                    xyzw_key = $_.Key
                    count = $_.Value.count
                }
            }
    )
    return [ordered]@{
        available = $true
        path = $Path
        source_kind = Get-Prop $trace "source_kind" ""
        vertex_hemisphere_block_count = Get-Int64 (Get-Prop $uploads "vertex_hemisphere_block_count" 0)
        candidate_block_count = $candidateBlocks.Count
        unique_candidate_vectors = $uniqueVectors
        final_fog_state = $fogState
        pica_draw_count = Get-Int64 (Get-Prop $drawSummary "draw_count" 0)
        pica_fog_decoded_draw_count = Get-Int64 (Get-Prop $drawSummary "fog_decoded_draw_count" 0)
        pica_fog_enabled_draw_count = Get-Int64 (Get-Prop $drawSummary "fog_enabled_draw_count" 0)
        pica_fog_or_gas_enabled_draw_count = Get-Int64 (Get-Prop $drawSummary "fog_or_gas_enabled_draw_count" 0)
        pica_fog_mode_counts = Get-Prop $drawSummary "fog_mode_counts"
        pica_fog_color_counts = Get-Prop $drawSummary "fog_color_counts"
        cmb_vertex_lighting_draw_trace_available = [bool](Get-Prop $cmbDrawTrace "available" $false)
        cmb_vertex_lighting_draw_count = Get-Int64 (Get-Prop $cmbDrawTrace "draw_count" 0)
        cmb_vertex_lighting_vertex_count = Get-Int64 (Get-Prop $cmbDrawTrace "vertex_count" 0)
        cmb_vertex_lighting_classification_counts = Get-Prop $cmbDrawTrace "classification_counts"
        cmb_vertex_lighting_classification_vertex_counts = Get-Prop $cmbDrawTrace "classification_vertex_counts"
    }
}

if (-not (Test-Path -LiteralPath $DemoJson)) {
    throw "Demo JSON not found: $DemoJson"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$demo = Get-Content -LiteralPath $DemoJson -Raw | ConvertFrom-Json
$scene = Get-Prop $demo "engine_render_scene"
$rows = [System.Collections.Generic.List[object]]::new()

Add-ModelRows -Rows $rows -ModelKey "room" -Model (Get-Prop $scene "room")
Add-ModelRows -Rows $rows -ModelKey "link_child" -Model (Get-Prop $scene "link_child")
$actorIndex = 0
foreach ($actor in @(Get-Prop $scene "native_actor_visuals" @())) {
    Add-ModelRows -Rows $rows -ModelKey ("actor_{0}" -f $actorIndex) -Model $actor
    ++$actorIndex
}

$csvPath = Join-Path $OutputDir "lighting_batches.csv"
$jsonPath = Join-Path $OutputDir "lighting_diagnostics.json"
$markdownPath = Join-Path $OutputDir "lighting_findings.md"

$rows | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding ascii

$aggregates = @()
foreach ($group in ($rows | Group-Object -Property model_key)) {
    $aggregates += New-ModelAggregate -ModelKey $group.Name -Rows @($group.Group)
}

$traceSummary = Get-PicaTraceSummary -Path $PicaTraceJson
$visualSummaryObject = $null
if (Test-Path -LiteralPath $VisualCompareSummary) {
    $visualSummaryObject = Get-Content -LiteralPath $VisualCompareSummary -Raw | ConvertFrom-Json
}

$findings = [System.Collections.Generic.List[string]]::new()
foreach ($aggregate in $aggregates) {
    if ([int64]$aggregate.directional_batch_count -eq 0) {
        $findings.Add("$($aggregate.model_key): no directional lighting batches were evaluated")
    }
    if ([int64]$aggregate.directional_vertex_count -gt 0 -and
        [double]$aggregate.light0_dot_negative_ratio -lt 0.05) {
        $findings.Add("$($aggregate.model_key): primary light has almost no negative dot products; light direction or normal space is suspicious")
    }
    if ([int64]$aggregate.directional_vertex_count -gt 0 -and
        [double]$aggregate.light0_diffuse_scale_average -gt 0.75) {
        $findings.Add("$($aggregate.model_key): average primary diffuse scale is high, so little geometry is being darkened")
    }
    if ([int64]$aggregate.directional_vertex_count -gt 0 -and
        [double]$aggregate.light0_opposite_diffuse_scale_average -gt
        ([double]$aggregate.light0_diffuse_scale_average + 0.15)) {
        $findings.Add("$($aggregate.model_key): opposite primary vector produces materially higher diffuse scale; verify native vector sign/space")
    }
    if ([int64]$aggregate.nearly_flat_modulation_nonambient_batch_count -gt 0) {
        $findings.Add("$($aggregate.model_key): $($aggregate.nearly_flat_modulation_nonambient_batch_count) non-ambient-only batches have near-flat modulation luma")
    }
    if ([int64]$aggregate.ambient_only_lighting_batch_count -gt 0 -and
        [int64]$aggregate.ambient_only_varied_shader_primary_batch_count -eq 0) {
        $findings.Add("$($aggregate.model_key): ambient-only batches have flat estimated shader primary color; native primary/vertex color TextureEnv route is suspicious")
    }
    if ([bool]$traceSummary.available -and
        [int64]$aggregate.runtime_environment_vector_relevant_batch_count -gt 0) {
        $findings.Add("$($aggregate.model_key): demo uses runtime environment vectors on $($aggregate.runtime_environment_vector_relevant_batch_count) diffuse-relevant batches while a PICA VSH uniform trace is available for cross-checking")
    }
}
if ([bool]$traceSummary.available -and [int64]$traceSummary.pica_fog_enabled_draw_count -gt 0) {
    $findings.Add(("PICA trace enables native fog on {0}/{1} decoded draws; validate demo fog application before relying on screenshot luma or flat-lighting findings" -f
        $traceSummary.pica_fog_enabled_draw_count,
        $traceSummary.pica_fog_decoded_draw_count))
}

$report = [ordered]@{
    format = "oot3d_native_pica_lighting_diagnostics_v1"
    generated_at = (Get-Date).ToString("o")
    demo_json = $DemoJson
    batch_csv = $csvPath
    pica_trace = $traceSummary
    visual_compare_summary = if ($null -ne $visualSummaryObject) {
        [ordered]@{
            path = $VisualCompareSummary
            demo_stats = Get-Prop $visualSummaryObject "demo_stats"
            emulator_matched_stats = Get-Prop $visualSummaryObject "emulator_matched_stats"
            comparison = Get-Prop $visualSummaryObject "comparison"
            findings = @(Get-Prop $visualSummaryObject "findings" @())
        }
    } else {
        [ordered]@{ path = $VisualCompareSummary; available = $false }
    }
    pica_lighting_state = Get-Prop $scene "pica_lighting"
    model_aggregates = $aggregates
    findings = @($findings)
    batches = @($rows)
}

$report | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $jsonPath -Encoding ascii

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# OOT3D Lighting Diagnostics")
$lines.Add("")
$lines.Add("- Demo JSON: ``$DemoJson``")
$lines.Add("- Batch CSV: ``$csvPath``")
$lines.Add("- PICA trace: ``$PicaTraceJson``")
$lines.Add("")
$lines.Add("## PICA Fog")
if ([bool]$traceSummary.available) {
    $finalFog = Get-Prop $traceSummary "final_fog_state"
    $fogColor = Get-Prop $finalFog "color_rgb_u8"
    $lines.Add(("- decoded draw fog: {0}/{1} enabled; modes {2}; colors {3}" -f
        $traceSummary.pica_fog_enabled_draw_count,
        $traceSummary.pica_fog_decoded_draw_count,
        (($traceSummary.pica_fog_mode_counts | ConvertTo-Json -Compress) -replace '"', "'"),
        (($traceSummary.pica_fog_color_counts | ConvertTo-Json -Compress) -replace '"', "'")))
    if ($null -ne $fogColor) {
        $lines.Add(("- final register fog mode: {0}; color rgb {1},{2},{3}" -f
            (Get-Prop $finalFog "mode_name" ""),
            (Get-Prop $fogColor "r" 0),
            (Get-Prop $fogColor "g" 0),
            (Get-Prop $fogColor "b" 0)))
    }
} else {
    $lines.Add("- PICA trace unavailable.")
}
$lines.Add("")
$lines.Add("## Model Aggregates")
foreach ($aggregate in $aggregates) {
    $lines.Add(("- {0}: directional batches {1}/{2}, ambient-only batches {11}, dot avg {3}, dot range {4}..{5}, positive {6}, negative {7}, diffuse scale avg {8}, opposite scale avg {9}, flat modulation batches {10}, ambient-only varied primary batches {12}, ambient-only flat primary batches {13}" -f
        $aggregate.model_key,
        $aggregate.directional_batch_count,
        $aggregate.batch_count,
        $aggregate.light0_dot_average,
        $aggregate.light0_dot_min,
        $aggregate.light0_dot_max,
        $aggregate.light0_dot_positive_ratio,
        $aggregate.light0_dot_negative_ratio,
        $aggregate.light0_diffuse_scale_average,
        $aggregate.light0_opposite_diffuse_scale_average,
        $aggregate.nearly_flat_modulation_batch_count,
        $aggregate.ambient_only_lighting_batch_count,
        $aggregate.ambient_only_varied_shader_primary_batch_count,
        $aggregate.ambient_only_flat_shader_primary_batch_count))
}
$lines.Add("")
$lines.Add("## Findings")
if ($findings.Count -eq 0) {
    $lines.Add("- No diagnostic findings triggered.")
} else {
    foreach ($finding in $findings) {
        $lines.Add("- $finding")
    }
}
$lines | Set-Content -LiteralPath $markdownPath -Encoding ascii

$report | ConvertTo-Json -Depth 16
