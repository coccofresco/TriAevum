#pragma once

#include <string_view>

namespace Fast::Extension {

// Linear direct radiance, depth and world normals of one presented sample
// for the GI dispatch (renderer-owned; catalogued with the built-in passes).
inline constexpr std::string_view kBrixelizerGiPrepareShader = R"glsl(#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform texture2D surface_guide;
layout(set=0,binding=1) uniform texture2D native_depth;
layout(set=0,binding=2) uniform texture2D normal_guide;
layout(set=0,binding=3,rgba16f) uniform writeonly image2D lit_output;
layout(set=0,binding=4,r32f) uniform writeonly image2D depth_output;
layout(set=0,binding=5,rgba16f) uniform writeonly image2D normal_output;
layout(push_constant) uniform Prepare { mat4 view_to_world; } pc;
vec3 srgb_to_linear(vec3 value) {
    value=max(value,vec3(0.0));
    return mix(pow((value+0.055)/1.055,vec3(2.4)),value/12.92,lessThanEqual(value,vec3(0.04045)));
}
void main() {
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);
    ivec2 extent=imageSize(lit_output);
    if(any(greaterThanEqual(p,extent))) return;
    vec3 packed_bits=floor(texelFetch(surface_guide,p,0).rgb*65535.0+0.5);
    imageStore(lit_output,p,vec4(srgb_to_linear(mod(packed_bits,256.0)/255.0),1.0));
    imageStore(depth_output,p,vec4(texelFetch(native_depth,p,0).r));
    vec3 n=texelFetch(normal_guide,p,0).xyz*2.0-1.0;
    n=normalize(mat3(pc.view_to_world)*n);
    imageStore(normal_output,p,vec4(n*0.5+0.5,1.0));
}
)glsl";

} // namespace Fast::Extension
