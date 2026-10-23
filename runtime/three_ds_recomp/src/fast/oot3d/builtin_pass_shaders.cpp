#include "fast/oot3d/builtin_pass_shaders.h"
#include "fast/oot3d/brixelizer_gi_prepare_shader.h"
#include "fast/oot3d/contact_shadows.h"
#include "fast/oot3d/diffuse_gi_adapter.h"
#include "fast/oot3d/grass_shader_sources.h"
#include "fast/oot3d/hiz_depth_pyramid.h"
#include "fast/oot3d/hiz_reflection.h"
#include "fast/oot3d/linear_scene_color.h"
#include "fast/oot3d/motion_vectors.h"
#include "fast/oot3d/normal_space_shader.h"
#include "fast/oot3d/pica_display_transfer.h"
#include "fast/oot3d/pica_directional_shadow_caster.h"
#include "fast/oot3d/pica_scanout_effects.h"
#include "fast/oot3d/reflection_ibl.h"
#include "fast/oot3d/scene_composite.h"
#include "fast/oot3d/smaa_1x.h"
#include "fast/oot3d/temporal_aa.h"
#include "fast/oot3d/upscaler_guides.h"
#include "fast/oot3d/upscaler_orientation.h"
#include "fast/oot3d/native_ui_overlay.h"
#include "fast/renderer/diffuse_gi_pass_shader.h"
#include "fast/renderer/gtao_shader.h"
#include "fast/renderer3ds/pica_surface_lighting_shader.h"
#include "fast/renderer3ds/pica_texture_copy_derive_gpu.h"

namespace Fast::Oot3d {
std::vector<BuiltinPassShader> BuildBuiltinPassShaders() {
    using enum Renderer::SpirvStage;
    std::vector<BuiltinPassShader> shaders{
        {"multiscale_gtao.comp", Compute, Renderer::BuildMultiscaleGtaoShader(), {}},
        {"multiscale_gtao_resolve.comp", Compute, Renderer::BuildMultiscaleGtaoResolveShader(), {}},
        {"oot3d_pica_display_transfer.comp", Compute, BuildPicaDisplayTransferComputeShader(), {}},
        {"oot3d_hiz_reduce.comp", Compute, BuildHiZReductionComputeShader(), {}},
        {"oot3d_hiz_reflection.comp", Compute, BuildHiZReflectionComputeShader(), {}},
        {"oot3d_hiz_reflection_filter.comp", Compute, BuildHiZReflectionBilateralFilterShader(), {}},
        {"oot3d_reflection_environment.comp", Compute, BuildReflectionEnvironmentComputeShader(), {}},
        {"oot3d_reflection_brdf.comp", Compute, BuildReflectionBrdfComputeShader(), {}},
        {"oot3d_reflection_material_resolve.comp", Compute, BuildReflectionMaterialResolveComputeShader(), {}},
        {"oot3d_linear_scene_color.comp", Compute, BuildLinearSceneColorComputeShader(), {}},
        {"oot3d_motion_vectors.comp", Compute, BuildCameraMotionComputeShader(), {}},
        {"oot3d_temporal_aa.comp", Compute, BuildTemporalAaComputeShader(), {}},
        {"oot3d_upscaler_guides.comp", Compute, BuildUpscalerGuideResampleShader(), {}},
        {"oot3d_upscaler_upright_inputs.comp", Compute, BuildUpscalerUprightInputShader(), {}},
        {"oot3d_upscaler_upright_output.comp", Compute, BuildUpscalerUprightOutputShader(), {}},
        {"oot3d_native_ui_overlay.comp", Compute, BuildNativeUiOverlayShader(), {}},
        {"oot3d_scene_composite.comp", Compute, BuildSceneCompositeComputeShader(), {}},
        {"oot3d_smaa_edges.comp", Compute, BuildSmaa1xComputeShader(Smaa1xStage::EdgeDetection), {}},
        {"oot3d_smaa_blend_weights.comp", Compute, BuildSmaa1xComputeShader(Smaa1xStage::BlendWeightCalculation), {}},
        {"oot3d_smaa_neighborhood.comp", Compute, BuildSmaa1xComputeShader(Smaa1xStage::NeighborhoodBlending), {}},
        {"oot3d_nri_pica_scanout.vert", Vertex, BuildPicaScanoutVertexShader(), {}},
        {"oot3d_nri_pica_scanout.frag", Fragment, BuildPicaScanoutFragmentShader(true, 0), {}},
        {"interactive_grass.vert", Vertex, BuildGrassVertexShader(), {}},
        {"interactive_grass_canonical.frag", Fragment, BuildGrassFragmentShader(), {{"GRASS_AUXILIARY_OUTPUTS", "0"}}},
        {"interactive_grass_instrumented.frag", Fragment, BuildGrassFragmentShader(), {{"GRASS_AUXILIARY_OUTPUTS", "1"}}},
        {"interactive_grass_canonical_raster_depth.frag", Fragment, BuildGrassFragmentShader(true), {{"GRASS_AUXILIARY_OUTPUTS", "0"}}},
        {"interactive_grass_instrumented_raster_depth.frag", Fragment, BuildGrassFragmentShader(true), {{"GRASS_AUXILIARY_OUTPUTS", "1"}}},
        {"grass_instance_compactor.comp", Compute, BuildGrassCompactionComputeShader(), {}},
        {"grass_shadow_grid.comp", Compute, BuildGrassShadowGridComputeShader(), {}},
        {"pica_surface_lighting_native.frag", Fragment, Renderer3ds::BuildPicaSurfaceLightingFragmentShader(false), {}},
        {"pica_surface_lighting.frag", Fragment, Renderer3ds::BuildPicaSurfaceLightingFragmentShader(true), {}},
        {"pica_surface_lighting_native_coefficients.frag", Fragment, Renderer3ds::BuildPicaSurfaceLightingFragmentShader(false, true), {}},
        {"pica_surface_lighting_coefficients.frag", Fragment, Renderer3ds::BuildPicaSurfaceLightingFragmentShader(true, true), {}},
        {"normal_space.comp", Compute, kNormalSpaceComputeShader, {}},
        {"oot3d_nri_directional_shadow.frag", Fragment, kPicaDirectionalShadowFragmentShader, {}},
        {"oot3d_nri_pica_scanout_physical.frag", Fragment,
         BuildPicaScanoutFragmentShader(true, std::nullopt, PicaScanoutOrientation::PhysicalScreen), {}},
        {"oot3d_nri_pica_scanout_surface.frag", Fragment,
         BuildPicaScanoutFragmentShader(true, std::nullopt, PicaScanoutOrientation::Surface), {}},
        {"oot3d_pica_texture_copy_derive.comp", Compute, std::string(Renderer3ds::PicaTextureCopyDeriveComputeSource), {}},
        {"brixelizer_gi_prepare.comp", Compute, std::string(kBrixelizerGiPrepareShader), {}},
    };
    // Diffuse screen-space GI runtime pass (title-neutral renderer kernels,
    // docs/TRIAEVUM_SCREEN_SPACE_GI.md "M5"), after every earlier module.
    for (auto& kernel : Renderer::DiffuseGi::BuildDiffuseGiPassKernels())
        shaders.push_back({kernel.Name, Compute, std::move(kernel.Source), {}});
    // Its OOT3D adapter kernel (pass inputs from the renderer's guides).
    shaders.push_back({"oot3d_diffuse_gi_inputs.comp", Compute, BuildDiffuseGiAdapterShader(), {}});
    // Contact shadows, one kernel per length (Godot's READ_COUNT).
    constexpr const char* kContactShadowNames[kContactShadowLengthCount] = {
        "oot3d_contact_shadows_0.comp", "oot3d_contact_shadows_1.comp", "oot3d_contact_shadows_2.comp"};
    for (uint32_t length = 0; length < kContactShadowLengthCount; ++length)
        shaders.push_back({kContactShadowNames[length], Compute, ContactShadowComputeShader(length), {}});
    return shaders;
}
}
