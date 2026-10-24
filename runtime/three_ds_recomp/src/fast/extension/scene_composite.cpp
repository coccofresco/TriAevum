#include "fast/extension/scene_composite.h"

#include "fast/extension/ambient_occlusion_composite.h"
#include "fast/extension/reflection_debug.h"
#include "fast/extension/toon_outline_shader.h"

namespace Fast::Extension {

std::string BuildSceneCompositeComputeShader() {
    std::string source = R"glsl(#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D scene_color;
layout(set=0,binding=1) uniform texture2D ao_visibility;
layout(set=0,binding=2) uniform texture2D filtered_reflection;
layout(set=0,binding=3) uniform texture2D material_guide;
layout(set=0,binding=4) uniform texture2D normal_guide;
layout(set=0,binding=5) uniform texture2D depth_guide;
layout(set=0,binding=6) uniform texture2D ambient_guide;
layout(set=0,binding=7,rgba16f) uniform writeonly image2D composite_output;
layout(set=0,binding=8) uniform sampler composite_sampler;
layout(set=0,binding=9) uniform texture2D transparent_depth_guide;
layout(set=0,binding=10) uniform texture2D fog_guide;
layout(set=0,binding=11) uniform utexture2D outline_geometry_guide;
layout(set=0,binding=12) uniform texture2D diffuse_illumination;
layout(set=0,binding=13) uniform texture2D diffuse_albedo_guide;
layout(push_constant) uniform CompositeState {
    uvec2 extent;
    uint ambient_occlusion;
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
    uint global_illumination;
    float diffuse_intensity;
    uint global_illumination_debug;
    uint ambient_occlusion_debug;
} state;

vec3 oot3d_gi_srgb_to_linear(vec3 value) {
    value=max(value,vec3(0.0));
    return mix(pow((value+0.055)/1.055,vec3(2.4)),value/12.92,lessThanEqual(value,vec3(0.04045)));
}
vec3 oot3d_gi_linear_to_srgb(vec3 value) {
    value=max(value,vec3(0.0));
    return mix(1.055*pow(value,vec3(1.0/2.4))-0.055,value*12.92,lessThanEqual(value,vec3(0.0031308)));
}
)glsl";
    source += AmbientOcclusionCompositeShaderLibrary();
    source += AmbientOcclusionDebugShaderLibrary();
    source += ReflectionDebugShaderLibrary();
    source += ToonOutlineComputeShaderLibrary();
    source += R"glsl(

void main(){
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(state.extent)))) return;
    vec2 uv=(vec2(pixel)+0.5)/vec2(state.extent);
    vec4 color=texture(sampler2D(scene_color,composite_sampler),uv);
    if(state.ambient_occlusion!=0u) {
        float ao_value=texture(
            sampler2D(ao_visibility,composite_sampler),uv).r;
        float scene_coverage=texture(
            sampler2D(ambient_guide,composite_sampler),uv).a;
        color.rgb*=oot3d_scene_ambient_occlusion_visibility(
            ao_value,scene_coverage);
    }
    // Diffuse GI: the direct light of the native scene directionals, the
    // added key and the dynamic lights, bounced through the scene distance
    // field. Ambient and fill are excluded at the source (OoT bakes them into
    // the native colour), so the received irradiance is added:
    // albedo x irradiance in linear light where the guide is valid
    // (coverage: zero on clear, HUD and replacing surfaces, the transmission
    // under overlays), attenuated by AO. The guide packs the sRGB albedo
    // (high bytes) and the sRGB direct radiance that seeded the trace (low).
    if(state.global_illumination!=0u) {
        ivec2 guide_size=textureSize(sampler2D(diffuse_albedo_guide,composite_sampler),0);
        vec4 guide=texelFetch(sampler2D(diffuse_albedo_guide,composite_sampler),
            clamp(ivec2(uv*vec2(guide_size)),ivec2(0),guide_size-1),0);
        vec3 packed_bits=floor(guide.rgb*65535.0+0.5);
        vec3 albedo=floor(packed_bits/256.0)/255.0;
        vec3 direct=mod(packed_bits,256.0)/255.0;
        vec3 irradiance=max(texture(sampler2D(diffuse_illumination,composite_sampler),uv).rgb,vec3(0.0));
        float coverage=clamp(guide.a,0.0,1.0);
        float visibility=state.ambient_occlusion!=0u
            ? clamp(texture(sampler2D(ao_visibility,composite_sampler),uv).r,0.0,1.0) : 1.0;
        vec3 bounce=coverage*visibility*state.diffuse_intensity*oot3d_gi_srgb_to_linear(albedo)*irradiance;
        color.rgb=state.input_linear!=0u ? color.rgb+bounce
            : oot3d_gi_linear_to_srgb(oot3d_gi_srgb_to_linear(color.rgb)+bounce);
        if(state.global_illumination_debug!=0u) {
            bool encoded=state.global_illumination_debug==2u || state.global_illumination_debug==5u;
            vec3 view=state.global_illumination_debug==1u ? irradiance
                : state.global_illumination_debug==2u ? albedo
                : state.global_illumination_debug==3u ? vec3(coverage)
                : state.global_illumination_debug==4u ? bounce
                : direct;
            color=vec4(state.input_linear!=0u ? (encoded ? oot3d_gi_srgb_to_linear(view) : view)
                : (encoded ? view : oot3d_gi_linear_to_srgb(view)),1.0);
        }
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
    // AO debug view: the provider's output itself, texel for texel (packed
    // views must not be filtered), as display values.
    if(state.ambient_occlusion!=0u && state.ambient_occlusion_debug!=0u) {
        ivec2 ao_size=textureSize(sampler2D(ao_visibility,composite_sampler),0);
        float value=texelFetch(sampler2D(ao_visibility,composite_sampler),
            clamp(ivec2(uv*vec2(ao_size)),ivec2(0),ao_size-1),0).r;
        vec3 view=oot3d_ambient_occlusion_debug_color(state.ambient_occlusion_debug,value);
        color=vec4(state.input_linear!=0u ? oot3d_gi_srgb_to_linear(view) : view,1.0);
    }
    imageStore(composite_output,pixel,color);
}
)glsl";
    return source;
}

} // namespace Fast::Extension
