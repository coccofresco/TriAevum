param(
    [string]$Manifest = "I:\oot3dre_work\standalone_demo\link_house\demo_manifest.json",
    [string]$Output = "",
    [string]$BuildRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$probeSource = Join-Path $repoRoot "tools\oot3d\native_demo_host\oot3d_native_fast3d_renderer_probe.cpp"
$nativeAssetsSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeAssets.cpp"
$nativeDemoSceneSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeDemoScene.cpp"
$nativeRenderSceneSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeRenderScene.cpp"
$nativeRendererSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeRenderer.cpp"
$nativeFast3dRendererSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeFast3dRenderer.cpp"
$includeRoot = Join-Path $repoRoot "runtime/three_ds_recomp\include"
$vcpkgInclude = Join-Path $repoRoot "build-codex\vcpkg\installed\x64-windows-static\include"
$imguiInclude = Join-Path $repoRoot "build-codex\_deps\imgui-src"
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"

if (-not (Test-Path -LiteralPath $Manifest)) {
    throw "OOT3D standalone manifest not found: $Manifest"
}
if (-not (Test-Path -LiteralPath $probeSource)) {
    throw "OOT3D native Fast3D renderer probe source not found: $probeSource"
}
if (-not (Test-Path -LiteralPath $nativeAssetsSource)) {
    throw "runtime/three_ds_recomp OOT3D native assets source not found: $nativeAssetsSource"
}
if (-not (Test-Path -LiteralPath $nativeDemoSceneSource)) {
    throw "runtime/three_ds_recomp OOT3D native demo scene source not found: $nativeDemoSceneSource"
}
if (-not (Test-Path -LiteralPath $nativeRenderSceneSource)) {
    throw "runtime/three_ds_recomp OOT3D native render scene source not found: $nativeRenderSceneSource"
}
if (-not (Test-Path -LiteralPath $nativeRendererSource)) {
    throw "runtime/three_ds_recomp OOT3D native renderer source not found: $nativeRendererSource"
}
if (-not (Test-Path -LiteralPath $nativeFast3dRendererSource)) {
    throw "runtime/three_ds_recomp OOT3D native Fast3D renderer source not found: $nativeFast3dRendererSource"
}
if (-not (Test-Path -LiteralPath $vcpkgInclude)) {
    throw "vcpkg include directory not found: $vcpkgInclude"
}
if (-not (Test-Path -LiteralPath $imguiInclude)) {
    throw "imgui include directory not found: $imguiInclude"
}
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio developer command prompt not found: $vsDevCmd"
}

if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path (Split-Path -Parent $Manifest) "native_host\runtime/three_ds_recomp_native_fast3d_renderer_probe.json"
}
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path (Split-Path -Parent $Output) "native_fast3d_renderer_probe_build"
}

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

$exe = Join-Path $BuildRoot "oot3d_native_fast3d_renderer_probe.exe"
$compileCommand = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 && " +
    "cl /nologo /std:c++20 /EHsc /utf-8 " +
    "/I`"$includeRoot`" " +
    "/I`"$vcpkgInclude`" " +
    "/I`"$imguiInclude`" " +
    "/Fe:`"$exe`" " +
    "`"$probeSource`" " +
    "`"$nativeAssetsSource`" " +
    "`"$nativeDemoSceneSource`" " +
    "`"$nativeRenderSceneSource`" " +
    "`"$nativeRendererSource`" " +
    "`"$nativeFast3dRendererSource`""

cmd.exe /d /c $compileCommand
if ($LASTEXITCODE -ne 0) {
    throw "OOT3D native Fast3D renderer probe compilation failed with exit code $LASTEXITCODE"
}

& $exe --manifest $Manifest --output $Output
if ($LASTEXITCODE -ne 0) {
    throw "OOT3D native Fast3D renderer probe failed with exit code $LASTEXITCODE"
}

if ($Verify) {
    if (-not (Test-Path -LiteralPath $Output)) {
        throw "OOT3D native Fast3D renderer probe output was not written: $Output"
    }
    $json = Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json
    if ($json.status -ne "valid") {
        throw "OOT3D native Fast3D renderer probe output is not valid: $Output"
    }
    if ([int]$json.submit_count -ne 2) {
        throw "OOT3D native Fast3D renderer probe did not exercise persistent backend reuse: $Output"
    }
    if ($json.second_submit_uses_animated_link_pose -ne $true -or
        $json.second_submit_uses_movement_clip -ne $true -or
        $json.animated_link_pose.sampled_pose_valid -ne $true -or
        $json.animated_link_pose.clip_id -ne "move" -or
        [int]$json.animated_link_pose.frame_count -le 1 -or
        [int]$json.animated_link_pose.world_transform_count -le 0) {
        throw "OOT3D native Fast3D renderer probe did not exercise animated Link CSAB playback: $Output"
    }
    if ($json.first_engine_renderer_submission.status -ne "valid" -or
        $json.second_engine_renderer_submission.status -ne "valid") {
        throw "OOT3D native renderer submission failed before Fast3D adapter: $Output"
    }
    $firstTextureCount = [int]$json.first_engine_renderer_submission.texture_upload_count
    $secondTextureCount = [int]$json.second_engine_renderer_submission.texture_upload_count
    $expectedTriangleCount = [int]$json.first_engine_renderer_submission.triangle_count +
        [int]$json.second_engine_renderer_submission.triangle_count
    if ([int]$json.fast3d_adapter.texture_upload_count -ne $firstTextureCount) {
        throw "OOT3D native Fast3D adapter reuploaded persistent native textures: $Output"
    }
    if ([int]$json.fast3d_adapter.texture_cache_hit_count -ne $secondTextureCount) {
        throw "OOT3D native Fast3D adapter did not resolve second-submit textures from cache: $Output"
    }
    if ([int]$json.fast3d_adapter.texture_resident_count -ne $firstTextureCount) {
        throw "OOT3D native Fast3D adapter resident texture count mismatch: $Output"
    }
    if ([int]$json.fast3d_adapter_after_first_submit.texture_cache_hit_count -ne 0) {
        throw "OOT3D native Fast3D adapter reported cache hits before persistent reuse: $Output"
    }
    if ([int]$json.fake_rendering_api.texture_upload_count -ne $firstTextureCount) {
        throw "Fake Fast3D API received repeated texture uploads: $Output"
    }
    if ([int]$json.fast3d_adapter.triangle_count -ne $expectedTriangleCount) {
        throw "OOT3D native Fast3D adapter triangle count mismatch: $Output"
    }
    if ([int]$json.fast3d_adapter.missing_texture_batch_count -ne 0) {
        throw "OOT3D native Fast3D adapter reported missing texture batches: $Output"
    }
    $expectedSelfShadowLightContributionFormula =
        "pica_shadow_map_multiplies_native_cmb_vertex_hemisphere_material_ambient_light_ambient_plus_material_diffuse_light_diffuse_dot"
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
    $expectedShadow2dTexCoord0WInputSource = "oot3d_cmb_vshader_shbin_output_texcoord0_w"
    $expectedShadow2dProjectionRegisterValueSource =
        "oot3d_codebin_default_shadow2d_inactive_or_validation_pending"
    $expectedShadow2dVisualPassApplication =
        "sample_shadow2d_encoded_depth_in_fragment_primary_rgb_shadow_term"
    $expectedShadow2dVisualPassBlockedReason =
        "shadow2d_default_or_inactive_for_current_fixture"
    $shadow2dCandidateBatchCount =
        [int]$json.fast3d_adapter.native_pica_self_shadow_candidate_batch_count
    $shadow2dProjectionDecodedBatchCount =
        [int]$json.fast3d_adapter.native_pica_shadow2d_material_texture_projection_decoded_batch_count
    $shadow2dTexCoord0WDecodedBatchCount =
        [int]$json.fast3d_adapter.native_pica_shadow2d_texcoord0_w_input_decoded_batch_count
    $shadow2dPendingBatchCount =
        [int]$json.fast3d_adapter.native_pica_self_shadow_pending_batch_count
    if ($json.fast3d_adapter.native_pica_self_shadow_route_available -ne $true -or
        $json.fast3d_adapter.native_pica_self_shadow_shader_route_decoded -ne $false -or
        $json.fast3d_adapter.native_pica_self_shadow_shader_route_pending -ne $true -or
        $json.fast3d_adapter.native_pica_self_shadow_shader_equation_semantics_supported -ne $true -or
        $json.fast3d_adapter.native_pica_self_shadow_texture_sampling_semantics_supported -ne $true -or
        $json.fast3d_adapter.native_pica_self_shadow_full_primary_light_contribution_supported -ne $true -or
        $json.fast3d_adapter.native_pica_shadow2d_texture_type_supported -ne $true -or
        $json.fast3d_adapter.native_pica_shadow2d_backend_pass_request_supported -ne $true -or
        $json.fast3d_adapter.native_pica_shadow2d_backend_pass_requested -ne $true -or
        $json.fast3d_adapter.native_pica_shadow2d_backend_pass_pending -ne $true -or
        $json.fast3d_adapter.native_pica_shadow2d_visual_pass_request_supported -ne $true -or
        $json.fast3d_adapter.native_pica_shadow2d_visual_pass_requested -ne $true -or
        $json.fast3d_adapter.native_pica_shadow2d_visual_pass_pending -ne $true -or
        $json.fast3d_adapter.native_pica_shadow2d_material_texture_projection_input_supported -ne
            $true -or
        $json.fast3d_adapter.native_pica_shadow2d_encoded_depth_compare_supported -ne $true -or
        $json.fast3d_adapter.native_pica_shadow2d_projection_register_values_decoded -ne
            $false -or
        $json.fast3d_adapter.native_pica_shadow2d_visual_pass_ready -ne $false -or
        $json.fast3d_adapter.native_pica_shadow2d_backend_pass_request_source -ne
            $expectedShadow2dRequestSource -or
        $json.fast3d_adapter.native_pica_shadow2d_visual_pass_source_kind -ne
            $expectedShadow2dVisualPassSourceKind -or
        $json.fast3d_adapter.native_pica_shadow2d_visual_pass_render_target_format -ne
            $expectedShadow2dFormat -or
        $json.fast3d_adapter.native_pica_shadow2d_material_texture_projection_input_source -ne
            $expectedShadow2dMaterialTextureProjectionInputSource -or
        $json.fast3d_adapter.native_pica_shadow2d_texcoord0_w_input_source -ne
            $expectedShadow2dTexCoord0WInputSource -or
        $json.fast3d_adapter.native_pica_shadow2d_projection_register_value_source -ne
            $expectedShadow2dProjectionRegisterValueSource -or
        $json.fast3d_adapter.native_pica_shadow2d_visual_pass_application -ne
            $expectedShadow2dVisualPassApplication -or
        $json.fast3d_adapter.native_pica_shadow2d_visual_pass_blocked_reason -ne
            $expectedShadow2dVisualPassBlockedReason -or
        $json.fast3d_adapter.native_pica_shadow2d_shadow_map_format -ne
            $expectedShadow2dFormat -or
        $json.fast3d_adapter.native_pica_shadow2d_compare_source -ne
            $expectedShadow2dCompareSource -or
        $json.fast3d_adapter.native_pica_shadow2d_filter -ne
            $expectedShadow2dFilter -or
        $json.fast3d_adapter.native_pica_shadow2d_encoded_depth_decode_source -ne
            $expectedShadow2dEncodedDepthDecodeSource -or
        [int]$json.fast3d_adapter.native_pica_shadow2d_encoded_depth_bits -ne 24 -or
        [int]$json.fast3d_adapter.native_pica_shadow2d_encoded_alpha_bits -ne 8 -or
        [int]$json.fast3d_adapter.native_pica_shadow2d_bias_shift -ne 1 -or
        [int]$json.fast3d_adapter.native_pica_shadow2d_filter_tap_count -ne 4 -or
        [int]$json.fast3d_adapter.native_pica_shadow2d_filter_result_channel_count -ne 4 -or
        $json.fast3d_adapter.native_pica_shadow2d_out_of_bounds_result -ne
            $expectedShadow2dOutOfBoundsResult -or
        $json.fast3d_adapter.native_pica_shadow2d_filter_interpolation_source -ne
            $expectedShadow2dFilterInterpolationSource -or
        [string]::IsNullOrWhiteSpace([string]$json.fast3d_adapter.native_pica_shadow2d_z_formula) -or
        $json.fast3d_adapter.native_pica_self_shadow_light_contribution_formula -ne
            $expectedSelfShadowLightContributionFormula -or
        $json.fast3d_adapter.native_pica_shadow2d_visual_pass_uses_runtime_n64_asset_substitution -ne
            $false -or
        $json.fast3d_adapter.native_pica_self_shadow_uses_runtime_n64_asset_substitution -ne $false -or
        [int]$json.fast3d_adapter.native_pica_self_shadowed_light_register_count -ne 8 -or
        $shadow2dProjectionDecodedBatchCount -gt $shadow2dCandidateBatchCount -or
        $shadow2dTexCoord0WDecodedBatchCount -gt $shadow2dCandidateBatchCount -or
        $shadow2dPendingBatchCount -gt $shadow2dCandidateBatchCount -or
        [int]$json.fast3d_adapter.native_pica_self_shadow_applied_batch_count -ne 0) {
        throw "OOT3D native Fast3D adapter did not preserve the inactive native Shadow2D fixture diagnostics: $Output"
    }
    if ([int]$json.fake_rendering_api.draw_triangles_triangle_count -ne
        $expectedTriangleCount) {
        throw "Fake Fast3D API did not receive all submitted triangles: $Output"
    }
    if ([int]$json.fake_rendering_api.max_triangles_per_draw -gt 256) {
        throw "OOT3D native Fast3D adapter exceeded the Fast3D triangle chunk size: $Output"
    }
}
