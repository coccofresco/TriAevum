#include "fast/oot3d/scene_composite.h"

#include "fast/oot3d/ambient_occlusion_composite.h"
#include "fast/oot3d/reflection_debug.h"
#include "fast/oot3d/toon_outline_shader.h"

namespace Fast::Oot3d {

std::string BuildSceneCompositeComputeShader() {
    std::string source = R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D scene_color;
layout(set=0,binding=1) uniform texture2D cacao_visibility;
layout(set=0,binding=2) uniform texture2D filtered_reflection;
layout(set=0,binding=3) uniform texture2D material_guide;
layout(set=0,binding=4) uniform texture2D normal_guide;
layout(set=0,binding=5) uniform texture2D depth_guide;
layout(set=0,binding=6) uniform texture2D ambient_guide;
layout(set=0,binding=7,rgba16f) uniform writeonly image2D composite_output;
layout(set=0,binding=8) uniform sampler composite_sampler;
layout(set=0,binding=9) uniform texture2D transparent_depth_guide;
layout(set=0,binding=10) uniform texture2D fog_guide;
layout(set=0,binding=11) uniform texture2D outline_geometry_guide;
layout(push_constant) uniform CompositeState {
    uvec2 extent;
    uint cacao;
    uint reflections;
    float reflection_strength;
    uint reflection_debug;
    uint outline;
    uint input_linear;
    vec2 inverse_size;
    float outline_width;
    float outline_depth_sensitivity;
    float outline_normal_sensitivity;
    float outline_softness;
    vec3 outline_color;
    float outline_opacity;
} state;
)glsl";
    source += AmbientOcclusionCompositeShaderLibrary();
    source += ReflectionDebugShaderLibrary();
    source += ToonOutlineComputeShaderLibrary();
    source += R"glsl(

void main(){
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(state.extent)))) return;
    vec2 uv=(vec2(pixel)+0.5)/vec2(state.extent);
    vec4 color=texture(sampler2D(scene_color,composite_sampler),uv);
    if(state.cacao!=0u) {
        float cacao_value=texture(
            sampler2D(cacao_visibility,composite_sampler),uv).r;
        float scene_coverage=texture(
            sampler2D(ambient_guide,composite_sampler),uv).a;
        color.rgb*=oot3d_scene_ambient_occlusion_visibility(
            cacao_value,scene_coverage);
    }
    if(state.reflections!=0u){
        vec4 reflection=texture(sampler2D(filtered_reflection,composite_sampler),uv);
        vec4 material=texture(sampler2D(material_guide,composite_sampler),uv);
        if(state.reflection_debug!=0u)
            color.rgb=oot3d_reflection_debug_color(
                state.reflection_debug,material,reflection);
        else
            color.rgb=mix(color.rgb,reflection.rgb,
                clamp(reflection.a*state.reflection_strength,0.0,1.0));
    }
    if(state.outline!=0u){
        float edge=oot3d_toon_outline_edge(
            uv,
            state.inverse_size,state.outline_width,
            state.outline_depth_sensitivity,
            state.outline_normal_sensitivity,state.outline_softness)*
            state.outline_opacity;
        vec4 fog=texture(sampler2D(fog_guide,composite_sampler),uv);
        vec3 nativeTint=mix(fog.rgb,state.outline_color,fog.a);
        vec3 tint=state.input_linear!=0u
            ? oot3d_outline_srgb_to_linear(nativeTint) : nativeTint;
        color.rgb=mix(color.rgb,tint,edge);
    }
    imageStore(composite_output,pixel,color);
}
)glsl";
    return source;
}

} // namespace Fast::Oot3d
