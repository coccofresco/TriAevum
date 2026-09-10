#include "fast/oot3d/builtin_pass_shaders.h"
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

namespace Fast::Oot3d {
std::vector<BuiltinPassShader> BuildBuiltinPassShaders() {
    using enum Renderer::SpirvStage;
    return {
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
        {"oot3d_scene_composite.comp", Compute, BuildSceneCompositeComputeShader(), {}},
        {"oot3d_smaa_edges.comp", Compute, BuildSmaa1xComputeShader(Smaa1xStage::EdgeDetection), {}},
        {"oot3d_smaa_blend_weights.comp", Compute, BuildSmaa1xComputeShader(Smaa1xStage::BlendWeightCalculation), {}},
        {"oot3d_smaa_neighborhood.comp", Compute, BuildSmaa1xComputeShader(Smaa1xStage::NeighborhoodBlending), {}},
        {"oot3d_nri_pica_scanout.vert", Vertex, BuildPicaScanoutVertexShader(), {}},
        {"oot3d_nri_pica_scanout.frag", Fragment, BuildPicaScanoutFragmentShader(true, 0), {}},
        {"interactive_grass.vert", Vertex, BuildGrassVertexShader(), {}},
        {"interactive_grass_canonical.frag", Fragment, BuildGrassFragmentShader(), {{"GRASS_AUXILIARY_OUTPUTS", "0"}}},
        {"interactive_grass_instrumented.frag", Fragment, BuildGrassFragmentShader(), {{"GRASS_AUXILIARY_OUTPUTS", "1"}}},
        {"grass_instance_compactor.comp", Compute, BuildGrassCompactionComputeShader(), {}},
        {"normal_space.comp", Compute, kNormalSpaceComputeShader, {}},
        {"oot3d_nri_directional_shadow.frag", Fragment, kPicaDirectionalShadowFragmentShader, {}},
    };
}
}
