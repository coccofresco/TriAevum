param(
    [string]$Manifest = "I:\oot3dre_work\standalone_demo\kokiri_forest\demo_manifest.json",
    [string]$ResourceRoot = "",
    [string]$Output = "",
    [string]$ScreenshotOutput = "",
    [string]$BuildDir = "",
    [string]$BuildType = "RelWithDebInfo",
    [int]$Frames = 0,
    [double]$MaxSeconds = 0,
    [int]$Width = 1280,
    [int]$Height = 720,
    [int]$Backend = 2,
    [double]$MaterialAnimationFrame = 0.0,
    [string]$RenderMode = "native_texture",
    [int]$EntranceIndex = -1,
    [string[]]$ExtraArgs = @(),
    [switch]$ScreenshotSequence,
    [switch]$Verify,
    [switch]$Launch,
    [switch]$TitleIntro
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$defaultKokiriManifest = "I:\oot3dre_work\standalone_demo\kokiri_forest\demo_manifest.json"
$defaultTitleIntroManifest = "I:\oot3dre_work\standalone_demo\title_intro\demo_manifest.json"
if ($TitleIntro -and $Manifest -eq $defaultKokiriManifest) {
    $Manifest = $defaultTitleIntroManifest
}
if ([string]::IsNullOrWhiteSpace($ResourceRoot)) {
    $ResourceRoot = Join-Path $repoRoot "runtime/three_ds_recomp\src\fast"
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path (Split-Path -Parent $Manifest) "native_host\runtime/three_ds_recomp_native_fast3d_demo.json"
}
if ([string]::IsNullOrWhiteSpace($ScreenshotOutput)) {
    $ScreenshotOutput = Join-Path (Split-Path -Parent $Output) "runtime/three_ds_recomp_native_fast3d_demo_framebuffer.bmp"
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build-codex-rel"
}

$source = Join-Path $repoRoot "tools\oot3d\native_demo_host\oot3d_native_fast3d_demo.cpp"
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
$cmakeExe = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninjaExe = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$clExe = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe"
$toolchainFile = Join-Path $BuildDir "vcpkg\scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path -LiteralPath $toolchainFile)) {
    $sharedToolchainFile = Join-Path $repoRoot "build-codex\vcpkg\scripts\buildsystems\vcpkg.cmake"
    if (Test-Path -LiteralPath $sharedToolchainFile) {
        $toolchainFile = $sharedToolchainFile
    }
}
$cmakeCache = Join-Path $BuildDir "CMakeCache.txt"

function Set-Oot3dProcessForeground {
    param(
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutMs = 10000
    )

    if ($null -eq ("Oot3dWindowFocus" -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class Oot3dWindowFocus {
    [DllImport("user32.dll")]
    public static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
}
"@
    }

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Process.HasExited) {
            return $false
        }
        $Process.Refresh()
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
            [Oot3dWindowFocus]::ShowWindowAsync($Process.MainWindowHandle, 9) | Out-Null
            [Oot3dWindowFocus]::SetForegroundWindow($Process.MainWindowHandle) | Out-Null
            return $true
        }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Assert-Oot3dFramebufferNotCollapsed {
    param(
        [string]$Path,
        [string]$OutputPath
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "OOT3D native Fast3D demo screenshot was not written: $Path"
    }

    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::new($Path)
    try {
        $stepX = [Math]::Max(1, [int][Math]::Floor($bitmap.Width / 96))
        $stepY = [Math]::Max(1, [int][Math]::Floor($bitmap.Height / 54))
        $unique = [System.Collections.Generic.HashSet[int]]::new()
        $minR = 255
        $minG = 255
        $minB = 255
        $maxR = 0
        $maxG = 0
        $maxB = 0
        for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
            for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
                $pixel = $bitmap.GetPixel($x, $y)
                $packed = ($pixel.R -shl 16) -bor ($pixel.G -shl 8) -bor $pixel.B
                [void]$unique.Add($packed)
                $minR = [Math]::Min($minR, [int]$pixel.R)
                $minG = [Math]::Min($minG, [int]$pixel.G)
                $minB = [Math]::Min($minB, [int]$pixel.B)
                $maxR = [Math]::Max($maxR, [int]$pixel.R)
                $maxG = [Math]::Max($maxG, [int]$pixel.G)
                $maxB = [Math]::Max($maxB, [int]$pixel.B)
            }
        }
        $range = [Math]::Max($maxR - $minR, [Math]::Max($maxG - $minG, $maxB - $minB))
        if ($unique.Count -lt 16 -or $range -lt 16) {
            throw "OOT3D native Fast3D demo framebuffer collapsed to a near-uniform image: $Path; output=$OutputPath; sampled_unique=$($unique.Count); sampled_rgb_range=$range"
        }
    } finally {
        $bitmap.Dispose()
    }
}

function Initialize-Oot3dKokiriNativeFast3dManifest {
    param(
        [string]$ManifestPath,
        [string]$RepoRoot
    )

    if (Test-Path -LiteralPath $ManifestPath) {
        return
    }

    $demoRoot = Split-Path -Parent $ManifestPath
    $workRoot = Split-Path -Parent (Split-Path -Parent $demoRoot)
    $nativeHostDir = Join-Path $demoRoot "native_host"
    New-Item -ItemType Directory -Force -Path $nativeHostDir | Out-Null

    $sceneDir = "E:\ppssppvr\oot3d_decomp\work\extract\romfs\scene"
    $linkChildManifest = "I:\oot3dre_work\character_conversion\link_child_character_conversion_manifest.json"
    $manifestObject = [ordered]@{
        format = "oot3d_standalone_demo_manifest_v1"
        status = "built"
        demo_id = "kokiri_forest"
        artifact_class = "native_fast3d_kokiri_fixture"
        claim_scope = @(
            "oot3d_native_zsi_room_scene_loading",
            "oot3d_native_actor_zar_cmb_visual_loading",
            "native_collision_floor_query_diagnostic",
            "fast3d_native_render_harness"
        )
        non_claims = @(
            "native_oot3d_actor_behavior_parity",
            "native_oot3d_actor_scale_parity",
            "native_oot3d_skinned_actor_animation_parity",
            "n64_runtime_asset_substitution"
        )
        repo_root = "$RepoRoot"
        work_root = $workRoot
        demo_root = $demoRoot
        policy = [ordered]@{
            runtime_asset_policy = "oot3d_native_or_offline_derived_only"
            no_shipwright_runtime_replacement = $true
            no_n64_runtime_asset_substitution = $true
            generated_assets_committable = $false
        }
        sources = [ordered]@{
            room_visual = [ordered]@{
                kind = "oot3d_room_zsi_embedded_cmb"
                path = (Join-Path $sceneDir "spot04_0_info.zsi")
            }
            collision = [ordered]@{
                kind = "oot3d_scene_zsi_native_collision"
                path = (Join-Path $sceneDir "spot04_info.zsi")
            }
            native_code_bin = [ordered]@{
                kind = "oot3d_exefs_code_bin"
                path = "E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin"
            }
            link_child = [ordered]@{
                kind = "oot3d_offline_character_conversion_manifest"
                path = $linkChildManifest
                cmb_name = "child/model/childlink_v2.cmb"
                csab_name = "child/anim/nml_run_free.csab"
                selection_manifest = (Join-Path $RepoRoot "tools\oot3d\oot3d_asset_tool\profiles\link_child_static_base_selection.json")
            }
        }
        assets = [ordered]@{
            viewer_script = (Join-Path $RepoRoot "tools\oot3d\standalone_demo\oot3d_viewer.py")
            native_host_probe = (Join-Path $nativeHostDir "native_host_probe.json")
            native_host_trace = (Join-Path $nativeHostDir "native_host_trace.json")
            native_host_trace_compare = (Join-Path $nativeHostDir "native_host_trace_compare.json")
            native_host_preview_png = (Join-Path $nativeHostDir "native_host_preview.png")
            runtime/three_ds_recomp_native_resource_probe = (Join-Path $nativeHostDir "runtime/three_ds_recomp_native_resource_probe.json")
            runtime/three_ds_recomp_native_runtime_probe = (Join-Path $nativeHostDir "runtime/three_ds_recomp_native_runtime_probe.json")
            runtime/three_ds_recomp_native_runtime_trace = (Join-Path $nativeHostDir "runtime/three_ds_recomp_native_runtime_trace.json")
            runtime/three_ds_recomp_native_runtime_preview_ppm = (Join-Path $nativeHostDir "runtime/three_ds_recomp_native_runtime_preview.ppm")
            oot3d_reference_trace = (Join-Path $nativeHostDir "oot3d_reference_trace.json")
        }
        movement = [ordered]@{
            controller = "demo_scripted_wasd"
            model = "native_locomotion_diagnostic"
            claim = "poc_only_not_full_oot3d_player_state_machine"
            radius_units = 12.0
            height_units = 56.0
            entrypoint = [ordered]@{
                source = "oot3d_link_house_exit_list_global_entrance"
                source_scene = "link_info.zsi"
                source_exit_list_value = 529
                source_exit_list_value_hex = "0x0211"
                global_entrance_index = 529
                global_entrance_index_hex = "0x0211"
                expected_scene_stem = "spot04"
                expected_scene_id = 85
            }
            spawn = [ordered]@{ x = -31.0; y = 100.0; z = 1073.0 }
            animation_source = "child/anim/nml_run_free.csab"
        }
    }

    New-Item -ItemType Directory -Force -Path $demoRoot | Out-Null
    $manifestObject | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ManifestPath -Encoding ascii
}

function Initialize-Oot3dTitleIntroNativeFast3dManifest {
    param(
        [string]$ManifestPath,
        [string]$RepoRoot
    )

    if (Test-Path -LiteralPath $ManifestPath) {
        try {
            $existing = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
            if ($existing.demo_id -eq "title_intro_hyrule_field" -and
                $null -ne $existing.sources -and
                $null -ne $existing.sources.title_intro -and
                $null -ne $existing.sources.room_visual -and
                [System.IO.Path]::GetFileName([string]$existing.sources.room_visual.path) -eq "spot99_0_info.zsi" -and
                @($existing.claim_scope) -contains "oot3d_native_title_intro_enmag_logo_asset_binding_loading" -and
                @($existing.claim_scope) -contains "oot3d_native_title_intro_opening_frame_runtime_sampling") {
                return
            }
        } catch {
        }
    }

    $demoRoot = Split-Path -Parent $ManifestPath
    $workRoot = Split-Path -Parent (Split-Path -Parent $demoRoot)
    $nativeHostDir = Join-Path $demoRoot "native_host"
    New-Item -ItemType Directory -Force -Path $nativeHostDir | Out-Null

    $romfsRoot = "E:\ppssppvr\oot3d_decomp\work\extract\romfs"
    $sceneDir = Join-Path $romfsRoot "scene"
    $linkChildManifest = "I:\oot3dre_work\character_conversion\link_child_character_conversion_manifest.json"
    $manifestObject = [ordered]@{
        format = "oot3d_standalone_demo_manifest_v1"
        status = "built"
        demo_id = "title_intro_hyrule_field"
        artifact_class = "native_fast3d_title_intro_fixture"
        claim_scope = @(
            "oot3d_native_spot99_title_scene_loading",
            "oot3d_native_title_intro_initial_spot99_cutscene_camera_loading",
            "oot3d_native_title_intro_qdb_actor_cue_loading",
            "oot3d_native_opening_link_epona_zar_cmb_csab_loading",
            "oot3d_native_title_intro_enmag_logo_asset_binding_loading",
            "oot3d_native_title_intro_enmag_draw_route_decoding",
            "oot3d_native_title_intro_enmag_update_route_decoding",
            "oot3d_native_title_intro_opening_frame_runtime_sampling",
            "fast3d_native_render_harness"
        )
        non_claims = @(
            "native_oot3d_title_intro_full_camera_state_machine_parity_pending_code_bin_selector_decode",
            "native_oot3d_title_intro_opening_frame_runtime_actor_logo_render_binding_pending",
            "native_oot3d_title_logo_projection_render_context_and_alpha_runtime_application_pending",
            "native_oot3d_title_intro_cue_id_to_animation_binding_finalized",
            "n64_runtime_asset_substitution"
        )
        repo_root = "$RepoRoot"
        work_root = $workRoot
        demo_root = $demoRoot
        policy = [ordered]@{
            runtime_asset_policy = "oot3d_native_or_offline_derived_only"
            no_shipwright_runtime_replacement = $true
            no_n64_runtime_asset_substitution = $true
            generated_assets_committable = $false
        }
        sources = [ordered]@{
            room_visual = [ordered]@{
                kind = "oot3d_room_zsi_embedded_cmb"
                path = (Join-Path $sceneDir "spot99_0_info.zsi")
            }
            collision = [ordered]@{
                kind = "oot3d_scene_zsi_native_collision"
                path = (Join-Path $sceneDir "spot99_info.zsi")
            }
            title_intro = [ordered]@{
                kind = "oot3d_title_intro_native_source_graph"
                romfs_root = $romfsRoot
                scene_zar_qdb_demo = (Join-Path $romfsRoot "scene\spot00.zar")
                link_opening_archive = (Join-Path $romfsRoot "actor\zelda_link_opening.zar")
                epona_archive = (Join-Path $romfsRoot "actor\zelda_horse.zar")
                title_logo_archive = (Join-Path $romfsRoot "actor\zelda_mag.zar")
                source_table = (Join-Path $RepoRoot "tools\oot3d\decomp_support\analysis\title_intro_source_table.json")
            }
            native_code_bin = [ordered]@{
                kind = "oot3d_exefs_code_bin"
                path = "E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin"
            }
            link_child = [ordered]@{
                kind = "oot3d_offline_character_conversion_manifest"
                path = $linkChildManifest
                cmb_name = "child/model/childlink_v2.cmb"
                csab_name = "child/anim/nml_run_free.csab"
                selection_manifest = (Join-Path $RepoRoot "tools\oot3d\oot3d_asset_tool\profiles\link_child_static_base_selection.json")
            }
        }
        assets = [ordered]@{
            native_host_probe = (Join-Path $nativeHostDir "native_host_probe.json")
            native_host_trace = (Join-Path $nativeHostDir "native_host_trace.json")
            runtime/three_ds_recomp_native_runtime_probe = (Join-Path $nativeHostDir "runtime/three_ds_recomp_native_runtime_probe.json")
            title_intro_reference_source_table = (Join-Path $RepoRoot "tools\oot3d\decomp_support\analysis\title_intro_source_table.md")
        }
        movement = [ordered]@{
            controller = "title_intro_qdb_actor_cues"
            model = "native_title_intro_runtime_diagnostic"
            claim = "title_intro_initial_spot99_scene_camera_plus_actor_visualization_until_logo_environment_commands_are_bound"
            radius_units = 12.0
            height_units = 56.0
            entrypoint = [ordered]@{
                source = "spot00_demo_epona_00_qdb_player_action_cue_0"
                qdb_index = 0
                cue_command_id_hex = "0x0000000a"
                cue_index = 0
            }
            spawn = [ordered]@{ x = -2961.0; y = 510.0; z = 7700.0 }
            animation_source = "title_intro_native_opening_actor_archives"
        }
    }

    New-Item -ItemType Directory -Force -Path $demoRoot | Out-Null
    $manifestObject | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ManifestPath -Encoding ascii
}

function Test-Oot3dNativeMaterialTextureEnvModel {
    param(
        [object]$Model
    )

    $materials = @($Model.native_materials)
    if ($materials.Count -le 0) {
        return $false
    }

    foreach ($material in $materials) {
        if ($material.raw_material_size -ne 348 -or
            $material.raw_texture_stage_selector_decoded -ne $true -or
            $material.texture_env_stage_indices_covered -ne $true -or
            $material.material_colors_decoded -ne $true -or
            $null -eq $material.material_colors -or
            $null -eq $material.material_lighting_flags -or
            $null -eq $material.raw_material_analysis -or
            $null -eq $material.texture_env -or
            $null -eq $material.texture_env_program) {
            return $false
        }

        $rawStageSlots = @($material.raw_texture_stage_slots)
        $textureEnvStages = @($material.texture_env_stages)
        $materialConstants = @($material.material_colors.constant)
        $candidateRange = @($material.raw_material_analysis.texture_stage_candidate_range)
        $textureEnv = $material.texture_env
        $textureEnvProgram = $material.texture_env_program
        $lightingFlags = $material.material_lighting_flags
        $rawStageCount = [int]$material.raw_texture_stage_count

        if ($rawStageSlots.Count -ne 6 -or
            $rawStageCount -gt $rawStageSlots.Count -or
            [int]$material.texture_env_stage_record_count -ne $rawStageCount -or
            $textureEnvStages.Count -ne $rawStageCount -or
            $materialConstants.Count -ne 6 -or
            $candidateRange.Count -ne 2 -or
            [int]$candidateRange[0] -ne 160 -or
            [int]$candidateRange[1] -ne 304 -or
            [int]$material.raw_material_analysis.texture_stage_candidate_nonzero_word_count -le 0 -or
            $textureEnv.decoded -ne $true -or
            [int]$textureEnv.raw_size -ne 40 -or
            $textureEnv.requires_shader_evaluation -ne $true -or
            @($textureEnv.source_rgb).Count -ne 3 -or
            @($textureEnv.operand_rgb).Count -ne 3 -or
            $textureEnvProgram.decoded -ne $true -or
            [int]$textureEnvProgram.stage_count -ne $rawStageCount -or
            $textureEnvProgram.rgb_combines_known -ne $true -or
            $textureEnvProgram.rgb_sources_known -ne $true -or
            $textureEnvProgram.rgb_operands_known -ne $true -or
            $textureEnvProgram.rgb_route_decoded -ne $true) {
            return $false
        }
        if ([string]$lightingFlags.source -ne "oot3d_cmb_material_header_bytes_0x00_0x03" -or
            $null -eq $lightingFlags.fragment_lighting_enabled -or
            $null -eq $lightingFlags.vertex_lighting_enabled -or
            $null -eq $lightingFlags.hemisphere_lighting_enabled -or
            $null -eq $lightingFlags.hemisphere_occlusion_enabled) {
            return $false
        }

        if ($rawStageCount -gt 0 -and [int]$textureEnv.table_index -ne [int]$rawStageSlots[0]) {
            return $false
        }
        if ($textureEnvProgram.requires_multi_stage_evaluation -ne
            ([int]$textureEnvProgram.stage_count -gt 1 -or $textureEnvProgram.uses_previous -eq $true)) {
            return $false
        }
        if ($textureEnvProgram.requires_multi_texture_sampling -ne
            ($textureEnvProgram.uses_texture1 -eq $true -or
                $textureEnvProgram.uses_texture2 -eq $true -or
                $textureEnvProgram.uses_texture3 -eq $true)) {
            return $false
        }
        if ($textureEnvProgram.requires_constant_color_selection -ne
            ($textureEnvProgram.uses_constant_color -eq $true)) {
            return $false
        }
        if ($textureEnvProgram.color_shader_path_supported -eq $true) {
            $vertexColorMultiplier = $textureEnvProgram.vertex_color_multiplier
            $primaryColorMultiplier = $textureEnvProgram.primary_color_multiplier
            if ([string]::IsNullOrWhiteSpace([string]$textureEnvProgram.color_shader_path) -or
                $null -eq $vertexColorMultiplier -or
                $null -eq $primaryColorMultiplier -or
                [int]$vertexColorMultiplier.r -lt 0 -or [int]$vertexColorMultiplier.r -gt 255 -or
                [int]$vertexColorMultiplier.g -lt 0 -or [int]$vertexColorMultiplier.g -gt 255 -or
                [int]$vertexColorMultiplier.b -lt 0 -or [int]$vertexColorMultiplier.b -gt 255 -or
                [int]$vertexColorMultiplier.a -lt 0 -or [int]$vertexColorMultiplier.a -gt 255 -or
                [double]$primaryColorMultiplier.x -le 0.0 -or
                [double]$primaryColorMultiplier.y -le 0.0 -or
                [double]$primaryColorMultiplier.z -le 0.0) {
                return $false
            }
            if ($textureEnvProgram.uses_color_scale -eq $true -and
                $textureEnvProgram.primary_color_multiplier_resolved -ne $true -and
                $textureEnvProgram.texture_color_multiplier_resolved -ne $true) {
                return $false
            }
            if ($textureEnvProgram.uses_constant_color -eq $true -and
                ($textureEnvProgram.constant_color_resolved -ne $true -or
                [int]$textureEnvProgram.constant_color_index -lt 0 -or
                [string]::IsNullOrWhiteSpace([string]$textureEnvProgram.constant_color_source) -or
                [int]$textureEnvProgram.vertex_color_constant_stage_count -le 0)) {
                return $false
            }
            if ($textureEnvProgram.requires_texture_color_add -eq $true) {
                $textureColorAddend = $textureEnvProgram.texture_color_addend
                if ($textureEnvProgram.texture_color_addend_resolved -ne $true -or
                    [int]$textureEnvProgram.texture_color_add_stage_count -le 0 -or
                    $null -eq $textureColorAddend -or
                    [int]$textureColorAddend.r -lt 0 -or [int]$textureColorAddend.r -gt 255 -or
                    [int]$textureColorAddend.g -lt 0 -or [int]$textureColorAddend.g -gt 255 -or
                    [int]$textureColorAddend.b -lt 0 -or [int]$textureColorAddend.b -gt 255 -or
                    [int]$textureColorAddend.a -lt 0 -or [int]$textureColorAddend.a -gt 255) {
                    return $false
                }
            }
        }

        for ($stage = 0; $stage -lt $rawStageCount; ++$stage) {
            $textureEnvStage = $textureEnvStages[$stage]
            if ([int]$rawStageSlots[$stage] -lt 0 -or
                [int]$textureEnvStage.table_index -ne [int]$rawStageSlots[$stage] -or
                [int]$textureEnvStage.stage_order -ne $stage -or
                $textureEnvStage.decoded -ne $true -or
                [int]$textureEnvStage.raw_size -ne 40 -or
                [int]$textureEnvStage.rgb_active_source_count -le 0 -or
                [int]$textureEnvStage.rgb_active_source_count -gt 3 -or
                @($textureEnvStage.source_rgb).Count -ne 3 -or
                @($textureEnvStage.operand_rgb).Count -ne 3) {
                return $false
            }
        }
    }

    return $true
}

function Test-Oot3dNativeKankyoTextureEnvModels {
    param(
        [object]$Json
    )

    if ($null -eq $Json.engine_render_scene -or $null -eq $Json.engine_render_scene.room) {
        return $false
    }

    [array]$environmentModels = @($Json.engine_render_scene.environment_models)
    if ($environmentModels.Count -ne 3) {
        return $false
    }

    $background = $Json.engine_render_scene.environment_background
    [array]$selectedCmabNames = @($background.native_kankyo_selected_cmab_names)
    if ($null -eq $background -or
        $selectedCmabNames.Count -ne 1 -or
        $selectedCmabNames -notcontains "misc/fogy_kumo_a.cmab" -or
        [int]$background.native_kankyo_material_animation_count -ne 1 -or
        [int]$background.native_kankyo_material_animation_applied_batch_count -ne 2 -or
        [math]::Abs([double]$background.native_kankyo_material_animation_frame) -gt 0.001) {
        return $false
    }

    $tenkyu = $environmentModels | Where-Object { $_.name -eq "kankyo:fogy_tenkyu1" } | Select-Object -First 1
    $kumo = $environmentModels | Where-Object { $_.name -eq "kankyo:fogy_kumo_a1" } | Select-Object -First 1
    if ($null -eq $tenkyu -or $null -eq $kumo) {
        return $false
    }

    if ([math]::Abs([double]$Json.engine_render_scene.bounds.max_extent -
            [double]$Json.engine_render_scene.room.bounds.max_extent) -gt 0.001) {
        return $false
    }
    if ([double]$tenkyu.bounds.max_extent -le [double]$Json.engine_render_scene.room.bounds.max_extent -or
        [double]$kumo.bounds.max_extent -le [double]$Json.engine_render_scene.room.bounds.max_extent) {
        return $false
    }
    if ([int]$tenkyu.native_material_animation_applied_batch_count -ne 0 -or
        [int]$tenkyu.native_material_animation_texture_transform_applied_batch_count -ne 0 -or
        [int]$kumo.native_material_animation_applied_batch_count -ne 1 -or
        [int]$kumo.native_material_animation_texture_transform_applied_batch_count -ne 1) {
        return $false
    }

    [array]$kumoMaterials = @($kumo.native_materials)
    if ($kumoMaterials.Count -ne 1 -or
        [int]$kumo.native_material_texture_env_program_texture1_color_multiply_batch_count -ne 1 -or
        [int]$kumo.native_material_texture_env_program_color_shader_applied_batch_count -ne 1 -or
        [int]$kumo.native_material_texture_env_program_uses_secondary_texture_batch_count -ne 1 -or
        [int]$kumo.native_material_combiner_pending_batch_count -ne 0) {
        return $false
    }

    $kumoMaterial = $kumoMaterials[0]
    $program = $kumoMaterial.texture_env_program
    if ($null -eq $program -or
        $program.color_shader_path -ne "texenv_program_texture0_texture1_vertex_color_multiply" -or
        $program.color_shader_path_supported -ne $true -or
        $program.color_shader_path_applied -ne $true -or
        $program.texture1_color_multiply_resolved -ne $true -or
        [int]$program.texture1_color_multiply_stage_count -ne 1 -or
        [int]$program.texture1_color_multiply_mapper_slot -ne 1 -or
        $program.texture1_color_multiply_source -ne
            "oot3d_cmb_texture_env_stage_modulate_texture0_texture1_then_previous_primary" -or
        [int]$kumoMaterial.secondary_texture_index -lt 0 -or
        [int]$kumoMaterial.secondary_texture_mapper_slot -ne 1 -or
        $kumoMaterial.secondary_texture_binding_source -ne
            "oot3d_cmb_texture_env_stage_modulate_texture0_texture1_then_previous_primary" -or
        $kumoMaterial.native_material_animation_applied -ne $true -or
        $kumoMaterial.native_material_animation_texture_transform_applied -ne $true -or
        $kumoMaterial.native_material_animation_texture_transform_kind -ne "transform_vec2_gate_1_plus_selector" -or
        [int]$kumoMaterial.native_material_animation_texture_transform_component_mask -ne 1 -or
        $kumoMaterial.native_material_combiner_requires_decoder -ne $false) {
        return $false
    }

    [array]$kumoTextureCoords = @($kumoMaterial.texture_coords)
    $animatedKumoTextureCoords = @($kumoTextureCoords | Where-Object {
        $_.native_material_animation_applied -eq $true -and
        $_.native_material_animation_kind -eq "transform_vec2_gate_1_plus_selector" -and
        [int]$_.native_material_animation_component_mask -eq 1
    })
    if ($animatedKumoTextureCoords.Count -ne 2) {
        return $false
    }

    if ($Json.PSObject.Properties.Name -contains "fast3d_adapter_lifetime") {
        $fast3d = $Json.fast3d_adapter_lifetime
        if ([int]$fast3d.secondary_texture_coord_batch_count -le 0 -or
            [int]$fast3d.secondary_texture_binding_count -le 0 -or
            [int]$fast3d.missing_secondary_texture_binding_count -ne 0 -or
            [int]$fast3d.shader_input_layout_mismatch_count -ne 0) {
            return $false
        }
    }

    return $true
}

function Assert-Oot3dNativeFast3dLaunchState {
    param(
        [object]$Json,
        [string]$OutputPath
    )

    $assetGraph = $Json.scene.asset_graph
    if ($null -eq $assetGraph -or $assetGraph.scene_zsi -notlike "*spot04_info.zsi") {
        return
    }

    $playerStart = $Json.scene.player_start
    $actorVisuals = $Json.scene.native_actor_visuals
    if ($null -eq $playerStart -or $null -eq $actorVisuals) {
        throw "OOT3D native Fast3D demo did not expose Kokiri launch state diagnostics: $OutputPath"
    }

    $isKokiriGlobal0211Entrance = [int]$playerStart.requested_global_entrance_index -eq 529
    if ($isKokiriGlobal0211Entrance -and (
        [int]$playerStart.requested_entrance_index -ne 3 -or
        [int]$playerStart.spawn_index -ne 3 -or
        [int]$playerStart.entrance_index -ne 3 -or
        [int]$playerStart.setup_index -ne 0 -or
        [int]$playerStart.room -ne 0)) {
        throw "OOT3D native Fast3D demo selected the wrong Kokiri entrance/setup for global entrance 0x0211: $OutputPath"
    }

    $actorVisualInstances = @($actorVisuals.instances)
    $grassInstances = @($actorVisualInstances | Where-Object { $_.actor_name -eq "ACTOR_EN_KUSA" })
    $kanbanInstances = @($actorVisualInstances | Where-Object { $_.actor_name -eq "ACTOR_EN_KANBAN" })
    $skinnedCharacterInstances = @($actorVisualInstances | Where-Object {
        $_.actor_name -eq "ACTOR_EN_MD" -or $_.actor_name -eq "ACTOR_EN_SA"
    })
    $gossipStoneInstances = @($actorVisualInstances | Where-Object { $_.actor_name -eq "ACTOR_EN_GS" })

    if ($isKokiriGlobal0211Entrance -and (
        [int]$actorVisuals.instance_count -ne 1 -or
        [int]$Json.engine_render_scene.native_actor_visual_count -ne 1 -or
        $grassInstances.Count -ne 0 -or
        $kanbanInstances.Count -ne 0 -or
        $skinnedCharacterInstances.Count -ne 0 -or
        $gossipStoneInstances.Count -ne 1)) {
        throw "OOT3D native Fast3D demo would render unresolved Kokiri actor visuals instead of preserving native selection/skinning diagnostics: $OutputPath"
    }

    $skippedActorVisualInstances = @($actorVisuals.skipped_instances)
    $skippedKanbanInstances = @($skippedActorVisualInstances | Where-Object {
        $_.actor_name -eq "ACTOR_EN_KANBAN" -and
        $_.reason -eq "native_actor_runtime_cmb_selection_not_decoded" -and
        [int]$_.parseable_cmb_entry_count -eq 12
    })
    $skippedGrassInstances = @($skippedActorVisualInstances | Where-Object {
        $_.actor_name -eq "ACTOR_EN_KUSA" -and
        $_.reason -eq "native_actor_runtime_cmb_selection_not_decoded" -and
        [int]$_.parseable_cmb_entry_count -eq 2
    })
    $skippedSkinnedInstances = @($skippedActorVisualInstances | Where-Object {
        $_.reason -eq "native_actor_skeleton_pose_contract_not_decoded"
    })

    if ($isKokiriGlobal0211Entrance -and (
        $skippedKanbanInstances.Count -ne 8 -or
        $skippedGrassInstances.Count -ne 12 -or
        $skippedSkinnedInstances.Count -ne 2)) {
        throw "OOT3D native Fast3D demo did not preserve Kokiri unresolved actor visual diagnostics: $OutputPath"
    }
}

if ($Manifest -eq $defaultTitleIntroManifest) {
    Initialize-Oot3dTitleIntroNativeFast3dManifest -ManifestPath $Manifest -RepoRoot "$repoRoot"
} elseif ($Manifest -eq $defaultKokiriManifest) {
    Initialize-Oot3dKokiriNativeFast3dManifest -ManifestPath $Manifest -RepoRoot "$repoRoot"
}

if (-not (Test-Path -LiteralPath $Manifest)) {
    throw "OOT3D standalone manifest not found: $Manifest"
}
if (-not (Test-Path -LiteralPath $ResourceRoot)) {
    throw "runtime/three_ds_recomp Fast3D resource root not found: $ResourceRoot"
}
if (-not (Test-Path -LiteralPath (Join-Path $ResourceRoot "shaders\directx\default.shader.hlsl")) -and
    -not (Test-Path -LiteralPath (Join-Path $ResourceRoot "shaders\opengl\default.shader.glsl"))) {
    throw "Fast3D shader resources not found below: $ResourceRoot"
}
if (-not (Test-Path -LiteralPath $source)) {
    throw "OOT3D native Fast3D demo source not found: $source"
}
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio developer command prompt not found: $vsDevCmd"
}
if (-not (Test-Path -LiteralPath $cmakeExe)) {
    throw "CMake executable not found: $cmakeExe"
}
if (-not (Test-Path -LiteralPath $ninjaExe) -and -not (Test-Path -LiteralPath $cmakeCache)) {
    throw "Ninja executable not found: $ninjaExe"
}
if (-not (Test-Path -LiteralPath $clExe) -and -not (Test-Path -LiteralPath $cmakeCache)) {
    throw "MSVC compiler not found: $clExe"
}
if (-not (Test-Path -LiteralPath $toolchainFile) -and -not (Test-Path -LiteralPath $cmakeCache)) {
    throw "vcpkg toolchain file not found: $toolchainFile"
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ScreenshotOutput) | Out-Null

$vcpkgRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $toolchainFile))
$env:VCPKG_ROOT = $vcpkgRoot

$configureCommand = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 && " +
    "set `"VCPKG_ROOT=$vcpkgRoot`" && " +
    "`"$cmakeExe`" -S `"$repoRoot`" -B `"$BuildDir`" " +
    "-DCMAKE_BUILD_TYPE:STRING=$BuildType"

if (-not (Test-Path -LiteralPath $cmakeCache)) {
    $configureCommand += " -G Ninja " +
        "-DCMAKE_MAKE_PROGRAM:FILEPATH=`"$ninjaExe`" " +
        "-DCMAKE_C_COMPILER:FILEPATH=`"$clExe`" " +
        "-DCMAKE_CXX_COMPILER:FILEPATH=`"$clExe`" " +
        "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=`"$toolchainFile`" " +
        "-DVCPKG_TARGET_TRIPLET=x64-windows-static"
}

$buildCommand = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 && " +
    "`"$cmakeExe`" --build `"$BuildDir`" --target oot3d_native_fast3d_demo"

$exe = Join-Path $BuildDir "oot3d_native_fast3d_demo.exe"
if (Test-Path -LiteralPath $exe) {
    $resolvedExe = (Resolve-Path -LiteralPath $exe).Path
    Get-Process -Name "oot3d_native_fast3d_demo" -ErrorAction SilentlyContinue |
        Where-Object {
            try {
                $_.Path -eq $resolvedExe
            } catch {
                $false
            }
        } |
        Stop-Process -Force
}

cmd.exe /d /c $configureCommand
if ($LASTEXITCODE -ne 0) {
    throw "OOT3D native Fast3D demo CMake configure failed with exit code $LASTEXITCODE"
}

cmd.exe /d /c $buildCommand
if ($LASTEXITCODE -ne 0) {
    throw "OOT3D native Fast3D demo build failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $exe)) {
    $exeMatch = Get-ChildItem -LiteralPath $BuildDir -Recurse -Filter "oot3d_native_fast3d_demo.exe" |
        Select-Object -First 1
    if ($null -eq $exeMatch) {
        throw "OOT3D native Fast3D demo executable was not produced under: $BuildDir"
    }
    $exe = $exeMatch.FullName
}

Push-Location $repoRoot
try {
    $selfTestArgs = @("--manifest", $Manifest, "--resource-root", $ResourceRoot, "--self-test",
        "--output", $Output, "--render-mode", $RenderMode,
        "--material-animation-frame", ([string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}", $MaterialAnimationFrame)))
    if ($EntranceIndex -ge 0) {
        $selfTestArgs += @("--entrance-index", "$EntranceIndex")
    }
    if ($TitleIntro) {
        $selfTestArgs += @("--title-intro", "--title-intro-qdb-index", "0")
    }
    $selfTestArgs += $ExtraArgs
    & $exe @selfTestArgs
    if ($LASTEXITCODE -ne 0) {
        throw "OOT3D native Fast3D demo self-test failed with exit code $LASTEXITCODE"
    }

    if (Test-Path -LiteralPath $Output) {
        $launchJson = Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json
        Assert-Oot3dNativeFast3dLaunchState -Json $launchJson -OutputPath $Output
    }

    if ($Verify) {
        if (-not (Test-Path -LiteralPath $Output)) {
            throw "OOT3D native Fast3D demo output was not written: $Output"
        }
        $json = Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json
        if ($json.status -ne "valid" -or $json.format -ne "oot3d_native_fast3d_window_demo_v1") {
            throw "OOT3D native Fast3D demo output is not valid: $Output"
        }
        $persistentFast3dBackendValid = $json.persistent_fast3d_backend -eq $true -or
            ($json.self_test -eq $true -and $json.persistent_fast3d_backend -eq $false)
        if ($json.runtime_n64_asset_substitution_used -ne $false -or
            $json.shipwright_replacement_path_used -ne $false -or
            $json.uses_glb_runtime_meshes -ne $false -or
            $json.visual_mesh_collision_source_used -ne $false -or
            $json.uses_standalone_gl_renderer -ne $false -or
            $json.uses_fast3d_window -ne $true -or
            -not $persistentFast3dBackendValid) {
            throw "OOT3D native Fast3D demo reported an invalid rendering path: $Output"
        }
        if ($TitleIntro) {
            $titleRuntime = $json.title_intro_runtime
            if ($null -eq $titleRuntime -or
                $titleRuntime.initial_scene_camera_applied -ne $true -or
                [int]$titleRuntime.initial_scene_cutscene_source_index -ne 94 -or
                [int]$titleRuntime.initial_scene_cutscene_row.setup_index -ne 1 -or
                [int]$titleRuntime.initial_scene_camera_row.camera_blob_segment_source_index -ne 452 -or
                $titleRuntime.qdb_camera_suppressed_by_initial_scene_camera -ne $true -or
                [int]$json.scene.player_start.setup_index -ne 0 -or
                [int]$json.scene.active_scene_setup_index -ne 1 -or
                $json.scene.active_scene_setup_source -ne "native_title_intro_initial_scene_cutscene_row_setup_index" -or
                [int]$json.scene.native_pica_lighting.active_setup_index -ne 1 -or
                [int]$json.scene.native_pica_lighting.active_setup_light_settings_record_count -ne 4 -or
                [int]$json.native_oot3d_pica_lighting_active_setup_record_count -ne 4 -or
                [int]$json.engine_render_scene.environment_background.active_setup_index -ne 1 -or
                [int]$json.engine_render_scene.environment_background.skybox_id -ne 8) {
                throw "OOT3D title intro did not bind the native spot99 setup 1 initial camera: $Output"
            }
            $titleEnvironmentBackground = $json.engine_render_scene.environment_background
            [array]$titleKankyoSelectedCmbNames = @($titleEnvironmentBackground.native_kankyo_selected_cmb_names)
            [array]$titleKankyoExtraCmbNames = @($titleEnvironmentBackground.native_kankyo_extra_cmb_names)
            if ($titleEnvironmentBackground.source_kind -ne "oot3d_scene_skybox_native_kankyo_archive" -or
                $titleEnvironmentBackground.native_kankyo_draw_route_decoded -ne $true -or
                $titleEnvironmentBackground.used_for_render -ne $true -or
                $titleEnvironmentBackground.native_kankyo_archive_available -ne $true -or
                $titleEnvironmentBackground.native_kankyo_rom_path -ne "rom:/kankyo/BlueSky.zar" -or
                [int]$titleEnvironmentBackground.native_kankyo_schedule_mode -ne 0 -or
                [int]$titleEnvironmentBackground.native_kankyo_schedule_entry_index -ne 8 -or
                [int]$titleEnvironmentBackground.native_kankyo_current_profile_index -ne 3 -or
                [int]$titleEnvironmentBackground.native_kankyo_next_profile_index -ne 3 -or
                [int]$titleEnvironmentBackground.native_kankyo_record_param44 -ne 16 -or
                [int]$titleEnvironmentBackground.native_kankyo_record_param48 -ne 2 -or
                [int]$titleEnvironmentBackground.native_kankyo_record_param4c -ne 32 -or
                [int]$json.engine_render_scene.environment_model_count -ne 3 -or
                $titleKankyoSelectedCmbNames.Count -ne 2 -or
                -not ($titleKankyoSelectedCmbNames -contains "model/fine_tenkyu_3.cmb") -or
                -not ($titleKankyoSelectedCmbNames -contains "model/fine_kumo_a3.cmb") -or
                -not ($titleKankyoExtraCmbNames -contains "model/fine_sun.cmb")) {
                throw "OOT3D title intro did not bind the native BlueSky kankyo skybox route: $Output"
            }
            $titleLinkActor = $titleRuntime.link_actor
            $titleEponaActor = $titleRuntime.epona_actor
            if ($titleRuntime.native_title_actor_scales_decoded -ne $true -or
                $null -eq $titleLinkActor -or
                $null -eq $titleEponaActor -or
                $titleLinkActor.scale_status -ne "decoded_from_oot3d_actor_setscale_vfp_s0_literal" -or
                $titleEponaActor.scale_status -ne "decoded_from_oot3d_actor_setscale_vfp_s0_literal" -or
                [math]::Abs([double]$titleLinkActor.native_actor_scale - 0.005) -gt 0.000001 -or
                [math]::Abs([double]$titleEponaActor.native_actor_scale - 0.01) -gt 0.000001 -or
                [math]::Abs([double]$titleLinkActor.native_actor_scale_base - 0.005) -gt 0.000001 -or
                [math]::Abs([double]$titleEponaActor.native_actor_scale_base - 0.005) -gt 0.000001 -or
                [math]::Abs([double]$titleEponaActor.scale - ([double]$titleLinkActor.scale * 2.0)) -gt 0.0001 -or
                $null -eq $titleLinkActor.scale_row -or
                $null -eq $titleEponaActor.scale_row -or
                [int]$titleLinkActor.scale_row.actor_set_scale_callsite -ne 0x0018BF90 -or
                [int]$titleEponaActor.scale_row.actor_set_scale_callsite -ne 0x001D927C -or
                [int]$titleLinkActor.scale_row.scale_literal_address -ne 0x0018C1B0 -or
                [int]$titleEponaActor.scale_row.scale_literal_address -ne 0x001D93EC -or
                [int]$titleLinkActor.scale_row.actor_set_scale_function -ne 0x0037572C -or
                [int]$titleEponaActor.scale_row.actor_set_scale_function -ne 0x0037572C) {
                throw "OOT3D title intro actor scales were not decoded from native Actor_SetScale callsite/literal evidence: $Output"
            }
            $titleAnimationDecodeStatus = "native_actor_zar_model_and_init_animation_table_decoded_title_cue_clip_binding_pending_oot3d_cutscene_consumer"
            if ($titleRuntime.native_title_actor_animation_rows_decoded -ne $true -or
                $null -eq $titleLinkActor.animation_row -or
                $null -eq $titleEponaActor.animation_row -or
                $titleLinkActor.animation_row.decode_status -ne $titleAnimationDecodeStatus -or
                $titleEponaActor.animation_row.decode_status -ne $titleAnimationDecodeStatus -or
                $titleLinkActor.animation_row.actor_role -ne "opening_link_boy" -or
                $titleLinkActor.animation_row.archive_path -ne "actor/zelda_link_opening.zar" -or
                $titleEponaActor.animation_row.archive_path -ne "actor/zelda_horse.zar" -or
                $titleLinkActor.animation_row.cmb_name -ne "boy/model/link_opening.cmb" -or
                $titleEponaActor.animation_row.cmb_name -ne "Model/epona.cmb" -or
                $titleLinkActor.animation_row.init_csab_name -ne "boy/anim/uma_wait_2.csab" -or
                $titleEponaActor.animation_row.init_csab_name -ne "Anim/hl_anim_wait2.csab" -or
                $titleLinkActor.animation_row.title_visual_csab_name -ne "boy/anim/uma_anim_fastrun.csab" -or
                $titleEponaActor.animation_row.title_visual_csab_name -ne "Anim/hl_anim_fastrun2_30.csab" -or
                $titleLinkActor.csab_name -ne $titleLinkActor.animation_row.title_visual_csab_name -or
                $titleEponaActor.csab_name -ne $titleEponaActor.animation_row.title_visual_csab_name -or
                [int]$titleLinkActor.animation_row.init_animation_table_runtime_address -ne 0x0052711C -or
                [int]$titleEponaActor.animation_row.animation_table_outer_runtime_address -ne 0x005265E8 -or
                [int]$titleEponaActor.animation_row.init_animation_table_runtime_address -ne 0x00526EB0 -or
                [int]$titleLinkActor.animation_row.init_csab_type_index -ne 2 -or
                [int]$titleEponaActor.animation_row.init_csab_type_index -ne 0 -or
                [int]$titleLinkActor.animation_row.title_visual_csab_type_index -ne 19 -or
                [int]$titleEponaActor.animation_row.title_visual_csab_type_index -ne 9 -or
                [int]$titleLinkActor.animation_row.zar_get_cmb_by_index_callsite -ne 0x0018C010 -or
                [int]$titleEponaActor.animation_row.zar_get_cmb_by_index_callsite -ne 0x001D94E8 -or
                [int]$titleLinkActor.animation_row.animation_play_once_callsite -ne 0x0018C078 -or
                [int]$titleEponaActor.animation_row.animation_play_once_callsite -ne 0x001D9568 -or
                [int]$titleEponaActor.animation_row.title_cue_semantic_reference_function -ne 0x003A7E14) {
                throw "OOT3D title intro actor model/animation binding was not loaded from generated native actor animation rows: $Output"
            }
            $titleEponaMotionStatus = "decoded_from_oot3d_enhorse_update_ingo_horse_anim_speed_state_table"
            $titleEponaMotion = $titleEponaActor.motion_animation_row
            if ($titleRuntime.native_title_epona_motion_animation_route_decoded -ne $true -or
                $null -eq $titleEponaMotion -or
                $titleEponaMotion.decode_status -ne $titleEponaMotionStatus -or
                $titleEponaMotion.archive_path -ne "actor/zelda_horse.zar" -or
                [int]$titleEponaMotion.source_function -ne 0x0033D88C -or
                [int]$titleEponaMotion.speed_field_offset -ne 0x006C -or
                [int]$titleEponaMotion.animation_index_field_offset -ne 0x0E74 -or
                [int]$titleEponaMotion.horse_type_field_offset -ne 0x01B0 -or
                [int]$titleEponaMotion.skel_anime_field_offset -ne 0x01C4 -or
                [int]$titleEponaMotion.table_pointer_literal_address -ne 0x0033DA84 -or
                [int]$titleEponaMotion.animation_table_outer_runtime_address -ne 0x005265E8 -or
                [int]$titleEponaMotion.animation_table_runtime_address -ne 0x00526EB0 -or
                [math]::Abs([double]$titleEponaMotion.zero_speed - 0.0) -gt 0.000001 -or
                [math]::Abs([double]$titleEponaMotion.walk_threshold - 3.0) -gt 0.000001 -or
                [math]::Abs([double]$titleEponaMotion.fast_threshold - 6.0) -gt 0.000001 -or
                [math]::Abs([double]$titleEponaMotion.fast_play_speed_scale - 0.5) -gt 0.000001 -or
                [int]$titleEponaMotion.idle_animation_index -ne 0 -or
                [int]$titleEponaMotion.walk_animation_index -ne 4 -or
                [int]$titleEponaMotion.trot_animation_index -ne 5 -or
                [int]$titleEponaMotion.fast_animation_index -ne 7 -or
                [int]$titleEponaMotion.fast_csab_type_index -ne 9 -or
                $titleEponaMotion.fast_csab_name -ne "Anim/hl_anim_fastrun2_30.csab" -or
                $titleEponaActor.csab_name -ne $titleEponaMotion.fast_csab_name) {
                throw "OOT3D title intro Epona motion animation route was not decoded from EnHorse_UpdateIngoHorseAnim native speed/state table: $Output"
            }
            [array]$titleEponaMotionClips = @($titleEponaActor.motion_clips)
            $titleEponaIdleClip = $titleEponaMotionClips | Where-Object { $_.role -eq "idle" } | Select-Object -First 1
            $titleEponaWalkClip = $titleEponaMotionClips | Where-Object { $_.role -eq "walk" } | Select-Object -First 1
            $titleEponaTrotClip = $titleEponaMotionClips | Where-Object { $_.role -eq "trot" } | Select-Object -First 1
            $titleEponaFastClip = $titleEponaMotionClips | Where-Object { $_.role -eq "fast" } | Select-Object -First 1
            $titleEponaGallopCarrotClip = $titleEponaMotionClips | Where-Object { $_.role -eq "gallop_carrot" } | Select-Object -First 1
            if ($titleEponaActor.motion_clip_status -ne "loaded_native_enhorse_speed_state_and_mounted_gallop_csab_set" -or
                $titleEponaMotionClips.Count -ne 5 -or
                $null -eq $titleEponaIdleClip -or
                $null -eq $titleEponaWalkClip -or
                $null -eq $titleEponaTrotClip -or
                $null -eq $titleEponaFastClip -or
                $null -eq $titleEponaGallopCarrotClip -or
                $titleEponaIdleClip.loaded -ne $true -or
                $titleEponaWalkClip.loaded -ne $true -or
                $titleEponaTrotClip.loaded -ne $true -or
                $titleEponaFastClip.loaded -ne $true -or
                $titleEponaGallopCarrotClip.loaded -ne $true -or
                $titleEponaIdleClip.csab_name -ne "Anim/hl_anim_wait2.csab" -or
                $titleEponaWalkClip.csab_name -ne "Anim/hl_anim_walk2_30.csab" -or
                $titleEponaTrotClip.csab_name -ne "Anim/hl_anim_slowrun2_30.csab" -or
                $titleEponaFastClip.csab_name -ne "Anim/hl_anim_fastrun2_30.csab" -or
                $titleEponaGallopCarrotClip.csab_name -ne "Anim/hl_anim_carrotrun.csab" -or
                [math]::Abs([double]$titleEponaIdleClip.play_speed_scale - 0.3) -gt 0.000001 -or
                [math]::Abs([double]$titleEponaWalkClip.play_speed_scale - 1.5) -gt 0.000001 -or
                [math]::Abs([double]$titleEponaTrotClip.play_speed_scale - 1.0) -gt 0.000001 -or
                [math]::Abs([double]$titleEponaFastClip.play_speed_scale - 0.5) -gt 0.000001 -or
                [math]::Abs([double]$titleEponaGallopCarrotClip.play_speed_scale - 3.5) -gt 0.000001) {
                throw "OOT3D title intro Epona native speed-state/mounted-gallop CSAB set was not loaded from EnHorse native route rows: $Output"
            }
            [array]$titleHorseStateRoutes = @($titleRuntime.horse_state_route_rows)
            $titleStartMovingRoute = $titleHorseStateRoutes | Where-Object { $_.route_role -eq "start_moving_animation_native_table" } | Select-Object -First 1
            $titleSetFollowRoute = $titleHorseStateRoutes | Where-Object { $_.route_role -eq "set_follow_animation_distance_hysteresis" } | Select-Object -First 1
            $titleUpdateSpeedRoute = $titleHorseStateRoutes | Where-Object { $_.route_role -eq "update_speed_native_control" } | Select-Object -First 1
            $titleGallopRoute = $titleHorseStateRoutes | Where-Object { $_.route_role -eq "mounted_gallop_state" } | Select-Object -First 1
            if ($titleRuntime.native_title_horse_state_route_decoded -ne $true -or
                $titleHorseStateRoutes.Count -lt 7 -or
                $null -eq $titleStartMovingRoute -or
                $null -eq $titleSetFollowRoute -or
                $null -eq $titleUpdateSpeedRoute -or
                $null -eq $titleGallopRoute -or
                [int]$titleStartMovingRoute.source_function -ne 0x0031C588 -or
                [int]$titleStartMovingRoute.action_value -ne 3 -or
                [int]$titleStartMovingRoute.action_field_offset -ne 0x01A4 -or
                [int]$titleStartMovingRoute.animation_index_field_offset -ne 0x0E74 -or
                [int]$titleStartMovingRoute.table_pointer_literal_address -ne 0x0031C690 -or
                [int]$titleStartMovingRoute.animation_table_outer_runtime_address -ne 0x005265E8 -or
                [int]$titleStartMovingRoute.animation_table_runtime_address -ne 0x00526EB0 -or
                [string]$titleStartMovingRoute.animation_indices -ne "valid 4/5/7/9, default 4" -or
                [string]$titleStartMovingRoute.resolved_csabs -notmatch "9->6 Anim/hl_anim_carrotrun\.csab" -or
                [int]$titleSetFollowRoute.source_function -ne 0x003352C8 -or
                [string]$titleSetFollowRoute.primary_literal_values -notmatch "near_distance=300" -or
                [string]$titleSetFollowRoute.primary_literal_values -notmatch "far_distance=400" -or
                [int]$titleUpdateSpeedRoute.source_function -ne 0x003182CC -or
                [long]$titleUpdateSpeedRoute.animation_table_runtime_address -ne 4294967295 -or
                [int]$titleGallopRoute.source_function -ne 0x003A7E14 -or
                [int]$titleGallopRoute.action_value -ne 10 -or
                [int]$titleGallopRoute.table_pointer_literal_address -ne 0x003A8184 -or
                [int]$titleGallopRoute.animation_table_outer_runtime_address -ne 0x005265E8 -or
                [int]$titleGallopRoute.animation_table_runtime_address -ne 0x00526EB0 -or
                [math]::Abs([double]$titleGallopRoute.forced_speed - 8.0) -gt 0.000001 -or
                [int]$titleGallopRoute.update_arg -ne 0x010B -or
                [int]$titleGallopRoute.gallop_control_block_runtime_address -ne 0x005265BC -or
                [math]::Abs([double]$titleGallopRoute.gallop_play_speed_scale - 0.450000018) -gt 0.000001 -or
                [math]::Abs([double]$titleGallopRoute.gallop_play_speed_switch - 4.4000001) -gt 0.000001 -or
                [math]::Abs([double]$titleGallopRoute.gallop_play_speed_min - 3.5) -gt 0.000001 -or
                [math]::Abs([double]$titleGallopRoute.gallop_play_speed_max - 5.0) -gt 0.000001 -or
                [math]::Abs([double]$titleGallopRoute.gallop_downshift_speed - 6.0) -gt 0.000001 -or
                [int]$titleGallopRoute.gallop_fast_animation_index -ne 7 -or
                [int]$titleGallopRoute.gallop_fast_csab_type_index -ne 9 -or
                $titleGallopRoute.gallop_fast_csab_name -ne "Anim/hl_anim_fastrun2_30.csab" -or
                [int]$titleGallopRoute.gallop_carrot_animation_index -ne 9 -or
                [int]$titleGallopRoute.gallop_carrot_csab_type_index -ne 6 -or
                $titleGallopRoute.gallop_carrot_csab_name -ne "Anim/hl_anim_carrotrun.csab" -or
                [string]$titleGallopRoute.resolved_csabs -notmatch "7->9 Anim/hl_anim_fastrun2_30\.csab" -or
                [string]$titleGallopRoute.resolved_csabs -notmatch "9->6 Anim/hl_anim_carrotrun\.csab" -or
                [string]$titleGallopRoute.resolved_csabs -notmatch "10->8 Anim/hl_anim_land1002\.csab") {
                throw "OOT3D title intro horse state route was not decoded from native code.bin/Ghidra evidence: $Output"
            }
            [array]$titleLinkBoyPlayerActions = @($titleRuntime.link_boy_player_action_rows)
            $titleLinkBoyFirstAction = $titleLinkBoyPlayerActions | Where-Object { [int]$_.player_action_index -eq 0 } | Select-Object -First 1
            $titleLinkBoyMovingAction = $titleLinkBoyPlayerActions | Where-Object { [int]$_.cue_id -eq 37 -and [int]$_.duration_frames -gt 80 } | Select-Object -First 1
            $titleLinkBoyCueIds = @($titleLinkBoyPlayerActions | ForEach-Object { [int]$_.cue_id } | Sort-Object -Unique)
            if ($titleRuntime.native_title_link_boy_player_action_decoded -ne $true -or
                $titleLinkBoyPlayerActions.Count -ne 14 -or
                $null -eq $titleLinkBoyFirstAction -or
                $null -eq $titleLinkBoyMovingAction -or
                -not ($titleLinkBoyCueIds -contains 36) -or
                -not ($titleLinkBoyCueIds -contains 37) -or
                -not ($titleLinkBoyCueIds -contains 38) -or
                [int]$titleLinkBoyFirstAction.cue_command_id -ne 0x0000000A -or
                [string]$titleLinkBoyFirstAction.actor_role -ne "opening_link_boy" -or
                [string]$titleLinkBoyFirstAction.archive_path -ne "actor/zelda_link_opening.zar" -or
                [string]$titleLinkBoyFirstAction.cmb_name -ne "boy/model/link_opening.cmb" -or
                [string]$titleLinkBoyFirstAction.title_visual_csab_name -ne "boy/anim/uma_anim_fastrun.csab" -or
                [int]$titleLinkBoyFirstAction.qdb_index -ne 0 -or
                [int]$titleLinkBoyFirstAction.start_x -ne -2961 -or
                [int]$titleLinkBoyFirstAction.start_y -ne 510 -or
                [int]$titleLinkBoyFirstAction.start_z -ne 7700 -or
                [int]$titleLinkBoyMovingAction.cue_id -ne 37 -or
                [string]$titleLinkBoyMovingAction.qdb_embedded_name -notmatch "spot00_demo_epona_" -or
                [string]$titleLinkBoyMovingAction.n64_reference -notmatch "csCtx->linkAction") {
                throw "OOT3D title intro Link boy player-action timeline was not decoded from native spot00_demo_epona QDB CS_CMD_SET_PLAYER_ACTION rows: $Output"
            }
            [array]$titleLinkBoyActorSymbolRoutes = @($titleRuntime.link_boy_actor_symbol_route_rows)
            $titleLinkBoySymbolSlot0 = $titleLinkBoyActorSymbolRoutes | Where-Object { [int]$_.action_slot -eq 0 } | Select-Object -First 1
            $titleLinkBoySymbolSlot1 = $titleLinkBoyActorSymbolRoutes | Where-Object { [int]$_.action_slot -eq 1 } | Select-Object -First 1
            $titleLinkBoySymbolSlot3 = $titleLinkBoyActorSymbolRoutes | Where-Object { [int]$_.action_slot -eq 3 } | Select-Object -First 1
            $titleLinkBoySymbolSlot4 = $titleLinkBoyActorSymbolRoutes | Where-Object { [int]$_.action_slot -eq 4 } | Select-Object -First 1
            $titleLinkBoySymbolSlot5 = $titleLinkBoyActorSymbolRoutes | Where-Object { [int]$_.action_slot -eq 5 } | Select-Object -First 1
            if ($titleRuntime.native_title_link_boy_actor_symbol_route_decoded -ne $true -or
                $titleLinkBoyActorSymbolRoutes.Count -ne 6 -or
                $null -eq $titleLinkBoySymbolSlot0 -or
                $null -eq $titleLinkBoySymbolSlot1 -or
                $null -eq $titleLinkBoySymbolSlot3 -or
                $null -eq $titleLinkBoySymbolSlot4 -or
                $null -eq $titleLinkBoySymbolSlot5 -or
                [int]$titleLinkBoySymbolSlot0.update_function -ne 0x001D8D7C -or
                [int]$titleLinkBoySymbolSlot0.action_field_offset -ne 0x01A4 -or
                [int]$titleLinkBoySymbolSlot0.animation_index_field_offset -ne 0x01A5 -or
                [int]$titleLinkBoySymbolSlot0.action_table_pointer_literal_address -ne 0x001D8FC4 -or
                [int]$titleLinkBoySymbolSlot0.action_table_runtime_address -ne 0x0052718C -or
                [int]$titleLinkBoySymbolSlot0.handler_function -ne 0x0037E568 -or
                [int]$titleLinkBoySymbolSlot3.handler_function -ne 0x00292B50 -or
                [int]$titleLinkBoySymbolSlot3.animation_table_pointer_literal_address -ne 0x0018C1CC -or
                [int]$titleLinkBoySymbolSlot3.animation_table_runtime_address -ne 0x0052711C -or
                [string]$titleLinkBoySymbolSlot3.resolved_csabs -notmatch "0->2 boy/anim/uma_wait_2\.csab" -or
                [string]$titleLinkBoySymbolSlot1.expected_speed_values -notmatch "speed_fast=5" -or
                [string]$titleLinkBoySymbolSlot1.expected_speed_values -notmatch "far_distance=1000" -or
                [string]$titleLinkBoySymbolSlot3.expected_speed_values -notmatch "near_player_turn_distance=250" -or
                [string]$titleLinkBoySymbolSlot4.expected_speed_values -notmatch "timer_limit=300" -or
                [string]$titleLinkBoySymbolSlot5.expected_speed_values -notmatch "cos_turn_threshold=0\.7071" -or
                [int]$titleLinkBoySymbolSlot5.handler_function -ne 0x00390760 -or
                [string]$titleLinkBoySymbolSlot5.n64_reference_action -notmatch "sActionFuncs\[5\]") {
                throw "OOT3D title intro Link-boy actor-symbol route was not decoded from native action table/Ghidra exports and verified N64 structure: $Output"
            }
            $titleLogoRuntime = $titleRuntime.title_logo_runtime
            if ($titleRuntime.title_logo_runtime_decoded -ne $true -or
                $null -eq $titleLogoRuntime -or
                $titleLogoRuntime.decoded -ne $true -or
                $titleLogoRuntime.loaded -ne $true) {
                throw "OOT3D title intro did not decode and load the native EnMag title-logo binding: $Output"
            }
            $titleLogoActorInit = $titleLogoRuntime.actor_init_row
            if ($null -eq $titleLogoActorInit -or
                $titleLogoActorInit.actor_name -ne "ACTOR_EN_MAG" -or
                $titleLogoActorInit.object_name -ne "OBJECT_MAG" -or
                [int]$titleLogoActorInit.actor_init_file_offset -ne 0x0042C328 -or
                [int]$titleLogoActorInit.actor_id -ne 0x0171 -or
                [int]$titleLogoActorInit.actor_category -ne 6 -or
                [int]$titleLogoActorInit.flags -ne 0x30 -or
                [int]$titleLogoActorInit.object_id -ne 0x014A -or
                [int]$titleLogoActorInit.instance_size -ne 0x01E0 -or
                [int]$titleLogoActorInit.init_function -ne 0x0018CBB8 -or
                [int]$titleLogoActorInit.destroy_function -ne 0x0018CF1C -or
                [int]$titleLogoActorInit.update_function -ne 0x001DA9F8 -or
                [int]$titleLogoActorInit.draw_function -ne 0x001DA4F4) {
                throw "OOT3D title intro EnMag ActorInit row does not match decoded code.bin evidence: $Output"
            }
            [array]$titleLogoComponents = @($titleLogoRuntime.components)
            [array]$titleLogoCmbNames = @($titleLogoComponents | ForEach-Object { $_.cmb_name })
            [array]$titleLogoCsabNames = @($titleLogoComponents | Where-Object { $_.csab_name } | ForEach-Object { $_.csab_name })
            if ([int]$titleLogoRuntime.component_count -ne 3 -or
                $titleLogoComponents.Count -ne 3 -or
                $titleLogoRuntime.variant -ne "jpeu" -or
                -not ($titleLogoCmbNames -contains "Model/title_logo_jpeu.cmb") -or
                -not ($titleLogoCmbNames -contains "Model/g_title.cmb") -or
                -not ($titleLogoCmbNames -contains "Model/copy_nintendo.cmb") -or
                -not ($titleLogoCsabNames -contains "Anim/title_logo_jpeu.csab")) {
                throw "OOT3D title intro EnMag logo component rows did not resolve the expected native zelda_mag CMB/CSAB assets: $Output"
            }
            [array]$titleLogoDrawRows = @($titleLogoRuntime.draw_rows)
            $titleLogoMainDraw = $titleLogoDrawRows | Where-Object { $_.component_role -eq "title_logo_main" } | Select-Object -First 1
            $titleTextDraw = $titleLogoDrawRows | Where-Object { $_.component_role -eq "title_text_g_title" } | Select-Object -First 1
            $titleCopyrightDraw = $titleLogoDrawRows | Where-Object { $_.component_role -eq "copyright_copy_nintendo" } | Select-Object -First 1
            if ($titleRuntime.title_logo_draw_route_decoded -ne $true -or
                $titleLogoRuntime.draw_route_decoded -ne $true -or
                [int]$titleLogoRuntime.draw_row_count -ne 3 -or
                $titleLogoDrawRows.Count -ne 3 -or
                $null -eq $titleLogoMainDraw -or
                $null -eq $titleTextDraw -or
                $null -eq $titleCopyrightDraw) {
                throw "OOT3D title intro EnMag draw route rows were not decoded from code.bin: $Output"
            }
            if ([int]$titleTextDraw.submit_order -ne 0 -or
                [int]$titleTextDraw.handle_field_offset -ne 0x01A8 -or
                [int]$titleTextDraw.alpha_field_offset -ne 0x01D0 -or
                [int]$titleTextDraw.effect_alpha_field_offset -ne 0xFFFF -or
                [int]$titleTextDraw.color_pointer_address -ne 0x001DA8C0 -or
                [int]$titleTextDraw.color_runtime_address -ne 0x004D9904 -or
                [int]$titleTextDraw.matrix_copy_function -ne 0x003721E0 -or
                [int]$titleTextDraw.submit_function -ne 0x0033D220 -or
                [math]::Abs([double]$titleTextDraw.alpha_scale - (1.0 / 255.0)) -gt 0.000001 -or
                [math]::Abs([double]$titleTextDraw.matrix_row_major_4x4[2][3] - -33.99) -gt 0.001) {
                throw "OOT3D title intro title-text EnMag draw row does not match native EnMag_Draw evidence: $Output"
            }
            if ([int]$titleLogoMainDraw.submit_order -ne 1 -or
                [int]$titleLogoMainDraw.handle_field_offset -ne 0x01A4 -or
                [int]$titleLogoMainDraw.alpha_field_offset -ne 0x01D4 -or
                [int]$titleLogoMainDraw.effect_alpha_field_offset -ne 0x01DC -or
                [int]$titleLogoMainDraw.color_pointer_address -ne 0x001DA8CC -or
                [int]$titleLogoMainDraw.color_runtime_address -ne 0x004D9914 -or
                [int]$titleLogoMainDraw.light_block_pointer_address -ne 0x001DA8D0 -or
                [int]$titleLogoMainDraw.light_block_runtime_address -ne 0x004D9924 -or
                [int]$titleLogoMainDraw.material_slot_select_function -ne 0x003589CC -or
                [int]$titleLogoMainDraw.material_color_apply_function -ne 0x00358964 -or
                [int]$titleLogoMainDraw.matrix_copy_function -ne 0x003721E0 -or
                [int]$titleLogoMainDraw.submit_function -ne 0x0033D220 -or
                [math]::Abs([double]$titleLogoMainDraw.alpha_scale - (1.0 / 255.0)) -gt 0.000001 -or
                [math]::Abs([double]$titleLogoMainDraw.base_translate_z - -34.0) -gt 0.001 -or
                [math]::Abs([double]$titleLogoMainDraw.matrix_row_major_4x4[2][3] - -34.0) -gt 0.001) {
                throw "OOT3D title intro main-logo EnMag draw row does not match native EnMag_Draw evidence: $Output"
            }
            if ([int]$titleCopyrightDraw.submit_order -ne 2 -or
                [int]$titleCopyrightDraw.handle_field_offset -ne 0x01AC -or
                [int]$titleCopyrightDraw.alpha_field_offset -ne 0x01D8 -or
                [int]$titleCopyrightDraw.effect_alpha_field_offset -ne 0xFFFF -or
                [int]$titleCopyrightDraw.color_pointer_address -ne 0x001DA9F4 -or
                [int]$titleCopyrightDraw.color_runtime_address -ne 0x004D9964 -or
                [int]$titleCopyrightDraw.matrix_copy_function -ne 0x003721E0 -or
                [int]$titleCopyrightDraw.submit_function -ne 0x0033D220 -or
                [math]::Abs([double]$titleCopyrightDraw.copyright_translate_y - -11.0) -gt 0.001 -or
                [math]::Abs([double]$titleCopyrightDraw.matrix_row_major_4x4[1][3] - -11.0) -gt 0.001 -or
                [math]::Abs([double]$titleCopyrightDraw.matrix_row_major_4x4[2][3] - -33.99) -gt 0.001) {
                throw "OOT3D title intro copyright EnMag draw row does not match native EnMag_Draw evidence: $Output"
            }
            [array]$titleLogoDrawContextRows = @($titleLogoRuntime.draw_context_rows)
            $titleLogoDrawContext = $titleLogoDrawContextRows | Where-Object { $_.context_role -eq "enmag_draw_small_queue_submit_manager_context" } | Select-Object -First 1
            if ($titleRuntime.title_logo_draw_context_decoded -ne $true -or
                $titleLogoRuntime.draw_context_decoded -ne $true -or
                [int]$titleLogoRuntime.draw_context_row_count -ne 1 -or
                $titleLogoDrawContextRows.Count -ne 1 -or
                $null -eq $titleLogoDrawContext) {
                throw "OOT3D title intro EnMag draw context route was not decoded from code.bin/Ghidra evidence: $Output"
            }
            if ([int]$titleLogoDrawContext.draw_function -ne 0x001DA4F4 -or
                [int]$titleLogoDrawContext.submit_function -ne 0x0033D220 -or
                [int]$titleLogoDrawContext.renderer_flag_pointer_address -ne 0x001DA8B8 -or
                [int]$titleLogoDrawContext.renderer_flag_runtime_address -ne 0x0055A21C -or
                [int]$titleLogoDrawContext.render_context_pointer_address -ne 0x001DA8BC -or
                [int]$titleLogoDrawContext.render_context_runtime_address -ne 0x005BE738 -or
                [int]$titleLogoDrawContext.alternate_render_context_pointer_address -ne 0x001DA9F0 -or
                [int]$titleLogoDrawContext.alternate_render_context_runtime_address -ne 0x005BE5B8 -or
                [int]$titleLogoDrawContext.global_context_runtime_address -ne 0x005BE5B8 -or
                [int]$titleLogoDrawContext.submit_manager_runtime_address -ne 0x005BE738 -or
                [int]$titleLogoDrawContext.global_context_submit_manager_offset -ne 0x0180 -or
                [int]$titleLogoDrawContext.submit_manager_constructor_function -ne 0x0041706C -or
                [int]$titleLogoDrawContext.submit_manager_vtable_address -ne 0x004EBD78 -or
                [int]$titleLogoDrawContext.submit_manager_storage_size -ne 0x2170 -or
                [int]$titleLogoDrawContext.small_queue_count_offset -ne 0x212C -or
                [int]$titleLogoDrawContext.small_queue_storage_offset -ne 0x2130 -or
                [int]$titleLogoDrawContext.small_queue_capacity -ne 8 -or
                [int]$titleLogoDrawContext.small_queue_record_stride -ne 8 -or
                [int]$titleLogoDrawContext.small_queue_record_state_byte_offset -ne 4 -or
                [int]$titleLogoDrawContext.small_queue_record_state_byte_value -ne 0 -or
                [int]$titleLogoDrawContext.small_queue_record_write_function -ne 0x0031487C -or
                [int]$titleLogoDrawContext.small_queue_drain_function -ne 0x0042250C -or
                [int]$titleLogoDrawContext.pass0_drain_function -ne 0x002FAE10 -or
                [int]$titleLogoDrawContext.pass1_drain_function -ne 0x002FAD2C -or
                [int]$titleLogoDrawContext.lazy_guard_function -ne 0x003679B4 -or
                [int]$titleLogoDrawContext.lazy_init_function -ne 0x0036788C -or
                [int]$titleLogoDrawContext.draw_handle_vtable_submit_slot_offset -ne 0x0008) {
                throw "OOT3D title intro EnMag draw context row does not match native submit-manager small-queue evidence: $Output"
            }
            [array]$titleLogoUpdateRows = @($titleLogoRuntime.update_rows)
            $titleLogoInitUpdate = $titleLogoUpdateRows | Where-Object { $_.phase_role -eq "init_defaults" } | Select-Object -First 1
            $titleLogoEnv3Update = $titleLogoUpdateRows | Where-Object { $_.phase_role -eq "initial_to_fade_in_on_env3" } | Select-Object -First 1
            $titleLogoMainFadeUpdate = $titleLogoUpdateRows | Where-Object { $_.phase_role -eq "fade_in_substate_2_main_logo_alpha" } | Select-Object -First 1
            $titleLogoTextFadeUpdate = $titleLogoUpdateRows | Where-Object { $_.phase_role -eq "fade_in_substate_3_title_text_effect_alpha" } | Select-Object -First 1
            $titleLogoCopyrightFadeUpdate = $titleLogoUpdateRows | Where-Object { $_.phase_role -eq "fade_in_substate_4_copyright_alpha" } | Select-Object -First 1
            $titleLogoEnv4Update = $titleLogoUpdateRows | Where-Object { $_.phase_role -eq "display_to_fade_out_on_env4" } | Select-Object -First 1
            $titleLogoFadeOutUpdate = $titleLogoUpdateRows | Where-Object { $_.phase_role -eq "fade_out_state3" } | Select-Object -First 1
            if ($titleRuntime.title_logo_update_route_decoded -ne $true -or
                $titleLogoRuntime.update_route_decoded -ne $true -or
                [int]$titleLogoRuntime.update_row_count -ne 13 -or
                $titleLogoUpdateRows.Count -ne 13 -or
                $null -eq $titleLogoInitUpdate -or
                $null -eq $titleLogoEnv3Update -or
                $null -eq $titleLogoMainFadeUpdate -or
                $null -eq $titleLogoTextFadeUpdate -or
                $null -eq $titleLogoCopyrightFadeUpdate -or
                $null -eq $titleLogoEnv4Update -or
                $null -eq $titleLogoFadeOutUpdate) {
                throw "OOT3D title intro EnMag update route rows were not decoded from code.bin: $Output"
            }
            if ([int]$titleLogoInitUpdate.state -ne 0 -or
                [int]$titleLogoInitUpdate.timer_field_offset -ne 0x01C6 -or
                [int]$titleLogoInitUpdate.timer_initial_value -ne 0x3C -or
                [int]$titleLogoInitUpdate.literal_address -ne 0x001DAD04 -or
                [math]::Abs([double]$titleLogoInitUpdate.literal_value) -gt 0.001 -or
                [math]::Abs([double]$titleLogoInitUpdate.max_alpha - 255.0) -gt 0.001 -or
                [math]::Abs([double]$titleLogoInitUpdate.main_alpha_step - 3.0) -gt 0.001 -or
                [math]::Abs([double]$titleLogoInitUpdate.title_text_effect_alpha_step - 4.25) -gt 0.001 -or
                [int]$titleLogoInitUpdate.init_function -ne 0x0018CBB8 -or
                [int]$titleLogoInitUpdate.update_function -ne 0x001DA9F8) {
                throw "OOT3D title intro EnMag init/default update row does not match native code.bin evidence: $Output"
            }
            if ([int]$titleLogoEnv3Update.flag_id -ne 3 -or
                [int]$titleLogoEnv3Update.next_state -ne 1 -or
                [int]$titleLogoEnv3Update.timer_initial_value -ne 0x28 -or
                [int]$titleLogoEnv3Update.flags_get_env_function -ne 0x0035A3C4) {
                throw "OOT3D title intro EnMag env-3 fade-in gate row does not match native EnMag_Update evidence: $Output"
            }
            if ([int]$titleLogoMainFadeUpdate.state -ne 1 -or
                [int]$titleLogoMainFadeUpdate.substate -ne 2 -or
                [int]$titleLogoMainFadeUpdate.next_substate -ne 3 -or
                [int]$titleLogoMainFadeUpdate.literal_address -ne 0x001DB030 -or
                $titleLogoMainFadeUpdate.alpha_field_offsets -ne "0x01D4" -or
                [math]::Abs([double]$titleLogoMainFadeUpdate.literal_value - 3.0) -gt 0.001) {
                throw "OOT3D title intro EnMag main-logo alpha fade row does not match native EnMag_Update evidence: $Output"
            }
            if ([int]$titleLogoTextFadeUpdate.state -ne 1 -or
                [int]$titleLogoTextFadeUpdate.substate -ne 3 -or
                [int]$titleLogoTextFadeUpdate.next_substate -ne 4 -or
                [int]$titleLogoTextFadeUpdate.literal_address -ne 0x001DB034 -or
                $titleLogoTextFadeUpdate.alpha_field_offsets -ne "0x01D0;0x01DC" -or
                [int]$titleLogoTextFadeUpdate.set_title_anim_state_function -ne 0x00347FBC -or
                [math]::Abs([double]$titleLogoTextFadeUpdate.literal_value - 4.25) -gt 0.001) {
                throw "OOT3D title intro EnMag title-text/effect alpha fade row does not match native EnMag_Update evidence: $Output"
            }
            if ([int]$titleLogoCopyrightFadeUpdate.state -ne 1 -or
                [int]$titleLogoCopyrightFadeUpdate.substate -ne 4 -or
                [int]$titleLogoCopyrightFadeUpdate.next_state -ne 2 -or
                [int]$titleLogoCopyrightFadeUpdate.timer_initial_value -ne 0x14 -or
                $titleLogoCopyrightFadeUpdate.alpha_field_offsets -ne "0x01D8" -or
                [int]$titleLogoCopyrightFadeUpdate.literal_address -ne 0x001DB038 -or
                [math]::Abs([double]$titleLogoCopyrightFadeUpdate.literal_value - 255.0) -gt 0.001) {
                throw "OOT3D title intro EnMag copyright alpha fade row does not match native EnMag_Update evidence: $Output"
            }
            if ([int]$titleLogoEnv4Update.flag_id -ne 4 -or
                [int]$titleLogoEnv4Update.state -ne 2 -or
                [int]$titleLogoEnv4Update.next_state -ne 3 -or
                [int]$titleLogoEnv4Update.flags_get_env_function -ne 0x0035A3C4 -or
                [int]$titleLogoFadeOutUpdate.state -ne 3 -or
                [int]$titleLogoFadeOutUpdate.next_state -ne 5 -or
                $titleLogoFadeOutUpdate.alpha_field_offsets -ne "0x01D0;0x01D4;0x01D8" -or
                [int]$titleLogoFadeOutUpdate.literal_address -ne 0x001DAD04) {
                throw "OOT3D title intro EnMag fade-out route rows do not match native EnMag_Update evidence: $Output"
            }
            if ([int]$titleLogoInitUpdate.copyright_alpha_step_default -ne 6 -or
                [int]$titleLogoInitUpdate.fade_out_alpha_step_default -ne 10 -or
                [int]$titleLogoInitUpdate.transition_copyright_alpha_step -ne 15 -or
                [int]$titleLogoInitUpdate.transition_fade_out_alpha_step -ne 25 -or
                [int]$titleLogoCopyrightFadeUpdate.copyright_alpha_step_default -ne 6 -or
                [int]$titleLogoFadeOutUpdate.fade_out_alpha_step_default -ne 10) {
                throw "OOT3D title intro EnMag alpha-step defaults were not exposed as native numeric fields: $Output"
            }
            $titleLogoAlphaState = $titleLogoRuntime.alpha_state
            [array]$titleLogoAlphaSamples = @($titleLogoRuntime.alpha_reference_samples)
            $titleLogoAlphaInitialSample = $titleLogoAlphaSamples | Where-Object { [double]$_.input_frame -eq 0.0 } | Select-Object -First 1
            $titleLogoAlphaDisplaySample = $titleLogoAlphaSamples | Where-Object { $_.display_reached -eq $true } | Select-Object -First 1
            if ($titleRuntime.title_logo_alpha_state_simulated -ne $true -or
                $titleRuntime.title_logo_alpha_state_used_for_render -ne $false -or
                $null -eq $titleLogoAlphaState -or
                $titleLogoAlphaState.decoded -ne $true -or
                $titleLogoAlphaState.simulated -ne $true -or
                $titleLogoAlphaState.used_for_render -ne $false -or
                $titleLogoAlphaState.native_rows_used -ne $true -or
                [int]$titleLogoAlphaState.copyright_alpha_step -ne 6 -or
                [int]$titleLogoAlphaState.fade_out_alpha_step -ne 10 -or
                [int]$titleLogoRuntime.alpha_reference_sample_count -ne 2 -or
                $titleLogoAlphaSamples.Count -ne 2 -or
                $null -eq $titleLogoAlphaInitialSample -or
                $titleLogoAlphaInitialSample.display_reached -ne $false -or
                [int]$titleLogoAlphaInitialSample.state -ne 0 -or
                [math]::Abs([double]$titleLogoAlphaInitialSample.main_logo_alpha) -gt 0.001 -or
                $null -eq $titleLogoAlphaDisplaySample -or
                $titleLogoAlphaDisplaySample.display_reached -ne $true -or
                [int]$titleLogoAlphaDisplaySample.state -ne 2 -or
                [int]$titleLogoAlphaDisplaySample.substate -ne 5 -or
                [math]::Abs([double]$titleLogoAlphaDisplaySample.title_text_alpha - 255.0) -gt 0.001 -or
                [math]::Abs([double]$titleLogoAlphaDisplaySample.main_logo_alpha - 255.0) -gt 0.001 -or
                [math]::Abs([double]$titleLogoAlphaDisplaySample.copyright_alpha - 255.0) -gt 0.001 -or
                [math]::Abs([double]$titleLogoAlphaDisplaySample.effect_alpha - 255.0) -gt 0.001) {
                throw "OOT3D title intro EnMag alpha-state diagnostic simulation did not execute decoded native update rows: $Output"
            }
            $openingFrameRuntime = $titleRuntime.opening_frame_runtime
            $openingFrameStep = $openingFrameRuntime.step
            $openingFrameCamera = $openingFrameStep.camera
            $openingFrameActorMotion = $openingFrameStep.actor_motion
            $openingFrameLogoDraw = $openingFrameStep.logo_draw
            $openingFrameCutscene = $openingFrameStep.cutscene_runtime
            $openingFrameCutsceneState = $openingFrameRuntime.state_after_step.cutscene
            [array]$openingFrameLogoDraws = @($openingFrameLogoDraw.draws)
            [array]$openingActorDirectRecordLayoutRows = @($titleRuntime.opening_actor_direct_record_layout_rows)
            [array]$openingActorMotionCueRows = @($titleRuntime.opening_actor_motion_cue_rows)
            [array]$openingActorMotionDirectStateRows = @($openingActorMotionCueRows | Where-Object { [int]$_.action_id -eq 0x24 })
            [array]$openingActorMotionMotionOnlyRows = @($openingActorMotionCueRows | Where-Object {
                [int]$_.action_id -eq 0x40 -or [int]$_.action_id -eq 0x41
            })
            [array]$invalidOpeningActorDirectRecordLayoutRows = @($openingActorDirectRecordLayoutRows | Where-Object {
                [int]$_.direct_record_layout_index -ne 0 -or
                [int]$_.entry_direct_state_id -ne 36 -or
                [int]$_.consumer_direct_state_id -ne 37 -or
                [int]$_.completion_direct_state_id -ne 38 -or
                [int]$_.branch_direct_state_id -ne 39 -or
                [int]$_.state37_handler_function -ne 0x00475654 -or
                [int]$_.record_pointer_getter_function -ne 0x002D0258 -or
                [int]$_.record_pointer_literal_pool_address -ne 0x002D0260 -or
                [int]$_.record_context_address -ne 0x0054AC48 -or
                [int]$_.record_table_address -ne 0x0054AC9D -or
                [int]$_.record_pointer_address -ne 0x0054ACA3 -or
                [int]$_.record_context_offset -ne 0x005B -or
                [int]$_.record_stride -ne 3 -or
                [int]$_.record_apply_function -ne 0x002D038C -or
                [int]$_.record_byte_table_literal_pool_address -ne 0x002D0404 -or
                [int]$_.record_byte_table_address -ne 0x005A2E7C -or
                [int]$_.first_change_latch_address -ne 0x0055A21C -or
                [int]$_.first_change_init_context_address -ne 0x005BE5B8 -or
                [int]$_.record_change_notify_context_address -ne 0x005C1878 -or
                [int]$_.record_apply_side_effect_index_limit -ne 8 -or
                [int]$_.state37_apply_current_callsite -ne 0x00475690 -or
                [int]$_.state37_apply_terminator_callsite -ne 0x004756A8 -or
                [int]$_.state37_player_record_pointer_offset -ne 0x2A80 -or
                [int]$_.state37_player_last_record_value_offset -ne 0x2BA0 -or
                $_.record_pointer_status -ne "decoded_from_oot3d_title_intro_record_pointer_audit" -or
                $_.record_apply_status -ne "decoded_from_oot3d_title_intro_record_apply_audit" -or
                $_.record_c_runtime_status -ne "oot3d_record_c_runtime_audit_ok" -or
                $_.state37_playback_runtime_status -ne "oot3d_state37_playback_runtime_audit_ok"
            })
            [array]$invalidOpeningActorMotionDirectStateRows = @($openingActorMotionDirectStateRows | Where-Object {
                [int]$_.direct_record_layout_index -ne 0 -or
                [int]$_.direct_state_id -ne 36 -or
                [int]$_.direct_state_switch_function -ne 0x00473EF8 -or
                [int]$_.direct_state_switch_wrapper_function -ne 0x00458460 -or
                [int]$_.direct_state_setter_function -ne 0x00340BDC -or
                [int]$_.direct_state_handler_function -ne 0x00475628 -or
                $_.direct_state_outgoing_targets -ne "37" -or
                $_.player_action_consumer_status -ne "decoded_from_oot3d_player_direct_state_switch_and_motion_consumer"
            })
            [array]$invalidOpeningActorMotionMotionOnlyRows = @($openingActorMotionMotionOnlyRows | Where-Object {
                [int]$_.direct_record_layout_index -ne 65535 -or
                [int]$_.direct_state_id -ne 65535 -or
                [int]$_.direct_state_handler_function -ne 0 -or
                $_.player_action_consumer_status -ne "decoded_from_oot3d_player_action_motion_consumer_no_direct_state_case"
            })
            if ($null -eq $openingFrameRuntime -or
                $openingFrameRuntime.decoded -ne $true -or
                $openingFrameRuntime.used_for_render -ne $true -or
                $openingFrameRuntime.camera_used_for_render -ne $true -or
                $openingFrameRuntime.actor_motion_used_for_render -ne $true -or
                $openingFrameRuntime.link_actor_transform_used_for_render -ne $true -or
                $openingFrameRuntime.epona_actor_transform_used_for_render -ne $true -or
                $openingFrameRuntime.logo_draw_used_for_render -ne $true -or
                [int]$openingFrameRuntime.logo_draw_resolved_component_count -ne 3 -or
                [int]$openingFrameRuntime.logo_draw_visible_component_count -ne 0 -or
                [int]$openingFrameRuntime.logo_draw_submitted_visual_count -ne 0 -or
                [int]$openingFrameRuntime.logo_draw_skipped_invisible_component_count -ne 3 -or
                [int]$openingFrameRuntime.logo_draw_material_color_slot -ne 5 -or
                [int]$openingFrameRuntime.logo_draw_material_color_override_batch_count -ne 0 -or
                $openingFrameRuntime.logo_draw_render_status -ne "native_enmag_draw_snapshot_bound_all_components_alpha_zero_no_visuals_submitted" -or
                $openingFrameRuntime.logo_draw_material_color_apply_supported -ne $true -or
                $openingFrameRuntime.logo_draw_material_color_apply_pending -ne $false -or
                $openingFrameRuntime.runtime_sample_source -ne "render_playback_state" -or
                $openingFrameRuntime.render_binding_status -ne "camera_link_epona_and_logo_draw_bound_native_alpha_zero_no_logo_visual_submitted" -or
                $json.camera.native_behavior -ne "title_intro_opening_frame_runtime_camera" -or
                $titleRuntime.native_title_opening_actor_motion_consumer_split_decoded -ne $true -or
                $titleRuntime.native_title_opening_actor_direct_record_layout_decoded -ne $true -or
                $openingActorDirectRecordLayoutRows.Count -ne 1 -or
                $openingActorMotionCueRows.Count -ne 15 -or
                $openingActorMotionDirectStateRows.Count -ne 9 -or
                $openingActorMotionMotionOnlyRows.Count -ne 6 -or
                $invalidOpeningActorDirectRecordLayoutRows.Count -ne 0 -or
                $invalidOpeningActorMotionDirectStateRows.Count -ne 0 -or
                $invalidOpeningActorMotionMotionOnlyRows.Count -ne 0 -or
                $openingFrameRuntime.init_status -ne "ok" -or
                $openingFrameRuntime.step_status -ne "ok" -or
                [int]$openingFrameRuntime.sample_frame -ne 1 -or
                [int]$openingFrameRuntime.native_end_frame -ne 2400 -or
                [int]$openingFrameRuntime.camera_segment_start_frame -ne 0 -or
                [int]$openingFrameRuntime.camera_segment_end_frame -ne 299 -or
                $null -eq $openingFrameStep -or
                [int]$openingFrameStep.frame -ne 1 -or
                [int]$openingFrameStep.orchestration.cutscene_source_index -ne 94 -or
                [int]$openingFrameStep.orchestration.setup_index -ne 1 -or
                [int]$openingFrameStep.orchestration.camera_blob_segment_source_index -ne 452 -or
                $openingFrameStep.camera_segment_active -ne $true -or
                $openingFrameStep.camera_status -ne "ok" -or
                $openingFrameStep.actor_motion_status -ne "ok" -or
                $openingFrameStep.actor_motion_active -ne $true -or
                $openingFrameStep.logo_step_status -ne "no_matching_step" -or
                $openingFrameStep.logo_step_applied -ne $false -or
                $openingFrameStep.logo_draw_status -ne "ok" -or
                [int]$openingFrameLogoDraw.draw_count -ne 3 -or
                $openingFrameLogoDraws.Count -ne 3) {
                throw "OOT3D title intro opening frame runtime did not sample the native camera/actor/logo state for the initial frame: $Output"
            }
            if ($null -eq $openingFrameCutscene -or
                $openingFrameStep.cutscene_qdb_index_bound -ne $true -or
                [int]$openingFrameStep.cutscene_qdb_index -ne 0 -or
                $openingFrameStep.direct_record_layout_active -ne $false -or
                $openingFrameStep.direct_record_layout -ne $null -or
                $openingFrameStep.direct_mode_open_applied -ne $false -or
                $openingFrameStep.direct_mode_open_status -ne "ok" -or
                $openingFrameStep.direct_mode_reset_applied -ne $false -or
                $openingFrameStep.direct_mode_reset_status -ne "ok" -or
                $openingFrameStep.cutscene_runtime_applied -ne $true -or
                $openingFrameStep.cutscene_runtime_status -ne "ok" -or
                $openingFrameCutscene.qdb_index_filtered -ne $true -or
                [int]$openingFrameCutscene.qdb_index -ne 0 -or
                $openingFrameCutscene.source_selector_feed_applied -ne $true -or
                $openingFrameCutscene.source_selector_feed_status -ne "ok" -or
                $openingFrameCutscene.record_c_produced -ne $true -or
                $openingFrameCutscene.record_c_status -ne "ok" -or
                [int]$openingFrameCutscene.active_state37_row_count -ne 0 -or
                $openingFrameCutscene.state37_playback_applied -ne $false -or
                $openingFrameCutscene.state37_status -ne "ok" -or
                $null -eq $openingFrameCutsceneState -or
                [int]$openingFrameCutsceneState.record_byte_table.byte_count -ne 10 -or
                [int]$openingFrameCutsceneState.record_c_context.mode_byte -ne 0 -or
                [int]$openingFrameCutsceneState.source_selector_feed.time_counter -ne 2 -or
                [int]$openingFrameCutsceneState.state37.step_count -ne 0) {
                throw "OOT3D title intro opening frame runtime did not advance the native cutscene record/feed bridge for the initial QDB-filtered frame: $Output"
            }
            if ($null -eq $openingFrameCamera -or
                [int]$openingFrameCamera.camera_blob_source_index -ne 88 -or
                [int]$openingFrameCamera.camera_blob_segment_source_index -ne 452 -or
                [math]::Abs([double]$openingFrameCamera.view.fov) -le 0.001 -or
                $null -eq $openingFrameCamera.view.eye -or
                $null -eq $openingFrameCamera.view.at) {
                throw "OOT3D title intro opening frame runtime did not expose a native CMAD camera view: $Output"
            }
            if ($null -eq $openingFrameActorMotion -or
                [int]$openingFrameActorMotion.action_id -ne 65 -or
                [int]$openingFrameActorMotion.actor_binding_index -ne 0 -or
                [int]$openingFrameActorMotion.paired_mount_actor_binding_index -ne 1 -or
                $openingFrameActorMotion.actor_binding.actor_role -ne "opening_link_boy_mounted_visual" -or
                $openingFrameActorMotion.paired_mount_binding.actor_role -ne "opening_epona_mount_visual" -or
                [int]$openingFrameActorMotion.player_action_source_index -ne 136 -or
                [int]$openingFrameActorMotion.player_action_source_row.player_action_index -ne 0 -or
                $openingFrameActorMotion.player_action_source_row.actor_role -ne "opening_link_boy" -or
                [int]$openingFrameActorMotion.paired_mount_cue_row.actor_cue_index -ne 4 -or
                [int]$openingFrameActorMotion.paired_mount_cue_row.cue_command_id -ne 62 -or
                [int]$openingFrameActorMotion.paired_mount_cue_row.qdb_index -ne 0 -or
                [int]$openingFrameActorMotion.cue.player_action_source_index -ne 136 -or
                [int]$openingFrameActorMotion.cue.native_command_source_index -ne 418 -or
                [int]$openingFrameActorMotion.cue.direct_state_id -ne 65535 -or
                [int]$openingFrameActorMotion.cue.direct_state_handler_function -ne 0 -or
                $openingFrameActorMotion.cue.player_action_consumer_status -ne "decoded_from_oot3d_player_action_motion_consumer_no_direct_state_case") {
                throw "OOT3D title intro opening frame runtime did not expose native Link-boy/Epona player-action motion state: $Output"
            }
            if ($null -eq $titleRuntime.link_cue -or
                $titleRuntime.link_cue.status -ne "active_opening_frame_runtime_link_boy_player_action" -or
                [int]$titleRuntime.link_cue.link_boy_player_action_row.player_action_index -ne 0 -or
                $titleRuntime.link_cue.row -ne $null) {
                throw "OOT3D title intro Link-boy visual transform was not bound through the opening frame runtime player-action source row: $Output"
            }
            if ($null -eq $titleRuntime.epona_cue -or
                $titleRuntime.epona_cue.status -ne "active_opening_frame_runtime_paired_mount_actor_cue" -or
                [int]$titleRuntime.epona_cue.row.actor_cue_index -ne 4 -or
                [int]$titleRuntime.epona_cue.row.cue_command_id -ne 62) {
                throw "OOT3D title intro Epona visual transform was not bound through the opening frame runtime paired mount cue row: $Output"
            }
        }
        if ($json.uses_collision -ne $true -or
            $json.native_collision_floor_probe_supported -ne $true -or
            $json.native_collision_decoded_from_zsi -ne $true -or
            $json.native_collision_xml_fallback_used -ne $false -or
            $json.native_collision_source_kind -ne "oot3d_scene_zsi_native_collision" -or
            $json.scene.uses_collision -ne $true -or
            $json.scene.collision.available -ne $true -or
            $json.scene.collision.decoded_from_native_zsi -ne $true -or
            $json.scene.collision.xml_fallback_used -ne $false -or
            $json.scene.collision.visual_mesh_collision_source_used -ne $false -or
            $json.scene.collision.native_collision_floor_probe_supported -ne $true) {
            throw "OOT3D native Fast3D demo did not report native OOT3D scene collision support: $Output"
        }
        $assetGraph = $json.scene.asset_graph
        $isKokiriFast3dFixture = $assetGraph.scene_zsi -like "*spot04_info.zsi"
        $isLinkHouseFast3dFixture = $assetGraph.scene_zsi -like "*link_info.zsi"
        if ($isKokiriFast3dFixture) {
            if ([int]$json.scene.collision.vertex_count -ne 2315 -or
                [int]$json.scene.collision.polygon_count -ne 3858 -or
                [int]$json.scene.collision.decoded_surface_type_count -ne 47 -or
                [int]$json.scene.collision.decoded_bgcam_count -ne 15 -or
                [int]$json.scene.collision.decoded_bgcam_position_count -ne 12 -or
                [int]$json.scene.collision.water_box_count -ne 1 -or
                [int]$json.scene.collision.header_offset -ne 93468 -or
                [int]$json.scene.collision.effective_vertex_offset -ne 1688 -or
                [int]$json.scene.collision.effective_polygon_offset -ne 15580) {
                throw "OOT3D native Fast3D demo did not load the expected Kokiri Forest collision header: $Output"
            }
            $cameraConfig = $json.native_oot3d_camera_config
            if ($json.native_oot3d_camera_supported -ne $true -or
                $json.native_oot3d_camera_collision_data_supported -ne $true -or
                $json.native_oot3d_camera_collision_line_test_supported -ne $true -or
                $json.native_oot3d_camera_collision_polygon_flags_supported -ne $true -or
                $json.native_oot3d_camera_start_camera_data_supported -ne $false -or
                $json.native_oot3d_camera_normal0_supported -ne $true -or
                $json.native_oot3d_camera_config_source -ne "oot3d_native_camera_table_and_scene_collision_bgcam" -or
                $json.native_oot3d_camera_table_available -ne $true -or
                $json.native_oot3d_camera_table_fallback_used -ne $false -or
                $json.scene.native_camera_table.available -ne $true -or
                $json.scene.native_camera_table.source_kind -ne "oot3d_ghidra_export_qualified_camera_table" -or
                $cameraConfig.source -ne "oot3d_native_camera_table_and_scene_collision_bgcam" -or
                $cameraConfig.native_camera_table_available -ne $true -or
                $cameraConfig.native_camera_table_fallback_used -ne $false -or
                $cameraConfig.native_camera_table_source_kind -ne "oot3d_ghidra_export_qualified_camera_table" -or
                $cameraConfig.supported -ne $true -or
                $cameraConfig.start_camera_data_supported -ne $false -or
                [int]$cameraConfig.start_camera_data_index -ne 255) {
                throw "OOT3D native Fast3D demo did not report the Kokiri native camera configuration: $Output"
            }
            $playerStart = $json.scene.player_start
            $isKokiriGlobal0211Entrance = [int]$playerStart.requested_global_entrance_index -eq 529
            $expectedKokiriSpawnIndex = if ($isKokiriGlobal0211Entrance) { 3 } else { 0 }
            $expectedKokiriEntranceIndex = if ($isKokiriGlobal0211Entrance) { 3 } else { 0 }
            $expectedKokiriPlayerX = if ($isKokiriGlobal0211Entrance) { -31.0 } else { -68.0 }
            $expectedKokiriPlayerY = if ($isKokiriGlobal0211Entrance) { 100.0 } else { -80.0 }
            $expectedKokiriPlayerZ = if ($isKokiriGlobal0211Entrance) { 1073.0 } else { 941.0 }
            $expectedKokiriParams = if ($isKokiriGlobal0211Entrance) { 3583 } else { 4095 }
            if ($playerStart.available -ne $true -or
                [int]$playerStart.setup_index -ne 0 -or
                [int]$playerStart.spawn_index -ne $expectedKokiriSpawnIndex -or
                [int]$playerStart.entrance_index -ne $expectedKokiriEntranceIndex -or
                [int]$playerStart.actor_id -ne 0 -or
                [double]$playerStart.position.x -ne $expectedKokiriPlayerX -or
                [double]$playerStart.position.y -ne $expectedKokiriPlayerY -or
                [double]$playerStart.position.z -ne $expectedKokiriPlayerZ -or
                [int]$playerStart.params -ne $expectedKokiriParams -or
                [int]$playerStart.camera_data_index -ne 255) {
                throw "OOT3D native Fast3D demo did not decode Kokiri Forest ACTOR_PLAYER start data: $Output"
            }
            if ($isKokiriGlobal0211Entrance -and (
                [int]$playerStart.requested_entrance_index -ne 3 -or
                [int]$playerStart.global_entrance_table_runtime_address -ne 5520312 -or
                [int]$playerStart.global_entrance_table_file_offset -ne 4471736 -or
                [int]$playerStart.global_entrance_entry_file_offset -ne 4473852 -or
                [int]$playerStart.global_entrance_scene_id -ne 85 -or
                [int]$playerStart.global_entrance_local_entrance_index -ne 3 -or
                [int]$playerStart.global_entrance_field -ne 16900 -or
                $playerStart.global_entrance_source_kind -ne "oot3d_code_bin_global_entrance_table_00543bb8" -or
                $playerStart.selection_source -ne "manifest_oot3d_code_bin_global_entrance_table")) {
                throw "OOT3D native Fast3D demo did not resolve Kokiri Forest entry 0x0211 through native code.bin: $Output"
            }
            if ($json.native_oot3d_asset_graph_supported -ne $true -or
                $json.native_oot3d_scene_room_command_graph_supported -ne $true -or
                $json.native_oot3d_room_object_actor_lists_supported -ne $true -or
                $json.native_oot3d_asset_semantics_supported -ne $true -or
                $json.native_oot3d_object_archive_resolution_supported -ne $true -or
                $json.native_oot3d_actor_archive_resolution_supported -ne $true -or
                $json.native_oot3d_actor_visual_cmb_loading_supported -ne $true -or
                $json.native_oot3d_actor_visual_render_instances_supported -ne $true -or
                $assetGraph.available -ne $true -or
                $assetGraph.source_kind -ne "oot3d_zsi_scene_room_asset_graph" -or
                $assetGraph.semantic_source_available -ne $true -or
                $assetGraph.actor_archive_root_available -ne $true -or
                [int]$assetGraph.scene_setup_count -ne 13 -or
                [int]$assetGraph.scene_command_count -ne 172 -or
                [int]$assetGraph.room_command_table_offset -ne 16 -or
                [int]$assetGraph.room_command_count -ne 9 -or
                [int]$assetGraph.room_reference_count -ne 39 -or
                [int]$assetGraph.room_object_count -ne 12 -or
                [int]$assetGraph.room_actor_count -ne 80 -or
                [int]$assetGraph.resolved_object_archive_count -ne 5 -or
                [int]$assetGraph.resolved_actor_archive_count -ne 23) {
                throw "OOT3D native Fast3D demo did not expose the Kokiri native scene/room asset graph: $Output"
            }
            $spot04RoomReference = @($assetGraph.room_references) | Where-Object {
                $_.rom_path -eq "rom:/scene/spot04_0_info.zsi" -and $_.available -eq $true
            } | Select-Object -First 1
            if ($null -eq $spot04RoomReference -or [int]$spot04RoomReference.command_offset -ne 32) {
                throw "OOT3D native Fast3D demo did not resolve the Kokiri room dependency from scene command 0x04: $Output"
            }
            $nativeLighting = $json.scene.native_pica_lighting
            if ($json.native_oot3d_pica_lighting_state_supported -ne $true -or
                $json.native_oot3d_pica_lighting_source -ne "oot3d_zsi_light_settings_list_native_pica_state" -or
                $json.native_oot3d_pica_lighting_layout -ne "oot3d_pica_light_settings_record_0x1c" -or
                [int]$json.native_oot3d_pica_lighting_record_count -ne 105 -or
                [int]$json.native_oot3d_pica_lighting_active_setup_record_count -ne 12 -or
                $nativeLighting.available -ne $true -or
                $nativeLighting.decoded_from_native_zsi -ne $true -or
                $nativeLighting.uses_runtime_n64_asset_substitution -ne $false -or
                [int]$nativeLighting.scene_setup_count -ne 13 -or
                [int]$nativeLighting.scene_light_settings_command_count -ne 13 -or
                [int]$nativeLighting.decoded_light_settings_record_count -ne 105 -or
                [int]$nativeLighting.active_setup_light_settings_record_count -ne 12) {
                throw "OOT3D native Fast3D demo did not decode Kokiri native PICA light settings from scene ZSI: $Output"
            }
            $firstLightSettingsList = @($nativeLighting.scene_light_settings_lists | Select-Object -First 1)
            $firstLightSetting = @($nativeLighting.light_settings | Select-Object -First 1)
            if ($firstLightSettingsList.Count -ne 1 -or
                [int]$firstLightSettingsList[0].entry_size -ne 28 -or
                $firstLightSettingsList[0].interpretation -ne "oot3d_pica_light_settings_record_0x1c" -or
                $firstLightSetting.Count -ne 1 -or
                [int]$firstLightSetting[0].entry_size -ne 28 -or
                $firstLightSetting[0].layout -ne "oot3d_pica_light_settings_record_0x1c" -or
                $firstLightSetting[0].native_env_light_settings_available -ne $true -or
                [int]$firstLightSetting[0].native_env_light_settings_prefix_size -ne 15 -or
                [int]$firstLightSetting[0].native_3ds_lighting_tail_float_param0_offset -ne 16 -or
                [int]$firstLightSetting[0].native_3ds_lighting_tail_float_param1_offset -ne 20 -or
                $firstLightSetting[0].float_params_finite -ne $true -or
                [int]$firstLightSetting[0].pica_byte_groups.Count -ne 4) {
                throw "OOT3D native Fast3D demo did not expose Kokiri light settings as native EnvLightSettings-prefix PICA records: $Output"
            }
            $picaSemantics = $json.scene.native_pica_lighting_semantics
            if ($json.native_oot3d_pica_lighting_semantics_available -ne $true -or
                $json.native_oot3d_pica_lighting_semantics_source_kind -ne "oot3d_pica_light_settings_record_layout_semantics" -or
                $json.native_oot3d_pica_lighting_semantics_format -ne "oot3d_pica_lighting_semantics_v1" -or
                $picaSemantics.available -ne $true -or
                $picaSemantics.source_kind -ne "oot3d_pica_light_settings_record_layout_semantics" -or
                $picaSemantics.format -ne "oot3d_pica_lighting_semantics_v1" -or
                $picaSemantics.uses_runtime_n64_asset_substitution -ne $false -or
                [int]$picaSemantics.byte_group_semantic_count -ne 4 -or
                [int]$picaSemantics.float_param_semantic_count -ne 2 -or
                [int]$picaSemantics.engine_mode_count -ne 1 -or
                [int]$picaSemantics.debug_mode_count -ne 1) {
                throw "OOT3D native Fast3D demo did not load the Kokiri native PICA lighting semantics table: $Output"
            }
            $picaLightingMode = $picaSemantics.engine_modes.native_pica_lighting_vertex_color
            if ($null -eq $picaLightingMode -or
                $picaLightingMode.record_selector -ne "active_setup_record_from_player_floor_light_setting_index" -or
                $picaLightingMode.record_selector_source -ne "oot3d_player_floor_surface_type_data2_bits_6_10_environment_change_light_setting" -or
                $picaLightingMode.ambient_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
                [int]$picaLightingMode.ambient_color_offset -ne 10 -or
                $picaLightingMode.diffuse_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
                [int]$picaLightingMode.diffuse_color_offset -ne 16 -or
                $picaLightingMode.light1_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
                [int]$picaLightingMode.light1_color_offset -ne 22 -or
                $picaLightingMode.directional_formula -ne "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * (light0.rgb * max(dot(normal, light0), 0) + light1.rgb * max(dot(normal, light1), 0)) / 255, 0, 255) / 255)" -or
                [int]$picaLightingMode.light0_vector_offset -ne 13 -or
                $picaLightingMode.light0_vector_source -ne "oot3d_runtime_light_settings_signed_vec3_normalized" -or
                [int]$picaLightingMode.light1_vector_offset -ne 19 -or
                $picaLightingMode.light1_vector_source -ne "oot3d_runtime_light_settings_signed_vec3_normalized" -or
                $picaLightingMode.material_lighting_enable_source -ne "oot3d_cmb_material_fragment_lighting_flag" -or
                $picaLightingMode.material_lighting_deferred_source -ne "oot3d_cmb_material_vertex_or_hemisphere_lighting_flags" -or
                $picaLightingMode.material_vertex_hemisphere_model_scope -ne "native_cmb_transform_instance_or_static_baked_models_with_native_normals" -or
                $picaLightingMode.material_vertex_hemisphere_lighting_source -ne "oot3d_cmb_material_vertex_or_hemisphere_lighting_flags" -or
                $picaLightingMode.vertex_hemisphere_lighting_formula -ne "pica_diffuse_accumulator_evaluated_per_vertex_normal_for_native_cmb_transform_instance_or_static_baked_models_with_native_normals" -or
                $picaLightingMode.vertex_hemisphere_lighting_reference -ne "azahar_pica_frame_vs_uniform_f82_f84_diffuse_accumulator_for_native_cmb_skeleton_vertex_output" -or
                $picaLightingMode.vertex_hemisphere_light_color_mode -ne "oot3d_runtime_environment_light_colors_with_actor_vs_compact_payload_vectors" -or
                $picaLightingMode.material_emission_source -ne "oot3d_cmb_material_emission_rgb" -or
                $picaLightingMode.material_ambient_source -ne "oot3d_cmb_material_ambient_rgb" -or
                $picaLightingMode.material_diffuse_source -ne "oot3d_cmb_material_diffuse_rgb") {
                throw "OOT3D native Fast3D demo did not load Kokiri directional native PICA lighting semantics: $Output"
            }
            $environmentBackground = $json.engine_render_scene.environment_background
            $expectedBackgroundUsedForRender =
                $json.engine_render_scene.pica_lighting.resolved_runtime_light_setting.transition_table_branch_applied -eq $true
            [array]$nativeKankyoSelectedCmbNames = @($environmentBackground.native_kankyo_selected_cmb_names)
            [array]$nativeKankyoExtraCmbNames = @($environmentBackground.native_kankyo_extra_cmb_names)
            if ($null -eq $environmentBackground -or
                $environmentBackground.available -ne $true -or
                $environmentBackground.used_for_render -ne $expectedBackgroundUsedForRender -or
                $environmentBackground.native_kankyo_draw_route_decoded -ne $true -or
                $environmentBackground.native_kankyo_archive_available -ne $true -or
                $environmentBackground.source_kind -ne "oot3d_scene_skybox_native_kankyo_archive" -or
                $environmentBackground.native_kankyo_rom_path -ne "rom:/kankyo/Fogy.zar" -or
                [int]$environmentBackground.native_kankyo_schedule_mode -ne 0 -or
                [int]$environmentBackground.native_kankyo_schedule_entry_index -ne 4 -or
                [int]$environmentBackground.native_kankyo_current_profile_index -ne 1 -or
                [int]$environmentBackground.native_kankyo_next_profile_index -ne 1 -or
                [int]$environmentBackground.native_kankyo_record_param44 -ne 4 -or
                [int]$environmentBackground.native_kankyo_record_param48 -ne 2 -or
                [int]$environmentBackground.native_kankyo_record_param4c -ne 9 -or
                [uint32]$environmentBackground.native_kankyo_draw_scale_address -ne 0x0047D1D0 -or
                [math]::Abs([double]$environmentBackground.native_kankyo_draw_scale - 320.0) -gt 0.001 -or
                $nativeKankyoSelectedCmbNames -notcontains "model/fogy_tenkyu1.cmb" -or
                $nativeKankyoSelectedCmbNames -notcontains "model/fogy_kumo_a1.cmb" -or
                $nativeKankyoExtraCmbNames -notcontains "model/fogy_sun.cmb" -or
                [int]$json.engine_render_scene.environment_model_count -ne 3 -or
                [int]$environmentBackground.active_setup_index -ne 0 -or
                [int]$environmentBackground.skybox_id -ne 29 -or
                [int]$environmentBackground.skybox_command_argument -ne 29 -or
                [int]$environmentBackground.special_files_command_argument -ne 2 -or
                [int]$environmentBackground.clear_color.r -ne 244 -or
                [int]$environmentBackground.clear_color.g -ne 239 -or
                [int]$environmentBackground.clear_color.b -ne 130) {
                throw "OOT3D native Fast3D demo did not decode Kokiri skybox 0x1d through the native Fogy.zar kankyo draw route: $Output"
            }
            if (-not (Test-Oot3dNativeKankyoTextureEnvModels -Json $json)) {
                throw "OOT3D native Fast3D demo did not keep Kokiri Fogy.zar kankyo models on the native environment TextureEnv route: $Output"
            }
            if ($json.native_oot3d_pica_lighting_debug_render_supported -ne $true -or
                $json.native_oot3d_pica_lighting_debug_render_mode -ne "native_pica_lighting_debug" -or
                $json.native_oot3d_pica_lighting_debug_record_selector -ne "first_active_setup_record" -or
                [int]$json.native_oot3d_pica_lighting_debug_record_index -lt 0 -or
                [int]$json.native_oot3d_pica_lighting_debug_record_offset -lt 0 -or
                [int]$json.native_oot3d_pica_lighting_debug_ambient_group_index -ne 0 -or
                [int]$json.native_oot3d_pica_lighting_debug_diffuse_group_index -ne 2 -or
                $json.native_oot3d_pica_lighting_debug_ambient_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
                [int]$json.native_oot3d_pica_lighting_debug_ambient_color_offset -ne 10 -or
                $json.native_oot3d_pica_lighting_debug_diffuse_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
                [int]$json.native_oot3d_pica_lighting_debug_diffuse_color_offset -ne 16 -or
                $json.native_oot3d_pica_lighting_debug_force_untextured_batches -ne $true -or
                [int]$json.native_oot3d_pica_lighting_debug_applied_batch_count -le 0 -or
                [int]$json.native_oot3d_pica_lighting_debug_applied_vertex_count -le 0) {
                throw "OOT3D native Fast3D demo did not apply Kokiri native PICA lighting semantics to a debug render scene: $Output"
            }
            $uniqueRoomReferences = @($assetGraph.room_references | Select-Object -ExpandProperty rom_path -Unique)
            $sceneMiscSettings = @($assetGraph.scene_commands | Where-Object { $_.command_name -eq "misc_settings" })
            $nonKokiriCameraMovement = @($sceneMiscSettings | Where-Object { [int]$_.argument -ne 4 })
            $roomBehavior = @($assetGraph.room_commands | Where-Object { $_.command_name -eq "room_behavior" } |
                Select-Object -First 1)
            if ($uniqueRoomReferences.Count -ne 3 -or
                $uniqueRoomReferences -notcontains "rom:/scene/spot04_0_info.zsi" -or
                $uniqueRoomReferences -notcontains "rom:/scene/spot04_1_info.zsi" -or
                $uniqueRoomReferences -notcontains "rom:/scene/spot04_2_info.zsi" -or
                $sceneMiscSettings.Count -ne 13 -or
                $nonKokiriCameraMovement.Count -ne 0 -or
                $null -eq $roomBehavior -or
                [int]$roomBehavior.parameter -ne 0 -or
                [int]$roomBehavior.argument -ne 0) {
                throw "OOT3D native Fast3D demo did not expose Kokiri room/setup camera context from native ZSI commands: $Output"
            }
            if ($assetGraph.room_object_list.available -ne $true -or
                [int]$assetGraph.room_object_list.command_offset -ne 64 -or
                [int]$assetGraph.room_object_list.count -ne 12 -or
                [int]$assetGraph.room_object_list.entry_size -ne 2 -or
                [int]$assetGraph.room_object_list.start_delta -ne 0 -or
                $assetGraph.room_actor_list.available -ne $true -or
                [int]$assetGraph.room_actor_list.command_offset -ne 72 -or
                [int]$assetGraph.room_actor_list.count -ne 80 -or
                [int]$assetGraph.room_actor_list.entry_size -ne 16 -or
                [int]$assetGraph.room_actor_list.start_delta -ne 0) {
                throw "OOT3D native Fast3D demo did not decode Kokiri room object/actor lists from native command payloads: $Output"
            }
            $actorVisuals = $json.scene.native_actor_visuals
            if ($actorVisuals.source_kind -ne "oot3d_zsi_actor_entries_to_native_zar_cmb" -or
                $actorVisuals.uses_runtime_n64_asset_substitution -ne $false -or
                [int]$actorVisuals.model_count -ne 3 -or
                [int]$actorVisuals.instance_count -ne 1 -or
                [int]$actorVisuals.skipped_instance_count -ne 22 -or
                [int]$json.engine_render_scene.native_actor_visual_count -ne 1 -or
                [int]$json.scene.engine_renderer_submission.model_count -ne 6 -or
                [int]$json.scene.engine_renderer_submission.issue_count -ne 0) {
                throw "OOT3D native Fast3D demo did not load Kokiri native actor CMB visuals into the engine render scene: $Output"
            }
            $actorVisualModels = @($actorVisuals.models)
            $gossipStone = $actorVisualModels | Where-Object { $_.actor_name -eq "ACTOR_EN_GS" } | Select-Object -First 1
            $mido = $actorVisualModels | Where-Object { $_.actor_name -eq "ACTOR_EN_MD" } | Select-Object -First 1
            $saria = $actorVisualModels | Where-Object { $_.actor_name -eq "ACTOR_EN_SA" } | Select-Object -First 1
            if ($null -eq $gossipStone -or
                $gossipStone.cmb_name -ne "Model/gossip_stone2_model.cmb" -or
                [int]$gossipStone.rigid_primitive_count -ne 2 -or
                $null -eq $mido -or
                $mido.cmb_name -ne "Model/mido.cmb" -or
                [int]$mido.skinned_primitive_count -ne 5 -or
                $null -eq $saria -or
                $saria.cmb_name -ne "Model/saria.cmb" -or
                [int]$saria.skinned_primitive_count -ne 8) {
                throw "OOT3D native Fast3D demo did not preserve Kokiri actor archive CMB selections and skinning diagnostics: $Output"
            }
            $actorVisualInstances = @($actorVisuals.instances)
            [array]$grassInstances = $actorVisualInstances | Where-Object { $_.actor_name -eq "ACTOR_EN_KUSA" }
            [array]$kanbanInstances = $actorVisualInstances | Where-Object { $_.actor_name -eq "ACTOR_EN_KANBAN" }
            [array]$gossipStoneInstances = $actorVisualInstances | Where-Object { $_.actor_name -eq "ACTOR_EN_GS" }
            [array]$sariaInstances = $actorVisualInstances | Where-Object { $_.actor_name -eq "ACTOR_EN_SA" }
            [array]$invalidGrassTransforms = @($grassInstances) | Where-Object {
                $null -ne $_ -and
                $_.transform_status -ne "actor_entry_position_rotation_source_units_rigid_cmb"
            }
            $grassInstanceCount = ($grassInstances | Measure-Object).Count
            $kanbanInstanceCount = ($kanbanInstances | Measure-Object).Count
            $gossipStoneInstanceCount = ($gossipStoneInstances | Measure-Object).Count
            $sariaInstanceCount = ($sariaInstances | Measure-Object).Count
            $invalidGrassTransformCount = ($invalidGrassTransforms | Measure-Object).Count
            if ($grassInstanceCount -ne 0 -or
                $kanbanInstanceCount -ne 0 -or
                $gossipStoneInstanceCount -ne 1 -or
                $sariaInstanceCount -ne 0 -or
                $invalidGrassTransformCount -ne 0) {
                throw "OOT3D native Fast3D demo did not limit Kokiri render instances to unambiguous native CMB actor entries: $Output"
            }
            $skippedActorVisualInstances = @($actorVisuals.skipped_instances)
            $skippedKanbanInstances = @($skippedActorVisualInstances | Where-Object {
                $_.actor_name -eq "ACTOR_EN_KANBAN" -and
                $_.reason -eq "native_actor_runtime_cmb_selection_not_decoded" -and
                [int]$_.parseable_cmb_entry_count -eq 12
            })
            $skippedGrassInstances = @($skippedActorVisualInstances | Where-Object {
                $_.actor_name -eq "ACTOR_EN_KUSA" -and
                $_.reason -eq "native_actor_runtime_cmb_selection_not_decoded" -and
                [int]$_.parseable_cmb_entry_count -eq 2
            })
            $skippedSkinnedInstances = @($skippedActorVisualInstances | Where-Object {
                $_.reason -eq "native_actor_skeleton_pose_contract_not_decoded"
            })
            if ($skippedKanbanInstances.Count -ne 8 -or
                $skippedGrassInstances.Count -ne 12 -or
                $skippedSkinnedInstances.Count -ne 2) {
                throw "OOT3D native Fast3D demo did not preserve Kokiri skipped actor visual diagnostics: $Output"
            }
            $polygonFlags = $json.scene.collision.polygon_flag_counts
            if ($polygonFlags -eq $null -or
                [int]$polygonFlags.ignore_projectiles -ne 154 -or
                [int]$polygonFlags.ignore_camera -ne 340 -or
                [int]$polygonFlags.conveyor -ne 16) {
                throw "OOT3D native Fast3D demo did not preserve Kokiri native collision polygon flag words: $Output"
            }
            $expectedKokiriCameraBehavior = if ($isKokiriGlobal0211Entrance) { "bgcam_pivot_in_front_fixd4" } else { "normal_follow" }
            $expectedKokiriCameraSet = if ($isKokiriGlobal0211Entrance) { 24 } else { 1 }
            $expectedKokiriCameraSceneDataIndex = if ($isKokiriGlobal0211Entrance) { 4 } else { 12 }
            $expectedKokiriCameraFloorDataIndex = if ($isKokiriGlobal0211Entrance) { -1 } else { 12 }
            $expectedKokiriCameraStartDataIndex = -1
            $expectedKokiriCameraSceneSetting = if ($isKokiriGlobal0211Entrance) { 24 } else { 1 }
            $expectedKokiriCameraSceneApplied = if ($isKokiriGlobal0211Entrance) { $true } else { $false }
            if ($json.camera.native_camera_active -ne $true -or
                $json.camera.native_behavior -ne $expectedKokiriCameraBehavior -or
                [int]$json.camera.native_set -ne $expectedKokiriCameraSet -or
                [int]$json.camera.native_scene_camera_data_index -ne $expectedKokiriCameraSceneDataIndex -or
                [int]$json.camera.native_floor_camera_data_index -ne $expectedKokiriCameraFloorDataIndex -or
                [int]$json.camera.native_start_camera_data_index -ne $expectedKokiriCameraStartDataIndex -or
                [int]$json.camera.native_scene_camera_setting -ne $expectedKokiriCameraSceneSetting -or
                $json.camera.native_scene_camera_applied -ne $expectedKokiriCameraSceneApplied -or
                $json.camera.native_start_scene_camera_applied -ne $false) {
                throw "OOT3D native Fast3D demo did not run Kokiri with the decoded native camera path: $Output"
            }
            $normal0CameraConfig = $json.native_oot3d_camera_config.normal0
            if ([double]$normal0CameraConfig.initial_distance -ne 180.0 -or
                [int]$normal0CameraConfig.initial_pitch_s16 -ne 1820 -or
                [int]$normal0CameraConfig.oreg5_max_pitch_s16 -ne 14500 -or
                [int]$normal0CameraConfig.oreg6_r_update_rate_inv -ne 20 -or
                [int]$normal0CameraConfig.oreg8_speed_ratio_percent -ne 150 -or
                [int]$normal0CameraConfig.oreg25_yaw_rate_lerp_percent -ne 50 -or
                [int]$normal0CameraConfig.oreg26_pitch_rate_lerp_percent -ne 20 -or
                [int]$normal0CameraConfig.oreg41_at_lerp_min_percent -ne 12 -or
                [int]$normal0CameraConfig.oreg42_at_lerp_scale_percent -ne 110 -or
                [int]$normal0CameraConfig.oreg48_idle_yaw_curve_percent -ne 30 -or
                [int]$normal0CameraConfig.oreg49_yaw_accel_scale_percent -ne 70 -or
                [int]$normal0CameraConfig.oreg50_start_swing_hold_ticks -ne 20 -or
                [int]$normal0CameraConfig.oreg51_start_swing_approach_ticks -ne 20 -or
                $null -eq $json.camera.native_input_yaw -or
                $null -eq $json.camera.native_normal1_yaw_curve -or
                $null -eq $json.camera.native_normal1_start_swing_timer -or
                $null -eq $json.camera.native_normal1_distance_target -or
                $null -eq $json.camera.native_normal1_yaw_velocity) {
                throw "OOT3D native Fast3D demo did not expose the native Camera_Normal1 register/state path: $Output"
            }
            $inputBasis = $json.native_oot3d_input_direction_basis_test
            if ($null -eq $inputBasis -or
                [math]::Abs([double]$inputBasis.behind_target.forward.x) -gt 0.001 -or
                [math]::Abs([double]$inputBasis.behind_target.forward.z - -1.0) -gt 0.001 -or
                [math]::Abs([double]$inputBasis.behind_target.right.x - 1.0) -gt 0.001 -or
                [math]::Abs([double]$inputBasis.behind_target.right.z) -gt 0.001 -or
                [math]::Abs([double]$inputBasis.left_of_target.forward.x - 1.0) -gt 0.001 -or
                [math]::Abs([double]$inputBasis.left_of_target.forward.z) -gt 0.001 -or
                [math]::Abs([double]$inputBasis.left_of_target.right.x) -gt 0.001 -or
                [math]::Abs([double]$inputBasis.left_of_target.right.z - 1.0) -gt 0.001) {
                throw "OOT3D native Fast3D demo has an inverted Camera_GetInputDirYaw keyboard movement basis: $Output"
            }
            $yawConvention = $json.native_oot3d_camera_yaw_convention_test
            if ($null -eq $yawConvention -or
                [math]::Abs([double]$yawConvention.link_yaw - ([math]::PI * 0.5)) -gt 0.001 -or
                [math]::Abs([double]$yawConvention.camera_at_eye_yaw - -([math]::PI * 0.5)) -gt 0.001 -or
                [double]$yawConvention.position_offset.x -ge -1.0 -or
                [math]::Abs([double]$yawConvention.position_offset.z) -gt 0.001 -or
                [math]::Abs([double]$yawConvention.forward.x - 1.0) -gt 0.001 -or
                [math]::Abs([double]$yawConvention.forward.z) -gt 0.001) {
                throw "OOT3D native Fast3D demo still treats at->eye camera yaw as viewer yaw: $Output"
            }
            $cameraTest = $json.native_oot3d_camera_test
            $bgcamTests = @($cameraTest.bgcam_application_tests)
            $missingKokiriCameraSettings = @(@(1, 3, 5, 18, 22, 24, 30, 32) | Where-Object {
                $requiredSetting = $_
                $null -eq ($bgcamTests | Where-Object { [int]$_.setting -eq $requiredSetting } | Select-Object -First 1)
            })
            $missingKokiriCameraBehaviors = @(
                "normal_follow",
                "normal_follow_dungeon0_norm1",
                "normal3_jump3",
                "tower_climb_norm2",
                "bgcam_pivot_crawlspace_fixd2",
                "bgcam_pivot_in_front_fixd4",
                "bgcam_crawlspace_subj4",
                "bgcam_start1_unique0"
            ) | Where-Object {
                $requiredBehavior = $_
                $null -eq ($bgcamTests | Where-Object { $_.camera.native_behavior -eq $requiredBehavior } | Select-Object -First 1)
            }
            $missingKokiriCameraBehaviors = @($missingKokiriCameraBehaviors)
            $failedKokiriBgcams = @($bgcamTests | Where-Object { $_.applied -ne $true })
            $expectedKokiriFinalCameraCollisionSupported = if ($isKokiriGlobal0211Entrance) { $false } else { $true }
            if ($cameraTest.collision_camera_data_supported -ne $true -or
                $cameraTest.collision_line_test_supported -ne $true -or
                $cameraTest.collision_polygon_flags_supported -ne $true -or
                $cameraTest.start_camera_data_supported -ne $false -or
                [int]$cameraTest.start_camera_data_index -ne 255 -or
                [int]$cameraTest.active_camera_data_index -ne $expectedKokiriCameraSceneDataIndex -or
                [int]$cameraTest.active_camera_setting -ne $expectedKokiriCameraSceneSetting -or
                $cameraTest.behavior -ne $expectedKokiriCameraBehavior -or
                $cameraTest.final_camera.native_camera_active -ne $true -or
                $cameraTest.final_camera.native_behavior -ne $expectedKokiriCameraBehavior -or
                [int]$cameraTest.final_camera.native_set -ne $expectedKokiriCameraSet -or
                [int]$cameraTest.final_camera.native_scene_camera_data_index -ne $expectedKokiriCameraSceneDataIndex -or
                $cameraTest.final_camera.native_collision_supported -ne $expectedKokiriFinalCameraCollisionSupported -or
                $bgcamTests.Count -ne 14 -or
                $failedKokiriBgcams.Count -ne 0 -or
                $missingKokiriCameraSettings.Count -ne 0 -or
                $missingKokiriCameraBehaviors.Count -ne 0) {
                throw "OOT3D native Fast3D demo did not apply all decoded Kokiri native camera records: $Output"
            }
            $kokiriTowerCamera = $bgcamTests | Where-Object { [int]$_.setting -eq 18 } | Select-Object -First 1
            $kokiriPivotCamera = $bgcamTests | Where-Object { [int]$_.setting -eq 24 } | Select-Object -First 1
            $kokiriCrawlspaceCamera = $bgcamTests | Where-Object { [int]$_.setting -eq 30 } | Select-Object -First 1
            $kokiriNormal3Camera = $bgcamTests | Where-Object { [int]$_.setting -eq 5 } | Select-Object -First 1
            if ([int]$kokiriTowerCamera.camera.native_function -ne 3 -or
                [int]$kokiriPivotCamera.camera.native_function -ne 35 -or
                [int]$kokiriCrawlspaceCamera.camera.native_function -ne 20 -or
                [int]$kokiriNormal3Camera.camera.native_function -ne 24) {
                throw "OOT3D native Fast3D demo did not map Kokiri camera settings to native camera functions: $Output"
            }
            $unique0Exit = $cameraTest.unique0_exit_test
            if ($unique0Exit.available -ne $true -or
                [int]$unique0Exit.index -lt 0 -or
                $unique0Exit.initial_applied -ne $true -or
                $unique0Exit.exit_applied -ne $false -or
                [int]$unique0Exit.camera.native_suppressed_camera_setting -ne 32 -or
                [int]$unique0Exit.camera.native_suppressed_camera_data_index -ne [int]$unique0Exit.index) {
                throw "OOT3D native Fast3D demo did not exit CAM_SET_START1/UNIQ0 after native movement conditions: $Output"
            }
            if ($json.link_instance.native_collision_grounding_supported -ne $true -or
                $json.link_instance.grounded -ne $true -or
                [int]$json.link_instance.floor_polygon_index -lt 0 -or
                [math]::Abs([double]$json.link_instance.actor_position.y - [double]$json.link_instance.ground_y) -gt 0.001 -or
                [double]$json.link_instance.floor_probe_height -ne 50.0) {
                throw "OOT3D native Fast3D demo did not ground Link on Kokiri native collision: $Output"
            }
            $floorDrop = $json.link_floor_drop_continuity_test
            if ([double]$floorDrop.threshold_units -ne 11.0 -or
                $floorDrop.small_drop_link_instance.grounded -ne $true -or
                [math]::Abs([double]$floorDrop.small_drop_link_instance.native_floor_height_diff - -10.0) -gt 0.001 -or
                $floorDrop.large_drop_link_instance.grounded -ne $false -or
                [int]$floorDrop.large_drop_link_instance.floor_polygon_index -ne -1 -or
                [double]$floorDrop.large_drop_link_instance.native_floor_height_diff -ge -11.0) {
                throw "OOT3D native Fast3D demo did not preserve native floor drop continuity semantics: $Output"
            }
            if ($json.link_csab_runtime_playback_supported -ne $true -or
                $json.link_csab_clip_registry_supported -ne $true -or
                $json.link_native_locomotion_controller_supported -ne $true -or
                $json.engine_render_scene.room.triangle_count -le 0 -or
                $json.engine_render_scene.link_child.triangle_count -le 0 -or
                $json.engine_render_scene.room.uploadable_texture_count -le 0 -or
                $json.engine_render_scene.link_child.uploadable_texture_count -le 0 -or
                $json.world_to_clip_matrix_ready -ne $true) {
                throw "OOT3D native Fast3D demo did not keep the native Kokiri Fast3D render path complete: $Output"
            }
        } elseif ($isLinkHouseFast3dFixture) {
        if ([int]$json.scene.collision.vertex_count -ne 135 -or
            [int]$json.scene.collision.polygon_count -ne 151 -or
            $json.scene.collision.source_kind -ne "oot3d_scene_zsi_native_collision" -or
            $json.scene.collision.resource_kind -ne "oot3d_zsi_collision_header" -or
            [int]$json.scene.collision.header_offset -ne 4440 -or
            [int]$json.scene.collision.effective_vertex_offset -ne 492 -or
            [int]$json.scene.collision.effective_polygon_offset -ne 1304) {
            throw "OOT3D native Fast3D demo did not load the expected OOT3D-derived collision header: $Output"
        }
        if ([int]$json.scene.collision.decoded_surface_type_count -ne 7 -or
            [int]$json.scene.collision.decoded_bgcam_count -ne 3 -or
            [int]$json.scene.collision.decoded_bgcam_position_count -ne 2 -or
            [int]$json.scene.collision.effective_surface_type_offset -ne 4324 -or
            [int]$json.scene.collision.effective_bgcam_offset -ne 4380 -or
            [int]$json.scene.collision.camera_position_offset -ne 4404 -or
            [int]$json.scene.collision.camera_pointer_adjustment -ne 16) {
            throw "OOT3D native Fast3D demo did not decode the native OOT3D collision camera tables: $Output"
        }
        $bgCameras = @($json.scene.collision.bg_cameras)
        $bgCameraPositions = @($json.scene.collision.bg_camera_positions)
        if ($bgCameras.Count -ne 3 -or
            [int]$bgCameras[0].setting -ne 25 -or
            [int]$bgCameras[0].count -ne 3 -or
            [int]$bgCameras[0].camera_position_vector_index -ne 0 -or
            [int]$bgCameras[1].setting -ne 26 -or
            [int]$bgCameras[1].count -ne 3 -or
            [int]$bgCameras[1].camera_position_vector_index -ne 3 -or
            [int]$bgCameras[2].setting -ne 0 -or
            [int]$bgCameras[2].count -ne 0) {
            throw "OOT3D native Fast3D demo did not expose the expected CameraData records: $Output"
        }
        if ($bgCameraPositions.Count -ne 2 -or
            [double]$bgCameraPositions[0].position.x -ne -170.0 -or
            [double]$bgCameraPositions[0].position.y -ne 276.0 -or
            [double]$bgCameraPositions[0].position.z -ne 18.0 -or
            [double]$bgCameraPositions[0].rotation.x -ne 10727.0 -or
            [double]$bgCameraPositions[0].rotation.y -ne 17638.0 -or
            [double]$bgCameraPositions[0].other.x -ne 4000.0 -or
            [double]$bgCameraPositions[1].position.x -ne 0.0 -or
            [double]$bgCameraPositions[1].position.y -ne 34.0 -or
            [double]$bgCameraPositions[1].position.z -ne 0.0 -or
            [double]$bgCameraPositions[1].rotation.y -ne 16384.0 -or
            [double]$bgCameraPositions[1].other.x -ne 6000.0) {
            throw "OOT3D native Fast3D demo did not expose the expected CameraPositionData vectors: $Output"
        }
        $playerStart = $json.scene.player_start
        $isLinkHouseKokiriReturnEntrance = [int]$playerStart.requested_entrance_index -eq 1
        $expectedStartCameraSupported = -not $isLinkHouseKokiriReturnEntrance
        $expectedStartCameraDataIndex = if ($isLinkHouseKokiriReturnEntrance) { 255 } else { 0 }
        $cameraConfig = $json.native_oot3d_camera_config
        if ($json.native_oot3d_camera_supported -ne $true -or
            $json.native_oot3d_camera_collision_data_supported -ne $true -or
            $json.native_oot3d_camera_collision_line_test_supported -ne $true -or
            $json.native_oot3d_camera_collision_polygon_flags_supported -ne $true -or
            $json.native_oot3d_camera_start_camera_data_supported -ne $expectedStartCameraSupported -or
            $json.native_oot3d_camera_normal0_supported -ne $true -or
            $json.native_oot3d_camera_config_source -ne "oot3d_native_camera_table_and_scene_collision_bgcam" -or
            $json.native_oot3d_camera_table_available -ne $true -or
            $json.native_oot3d_camera_table_fallback_used -ne $false -or
            $json.scene.native_camera_table.available -ne $true -or
            $json.scene.native_camera_table.source_kind -ne "oot3d_ghidra_export_qualified_camera_table" -or
            $cameraConfig.source -ne "oot3d_native_camera_table_and_scene_collision_bgcam" -or
            $cameraConfig.native_camera_table_available -ne $true -or
            $cameraConfig.native_camera_table_fallback_used -ne $false -or
            $cameraConfig.native_camera_table_source_kind -ne "oot3d_ghidra_export_qualified_camera_table" -or
            $cameraConfig.supported -ne $true -or
            $cameraConfig.collision_camera_data_supported -ne $true -or
            $cameraConfig.collision_line_test_supported -ne $true -or
            $cameraConfig.collision_polygon_flags_supported -ne $true -or
            $cameraConfig.start_camera_data_supported -ne $expectedStartCameraSupported -or
            [int]$cameraConfig.start_camera_data_index -ne $expectedStartCameraDataIndex) {
            throw "OOT3D native Fast3D demo did not report native OOT3D camera support: $Output"
        }
        if ([int]$cameraConfig.normal0.set -ne 1 -or
            [int]$cameraConfig.normal0.mode -ne 0 -or
            [int]$cameraConfig.normal0.function -ne 2 -or
            [double]$cameraConfig.normal0.data_y_offset -ne -20.0 -or
            [double]$cameraConfig.normal0.data_eye_distance_min -ne 200.0 -or
            [double]$cameraConfig.normal0.data_eye_distance_max -ne 300.0 -or
            [double]$cameraConfig.normal0.data_pitch_target_degrees -ne 10.0 -or
            [double]$cameraConfig.normal0.data_yaw_update_rate_target -ne 12.0 -or
            [double]$cameraConfig.normal0.data_max_yaw_update -ne 35.0 -or
            [double]$cameraConfig.normal0.data_fov_degrees -ne 60.0 -or
            [double]$cameraConfig.normal0.data_at_lerp_step_scale -ne 0.6 -or
            [double]$cameraConfig.normal0.initial_distance -ne 180.0 -or
            [int]$cameraConfig.normal0.initial_pitch_s16 -ne 1820 -or
            [int]$cameraConfig.normal0.oreg46_y_offset_norm -ne -10 -or
            [int]$cameraConfig.normal0.oreg50_start_swing_hold_ticks -ne 20 -or
            [int]$cameraConfig.normal0.oreg51_start_swing_approach_ticks -ne 20 -or
            [double]$cameraConfig.normal0.player_height -ne 56.0 -or
            [math]::Abs([double]$cameraConfig.normal0.camera_unit - 0.572) -gt 0.000001 -or
            [math]::Abs([double]$cameraConfig.normal0.focus_y_offset - -11.44) -gt 0.000001 -or
            [math]::Abs([double]$cameraConfig.normal0.initial_eye_distance - 102.96) -gt 0.000001 -or
            [math]::Abs([double]$cameraConfig.normal0.eye_distance_max - 171.6) -gt 0.000001) {
            throw "OOT3D native Fast3D demo did not derive CAM_SET_NORMAL0 parameters from native camera data: $Output"
        }
        if ([double]$cameraConfig.collision_ray_extend -ne 8.0 -or
            [double]$cameraConfig.collision_surface_push -ne 1.0) {
            throw "OOT3D native Fast3D demo did not expose Camera_BGCheckInfo collision parameters: $Output"
        }
        $floorNoneDefaultSettings = @($cameraConfig.floor_none_default_camera_settings)
        $floorNoneDefaultYieldSettings = @($cameraConfig.floor_none_default_start_settings_that_yield)
        if ($cameraConfig.floor_none_default_camera_selector_supported -ne $true -or
            $cameraConfig.floor_none_default_camera_selector -ne "first_active_camera_with_preferred_setting_when_floor_setting_is_none" -or
            $floorNoneDefaultSettings.Count -ne 1 -or
            [int]$floorNoneDefaultSettings[0] -ne 26 -or
            $floorNoneDefaultYieldSettings.Count -ne 1 -or
            [int]$floorNoneDefaultYieldSettings[0] -ne 25) {
            throw "OOT3D native Fast3D demo did not load the native floor-none room camera selector: $Output"
        }
        $expectedSpawnIndex = if ($isLinkHouseKokiriReturnEntrance) { 1 } else { 0 }
        $expectedEntranceIndex = if ($isLinkHouseKokiriReturnEntrance) { 1 } else { 0 }
        $expectedPlayerX = if ($isLinkHouseKokiriReturnEntrance) { -4.0 } else { 1.0 }
        $expectedPlayerZ = if ($isLinkHouseKokiriReturnEntrance) { -114.0 } else { 95.0 }
        $expectedPlayerParams = if ($isLinkHouseKokiriReturnEntrance) { 3839 } else { 3328 }
        $expectedPlayerCameraIndex = if ($isLinkHouseKokiriReturnEntrance) { 255 } else { 0 }
        $expectedSelectionSources = if ($isLinkHouseKokiriReturnEntrance) {
            @("manifest_native_entrypoint", "argument_native_entrance_index")
        } else {
            @("first_valid_native_entrance")
        }
        if ($playerStart.available -ne $true -or
            [int]$playerStart.setup_index -ne 0 -or
            [int]$playerStart.spawn_index -ne $expectedSpawnIndex -or
            [int]$playerStart.entrance_index -ne $expectedEntranceIndex -or
            [int]$playerStart.requested_entrance_index -ne $(if ($isLinkHouseKokiriReturnEntrance) { 1 } else { -1 }) -or
            $expectedSelectionSources -notcontains $playerStart.selection_source -or
            [int]$playerStart.actor_id -ne 0 -or
            [double]$playerStart.position.x -ne $expectedPlayerX -or
            [double]$playerStart.position.y -ne 0.0 -or
            [double]$playerStart.position.z -ne $expectedPlayerZ -or
            [int]$playerStart.params -ne $expectedPlayerParams -or
            [int]$playerStart.camera_data_index -ne $expectedPlayerCameraIndex) {
            throw "OOT3D native Fast3D demo did not decode ACTOR_PLAYER start camera data from the scene ZSI: $Output"
        }
        $assetGraph = $json.scene.asset_graph
        if ($json.native_oot3d_asset_graph_supported -ne $true -or
            $json.native_oot3d_scene_room_command_graph_supported -ne $true -or
            $json.native_oot3d_room_object_actor_lists_supported -ne $true -or
            $json.native_oot3d_asset_semantics_supported -ne $true -or
            $json.native_oot3d_object_archive_resolution_supported -ne $true -or
            $json.native_oot3d_actor_archive_resolution_supported -ne $true -or
            $assetGraph.available -ne $true -or
            $assetGraph.source_kind -ne "oot3d_zsi_scene_room_asset_graph" -or
            $assetGraph.semantic_source_available -ne $true -or
            $assetGraph.actor_archive_root_available -ne $true -or
            [int]$assetGraph.scene_setup_count -ne 4 -or
            [int]$assetGraph.scene_command_count -ne 47 -or
            [int]$assetGraph.room_command_table_offset -ne 16 -or
            [int]$assetGraph.room_command_count -ne 9 -or
            [int]$assetGraph.room_reference_count -ne 4 -or
            [int]$assetGraph.room_object_count -ne 3 -or
            [int]$assetGraph.room_actor_count -ne 4 -or
            [int]$assetGraph.resolved_object_archive_count -ne 1 -or
            [int]$assetGraph.resolved_actor_archive_count -ne 2 -or
            $assetGraph.manifest_room_path_fallback_used -ne $false) {
            throw "OOT3D native Fast3D demo did not expose the native scene/room ZSI asset graph: $Output"
        }
        if ($assetGraph.semantic_source -notlike "*actor_object_semantics.h" -or
            $assetGraph.actor_archive_root -notlike "*actor") {
            throw "OOT3D native Fast3D demo did not derive native semantic and actor archive roots: $Output"
        }
        $roomReferences = @($assetGraph.room_references)
        $linkRoomReference = $roomReferences | Where-Object {
            $_.rom_path -eq "rom:/scene/link_0_info.zsi" -and $_.available -eq $true
        } | Select-Object -First 1
        if ($null -eq $linkRoomReference -or [int]$linkRoomReference.command_offset -ne 32) {
            throw "OOT3D native Fast3D demo did not resolve the room dependency from scene command 0x04: $Output"
        }
        $nativeLighting = $json.scene.native_pica_lighting
        if ($json.native_oot3d_pica_lighting_state_supported -ne $true -or
            $json.native_oot3d_pica_lighting_source -ne "oot3d_zsi_light_settings_list_native_pica_state" -or
            $json.native_oot3d_pica_lighting_layout -ne "oot3d_pica_light_settings_record_0x1c" -or
            [int]$json.native_oot3d_pica_lighting_record_count -ne 24 -or
            [int]$json.native_oot3d_pica_lighting_active_setup_record_count -ne 12 -or
            $nativeLighting.available -ne $true -or
            $nativeLighting.decoded_from_native_zsi -ne $true -or
            $nativeLighting.uses_runtime_n64_asset_substitution -ne $false -or
            [int]$nativeLighting.scene_setup_count -ne 4 -or
            [int]$nativeLighting.scene_light_settings_command_count -ne 4 -or
            [int]$nativeLighting.decoded_light_settings_record_count -ne 24 -or
            [int]$nativeLighting.active_setup_light_settings_record_count -ne 12) {
            throw "OOT3D native Fast3D demo did not decode Link House native PICA light settings from scene ZSI: $Output"
        }
        $firstLightSettingsList = @($nativeLighting.scene_light_settings_lists | Select-Object -First 1)
        $firstLightSetting = @($nativeLighting.light_settings | Select-Object -First 1)
        if ($firstLightSettingsList.Count -ne 1 -or
            [int]$firstLightSettingsList[0].entry_size -ne 28 -or
            $firstLightSettingsList[0].interpretation -ne "oot3d_pica_light_settings_record_0x1c" -or
            $firstLightSetting.Count -ne 1 -or
            [int]$firstLightSetting[0].entry_size -ne 28 -or
            $firstLightSetting[0].layout -ne "oot3d_pica_light_settings_record_0x1c" -or
            $firstLightSetting[0].native_env_light_settings_available -ne $true -or
            [int]$firstLightSetting[0].native_env_light_settings_prefix_size -ne 15 -or
            [int]$firstLightSetting[0].native_3ds_lighting_tail_float_param0_offset -ne 16 -or
            [int]$firstLightSetting[0].native_3ds_lighting_tail_float_param1_offset -ne 20 -or
            $firstLightSetting[0].float_params_finite -ne $true -or
            [int]$firstLightSetting[0].pica_byte_groups.Count -ne 4) {
            throw "OOT3D native Fast3D demo did not expose Link House light settings as native EnvLightSettings-prefix PICA records: $Output"
        }
        $picaSemantics = $json.scene.native_pica_lighting_semantics
        if ($json.native_oot3d_pica_lighting_semantics_available -ne $true -or
            $json.native_oot3d_pica_lighting_semantics_source_kind -ne "oot3d_pica_light_settings_record_layout_semantics" -or
            $json.native_oot3d_pica_lighting_semantics_format -ne "oot3d_pica_lighting_semantics_v1" -or
            $picaSemantics.available -ne $true -or
            $picaSemantics.source_kind -ne "oot3d_pica_light_settings_record_layout_semantics" -or
            $picaSemantics.format -ne "oot3d_pica_lighting_semantics_v1" -or
            $picaSemantics.uses_runtime_n64_asset_substitution -ne $false -or
            [int]$picaSemantics.byte_group_semantic_count -ne 4 -or
            [int]$picaSemantics.float_param_semantic_count -ne 2 -or
            [int]$picaSemantics.engine_mode_count -ne 1 -or
            [int]$picaSemantics.debug_mode_count -ne 1) {
            throw "OOT3D native Fast3D demo did not load the Link House native PICA lighting semantics table: $Output"
        }
        $picaLightingMode = $picaSemantics.engine_modes.native_pica_lighting_vertex_color
        if ($null -eq $picaLightingMode -or
            $picaLightingMode.record_selector -ne "active_setup_record_from_player_floor_light_setting_index" -or
            $picaLightingMode.record_selector_source -ne "oot3d_player_floor_surface_type_data2_bits_6_10_environment_change_light_setting" -or
            $picaLightingMode.ambient_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
            [int]$picaLightingMode.ambient_color_offset -ne 10 -or
            $picaLightingMode.diffuse_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
            [int]$picaLightingMode.diffuse_color_offset -ne 16 -or
            $picaLightingMode.light1_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
            [int]$picaLightingMode.light1_color_offset -ne 22 -or
            $picaLightingMode.directional_formula -ne "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * (light0.rgb * max(dot(normal, light0), 0) + light1.rgb * max(dot(normal, light1), 0)) / 255, 0, 255) / 255)" -or
            [int]$picaLightingMode.light0_vector_offset -ne 13 -or
            $picaLightingMode.light0_vector_source -ne "oot3d_runtime_light_settings_signed_vec3_normalized" -or
            [int]$picaLightingMode.light1_vector_offset -ne 19 -or
            $picaLightingMode.light1_vector_source -ne "oot3d_runtime_light_settings_signed_vec3_normalized" -or
            $picaLightingMode.material_lighting_enable_source -ne "oot3d_cmb_material_fragment_lighting_flag" -or
            $picaLightingMode.material_lighting_deferred_source -ne "oot3d_cmb_material_vertex_or_hemisphere_lighting_flags" -or
            $picaLightingMode.material_vertex_hemisphere_model_scope -ne "native_cmb_transform_instance_or_static_baked_models_with_native_normals" -or
            $picaLightingMode.material_vertex_hemisphere_lighting_source -ne "oot3d_cmb_material_vertex_or_hemisphere_lighting_flags" -or
            $picaLightingMode.vertex_hemisphere_lighting_formula -ne "pica_diffuse_accumulator_evaluated_per_vertex_normal_for_native_cmb_transform_instance_or_static_baked_models_with_native_normals" -or
            $picaLightingMode.vertex_hemisphere_lighting_reference -ne "azahar_pica_frame_vs_uniform_f82_f84_diffuse_accumulator_for_native_cmb_skeleton_vertex_output" -or
            $picaLightingMode.vertex_hemisphere_light_color_mode -ne "oot3d_runtime_environment_light_colors_with_actor_vs_compact_payload_vectors" -or
            $picaLightingMode.material_emission_source -ne "oot3d_cmb_material_emission_rgb" -or
            $picaLightingMode.material_ambient_source -ne "oot3d_cmb_material_ambient_rgb" -or
            $picaLightingMode.material_diffuse_source -ne "oot3d_cmb_material_diffuse_rgb") {
            throw "OOT3D native Fast3D demo did not load Link House directional native PICA lighting semantics: $Output"
        }
        if ($json.native_oot3d_pica_lighting_debug_render_supported -ne $true -or
            $json.native_oot3d_pica_lighting_debug_render_mode -ne "native_pica_lighting_debug" -or
            $json.native_oot3d_pica_lighting_debug_record_selector -ne "first_active_setup_record" -or
            [int]$json.native_oot3d_pica_lighting_debug_record_index -lt 0 -or
            [int]$json.native_oot3d_pica_lighting_debug_record_offset -lt 0 -or
            [int]$json.native_oot3d_pica_lighting_debug_ambient_group_index -ne 0 -or
            [int]$json.native_oot3d_pica_lighting_debug_diffuse_group_index -ne 2 -or
            $json.native_oot3d_pica_lighting_debug_ambient_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
            [int]$json.native_oot3d_pica_lighting_debug_ambient_color_offset -ne 10 -or
            $json.native_oot3d_pica_lighting_debug_diffuse_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
            [int]$json.native_oot3d_pica_lighting_debug_diffuse_color_offset -ne 16 -or
            $json.native_oot3d_pica_lighting_debug_force_untextured_batches -ne $true -or
            [int]$json.native_oot3d_pica_lighting_debug_applied_batch_count -le 0 -or
            [int]$json.native_oot3d_pica_lighting_debug_applied_vertex_count -le 0) {
            throw "OOT3D native Fast3D demo did not apply Link House native PICA lighting semantics to a debug render scene: $Output"
        }
        $roomCommands = @($assetGraph.room_commands)
        $roomObjectCommand = $roomCommands | Where-Object {
            $_.command_name -eq "object_list" -and [int]$_.offset -eq 64
        } | Select-Object -First 1
        $roomActorCommand = $roomCommands | Where-Object {
            $_.command_name -eq "actor_list" -and [int]$_.offset -eq 72
        } | Select-Object -First 1
        if ($null -eq $roomObjectCommand -or $null -eq $roomActorCommand -or
            [int]$roomObjectCommand.parameter -ne 3 -or
            [int]$roomActorCommand.parameter -ne 4) {
            throw "OOT3D native Fast3D demo did not decode room object/actor command records from the room ZSI: $Output"
        }
        if ($assetGraph.room_object_list.available -ne $true -or
            $assetGraph.room_object_list.interpretation -ne "oot3d_room_object_list_command_0x0b" -or
            [int]$assetGraph.room_object_list.command_offset -ne 64 -or
            [int]$assetGraph.room_object_list.count -ne 3 -or
            [int]$assetGraph.room_object_list.entry_size -ne 2 -or
            [int]$assetGraph.room_object_list.start_delta -ne 0) {
            throw "OOT3D native Fast3D demo did not interpret the room object list from the native command payload: $Output"
        }
        if ($assetGraph.room_actor_list.available -ne $true -or
            $assetGraph.room_actor_list.interpretation -ne "oot3d_room_actor_list_command_0x01" -or
            [int]$assetGraph.room_actor_list.command_offset -ne 72 -or
            [int]$assetGraph.room_actor_list.count -ne 4 -or
            [int]$assetGraph.room_actor_list.entry_size -ne 16 -or
            [int]$assetGraph.room_actor_list.start_delta -ne 16) {
            throw "OOT3D native Fast3D demo did not select the native room actor entry run from the room command payload: $Output"
        }
        $roomObjects = @($assetGraph.room_objects)
        $gameplayKeepObject = $roomObjects | Where-Object {
            [int]$_.object_id -eq 1
        } | Select-Object -First 1
        $kokiriHeadObject = $roomObjects | Where-Object {
            [int]$_.object_id -eq 258
        } | Select-Object -First 1
        $invalidObject = $roomObjects | Where-Object {
            [int]$_.object_id -eq 0
        } | Select-Object -First 1
        if ($null -eq $gameplayKeepObject -or
            $gameplayKeepObject.object_name -ne "OBJECT_GAMEPLAY_KEEP" -or
            $gameplayKeepObject.archive_available -ne $true -or
            $gameplayKeepObject.archive_path -notlike "*zelda_keep.zar" -or
            [int]$gameplayKeepObject.archive_file_count -ne 269 -or
            [int]$gameplayKeepObject.archive_cmb_count -ne 102 -or
            [int]$gameplayKeepObject.archive_csab_count -ne 11) {
            throw "OOT3D native Fast3D demo did not resolve OBJECT_GAMEPLAY_KEEP to its native ZAR archive: $Output"
        }
        if ($null -eq $kokiriHeadObject -or
            $kokiriHeadObject.object_name -ne "OBJECT_MASTERKOKIRIHEAD" -or
            $kokiriHeadObject.archive_available -ne $false -or
            $kokiriHeadObject.archive_resolution_status -ne "archive_not_found_by_semantic_name") {
            throw "OOT3D native Fast3D demo did not preserve unresolved native object archive status: $Output"
        }
        if ($null -eq $invalidObject -or
            $invalidObject.object_name -ne "OBJECT_INVALID" -or
            $invalidObject.archive_available -ne $false -or
            $invalidObject.archive_resolution_status -ne "invalid_object_id") {
            throw "OOT3D native Fast3D demo did not preserve invalid native object archive status: $Output"
        }
        $roomActors = @($assetGraph.room_actors)
        $wonderTalkActor = $roomActors | Where-Object {
            [int]$_.actor_id -eq 389
        } | Select-Object -First 1
        $cowActor = $roomActors | Where-Object {
            [int]$_.actor_id -eq 454
        } | Select-Object -First 1
        $tsuboActor = $roomActors | Where-Object {
            [int]$_.actor_id -eq 273
        } | Select-Object -First 1
        if ($null -eq $wonderTalkActor -or
            $wonderTalkActor.actor_name -ne "ACTOR_EN_WONDER_TALK2" -or
            $wonderTalkActor.archive_available -ne $false -or
            $wonderTalkActor.archive_resolution_status -ne "archive_not_found_by_semantic_name") {
            throw "OOT3D native Fast3D demo did not preserve unresolved native actor archive status: $Output"
        }
        if ($null -eq $cowActor -or
            $cowActor.actor_name -ne "ACTOR_EN_COW" -or
            $cowActor.archive_available -ne $true -or
            $cowActor.archive_path -notlike "*zelda_cow.zar" -or
            [int]$cowActor.archive_file_count -ne 6 -or
            [int]$cowActor.archive_cmb_count -ne 2 -or
            [int]$cowActor.archive_csab_count -ne 4) {
            throw "OOT3D native Fast3D demo did not resolve ACTOR_EN_COW to its native ZAR archive: $Output"
        }
        if ($null -eq $tsuboActor -or
            $tsuboActor.actor_name -ne "ACTOR_OBJ_TSUBO" -or
            $tsuboActor.archive_available -ne $true -or
            $tsuboActor.archive_path -notlike "*zelda_tsubo.zar" -or
            [int]$tsuboActor.archive_file_count -ne 2 -or
            [int]$tsuboActor.archive_cmb_count -ne 2 -or
            [int]$tsuboActor.archive_csab_count -ne 0) {
            throw "OOT3D native Fast3D demo did not resolve ACTOR_OBJ_TSUBO to its native ZAR archive: $Output"
        }
        $polygonFlags = $json.scene.collision.polygon_flag_counts
        if ($polygonFlags -eq $null -or
            [int]$polygonFlags.ignore_projectiles -ne 2 -or
            [int]$polygonFlags.conveyor -ne 2) {
            throw "OOT3D native Fast3D demo did not preserve native collision polygon flag words: $Output"
        }
        $expectedCameraBehavior = if ($isLinkHouseKokiriReturnEntrance) { "bgcam_prerend_pivot_uniq7" } else { "bgcam_prerend_fixed_fixd3" }
        $expectedCameraSet = if ($isLinkHouseKokiriReturnEntrance) { 26 } else { 25 }
        $expectedCameraFunction = if ($isLinkHouseKokiriReturnEntrance) { 48 } else { 34 }
        $expectedSceneCameraDataIndex = if ($isLinkHouseKokiriReturnEntrance) { 1 } else { 0 }
        $expectedNativeStartCameraDataIndex = if ($isLinkHouseKokiriReturnEntrance) { -1 } else { 0 }
        $expectedStartSceneApplied = -not $isLinkHouseKokiriReturnEntrance
        $expectedCameraX = if ($isLinkHouseKokiriReturnEntrance) { 0.0 } else { -170.0 }
        $expectedCameraY = if ($isLinkHouseKokiriReturnEntrance) { 34.0 } else { 276.0 }
        $expectedCameraZ = if ($isLinkHouseKokiriReturnEntrance) { 0.0 } else { 18.0 }
        $expectedCameraFov = if ($isLinkHouseKokiriReturnEntrance) { 60.0 } else { 40.0 }
        if ($json.camera.native_camera_active -ne $true -or
            $json.camera.native_behavior -ne $expectedCameraBehavior -or
            [int]$json.camera.native_set -ne $expectedCameraSet -or
            [int]$json.camera.native_mode -ne 0 -or
            [int]$json.camera.native_function -ne $expectedCameraFunction -or
            [int]$json.camera.native_scene_camera_data_index -ne $expectedSceneCameraDataIndex -or
            [int]$json.camera.native_floor_camera_data_index -ne 2 -or
            [int]$json.camera.native_start_camera_data_index -ne $expectedNativeStartCameraDataIndex -or
            [int]$json.camera.native_scene_camera_setting -ne $expectedCameraSet -or
            $json.camera.native_scene_camera_applied -ne $true -or
            $json.camera.native_start_scene_camera_applied -ne $expectedStartSceneApplied -or
            [double]$json.camera.position.x -ne $expectedCameraX -or
            [double]$json.camera.position.y -ne $expectedCameraY -or
            [double]$json.camera.position.z -ne $expectedCameraZ -or
            [double]$json.camera.fov_degrees -ne $expectedCameraFov -or
            $json.camera.native_collision_polygon_flags_supported -ne $true -or
            [double]$json.camera.native_collision_ray_extend -ne 8.0 -or
            [double]$json.camera.native_collision_surface_push -ne 1.0) {
            throw "OOT3D native Fast3D demo did not run with the native OOT3D start background camera: $Output"
        }
        $cameraTest = $json.native_oot3d_camera_test
        if ($cameraTest.collision_camera_data_supported -ne $true -or
            $cameraTest.collision_line_test_supported -ne $true -or
            $cameraTest.collision_polygon_flags_supported -ne $true -or
            $cameraTest.start_camera_data_supported -ne $expectedStartCameraSupported -or
            [int]$cameraTest.start_camera_data_index -ne $expectedStartCameraDataIndex -or
            $cameraTest.behavior -ne $expectedCameraBehavior -or
            [int]$cameraTest.active_camera_data_index -ne $expectedSceneCameraDataIndex -or
            [int]$cameraTest.active_camera_setting -ne $expectedCameraSet -or
            [int]$cameraTest.floor_camera_data_index -ne 2 -or
            [int]$cameraTest.surface_camera_data_index -ne 2 -or
            [int]$cameraTest.surface_camera_setting -ne 0 -or
            $cameraTest.normal_follow_camera.native_behavior -ne "normal_follow" -or
            $cameraTest.normal_follow_camera.native_collision_supported -ne $true -or
            $cameraTest.normal_follow_camera.native_collision_polygon_flags_supported -ne $true -or
            $cameraTest.final_motion.moving -ne $true -or
            $cameraTest.final_camera.native_camera_active -ne $true) {
            throw "OOT3D native Fast3D demo native camera/bgcam self-test failed: $Output"
        }
        if ($isLinkHouseKokiriReturnEntrance) {
            if ($cameraTest.wall_probe_after.native_collision_supported -ne $true -or
                $cameraTest.wall_probe_after.native_collision_polygon_flags_supported -ne $true) {
                throw "OOT3D native Fast3D demo native camera wall collision probe did not preserve ZSI collision support for entrance 1: $Output"
            }
        } elseif ($cameraTest.wall_probe_hit -ne $true -or
            $cameraTest.wall_probe_after.native_collision_active -ne $true -or
            [int]$cameraTest.wall_probe_after.native_collision_polygon_index -lt 0 -or
            $cameraTest.wall_probe_after.native_collision_polygon_flags_supported -ne $true -or
            [double]$cameraTest.wall_probe_after.native_collision_displacement -le 10.0 -or
            [double]$cameraTest.wall_probe_after.position.z -ge
                [double]$cameraTest.wall_probe_before.position.z - 10.0) {
            throw "OOT3D native Fast3D demo native camera wall collision probe did not resolve against ZSI collision: $Output"
        }
        if ($isLinkHouseKokiriReturnEntrance) {
            if ($cameraTest.wall_probe_repeat_after.native_collision_polygon_flags_supported -ne $true) {
                throw "OOT3D native Fast3D demo native camera collision stability probe did not preserve polygon flag support for entrance 1: $Output"
            }
        } elseif ($cameraTest.wall_probe_repeat_hit -ne $true -or
            $cameraTest.wall_probe_repeat_after.native_collision_retained_previous_polygon -ne $true -or
            [int]$cameraTest.wall_probe_repeat_after.native_collision_previous_polygon_index -ne
                [int]$cameraTest.wall_probe_after.native_collision_polygon_index -or
            [int]$cameraTest.wall_probe_repeat_after.native_collision_polygon_index -ne
                [int]$cameraTest.wall_probe_after.native_collision_polygon_index) {
            throw "OOT3D native Fast3D demo native camera collision stability probe did not retain the previous line-test polygon: $Output"
        }
        $bgcamTests = @($cameraTest.bgcam_application_tests)
        if ($bgcamTests.Count -ne 2 -or
            $bgcamTests[0].applied -ne $true -or
            [int]$bgcamTests[0].setting -ne 25 -or
            $bgcamTests[0].camera.native_behavior -ne "bgcam_prerend_fixed_fixd3" -or
            [double]$bgcamTests[0].camera.position.x -ne -170.0 -or
            [double]$bgcamTests[0].camera.position.y -ne 276.0 -or
            [double]$bgcamTests[0].camera.position.z -ne 18.0 -or
            [double]$bgcamTests[0].camera.fov_degrees -ne 40.0 -or
            $bgcamTests[1].applied -ne $true -or
            [int]$bgcamTests[1].setting -ne 26 -or
            $bgcamTests[1].camera.native_behavior -ne "bgcam_prerend_pivot_uniq7" -or
            [double]$bgcamTests[1].camera.position.x -ne 0.0 -or
            [double]$bgcamTests[1].camera.position.y -ne 34.0 -or
            [double]$bgcamTests[1].camera.position.z -ne 0.0 -or
            [double]$bgcamTests[1].camera.fov_degrees -ne 60.0) {
            throw "OOT3D native Fast3D demo did not apply decoded OOT3D bg camera records: $Output"
        }
        if ($json.link_instance.native_collision_grounding_supported -ne $true -or
            $json.link_instance.grounded -ne $true -or
            [int]$json.link_instance.floor_polygon_index -lt 0) {
            throw "OOT3D native Fast3D demo did not ground Link on the native collision floor: $Output"
        }
        if ([math]::Abs([double]$json.link_instance.actor_position.y - [double]$json.link_instance.ground_y) -gt 0.001) {
            throw "OOT3D native Fast3D demo Link actor Y is not aligned to the native collision floor: $Output"
        }
        if ($json.link_horizontal_collision_supported -ne $true -or
            [double]$json.link_collider_radius_units -ne 12.0 -or
            [double]$json.link_collider_height_units -ne 56.0 -or
            [double]$json.link_wall_check_height_units -ne 26.0) {
            throw "OOT3D native Fast3D demo did not report the manifest-derived Link collision capsule: $Output"
        }
        $wallTestLink = $json.link_wall_collision_test.after_link_instance
        if ($wallTestLink.native_horizontal_collision_supported -ne $true -or
            $wallTestLink.horizontal_collision -ne $true -or
            [int]$wallTestLink.wall_polygon_index -lt 0 -or
            [int]$wallTestLink.horizontal_collision_iteration_count -le 0 -or
            $wallTestLink.native_wall_speed_limit_active -ne $true -or
            [double]$wallTestLink.native_wall_speed_limit_units_per_tick -ge 5.5) {
            throw "OOT3D native Fast3D demo did not resolve Link movement against native collision walls: $Output"
        }
        if ([double]$wallTestLink.actor_position.z -gt 139.25 -or
            [math]::Abs([double]$wallTestLink.actor_position.y - [double]$wallTestLink.ground_y) -gt 0.001) {
            throw "OOT3D native Fast3D demo wall collision test let Link pass the room wall or leave the floor: $Output"
        }
        $wallStability = $json.link_wall_stability_test
        if ($wallStability.contact_seen -ne $true -or
            $wallStability.wall_speed_limit_seen -ne $true -or
            [int]$wallStability.sample_count -le 0 -or
            [double]$wallStability.contact_projection_span -gt 0.11 -or
            [double]$wallStability.final_link_instance.native_wall_speed_limit_units_per_tick -gt 0.11 -or
            $wallStability.final_link_instance.grounded -ne $true) {
            throw "OOT3D native Fast3D demo repeated wall contact is unstable or missing the native wall speed cap: $Output"
        }
        if ($json.link_csab_runtime_playback_supported -ne $true -or
            $json.link_csab_clip_registry_supported -ne $true -or
            $json.link_vector_movement_supported -ne $true -or
            $json.link_camera_relative_ijkl_movement_supported -ne $true -or
            $json.link_run_playback_speed_coupled_to_motion -ne $true -or
            $json.link_walk_clip_supported -ne $true -or
            $json.link_walk_end_clip_supported -ne $true -or
            $json.link_wall_limited_walk_animation_supported -ne $true -or
            $json.link_walk_playback_speed_uses_native_regs -ne $true -or
            $json.link_run_playback_speed_uses_native_regs -ne $true -or
            $json.link_native_locomotion_blend_supported -ne $true -or
            $json.link_native_walk_end_supported -ne $true -or
            $json.link_native_locomotion_controller_supported -ne $true -or
            $json.link_start_morph_uses_previous_pose -ne $true -or
            $json.link_animation.csab_runtime_playback -ne $true -or
            $json.link_animation.sampled_pose_valid -ne $true) {
            throw "OOT3D native Fast3D demo did not report native CSAB runtime playback for Link: $Output"
        }
        $locomotionConfig = $json.link_native_locomotion_config
        if ($json.link_native_locomotion_config_source -ne "oot3d_player_boot_data_and_scene_csab_registry" -or
            $locomotionConfig.source -ne "oot3d_player_boot_data_and_scene_csab_registry" -or
            $locomotionConfig.supported -ne $true -or
            $locomotionConfig.boot_profile -ne "PLAYER_BOOTS_KOKIRI_CHILD" -or
            $locomotionConfig.clips.idle.csab -ne "boy/anim/nml_wait_free.csab" -or
            $locomotionConfig.clips.walk.csab -ne "child/anim/nml_walk_free.csab" -or
            $locomotionConfig.clips.run.csab -ne "child/anim/nml_run_free.csab" -or
            $locomotionConfig.clips.walk_end_left.csab -ne "boy/anim/nml_walk_endL_free.csab" -or
            $locomotionConfig.clips.walk_end_right.csab -ne "boy/anim/nml_walk_endR_free.csab") {
            throw "OOT3D native Fast3D demo did not expose the native locomotion config/CSAB registry: $Output"
        }
        if ([double]$locomotionConfig.boot_regs.REG19_LINEAR_ACCEL -ne 200.0 -or
            [double]$locomotionConfig.boot_regs.REG35_WALK_ANIM_BASE -ne 500.0 -or
            [double]$locomotionConfig.boot_regs.REG36_WALK_ANIM_VELOCITY -ne 400.0 -or
            [double]$locomotionConfig.boot_regs.REG37_WALK_RUN_BLEND -ne 800.0 -or
            [double]$locomotionConfig.boot_regs.REG38_RUN_ANIM_VELOCITY -ne 400.0 -or
            [double]$locomotionConfig.boot_regs.REG43_DECELERATE_TO_ZERO -ne 800.0 -or
            [double]$locomotionConfig.boot_regs.REG45_RUN_SPEED_LIMIT -ne 550.0 -or
            [double]$locomotionConfig.boot_regs.REG48_WALK_RUN_THRESHOLD -ne 370.0) {
            throw "OOT3D native Fast3D demo did not derive locomotion config from the expected OOT3D boot regs: $Output"
        }
        $movementVelocity = [double]$json.link_motion.native_linear_velocity_units_per_tick
        $movementThreshold = [double]$json.link_native_walk_run_threshold_units_per_tick
        $expectedMovementClass = if ($json.link_motion.native_wall_limited_walk -eq $true -or
            $movementVelocity -lt $movementThreshold) { "walk" } else { "run" }
        $expectedMovementClip = if ($expectedMovementClass -eq "walk") { "walk" } else { "move" }
        $expectedMovementCsab = if ($expectedMovementClass -eq "walk") {
            $locomotionConfig.clips.walk.csab
        } else {
            $locomotionConfig.clips.run.csab
        }
        $expectedMovementState = if ($json.link_motion.native_wall_limited_walk -eq $true) {
            "wall_limited"
        } else {
            "start_move"
        }
        if ($json.link_animation.clip_id -ne $expectedMovementClip -or
            $json.link_animation.csab -ne $expectedMovementCsab -or
            $json.link_animation.moving -ne $true -or
            $json.link_animation.locomotion_class -ne $expectedMovementClass -or
            $json.link_animation.native_locomotion_controller_active -ne $true -or
            $json.link_animation.native_locomotion_config_source -ne "oot3d_player_boot_data_and_scene_csab_registry" -or
            $json.link_animation.native_locomotion_state -ne $expectedMovementState) {
            throw "OOT3D native Fast3D demo self-test did not select the native movement CSAB clip: $Output"
        }
        if ($json.link_motion.moving -ne $true -or
            [double]$json.link_motion.speed -le 0.0 -or
            [double]$json.link_motion.speed_ratio -le 0.0 -or
            [math]::Abs([double]$json.link_motion.yaw_delta) -le 0.0001 -or
            [math]::Abs([double]$json.link_instance.yaw) -le 0.0001) {
            throw "OOT3D native Fast3D demo self-test did not update Link velocity/yaw from movement vector: $Output"
        }
        if ([double]$json.native_character_animation_frames_per_second -ne 30.0 -or
            [double]$json.native_player_tick_rate -ne 30.0 -or
            [double]$json.link_animation.native_base_frames_per_second -ne 30.0) {
            throw "OOT3D native Fast3D demo did not report the OOT3D CSAB 30 Hz frame-slot playback basis: $Output"
        }
        if ([double]$json.link_native_run_speed_limit_reg -ne 550.0 -or
            [double]$json.link_native_run_speed_units_per_tick -ne 5.5 -or
            [double]$json.link_native_run_speed_units_per_second -ne 165.0 -or
            [double]$json.link_instance.move_speed -ne 165.0) {
            throw "OOT3D native Fast3D demo did not report child Kokiri native run speed: $Output"
        }
        if ([double]$json.link_native_walk_run_threshold_reg48 -ne 370.0 -or
            [double]$json.link_native_walk_run_threshold_units_per_tick -ne 3.7 -or
            [double]$json.link_native_walk_anim_base_reg35 -ne 500.0 -or
            [double]$json.link_native_walk_anim_velocity_reg36 -ne 400.0 -or
            [double]$json.link_native_walk_run_blend_reg37 -ne 800.0 -or
            [double]$json.link_native_run_anim_velocity_reg38 -ne 400.0 -or
            [double]$json.link_native_locomotion_cycle_frame_span -ne 29.0 -or
            [math]::Abs([double]$json.link_native_run_frame_scale_from_locomotion_cycle - (20.0 / 29.0)) -gt 0.000001 -or
            [double]$json.link_native_walk_end_phase_offset -ne 3.0 -or
            [double]$json.link_native_walk_end_left_phase_threshold -ne 14.0 -or
            [double]$json.link_native_walk_end_default_loop_morph_frames -ne 6.0) {
            throw "OOT3D native Fast3D demo did not report native walk/run animation registers: $Output"
        }
        if ([double]$json.link_native_linear_accel_reg -ne 200.0 -or
            [double]$json.link_native_linear_accel_units_per_tick -ne 2.0 -or
            [double]$json.link_native_linear_decel_units_per_tick -ne 1.5) {
            throw "OOT3D native Fast3D demo did not report native linear velocity stepping: $Output"
        }
        if ([double]$json.link_native_decelerate_to_zero_reg -ne 800.0 -or
            [double]$json.link_native_decelerate_to_zero_units_per_tick -ne 8.0 -or
            [double]$json.link_large_yaw_brake_threshold_s16 -ne 24576.0) {
            throw "OOT3D native Fast3D demo did not report native large-yaw braking parameters: $Output"
        }
        if ([double]$json.link_moving_yaw_step_s16_per_tick -ne 2000.0 -or
            [double]$json.link_shape_yaw_step_s16_per_tick -ne 2000.0 -or
            [double]$json.link_instance.moving_yaw_step_s16_per_tick -ne 2000.0 -or
            [double]$json.link_instance.shape_yaw_step_s16_per_tick -ne 2000.0) {
            throw "OOT3D native Fast3D demo did not report native moving yaw stepping: $Output"
        }
        if ($json.link_idle_start_motion.yaw_alignment_mode -ne "idle_speedTarget_nonzero_snap_func_8083C8DC" -or
            [math]::Abs([double]$json.link_idle_start_motion.movement_yaw -
                [double]$json.link_idle_start_motion.target_yaw) -gt 0.0001 -or
            [math]::Abs([double]$json.link_idle_start_motion.shape_yaw -
                [double]$json.link_idle_start_motion.target_yaw) -gt 0.0001) {
            throw "OOT3D native Fast3D demo self-test did not snap yaw on idle movement start: $Output"
        }
        if ($json.link_running_turn_motion.yaw_alignment_mode -ne "run_func_8083DF68_REG27_scaled_step" -or
            [math]::Abs([double]$json.link_running_turn_motion.movement_yaw -
                [double]$json.link_running_turn_motion.target_yaw) -le 0.0001) {
            throw "OOT3D native Fast3D demo self-test still snaps running yaw immediately: $Output"
        }
        if ($json.link_sharp_turn_motion.yaw_alignment_mode -ne "run_large_yaw_brake_func_8083C484" -or
            $json.link_sharp_turn_motion.large_yaw_brake -ne $true -or
            [double]$json.link_sharp_turn_motion.native_speed_target_units_per_tick -ne 0.0 -or
            [double]$json.link_sharp_turn_motion.native_linear_velocity_units_per_tick -ne 0.0 -or
            [math]::Abs([double]$json.link_sharp_turn_motion.movement_yaw -
                [double]$json.link_sharp_turn_motion.target_yaw) -le 0.0001) {
            throw "OOT3D native Fast3D demo self-test did not brake instead of snapping on large yaw change: $Output"
        }
        $movementTemp2 = $movementVelocity - [double]$json.link_native_walk_run_threshold_units_per_tick
        $movementBlend = ([double]$json.link_native_walk_run_blend_reg37 / 1000.0) * $movementTemp2
        if ($expectedMovementClass -eq "run" -and $movementTemp2 -ge 0.0 -and $movementBlend -ge 1.0) {
            $expectedMovementCycleFramesPerSecond =
                [double]$json.native_character_animation_frames_per_second *
                (1.2 + (([double]$json.link_native_run_anim_velocity_reg38 / 1000.0) * $movementTemp2))
            $expectedMovementFramesPerSecond =
                $expectedMovementCycleFramesPerSecond *
                [double]$json.link_native_run_frame_scale_from_locomotion_cycle
            $expectedMovementBlendWeight = 1.0
        } else {
            $expectedMovementCycleFramesPerSecond =
                [double]$json.native_character_animation_frames_per_second *
                (([double]$json.link_native_walk_anim_base_reg35 / 1000.0) +
                    (([double]$json.link_native_walk_anim_velocity_reg36 / 1000.0) * $movementVelocity))
            $expectedMovementFramesPerSecond = $expectedMovementCycleFramesPerSecond
            $expectedMovementBlendWeight = 0.0
        }
        if ([math]::Abs([double]$json.link_animation.frames_per_second - $expectedMovementFramesPerSecond) -gt 0.001) {
            throw "OOT3D native Fast3D demo did not couple run animation rate to movement speed: $Output"
        }
        if ([math]::Abs([double]$json.link_animation.native_cycle_frames_per_second -
                $expectedMovementCycleFramesPerSecond) -gt 0.001 -or
            [double]$json.link_animation.native_walk_run_blend_weight -ne $expectedMovementBlendWeight) {
            throw "OOT3D native Fast3D demo did not advance movement from the native locomotion cycle: $Output"
        }
        if ($expectedMovementClass -eq "run" -and
            [math]::Abs([double]$json.link_animation.run_frame -
                ([double]$json.link_animation.native_cycle_frame *
                    [double]$json.link_native_run_frame_scale_from_locomotion_cycle)) -gt 0.001) {
            throw "OOT3D native Fast3D demo did not advance run from the native locomotion cycle: $Output"
        }
        if ($expectedMovementClass -eq "walk" -and
            [math]::Abs([double]$json.link_animation.walk_frame -
                [double]$json.link_animation.native_cycle_frame) -gt 0.001) {
            throw "OOT3D native Fast3D demo did not advance walk from the native locomotion cycle: $Output"
        }
        $wallLimitedAnimation = $json.link_wall_limited_animation
        if ($wallLimitedAnimation.clip_id -ne "walk" -or
            $wallLimitedAnimation.csab -ne "child/anim/nml_walk_free.csab" -or
            $wallLimitedAnimation.moving -ne $true -or
            $wallLimitedAnimation.locomotion_class -ne "walk" -or
            $wallLimitedAnimation.native_locomotion_state -ne "wall_limited" -or
            $wallLimitedAnimation.native_wall_limited_walk -ne $true -or
            $json.link_wall_stability_test.final_motion.locomotion_animation_class -ne "walk" -or
            $json.link_wall_stability_test.final_motion.native_wall_limited_walk -ne $true) {
            throw "OOT3D native Fast3D demo did not switch wall-limited Link movement to the native walk CSAB clip: $Output"
        }
        $expectedWalkFramesPerSecond =
            [double]$json.native_character_animation_frames_per_second *
            (([double]$json.link_native_walk_anim_base_reg35 / 1000.0) +
                (([double]$json.link_native_walk_anim_velocity_reg36 / 1000.0) *
                    [double]$json.link_wall_stability_test.final_motion.native_linear_velocity_units_per_tick))
        if ([math]::Abs([double]$wallLimitedAnimation.frames_per_second - $expectedWalkFramesPerSecond) -gt 0.001) {
            throw "OOT3D native Fast3D demo did not derive walk animation rate from native REG(35)/REG(36): $Output"
        }
        $walkEndTest = $json.link_walk_end_test
        if ($walkEndTest.trigger_seen -ne $true -or
            $walkEndTest.idle_morph_seen -ne $true -or
            $walkEndTest.trigger_motion.has_input -ne $false -or
            [math]::Abs([double]$walkEndTest.trigger_motion.native_linear_velocity_units_per_tick) -gt 0.001) {
            throw "OOT3D native Fast3D demo did not enter walk_end after input release and native deceleration: $Output"
        }
        $walkEndTrigger = $walkEndTest.trigger_animation
        if (($walkEndTrigger.clip_id -ne "walk_end_left" -and $walkEndTrigger.clip_id -ne "walk_end_right") -or
            ($walkEndTrigger.csab -ne "boy/anim/nml_walk_endL_free.csab" -and
                $walkEndTrigger.csab -ne "boy/anim/nml_walk_endR_free.csab") -or
            $walkEndTrigger.native_walk_end_active -ne $true -or
            $walkEndTrigger.native_locomotion_state -ne "walk_end" -or
            $walkEndTrigger.native_walk_end_entry_morph_active -ne $true -or
            [double]$walkEndTrigger.native_walk_end_selection_phase -lt 0.0 -or
            [double]$walkEndTrigger.native_walk_end_selection_phase -ge 29.0 -or
            [double]$walkEndTrigger.native_walk_end_entry_morph_frames -lt 0.0) {
            throw "OOT3D native Fast3D demo did not select a native walk_end CSAB from unk_868 phase: $Output"
        }
        $walkEndIdleMorph = $walkEndTest.idle_morph_animation
        if ($walkEndIdleMorph.clip_id -ne "idle" -or
            $walkEndIdleMorph.locomotion_class -ne "idle" -or
            $walkEndIdleMorph.native_locomotion_state -ne "return_idle" -or
            $walkEndIdleMorph.native_walk_end_to_idle_morph_active -ne $true -or
            $walkEndIdleMorph.pose_blend_active -ne $true -or
            $walkEndIdleMorph.pose_blend_to_clip_id -ne "idle") {
            throw "OOT3D native Fast3D demo did not morph from native walk_end back to idle: $Output"
        }
        $blendAnimation = $json.link_blend_zone_animation
        $expectedBlendWeight =
            ([double]$json.link_native_walk_run_blend_reg37 / 1000.0) *
            ([double]$json.link_blend_zone_motion.native_linear_velocity_units_per_tick -
                [double]$json.link_native_walk_run_threshold_units_per_tick)
        if ($blendAnimation.clip_id -ne "move" -or
            $blendAnimation.locomotion_class -ne "run" -or
            $blendAnimation.native_locomotion_state -ne "locomotion" -or
            $blendAnimation.native_walk_run_blend_active -ne $true -or
            $blendAnimation.pose_blend_active -ne $true -or
            $blendAnimation.pose_blend_from_clip_id -ne "walk" -or
            $blendAnimation.pose_blend_to_clip_id -ne "move" -or
            [math]::Abs([double]$blendAnimation.native_walk_run_blend_weight - $expectedBlendWeight) -gt 0.001) {
            throw "OOT3D native Fast3D demo did not blend native walk/run locomotion poses: $Output"
        }
        $startMorphAnimation = $json.link_start_morph_animation
        if ($startMorphAnimation.native_start_morph_active -ne $true -or
            $startMorphAnimation.native_locomotion_state -ne "start_move" -or
            $startMorphAnimation.pose_blend_active -ne $true -or
            $startMorphAnimation.pose_blend_from_clip_id -ne "idle" -or
            $startMorphAnimation.pose_blend_to_clip_id -ne "walk" -or
            [math]::Abs([double]$startMorphAnimation.native_start_blend_weight - 0.5) -gt 0.001) {
            throw "OOT3D native Fast3D demo did not morph from idle pose into locomotion: $Output"
        }
        if ([int]$json.link_animation.frame_count -le 1 -or
            [int]$json.link_animation.world_transform_count -le 0) {
            throw "OOT3D native Fast3D demo did not expose a usable Link CSAB animation pose: $Output"
        }
        if ([int]$json.scene.link_child.csab_clip_count -lt 2 -or
            [int]$json.scene.link_child.movement_clip_index -eq [int]$json.scene.link_child.standing_clip_index) {
            throw "OOT3D native Fast3D demo did not expose distinct standing and movement CSAB clips: $Output"
        }
        if ([int]$json.scene.link_child.csab_clip_count -lt 5 -or
            [int]$json.scene.link_child.walk_clip_index -eq [int]$json.scene.link_child.standing_clip_index -or
            [int]$json.scene.link_child.walk_clip_index -eq [int]$json.scene.link_child.movement_clip_index) {
            throw "OOT3D native Fast3D demo did not expose a distinct native walk CSAB clip: $Output"
        }
        $walkClip = $json.scene.link_child.csab_clips | Where-Object { $_.id -eq "walk" } | Select-Object -First 1
        if ($null -eq $walkClip -or
            $walkClip.csab -ne "child/anim/nml_walk_free.csab" -or
            $walkClip.is_walk -ne $true) {
            throw "OOT3D native Fast3D demo did not register child/anim/nml_walk_free.csab as the native walk clip: $Output"
        }
        $walkEndLeftClip = $json.scene.link_child.csab_clips | Where-Object { $_.id -eq "walk_end_left" } | Select-Object -First 1
        $walkEndRightClip = $json.scene.link_child.csab_clips | Where-Object { $_.id -eq "walk_end_right" } | Select-Object -First 1
        if ($null -eq $walkEndLeftClip -or
            $null -eq $walkEndRightClip -or
            $walkEndLeftClip.csab -ne "boy/anim/nml_walk_endL_free.csab" -or
            $walkEndRightClip.csab -ne "boy/anim/nml_walk_endR_free.csab" -or
            $walkEndLeftClip.is_walk_end_left -ne $true -or
            $walkEndRightClip.is_walk_end_right -ne $true) {
            throw "OOT3D native Fast3D demo did not register native walk_end L/R CSAB clips from the OOT3D ZAR: $Output"
        }
        if ($json.link_instance_transform_supported -ne $true -or
            $json.engine_render_scene.link_child.transform_baked_into_vertices -ne $false) {
            throw "OOT3D native Fast3D demo did not keep Link as a transformable engine instance: $Output"
        }
        if ($json.engine_render_scene.room.triangle_count -le 0 -or
            $json.engine_render_scene.link_child.triangle_count -le 0) {
            throw "OOT3D native Fast3D demo did not build drawable room/link render batches: $Output"
        }
        if ($json.engine_render_scene.room.uploadable_texture_count -le 0 -or
            $json.engine_render_scene.link_child.uploadable_texture_count -le 0) {
            throw "OOT3D native Fast3D demo did not expose uploadable native textures: $Output"
        }
        $picaLighting = $json.engine_render_scene.pica_lighting
        $picaShadow = $json.engine_render_scene.pica_shadow
        $expectedSelfShadowLightContributionFormula =
            "pica_shadow_map_multiplies_native_cmb_vertex_hemisphere_material_ambient_light_ambient_plus_material_diffuse_light_diffuse_dot"
        $expectedSelfShadowOcclusionFormula =
            "shadow_rgb_from_native_pica_shadow2d_encoded_depth_compare_pending"
        $expectedShadow2dRequestSource = "oot3d_pica_shadow_texture_projection_registers"
        $expectedShadow2dFormat = "r32ui_encoded_shadow_depth_reference"
        $expectedShadow2dCompareSource = "CompareShadow(DecodeShadow(encoded_shadow_pixel), z)"
        $expectedShadow2dFilter = "2x2_neighbor_compare_results_returned_as_rgba_shadow_vector"
        $expectedShadow2dEncodedDepthDecodeSource =
            "DecodeShadow(pixel) -> depth24 = pixel >> 8, alpha8 = pixel & 0xFF"
        $expectedShadow2dOutOfBoundsResult = "lit_1_0"
        $expectedShadow2dFilterInterpolationSource =
            "bilinear_mix_of_2x2_compare_results"
        $expectedShadow2dVisualPassSourceKind = "oot3d_pica_shadow2d_visual_pass_contract"
        $expectedShadow2dMaterialTextureProjectionInputSource =
            "oot3d_cmb_material_texture_coord0_plus_pica_texcoord0_w"
        $expectedShadow2dTexCoord0WInputSource =
            "oot3d_cmb_vshader_shbin_output_texcoord0_w"
        $expectedShadow2dProjectionRegisterValueSource =
            "oot3d_codebin_default_shadow2d_inactive_or_validation_pending"
        $expectedShadow2dVisualPassApplication =
            "sample_shadow2d_encoded_depth_in_fragment_primary_rgb_shadow_term"
        $expectedShadow2dVisualPassBlockedReason =
            "shadow2d_default_or_inactive_for_current_fixture"
        $expectedSelfShadowLightVectorSources = @(
            "active_env_light_settings_primary_nonzero_directional_light_vector",
            "active_0045dd50_runtime_environment_primary_nonzero_directional_light_vector"
        )
        $roomFragmentLightingBatches =
            [int]$json.engine_render_scene.room.native_material_fragment_lighting_enabled_batch_count
        $linkFragmentLightingBatches =
            [int]$json.engine_render_scene.link_child.native_material_fragment_lighting_enabled_batch_count
        $fragmentLightingBatches = $roomFragmentLightingBatches + $linkFragmentLightingBatches
        $roomPicaLightingBatches =
            [int]$json.engine_render_scene.room.native_pica_lighting_batch_count
        $linkPicaLightingBatches =
            [int]$json.engine_render_scene.link_child.native_pica_lighting_batch_count
        $picaLightingBatches = $roomPicaLightingBatches + $linkPicaLightingBatches
        $picaLightingTexturedBatches =
            [int]$json.engine_render_scene.room.native_pica_lighting_textured_batch_count +
            [int]$json.engine_render_scene.link_child.native_pica_lighting_textured_batch_count
        $picaLightingVertices =
            [int]$json.engine_render_scene.room.native_pica_lighting_vertex_count +
            [int]$json.engine_render_scene.link_child.native_pica_lighting_vertex_count
        if ($json.native_oot3d_pica_lighting_render_supported -ne $true -or
            $picaLighting.available -ne $true -or
            $picaLighting.mode -ne "native_pica_lighting_vertex_color" -or
            $picaLighting.record_selector -ne "active_setup_record_from_player_floor_light_setting_index" -or
            $picaLighting.record_selector_source -ne "oot3d_player_floor_surface_type_data2_bits_6_10_environment_change_light_setting" -or
            [int]$picaLighting.record_index -lt 0 -or
            [int]$picaLighting.record_index -ne [int]$picaLighting.floor_light_setting_index -or
            [int]$picaLighting.floor_polygon_index -lt 0 -or
            [int]$picaLighting.floor_surface_type -lt 0 -or
            [int]$picaLighting.floor_light_setting_raw_index -lt 0 -or
            $picaLighting.texture_combiner -ne "texel0_times_vertex_color" -or
            $picaLighting.textured_base_color_source -ne "constant_white_until_material_combiner_route_decoded" -or
            $picaLighting.ambient_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
            [int]$picaLighting.ambient_color_offset -ne 10 -or
            $picaLighting.diffuse_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
            [int]$picaLighting.diffuse_color_offset -ne 16 -or
            $picaLighting.light1_color_source -ne "oot3d_runtime_light_settings_rgb_u8" -or
            [int]$picaLighting.light1_color_offset -ne 22 -or
            $picaLighting.material_lighting_enable_source -ne "oot3d_cmb_material_fragment_lighting_flag" -or
            $picaLighting.material_lighting_deferred_source -ne "oot3d_cmb_material_vertex_or_hemisphere_lighting_flags" -or
            $picaLighting.material_vertex_hemisphere_model_scope -ne "native_cmb_transform_instance_or_static_baked_models_with_native_normals" -or
            $picaLighting.material_vertex_hemisphere_lighting_source -ne "oot3d_cmb_material_vertex_or_hemisphere_lighting_flags" -or
            $picaLighting.vertex_hemisphere_lighting_formula -ne "pica_diffuse_accumulator_evaluated_per_vertex_normal_for_native_cmb_transform_instance_or_static_baked_models_with_native_normals" -or
            $picaLighting.vertex_hemisphere_lighting_reference -ne "azahar_pica_frame_vs_uniform_f82_f84_diffuse_accumulator_for_native_cmb_skeleton_vertex_output" -or
            $picaLighting.vertex_hemisphere_light_color_mode -ne "oot3d_runtime_environment_light_colors_with_actor_vs_compact_payload_vectors" -or
            $picaLighting.vertex_hemisphere_lighting_supported -ne $true -or
            $picaLighting.material_emission_source -ne "oot3d_cmb_material_emission_rgb" -or
            $picaLighting.material_ambient_source -ne "oot3d_cmb_material_ambient_rgb" -or
            $picaLighting.material_diffuse_source -ne "oot3d_cmb_material_diffuse_rgb" -or
            $picaLighting.vertex_color_formula -ne "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * diffuse.rgb / 255, 0, 255) / 255)" -or
            $picaLighting.directional_formula -ne "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * (light0.rgb * max(dot(normal, light0), 0) + light1.rgb * max(dot(normal, light1), 0)) / 255, 0, 255) / 255)" -or
            [int]$picaLighting.light0_vector_offset -ne 13 -or
            $picaLighting.light0_vector_source -ne "oot3d_runtime_light_settings_signed_vec3_normalized" -or
            $null -eq $picaLighting.light0_vector -or
            [Math]::Abs([double]$picaLighting.light0_vector.x) +
                [Math]::Abs([double]$picaLighting.light0_vector.y) +
                [Math]::Abs([double]$picaLighting.light0_vector.z) -le 0.01 -or
            [int]$picaLighting.light1_vector_offset -ne 19 -or
            $picaLighting.light1_vector_source -ne "oot3d_runtime_light_settings_signed_vec3_normalized" -or
            $null -eq $picaLighting.light1_vector -or
            [Math]::Abs([double]$picaLighting.light1_vector.x) +
                [Math]::Abs([double]$picaLighting.light1_vector.y) +
                [Math]::Abs([double]$picaLighting.light1_vector.z) -le 0.01 -or
            [int]$picaLighting.directional_light_count -ne 2 -or
            $json.native_oot3d_pica_lighting_render_batches_applied -ne
                ($picaLightingBatches -gt 0) -or
            $json.native_oot3d_pica_lighting_render_fragment_batches_applied -ne
                ($picaLightingBatches -gt 0) -or
            $json.native_oot3d_pica_lighting_render_vertex_hemisphere_batches_applied -ne $true -or
            ($picaLightingBatches -gt 0 -and
                ($picaLighting.directional_lighting_applied -ne $true -or
                [int]$picaLighting.native_normal_vertex_count -le 0)) -or
            ($picaLightingBatches -eq 0 -and
                ($picaLighting.directional_lighting_applied -ne $false -or
                [int]$picaLighting.native_normal_vertex_count -ne 0)) -or
            [int]$picaLighting.applied_batch_count -ne $picaLightingBatches -or
            [int]$picaLighting.applied_textured_batch_count -ne $picaLightingTexturedBatches -or
            [int]$picaLighting.applied_vertex_count -ne $picaLightingVertices -or
            [int]$picaLighting.applied_vertex_lighting_batch_count -ne
                [int]$json.engine_render_scene.link_child.native_pica_vertex_lighting_applied_batch_count -or
            [int]$picaLighting.applied_hemisphere_lighting_batch_count -ne
                [int]$json.engine_render_scene.link_child.native_pica_hemisphere_lighting_applied_batch_count -or
            $picaLighting.uses_runtime_n64_asset_substitution -ne $false) {
            throw "OOT3D native Fast3D demo did not apply native PICA lighting through the engine render path: $Output"
        }
        if ($json.native_oot3d_pica_self_shadow_state_supported -ne $true -or
            $json.native_oot3d_pica_self_shadow_source_kind -ne "oot3d_pica_lightenv_shadow_semantics" -or
            $json.native_oot3d_pica_self_shadow_mode -ne "native_pica_self_shadow" -or
            $json.native_oot3d_pica_self_shadow_lightenv_registers_supported -ne $true -or
            $json.native_oot3d_pica_self_shadow_fragment_light_flags_supported -ne $true -or
            $json.native_oot3d_pica_self_shadow_texture_projection_supported -ne $true -or
            $json.native_oot3d_pica_self_shadow_shader_equation_supported -ne $true -or
            $json.native_oot3d_pica_self_shadow_texture_sampling_supported -ne $true -or
            $json.native_oot3d_pica_self_shadow_primary_rgb_shadow_term_supported -ne $true -or
            $json.native_oot3d_pica_self_shadow_light_vector_source_supported -ne $true -or
            $json.native_oot3d_pica_self_shadow_candidate_route_supported -ne $true -or
            $json.native_oot3d_pica_self_shadow_native_geometry_occlusion_supported -ne $false -or
            $json.native_oot3d_pica_self_shadow_full_primary_light_contribution_supported -ne $true -or
            $json.native_oot3d_pica_shadow2d_texture_type_supported -ne $true -or
            $json.native_oot3d_pica_shadow2d_backend_pass_request_supported -ne $true -or
            $json.native_oot3d_pica_shadow2d_visual_pass_request_supported -ne $true -or
            $json.native_oot3d_pica_shadow2d_material_texture_projection_input_supported -ne $true -or
            $json.native_oot3d_pica_shadow2d_texcoord0_w_input_supported -ne $true -or
            $json.native_oot3d_pica_shadow2d_encoded_depth_compare_supported -ne $true -or
            $json.native_oot3d_pica_shadow2d_projection_register_values_decoded -ne $false -or
            $json.native_oot3d_pica_shadow2d_visual_pass_ready -ne $false -or
            $json.native_oot3d_pica_shadow2d_visual_pass_uses_runtime_n64_asset_substitution -ne
                $false -or
            $json.native_oot3d_pica_self_shadow_shader_equation_source_kind -ne
                "azahar_pica_fragment_lighting_shadow_equation" -or
            $json.native_oot3d_pica_self_shadow_texture_sampling_source_kind -ne
                "azahar_pica_shadow_texture_sampling_equation" -or
            $json.native_oot3d_pica_self_shadow_shader_route_decoded -ne $false -or
            $json.native_oot3d_pica_self_shadow_uses_runtime_n64_asset_substitution -ne $false -or
            $json.native_oot3d_pica_self_shadow_shader_route_application -ne
                "pica_primary_rgb_diffuse_sum_shadow_term" -or
            $json.native_oot3d_pica_self_shadow_caster_scope -ne
                "native_cmb_skeleton_transform_instance_models_with_native_normals" -or
            $expectedSelfShadowLightVectorSources -notcontains
                $json.native_oot3d_pica_self_shadow_light_vector_source -or
            $json.native_oot3d_pica_self_shadow_occlusion_formula -ne
                $expectedSelfShadowOcclusionFormula -or
            $json.native_oot3d_pica_self_shadow_light_contribution_formula -ne
                $expectedSelfShadowLightContributionFormula -or
            $json.native_oot3d_pica_shadow2d_backend_pass_request_source -ne
                $expectedShadow2dRequestSource -or
            $json.native_oot3d_pica_shadow2d_visual_pass_source_kind -ne
                $expectedShadow2dVisualPassSourceKind -or
            $json.native_oot3d_pica_shadow2d_visual_pass_render_target_format -ne
                $expectedShadow2dFormat -or
            $json.native_oot3d_pica_shadow2d_material_texture_projection_input_source -ne
                $expectedShadow2dMaterialTextureProjectionInputSource -or
            $json.native_oot3d_pica_shadow2d_projection_register_value_source -ne
                $expectedShadow2dProjectionRegisterValueSource -or
            $json.native_oot3d_pica_shadow2d_visual_pass_application -ne
                $expectedShadow2dVisualPassApplication -or
            $json.native_oot3d_pica_shadow2d_visual_pass_blocked_reason -ne
                $expectedShadow2dVisualPassBlockedReason -or
            $json.native_oot3d_pica_shadow2d_shadow_map_format -ne
                $expectedShadow2dFormat -or
            $json.native_oot3d_pica_shadow2d_compare_source -ne
                $expectedShadow2dCompareSource -or
            $json.native_oot3d_pica_shadow2d_filter -ne
                $expectedShadow2dFilter -or
            $json.native_oot3d_pica_shadow2d_encoded_depth_decode_source -ne
                $expectedShadow2dEncodedDepthDecodeSource -or
            [int]$json.native_oot3d_pica_shadow2d_encoded_depth_bits -ne 24 -or
            [int]$json.native_oot3d_pica_shadow2d_encoded_alpha_bits -ne 8 -or
            [int]$json.native_oot3d_pica_shadow2d_bias_shift -ne 1 -or
            [int]$json.native_oot3d_pica_shadow2d_filter_tap_count -ne 4 -or
            [int]$json.native_oot3d_pica_shadow2d_filter_result_channel_count -ne 4 -or
            $json.native_oot3d_pica_shadow2d_out_of_bounds_result -ne
                $expectedShadow2dOutOfBoundsResult -or
            $json.native_oot3d_pica_shadow2d_filter_interpolation_source -ne
                $expectedShadow2dFilterInterpolationSource -or
            [string]::IsNullOrWhiteSpace([string]$json.native_oot3d_pica_shadow2d_z_formula) -or
            [int]$json.native_oot3d_pica_self_shadow_candidate_batch_count -le 0 -or
            [int]$json.native_oot3d_pica_shadow2d_material_texture_projection_candidate_batch_count -ne
                [int]$json.native_oot3d_pica_self_shadow_candidate_batch_count -or
            [int]$json.native_oot3d_pica_shadow2d_material_texture_projection_decoded_batch_count -ne
                [int]$json.native_oot3d_pica_self_shadow_candidate_batch_count -or
            [int]$json.native_oot3d_pica_shadow2d_texcoord0_w_input_candidate_batch_count -ne
                [int]$json.native_oot3d_pica_self_shadow_candidate_batch_count -or
            [int]$json.native_oot3d_pica_shadow2d_texcoord0_w_input_decoded_batch_count -ne
                [int]$json.native_oot3d_pica_self_shadow_candidate_batch_count -or
            [int]$json.native_oot3d_pica_shadow2d_native_material_lane_decoded_batch_count -ne
                [int]$json.native_oot3d_pica_self_shadow_candidate_batch_count -or
            [int]$json.native_oot3d_pica_self_shadow_applied_batch_count -ne 0 -or
            [int]$json.native_oot3d_pica_self_shadow_applied_vertex_count -ne 0 -or
            [int]$json.native_oot3d_pica_self_shadow_occluded_vertex_count -ne 0 -or
            [int]$json.native_oot3d_pica_self_shadow_fragment_light_register_count -ne 8 -or
            $null -eq $picaShadow -or
            $picaShadow.available -ne $true -or
            $picaShadow.source_kind -ne "oot3d_pica_lightenv_shadow_semantics" -or
            $picaShadow.mode -ne "native_pica_self_shadow" -or
            $picaShadow.light_env_shadow_registers_supported -ne $true -or
            $picaShadow.fragment_light_shadow_flags_supported -ne $true -or
            $picaShadow.shadow_texture_projection_registers_supported -ne $true -or
            $picaShadow.light_env_shadow_alpha_name -ne "dmp_LightEnv.shadowAlpha" -or
            $picaShadow.light_env_shadow_selector_name -ne "dmp_LightEnv.shadowSelector" -or
            $picaShadow.light_env_shadow_primary_name -ne "dmp_LightEnv.shadowPrimary" -or
            $picaShadow.light_env_shadow_secondary_name -ne "dmp_LightEnv.shadowSecondary" -or
            $picaShadow.fragment_light_shadowed_name_pattern -ne "dmp_FragmentLightSource[%d].shadowed" -or
            [int]$picaShadow.fragment_light_shadowed_register_count -ne 8 -or
            $picaShadow.texture_shadow_z_scale_name -ne "dmp_Texture[0].shadowZScale" -or
            $picaShadow.texture_shadow_z_bias_name -ne "dmp_Texture[0].shadowZBias" -or
            $picaShadow.shader_equation_semantics_supported -ne $true -or
            $picaShadow.shadow_texture_sampling_semantics_supported -ne $true -or
            $picaShadow.primary_rgb_shadow_term_supported -ne $true -or
            $picaShadow.native_geometry_occlusion_supported -ne $false -or
            $picaShadow.full_primary_light_contribution_shadow_supported -ne $true -or
            $picaShadow.shadow2d_texture_type_supported -ne $true -or
            $picaShadow.shadow2d_backend_pass_request_supported -ne $true -or
            $picaShadow.shadow2d_visual_pass_request_supported -ne $true -or
            $picaShadow.shadow2d_material_texture_projection_input_supported -ne $true -or
            $picaShadow.shadow2d_encoded_depth_compare_supported -ne $true -or
            $picaShadow.shadow2d_projection_register_values_decoded -ne $false -or
            $picaShadow.shadow2d_visual_pass_ready -ne $false -or
            $picaShadow.shadow2d_visual_pass_uses_runtime_n64_asset_substitution -ne $false -or
            $picaShadow.shader_equation_source_kind -ne
                "azahar_pica_fragment_lighting_shadow_equation" -or
            $picaShadow.enable_shadow_source -ne "Pica::LightingRegs::config0.enable_shadow" -or
            $picaShadow.shadow_selector_source -ne
                "Pica::LightingRegs::config0.shadow_selector" -or
            $picaShadow.shadow_invert_source -ne "Pica::LightingRegs::config0.shadow_invert" -or
            $picaShadow.shadow_primary_source -ne "Pica::LightingRegs::config0.shadow_primary" -or
            $picaShadow.shadow_secondary_source -ne
                "Pica::LightingRegs::config0.shadow_secondary" -or
            $picaShadow.shadow_alpha_source -ne "Pica::LightingRegs::config0.shadow_alpha" -or
            $picaShadow.per_light_shadow_enable_source -ne
                "not Pica::LightingRegs::config1.disable_shadow[light_index]" -or
            $picaShadow.shadow_sample_source -ne "sampleTexUnit[shadow_selector]" -or
            [string]::IsNullOrWhiteSpace([string]$picaShadow.shadow_default_formula) -or
            [string]::IsNullOrWhiteSpace([string]$picaShadow.shadow_invert_formula) -or
            [string]::IsNullOrWhiteSpace([string]$picaShadow.primary_rgb_formula) -or
            [string]::IsNullOrWhiteSpace([string]$picaShadow.secondary_rgb_formula) -or
            [string]::IsNullOrWhiteSpace([string]$picaShadow.alpha_formula) -or
            $picaShadow.shadow_texture_sampling_source_kind -ne
                "azahar_pica_shadow_texture_sampling_equation" -or
            @($picaShadow.shadow_texture_types).Count -lt 2 -or
            $picaShadow.shadow_texture_types -notcontains
                "Pica::TexturingRegs::TextureConfig::Shadow2D" -or
            $picaShadow.shadow_texture_types -notcontains
                "Pica::TexturingRegs::TextureConfig::ShadowCube" -or
            $picaShadow.shadow_texture_orthographic_source -ne
                "Pica::TexturingRegs::shadow.orthographic" -or
            $picaShadow.shadow_texture_bias_source -ne "Pica::TexturingRegs::shadow.bias << 1" -or
            [string]::IsNullOrWhiteSpace([string]$picaShadow.shadow_texture_z_formula) -or
            $picaShadow.shadow_texture_compare_source -ne
                "CompareShadow(DecodeShadow(encoded_shadow_pixel), z)" -or
            $picaShadow.shadow_texture_filter -ne
                "2x2_neighbor_compare_results_returned_as_rgba_shadow_vector" -or
            $picaShadow.shadow_map_format -ne "r32ui_encoded_shadow_depth_reference" -or
            $picaShadow.shadow2d_encoded_depth_decode_source -ne
                $expectedShadow2dEncodedDepthDecodeSource -or
            [int]$picaShadow.shadow2d_encoded_depth_bits -ne 24 -or
            [int]$picaShadow.shadow2d_encoded_alpha_bits -ne 8 -or
            [int]$picaShadow.shadow2d_bias_shift -ne 1 -or
            [int]$picaShadow.shadow2d_filter_tap_count -ne 4 -or
            [int]$picaShadow.shadow2d_filter_result_channel_count -ne 4 -or
            $picaShadow.shadow2d_out_of_bounds_result -ne
                $expectedShadow2dOutOfBoundsResult -or
            $picaShadow.shadow2d_filter_interpolation_source -ne
                $expectedShadow2dFilterInterpolationSource -or
            $picaShadow.shader_route_source -ne "oot3d_pica_lightenv_fragment_light_and_shadow_texture_registers" -or
            $picaShadow.shadow_map_source -ne "oot3d_pica_shadow_texture_projection_registers" -or
            $picaShadow.shader_route_application -ne "pica_primary_rgb_diffuse_sum_shadow_term" -or
            $picaShadow.shadow_caster_scope -ne "native_cmb_skeleton_transform_instance_models_with_native_normals" -or
            $picaShadow.shadow_light_vector_source_supported -ne $true -or
            $picaShadow.self_shadow_candidate_route_supported -ne $true -or
            $expectedSelfShadowLightVectorSources -notcontains
                $picaShadow.shadow_light_vector_source -or
            $picaShadow.shadow_occlusion_formula -ne
                $expectedSelfShadowOcclusionFormula -or
            $picaShadow.shadow_light_contribution_formula -ne
                $expectedSelfShadowLightContributionFormula -or
            $picaShadow.shadow2d_backend_pass_request_source -ne
                $expectedShadow2dRequestSource -or
            $picaShadow.shadow2d_visual_pass_source_kind -ne
                $expectedShadow2dVisualPassSourceKind -or
            $picaShadow.shadow2d_visual_pass_render_target_format -ne
                $expectedShadow2dFormat -or
            $picaShadow.shadow2d_material_texture_projection_input_source -ne
                $expectedShadow2dMaterialTextureProjectionInputSource -or
            $picaShadow.shadow2d_texcoord0_w_input_supported -ne $true -or
            $picaShadow.shadow2d_texcoord0_w_input_source -ne
                $expectedShadow2dTexCoord0WInputSource -or
            $picaShadow.shadow2d_projection_register_value_source -ne
                $expectedShadow2dProjectionRegisterValueSource -or
            $picaShadow.shadow2d_visual_pass_application -ne
                $expectedShadow2dVisualPassApplication -or
            $picaShadow.shadow2d_visual_pass_blocked_reason -ne
                $expectedShadow2dVisualPassBlockedReason -or
            $picaShadow.pending_route -ne "native_pica_shadow2d_texture_projection_backend_pass_pending" -or
            $picaShadow.self_shadow_shader_route_decoded -ne $false -or
            [int]$picaShadow.candidate_batch_count -le 0 -or
            [int]$picaShadow.material_texture_projection_candidate_batch_count -ne
                [int]$picaShadow.candidate_batch_count -or
            [int]$picaShadow.material_texture_projection_decoded_batch_count -ne
                [int]$picaShadow.candidate_batch_count -or
            [int]$picaShadow.texcoord0_w_input_candidate_batch_count -ne
                [int]$picaShadow.candidate_batch_count -or
            [int]$picaShadow.texcoord0_w_input_decoded_batch_count -ne
                [int]$picaShadow.candidate_batch_count -or
            [int]$picaShadow.native_material_lane_decoded_batch_count -ne
                [int]$picaShadow.candidate_batch_count -or
            [int]$picaShadow.applied_batch_count -ne 0 -or
            [int]$picaShadow.applied_vertex_count -ne 0 -or
            [int]$picaShadow.occluded_vertex_count -ne 0 -or
            $picaShadow.uses_runtime_n64_asset_substitution -ne $false) {
            throw "OOT3D native Fast3D demo did not expose the native PICA self-shadow register route without N64 runtime substitution: $Output"
        }
        if ($RenderMode -ne "native_pica_lighting_debug" -and
            ([int]$json.engine_render_scene.room.native_pica_lighting_batch_count -ne
                $roomFragmentLightingBatches -or
            [int]$json.engine_render_scene.link_child.native_pica_lighting_batch_count -ne
                [int]$json.engine_render_scene.link_child.native_pica_vertex_or_hemisphere_lighting_applied_batch_count -or
            [int]$json.engine_render_scene.room.vertex_color_texture_modulated_batch_count -ne
                [int]$json.engine_render_scene.room.textured_batch_count -or
            [int]$json.engine_render_scene.link_child.vertex_color_texture_modulated_batch_count -ne
                [int]$json.engine_render_scene.link_child.textured_batch_count -or
            [int]$json.engine_render_scene.link_child.native_pica_directional_lighting_batch_count -ne
                [int]$json.engine_render_scene.link_child.native_pica_lighting_batch_count -or
            [int]$json.engine_render_scene.room.native_pica_directional_lighting_batch_count -ne 0 -or
            $json.engine_render_scene.room.transform_baked_into_vertices -ne $true -or
            $json.engine_render_scene.link_child.transform_baked_into_vertices -ne $false -or
            [int]$json.engine_render_scene.link_child.native_cmb_skeleton_bone_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_pica_vertex_or_hemisphere_lighting_applied_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_pica_vertex_lighting_applied_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_pica_hemisphere_lighting_applied_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_pica_vertex_or_hemisphere_lighting_deferred_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_pica_vertex_or_hemisphere_lighting_deferred_batch_count -ne 0 -or
            [int]$json.engine_render_scene.room.native_pica_vertex_lighting_deferred_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_pica_vertex_lighting_deferred_batch_count -ne 0 -or
            [int]$json.engine_render_scene.room.native_pica_hemisphere_lighting_deferred_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_pica_hemisphere_lighting_deferred_batch_count -ne 0 -or
            [int]$json.engine_render_scene.room.native_pica_self_shadow_candidate_batch_count -ne 0 -or
            [int]$json.engine_render_scene.room.native_pica_self_shadow_applied_batch_count -ne 0 -or
            [int]$json.engine_render_scene.link_child.native_pica_self_shadow_applied_batch_count -ne 0 -or
            [int]$json.engine_render_scene.link_child.native_pica_self_shadow_vertex_count -ne 0 -or
            [int]$json.engine_render_scene.link_child.native_pica_self_shadow_occluded_vertex_count -ne 0 -or
            [int]$json.engine_render_scene.link_child.average_vertex_color.r -ge 255 -or
            [int]$json.engine_render_scene.link_child.average_vertex_color.g -ge 255 -or
            [int]$json.engine_render_scene.link_child.average_vertex_color.b -ge 255 -or
            [int]$json.engine_render_scene.room.native_normal_available_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_normal_available_batch_count -gt
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_normal_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_normal_vertex_count -le 0 -or
            [int]$json.engine_render_scene.room.native_normal_vertex_count -gt
                [int]$json.engine_render_scene.room.vertex_count -or
            [int]$json.engine_render_scene.link_child.native_normal_vertex_count -ne
                [int]$json.engine_render_scene.link_child.vertex_count)) {
            throw "OOT3D native Fast3D demo did not preserve native textures while modulating them with PICA vertex color: $Output"
        }
        if ([int]$json.engine_render_scene.room.native_material_batch_count -ne
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_color_decoded_batch_count -ne
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_color_decoded_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_render_state_decoded_batch_count -ne
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_render_state_decoded_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_raw_texture_stage_selector_batch_count -ne
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_raw_texture_stage_selector_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_texture_stage_candidate_window_batch_count -ne
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_texture_stage_candidate_window_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_texture_stage_candidate_nonzero_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_material_texture_stage_candidate_nonzero_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_texture_env_decoded_batch_count -ne
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_decoded_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_texture_env_raw_size_batch_count -ne
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_raw_size_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_texture_env_table_record_count -le
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_table_record_count -le
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_vertex_lighting_enabled_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_hemisphere_lighting_enabled_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_cmb_lighting_flag_enabled_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_material_vertex_lighting_enabled_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_material_hemisphere_lighting_enabled_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_material_cmb_lighting_flag_enabled_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_texture_env_stage_index_covered_batch_count -ne
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_stage_index_covered_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_texture_env_multi_stage_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_multi_stage_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_decoded_batch_count -ne
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_decoded_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_rgb_route_decoded_batch_count -ne
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_rgb_route_decoded_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_uses_previous_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_uses_previous_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_uses_constant_color_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_uses_constant_color_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_requires_multi_stage_batch_count -ne
                [int]$json.engine_render_scene.room.native_material_texture_env_multi_stage_batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_requires_multi_stage_batch_count -ne
                [int]$json.engine_render_scene.link_child.native_material_texture_env_multi_stage_batch_count -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_requires_constant_color_selection_batch_count -ne
                [int]$json.engine_render_scene.room.native_material_texture_env_program_uses_constant_color_batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_requires_constant_color_selection_batch_count -ne
                [int]$json.engine_render_scene.link_child.native_material_texture_env_program_uses_constant_color_batch_count -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_color_shader_supported_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_color_shader_supported_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_color_shader_applied_batch_count -ne
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_color_shader_applied_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_fallback_texture_light_modulation_batch_count -ne 0 -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_fallback_texture_light_modulation_batch_count -ne 0 -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_vertex_color_constant_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_vertex_color_constant_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_requires_texture_color_add_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_texture_color_addend_batch_count -ne
                [int]$json.engine_render_scene.room.native_material_texture_env_program_requires_texture_color_add_batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_texture_color_addend_batch_count -ne
                [int]$json.engine_render_scene.link_child.native_material_texture_env_program_requires_texture_color_add_batch_count -or
            [int]$json.engine_render_scene.room.native_material_texture_env_color_shader_supported_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_color_shader_supported_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_texture_env_shader_pending_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_shader_pending_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_combiner_pending_batch_count -ne 0 -or
            [int]$json.engine_render_scene.link_child.native_material_combiner_pending_batch_count -ne 0) {
            throw "OOT3D native Fast3D demo did not expose native CMB material raw stage selectors and MATS texture environment settings for the lighting path: $Output"
        }
        $roomNativeMaterials = @($json.engine_render_scene.room.native_materials)
        $linkNativeMaterials = @($json.engine_render_scene.link_child.native_materials)
        if ($roomNativeMaterials.Count -le 0 -or
            $linkNativeMaterials.Count -le 0 -or
            -not (Test-Oot3dNativeMaterialTextureEnvModel -Model $json.engine_render_scene.room) -or
            -not (Test-Oot3dNativeMaterialTextureEnvModel -Model $json.engine_render_scene.link_child)) {
            throw "OOT3D native Fast3D demo did not preserve raw OOT3D CMB material records and MATS texture environment settings in the engine scene: $Output"
        }
        if ($RenderMode -ne "native_pica_lighting_debug" -and
            ([int]$json.engine_render_scene.room.native_material_texture_env_program_color_shader_applied_batch_count -le 0 -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_color_shader_applied_batch_count -le 0 -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_color_shader_applied_batch_count -ne
                [int]$json.engine_render_scene.room.batch_count -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_color_shader_applied_batch_count -ne
                [int]$json.engine_render_scene.link_child.batch_count -or
            [int]$json.engine_render_scene.room.native_material_texture_env_program_fallback_texture_light_modulation_batch_count -ne 0 -or
            [int]$json.engine_render_scene.link_child.native_material_texture_env_program_fallback_texture_light_modulation_batch_count -ne 0)) {
            throw "OOT3D native Fast3D demo did not distinguish native TextureEnv shader coverage from temporary texture-light fallback: $Output"
        }
        if ($RenderMode -ne "native_pica_lighting_debug" -and
            $json.PSObject.Properties.Name -contains "fast3d_adapter_lifetime" -and
            ([int]$json.fast3d_adapter_lifetime.shader_input_layout_mismatch_count -ne 0 -or
            [int]$json.fast3d_adapter_lifetime.native_blend_state_unsupported_batch_count -ne 0 -or
            [int]$json.fast3d_adapter_lifetime.secondary_texture_coord_batch_count -le 0 -or
            [int]$json.fast3d_adapter_lifetime.secondary_texture_binding_count -ne
                [int]$json.fast3d_adapter_lifetime.secondary_texture_coord_batch_count)) {
            throw "OOT3D native Fast3D backend did not match the generated PICA TextureEnv shader vertex layout: $Output"
        }
        $fast3dShadow2dCandidateBatchCount =
            [int]$json.fast3d_adapter_lifetime.native_pica_self_shadow_candidate_batch_count
        $fast3dShadow2dProjectionDecodedBatchCount =
            [int]$json.fast3d_adapter_lifetime.native_pica_shadow2d_material_texture_projection_decoded_batch_count
        $fast3dShadow2dTexCoord0WDecodedBatchCount =
            [int]$json.fast3d_adapter_lifetime.native_pica_shadow2d_texcoord0_w_input_decoded_batch_count
        $fast3dShadow2dPendingBatchCount =
            [int]$json.fast3d_adapter_lifetime.native_pica_self_shadow_pending_batch_count
        if ($RenderMode -ne "native_pica_lighting_debug" -and
            $json.PSObject.Properties.Name -contains "fast3d_adapter_lifetime" -and
            ($json.fast3d_adapter_lifetime.native_pica_self_shadow_route_available -ne $true -or
            $json.fast3d_adapter_lifetime.native_pica_self_shadow_shader_route_decoded -ne $false -or
            $json.fast3d_adapter_lifetime.native_pica_self_shadow_shader_route_pending -ne $true -or
            $json.fast3d_adapter_lifetime.native_pica_self_shadow_shader_equation_semantics_supported -ne
                $true -or
            $json.fast3d_adapter_lifetime.native_pica_self_shadow_texture_sampling_semantics_supported -ne
                $true -or
            $json.fast3d_adapter_lifetime.native_pica_self_shadow_full_primary_light_contribution_supported -ne
                $true -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_texture_type_supported -ne $true -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_backend_pass_request_supported -ne $true -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_backend_pass_requested -ne $true -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_backend_pass_pending -ne $true -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_visual_pass_request_supported -ne
                $true -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_visual_pass_requested -ne $true -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_visual_pass_pending -ne $true -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_material_texture_projection_input_supported -ne
                $true -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_encoded_depth_compare_supported -ne
                $true -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_projection_register_values_decoded -ne
                $false -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_visual_pass_ready -ne $false -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_backend_pass_request_source -ne
                $expectedShadow2dRequestSource -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_visual_pass_source_kind -ne
                $expectedShadow2dVisualPassSourceKind -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_visual_pass_render_target_format -ne
                $expectedShadow2dFormat -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_material_texture_projection_input_source -ne
                $expectedShadow2dMaterialTextureProjectionInputSource -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_texcoord0_w_input_source -ne
                $expectedShadow2dTexCoord0WInputSource -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_projection_register_value_source -ne
                $expectedShadow2dProjectionRegisterValueSource -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_visual_pass_application -ne
                $expectedShadow2dVisualPassApplication -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_visual_pass_blocked_reason -ne
                $expectedShadow2dVisualPassBlockedReason -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_shadow_map_format -ne
                $expectedShadow2dFormat -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_compare_source -ne
                $expectedShadow2dCompareSource -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_filter -ne
                $expectedShadow2dFilter -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_encoded_depth_decode_source -ne
                $expectedShadow2dEncodedDepthDecodeSource -or
            [int]$json.fast3d_adapter_lifetime.native_pica_shadow2d_encoded_depth_bits -ne 24 -or
            [int]$json.fast3d_adapter_lifetime.native_pica_shadow2d_encoded_alpha_bits -ne 8 -or
            [int]$json.fast3d_adapter_lifetime.native_pica_shadow2d_bias_shift -ne 1 -or
            [int]$json.fast3d_adapter_lifetime.native_pica_shadow2d_filter_tap_count -ne 4 -or
            [int]$json.fast3d_adapter_lifetime.native_pica_shadow2d_filter_result_channel_count -ne 4 -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_out_of_bounds_result -ne
                $expectedShadow2dOutOfBoundsResult -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_filter_interpolation_source -ne
                $expectedShadow2dFilterInterpolationSource -or
            [string]::IsNullOrWhiteSpace(
                [string]$json.fast3d_adapter_lifetime.native_pica_shadow2d_z_formula) -or
            $json.fast3d_adapter_lifetime.native_pica_self_shadow_light_contribution_formula -ne
                $expectedSelfShadowLightContributionFormula -or
            $json.fast3d_adapter_lifetime.native_pica_shadow2d_visual_pass_uses_runtime_n64_asset_substitution -ne
                $false -or
            $json.fast3d_adapter_lifetime.native_pica_self_shadow_uses_runtime_n64_asset_substitution -ne $false -or
            [int]$json.fast3d_adapter_lifetime.native_pica_self_shadowed_light_register_count -ne 8 -or
            $fast3dShadow2dProjectionDecodedBatchCount -gt $fast3dShadow2dCandidateBatchCount -or
            $fast3dShadow2dTexCoord0WDecodedBatchCount -gt $fast3dShadow2dCandidateBatchCount -or
            $fast3dShadow2dPendingBatchCount -gt $fast3dShadow2dCandidateBatchCount -or
            [int]$json.fast3d_adapter_lifetime.native_pica_self_shadow_applied_batch_count -ne 0)) {
            throw "OOT3D native Fast3D backend did not preserve the inactive native Shadow2D fixture diagnostics: $Output"
        }
        $linkShadow = $json.engine_render_scene.link_actor_shadow
        if ($json.native_oot3d_actor_shadow_state_supported -ne $true -or
            $json.native_oot3d_actor_shadow_source_kind -ne "oot3d_actor_shape_shadow_state" -or
            $json.native_oot3d_actor_shadow_actor -ne "player_link_child" -or
            $json.native_oot3d_actor_shadow_draw_function -ne "ActorShadow_DrawFeet" -or
            $json.native_oot3d_actor_shadow_shape_source -ne "oot3d_player_initcommon_actor_shape_init_age_properties_offset_0x04" -or
            $json.native_oot3d_actor_shadow_receiver_source -ne "oot3d_actor_floor_poly_from_zsi_collision_bgcheck" -or
            $json.native_oot3d_actor_shadow_blend_source -ne "oot3d_actor_shape_shadow_alpha_native_render_state" -or
            $json.native_oot3d_actor_shadow_uses_runtime_n64_asset_substitution -ne $false -or
            $null -eq $linkShadow -or
            $linkShadow.available -ne $true -or
            $linkShadow.actor_shape_state_supported -ne $true -or
            $linkShadow.receiver_from_native_collision -ne $true -or
            $linkShadow.native_blend_route_supported -ne $true -or
            $linkShadow.uses_runtime_n64_asset_substitution -ne $false -or
            $linkShadow.foot_shadow_draw_supported -ne $true -or
            $linkShadow.foot_contact_pair_projection_supported -ne $false -or
            $linkShadow.pending_route -ne "feet_contact_pair_projection_pending_native_skelanime_limb_contact_mapping" -or
            [math]::Abs([double]$linkShadow.shape_shadow_scale - 60.0) -gt 0.001 -or
            [int]$linkShadow.shape_shadow_alpha -ne 255 -or
            [int]$linkShadow.floor_polygon_index -ne [int]$json.link_instance.floor_polygon_index -or
            [int]$linkShadow.floor_surface_type -ne [int]$json.link_instance.floor_surface_type -or
            [int]$linkShadow.floor_light_setting_index -ne [int]$json.link_instance.floor_light_setting_index -or
            [math]::Abs([double]$linkShadow.actor_position.x - [double]$json.link_instance.actor_position.x) -gt 0.001 -or
            [math]::Abs([double]$linkShadow.actor_position.y - [double]$json.link_instance.actor_position.y) -gt 0.001 -or
            [math]::Abs([double]$linkShadow.actor_position.z - [double]$json.link_instance.actor_position.z) -gt 0.001 -or
            [math]::Abs([double]$linkShadow.receiver_position.y - [double]$json.link_instance.ground_y) -gt 0.001 -or
            ([math]::Abs([double]$linkShadow.receiver_normal.x) +
                [math]::Abs([double]$linkShadow.receiver_normal.y) +
                [math]::Abs([double]$linkShadow.receiver_normal.z)) -le 0.01) {
            throw "OOT3D native Fast3D demo did not expose Link actor shadow state from native ActorShape and ZSI floor collision: $Output"
        }
        if ($json.world_to_clip_matrix_ready -ne $true) {
            throw "OOT3D native Fast3D demo did not prepare the camera world-to-clip matrix: $Output"
        }
        }
    }

    if ($Launch) {
        $args = @("--manifest", $Manifest, "--resource-root", $ResourceRoot, "--output", $Output,
            "--screenshot", $ScreenshotOutput, "--width", "$Width", "--height", "$Height",
            "--render-mode", $RenderMode,
            "--material-animation-frame",
            ([string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}", $MaterialAnimationFrame)))
        if ($ScreenshotSequence) {
            $args += @("--screenshot-sequence")
        }
        if ($Frames -gt 0) {
            $args += @("--frames", "$Frames")
            if ($MaxSeconds -le 0) {
                $MaxSeconds = 15
            }
        }
        if ($MaxSeconds -gt 0) {
            $args += @("--max-seconds", "$MaxSeconds")
        }
        if ($Backend -ge 0) {
            $args += @("--backend", "$Backend")
        }
        if ($EntranceIndex -ge 0) {
            $args += @("--entrance-index", "$EntranceIndex")
        }
        if ($TitleIntro) {
            $args += @("--title-intro", "--title-intro-qdb-index", "0")
        }
        $args += $ExtraArgs
        $demoProcess = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $repoRoot -PassThru
        Set-Oot3dProcessForeground -Process $demoProcess | Out-Null
        $demoProcess.WaitForExit()
        if ($demoProcess.ExitCode -ne 0) {
            throw "OOT3D native Fast3D demo exited with code $($demoProcess.ExitCode)"
        }
        Assert-Oot3dFramebufferNotCollapsed -Path $ScreenshotOutput -OutputPath $Output
    }
} finally {
    Pop-Location
}
