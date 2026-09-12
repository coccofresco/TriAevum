#pragma once

namespace Fast::Oot3d {
inline constexpr const char* kNormalSpaceComputeShader = R"glsl(#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform texture2D input_normal;
layout(set=0,binding=1,rgba16f) uniform writeonly image2D output_normal;
layout(push_constant) uniform Transform { mat4 normal_to_output; } pc;
void main() {
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(p,imageSize(output_normal)))) return;
    vec4 guide=texelFetch(input_normal,p,0);
    vec3 n=guide.xyz*2.0-1.0;
    n=normalize(mat3(pc.normal_to_output)*n);
    imageStore(output_normal,p,vec4(n*0.5+0.5,guide.a));
}
)glsl";
}
