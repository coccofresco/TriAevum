#include "fast/oot3d/upscaler_orientation.h"

namespace Fast::Oot3d {

// The texel mappings below are UprightToSurfaceTexel / SurfaceToUprightTexel
// and the motion turn is UprightOffset; the tests hold the three together.

std::string BuildUpscalerUprightInputShader() {
    return R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D source_color;
layout(set=0,binding=1) uniform texture2D source_depth;
layout(set=0,binding=2) uniform texture2D source_motion;
layout(set=0,binding=3) uniform texture2D source_reactive;
layout(set=0,binding=4,rgba16f) uniform writeonly image2D color_output;
layout(set=0,binding=5,r32f) uniform writeonly image2D depth_output;
layout(set=0,binding=6,rgba16f) uniform writeonly image2D motion_output;
layout(set=0,binding=7,r16f) uniform writeonly image2D reactive_output;
layout(set=0,binding=8) uniform sampler point_sampler;
layout(push_constant) uniform OrientationState {
    uvec2 extent;
    uvec2 source_extent;
    uint flip_y;
} pc;
void main(){
    uvec2 upright=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(upright,pc.extent))) return;
    ivec2 surface=ivec2(pc.source_extent.x-1u-upright.y,
                        pc.flip_y!=0u ? pc.source_extent.y-1u-upright.x : upright.x);
    vec4 motion=texelFetch(sampler2D(source_motion,point_sampler),surface,0);
    motion.xy=vec2(pc.flip_y!=0u ? -motion.y : motion.y,-motion.x);
    imageStore(color_output,ivec2(upright),texelFetch(sampler2D(source_color,point_sampler),surface,0));
    imageStore(depth_output,ivec2(upright),vec4(texelFetch(sampler2D(source_depth,point_sampler),surface,0).r,0.0,0.0,1.0));
    imageStore(motion_output,ivec2(upright),motion);
    imageStore(reactive_output,ivec2(upright),vec4(texelFetch(sampler2D(source_reactive,point_sampler),surface,0).r,0.0,0.0,1.0));
}
)glsl";
}

std::string BuildUpscalerUprightOutputShader() {
    return R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D upright_color;
layout(set=0,binding=1,rgba16f) uniform writeonly image2D surface_output;
layout(set=0,binding=2) uniform sampler point_sampler;
layout(push_constant) uniform OrientationState {
    uvec2 extent;
    uvec2 source_extent;
    uint flip_y;
} pc;
void main(){
    uvec2 surface=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(surface,pc.extent))) return;
    ivec2 upright=ivec2(pc.flip_y!=0u ? pc.extent.y-1u-surface.y : surface.y, pc.extent.x-1u-surface.x);
    imageStore(surface_output,ivec2(surface),texelFetch(sampler2D(upright_color,point_sampler),upright,0));
}
)glsl";
}

} // namespace Fast::Oot3d
