#include "fast/oot3d/smaa_1x.h"

#include "fast/oot3d/smaa_official_shader_source.h"

#include <string_view>

namespace Fast::Oot3d {
namespace {

void ReplaceAll(
    std::string& value, std::string_view from,
    std::string_view to) {
    size_t offset = 0U;
    while ((offset = value.find(from, offset)) !=
           std::string::npos) {
        value.replace(offset, from.size(), to);
        offset += to.size();
    }
}

std::string CommonSource() {
    std::string source = R"glsl(#version 450
layout(push_constant) uniform SmaaState {
    uvec2 extent;
    vec2 inverse_extent;
} smaa_state;
layout(set=0,binding=4) uniform sampler smaa_linear_sampler;
layout(set=0,binding=5) uniform sampler smaa_point_sampler;
#define SMAA_GLSL_4 1
#define SMAA_PRESET_HIGH 1
#define SMAA_RT_METRICS vec4( \
    smaa_state.inverse_extent, vec2(smaa_state.extent))
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1
)glsl";
    for (const std::string_view chunk :
         kOfficialSmaaShaderSourceChunks) {
        source.append(chunk);
    }
    // The official edge functions use fragment-stage discard as a stencil
    // optimization. A compute pass has no covered fragment to discard, so
    // return the exact zero-edge value instead.
    ReplaceAll(
        source, "discard;",
        "return float2(0.0, 0.0);");
    // Vulkan GLSL does not allow a temporary combined sampler to cross a
    // function boundary. Keep the official function structure, pass separate
    // texture objects, and combine them with the stage samplers exactly at
    // each sample site. Compute has no implicit derivatives, so every sample
    // remains explicitly on mip zero.
    ReplaceAll(
        source,
        "#define SMAATexture2D(tex) sampler2D tex",
        "#define SMAATexture2D(tex) texture2D tex");
    ReplaceAll(
        source,
        "#define SMAASampleLevelZero(tex, coord) "
        "textureLod(tex, coord, 0.0)",
        "#define SMAASampleLevelZero(tex, coord) "
        "textureLod(sampler2D(tex, smaa_linear_sampler), "
        "coord, 0.0)");
    ReplaceAll(
        source,
        "#define SMAASampleLevelZeroPoint(tex, coord) "
        "textureLod(tex, coord, 0.0)",
        "#define SMAASampleLevelZeroPoint(tex, coord) "
        "textureLod(sampler2D(tex, smaa_point_sampler), "
        "coord, 0.0)");
    ReplaceAll(
        source,
        "#define SMAASampleLevelZeroOffset(tex, coord, offset) "
        "textureLodOffset(tex, coord, 0.0, offset)",
        "#define SMAASampleLevelZeroOffset(tex, coord, offset) "
        "textureLodOffset("
        "sampler2D(tex, smaa_linear_sampler), "
        "coord, 0.0, offset)");
    ReplaceAll(
        source,
        "#define SMAASample(tex, coord) texture(tex, coord)",
        "#define SMAASample(tex, coord) "
        "textureLod(sampler2D(tex, smaa_linear_sampler), "
        "coord, 0.0)");
    ReplaceAll(
        source,
        "#define SMAASamplePoint(tex, coord) texture(tex, coord)",
        "#define SMAASamplePoint(tex, coord) "
        "textureLod(sampler2D(tex, smaa_point_sampler), "
        "coord, 0.0)");
    ReplaceAll(
        source,
        "#define SMAASampleOffset(tex, coord, offset) "
        "texture(tex, coord, offset)",
        "#define SMAASampleOffset(tex, coord, offset) "
        "textureLodOffset("
        "sampler2D(tex, smaa_linear_sampler), "
        "coord, 0.0, offset)");
    ReplaceAll(
        source,
        "#define SMAAGather(tex, coord) textureGather(tex, coord)",
        "#define SMAAGather(tex, coord) "
        "textureGather(sampler2D(tex, smaa_linear_sampler), coord)");
    return source;
}

std::string EdgeShader() {
    std::string source = CommonSource();
    source += R"glsl(
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D smaa_color;
layout(set=0,binding=3,rgba8) uniform writeonly image2D smaa_edges;

void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(smaa_state.extent)))) return;
    vec2 uv=(vec2(pixel)+0.5)*smaa_state.inverse_extent;
    vec4 offset[3];
    SMAAEdgeDetectionVS(uv,offset);
    vec2 edges=SMAALumaEdgeDetectionPS(
        uv,offset,smaa_color);
    imageStore(smaa_edges,pixel,vec4(edges,0.0,1.0));
}
)glsl";
    return source;
}

std::string WeightShader() {
    std::string source = CommonSource();
    source += R"glsl(
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D smaa_edges;
layout(set=0,binding=1) uniform texture2D smaa_area;
layout(set=0,binding=2) uniform texture2D smaa_search;
layout(set=0,binding=3,rgba8) uniform writeonly image2D smaa_weights;

void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(smaa_state.extent)))) return;
    vec2 uv=(vec2(pixel)+0.5)*smaa_state.inverse_extent;
    vec2 pixcoord;
    vec4 offset[3];
    SMAABlendingWeightCalculationVS(uv,pixcoord,offset);
    vec4 weights=SMAABlendingWeightCalculationPS(
        uv,pixcoord,offset,
        smaa_edges,smaa_area,smaa_search,
        vec4(0.0));
    imageStore(smaa_weights,pixel,weights);
}
)glsl";
    return source;
}

std::string NeighborhoodShader() {
    std::string source = CommonSource();
    source += R"glsl(
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D smaa_color;
layout(set=0,binding=1) uniform texture2D smaa_weights;
layout(set=0,binding=3,rgba16f) uniform writeonly image2D smaa_output;

void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(smaa_state.extent)))) return;
    vec2 uv=(vec2(pixel)+0.5)*smaa_state.inverse_extent;
    vec4 offset;
    SMAANeighborhoodBlendingVS(uv,offset);
    vec4 color=SMAANeighborhoodBlendingPS(
        uv,offset,smaa_color,smaa_weights);
    imageStore(smaa_output,pixel,color);
}
)glsl";
    return source;
}

} // namespace

std::string BuildSmaa1xComputeShader(Smaa1xStage stage) {
    switch (stage) {
        case Smaa1xStage::EdgeDetection:
            return EdgeShader();
        case Smaa1xStage::BlendWeightCalculation:
            return WeightShader();
        case Smaa1xStage::NeighborhoodBlending:
            return NeighborhoodShader();
    }
    return {};
}

} // namespace Fast::Oot3d
