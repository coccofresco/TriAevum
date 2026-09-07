#include "fast/oot3d/motion_vectors.h"
#include "fast/oot3d/pica_surface_coordinates.h"

#include <cmath>

namespace Fast::Oot3d {
namespace {

std::array<float, 4> Multiply(const std::array<float, 16>& matrix,
                              const std::array<float, 4>& value) {
    std::array<float, 4> result{};
    for (size_t row = 0; row < 4; ++row)
        for (size_t column = 0; column < 4; ++column)
            result[row] += matrix[column * 4 + row] * value[column];
    return result;
}

} // namespace

CameraMotionSample ComputeCameraMotionUv(
    const std::array<float, 2>& currentUv, float linearViewDepth,
    const std::array<float, 16>& inverseCurrentWorldToClip,
    const std::array<float, 16>& previousWorldToClip,
    const std::array<float, 3>& eye, const std::array<float, 3>& forward,
    bool historyValid) {
    CameraMotionSample result{};
    if (!historyValid || !std::isfinite(linearViewDepth) ||
        linearViewDepth <= 0.0F) return result;
    auto farWorld = Multiply(inverseCurrentWorldToClip,
        {currentUv[0] * 2.0F - 1.0F,
         currentUv[1] * 2.0F - 1.0F, 1.0F, 1.0F});
    if (std::abs(farWorld[3]) < 1.0e-7F) return result;
    for (size_t i = 0; i < 3; ++i) farWorld[i] /= farWorld[3];
    std::array<float, 3> ray{};
    float forwardProjection = 0.0F;
    for (size_t i = 0; i < 3; ++i) {
        ray[i] = farWorld[i] - eye[i];
        forwardProjection += ray[i] * forward[i];
    }
    if (forwardProjection <= 1.0e-7F) return result;
    std::array<float, 4> world{eye[0], eye[1], eye[2], 1.0F};
    for (size_t i = 0; i < 3; ++i)
        world[i] += ray[i] * linearViewDepth / forwardProjection;
    const auto previous = Multiply(previousWorldToClip, world);
    if (previous[3] <= 1.0e-7F) return result;
    const std::array<float, 2> previousUv{
        previous[0] / previous[3] * 0.5F + 0.5F,
        previous[1] / previous[3] * 0.5F + 0.5F};
    result.Disoccluded = previousUv[0] < 0.0F || previousUv[0] > 1.0F ||
                         previousUv[1] < 0.0F || previousUv[1] > 1.0F;
    if (!result.Disoccluded) {
        result.MotionUv = {previousUv[0] - currentUv[0],
                           previousUv[1] - currentUv[1]};
    }
    return result;
}

std::string BuildCameraMotionComputeShader() {
    return R"glsl(#version 450
layout(local_size_x=8, local_size_y=8, local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D source_depth;
layout(set=0,binding=1) uniform texture2D material_guide;
layout(set=0,binding=2,rgba16f) uniform writeonly image2D motion_output;
layout(set=0,binding=5,r16f) uniform writeonly image2D reactive_output;
layout(set=0,binding=3,std140) uniform MotionState {
    mat4 inverse_current_world_to_clip;
    mat4 previous_world_to_clip;
    vec4 eye;
    vec4 forward;
    vec4 depth_state; // near, far, convention, history valid
    vec4 jitter_state; // current uv, previous uv
    uvec4 extent;
} state;
layout(set=0,binding=4) uniform texture2D rigid_motion_guide;
layout(set=0,binding=6) uniform sampler source_sampler;
)glsl" + std::string(EffectSurfaceCoordinateShaderLibrary) + R"glsl(

float linearize_depth(float sample_value) {
    float d=clamp(sample_value,0.0,1.0);
    float n=state.depth_state.x, f=state.depth_state.y, range=f-n;
    uint convention=uint(state.depth_state.z+0.5);
    if(convention==0u) return n*f/max(f-d*range,1.17549435e-38);
    if(convention==1u) return n*f/max(n+d*range,1.17549435e-38);
    return convention==2u ? n+d*range : f-d*range;
}

void main(){
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(state.extent.xy)))) return;
    vec2 uv=(vec2(pixel)+0.5)/vec2(state.extent.xy);
    vec2 unjittered_uv=uv-state.jitter_state.xy;
    float raw=texelFetch(sampler2D(source_depth,source_sampler),pixel,0).r;
    bool valid=state.depth_state.w>0.5 && raw<0.999999;
    vec2 motion=vec2(0.0); float disocclusion=1.0;
    if(valid){
        vec4 far_world=state.inverse_current_world_to_clip*
            vec4(oot3d_surface_view_ndc(unjittered_uv,state.extent.z),1.0,1.0);
        float safe_w=abs(far_world.w)>1.0e-7 ? far_world.w : 1.0e-7;
        far_world/=safe_w;
        vec3 ray=far_world.xyz-state.eye.xyz;
        float projected=dot(ray,state.forward.xyz);
        if(projected>1.0e-7){
            vec3 world=state.eye.xyz+ray*(linearize_depth(raw)/projected);
            vec4 previous=state.previous_world_to_clip*vec4(world,1.0);
            if(previous.w>1.0e-7){
                vec2 previous_uv=oot3d_view_ndc_surface_uv(previous.xy/previous.w,state.extent.z)+
                    state.jitter_state.zw;
                bool inside=all(greaterThanEqual(previous_uv,vec2(0.0))) &&
                            all(lessThanEqual(previous_uv,vec2(1.0)));
                if(inside){ motion=previous_uv-uv; disocclusion=0.0; }
            }
        }
    }
    // Reactive is explicit draw-plan metadata carried by material-guide alpha;
    // it is never inferred from texture colour.
    float reactive=texelFetch(sampler2D(material_guide,source_sampler),pixel,0).a;
    vec4 rigid=texelFetch(sampler2D(rigid_motion_guide,source_sampler),pixel,0);
    if(rigid.b>0.5){ motion=rigid.rg; disocclusion=0.0; }
    imageStore(motion_output,pixel,vec4(motion,disocclusion,reactive));
    imageStore(reactive_output,pixel,vec4(reactive,0.0,0.0,1.0));
}
)glsl";
}

} // namespace Fast::Oot3d
