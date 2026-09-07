#include "fast/oot3d/linear_scene_color.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {

LinearSceneColorPolicy BuildLinearSceneColorPolicy(
    SceneColorEncoding source) {
    switch (source) {
        case SceneColorEncoding::Linear:
            return {source, false, true};
        case SceneColorEncoding::Srgb:
            return {source, true, true};
        case SceneColorEncoding::Unknown:
            return {};
    }
    return {};
}

float SrgbToLinear(float value) {
    value = std::max(value, 0.0F);
    return value <= 0.04045F
        ? value / 12.92F
        : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

float LinearToSrgb(float value) {
    value = std::max(value, 0.0F);
    return value <= 0.0031308F
        ? value * 12.92F
        : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

std::string BuildLinearSceneColorComputeShader() {
    return R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D pica_scene_color;
layout(set=0,binding=1,rgba16f) uniform writeonly image2D linear_scene_color;
layout(set=0,binding=2) uniform sampler scene_sampler;
layout(push_constant) uniform LinearSceneColorState {
    uvec2 extent;
    uint decode_srgb;
    uint reserved;
} state;

vec3 oot3d_srgb_to_linear(vec3 value) {
    value=max(value,vec3(0.0));
    bvec3 cutoff=lessThanEqual(value,vec3(0.04045));
    vec3 lower=value/12.92;
    vec3 higher=pow((value+0.055)/1.055,vec3(2.4));
    return mix(higher,lower,cutoff);
}

void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(state.extent)))) return;
    vec2 uv=(vec2(pixel)+0.5)/vec2(state.extent);
    vec4 color=texture(sampler2D(pica_scene_color,scene_sampler),uv);
    if(state.decode_srgb!=0u)
        color.rgb=oot3d_srgb_to_linear(color.rgb);
    imageStore(linear_scene_color,pixel,color);
}
)glsl";
}

} // namespace Fast::Oot3d
