#include "fast/oot3d/temporal_aa.h"
#include <algorithm>

namespace Fast::Oot3d {

float TemporalHistoryWeight(float configuredWeight, float disocclusion,
                            float reactive, bool historyValid) {
    if (!historyValid) return 0.0F;
    return std::clamp(configuredWeight, 0.0F, 0.98F) *
           (1.0F - std::clamp(disocclusion, 0.0F, 1.0F)) *
           (1.0F - std::clamp(reactive, 0.0F, 1.0F));
}

std::string BuildTemporalAaComputeShader() {
    return R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D current_color;
layout(set=0,binding=1) uniform texture2D motion_surface;
layout(set=0,binding=2) uniform texture2D previous_history;
layout(set=0,binding=3,rgba16f) uniform writeonly image2D output_history;
layout(set=0,binding=4) uniform sampler taa_sampler;
layout(push_constant) uniform TemporalPush {
    uvec2 extent;
    vec2 inverse_extent;
    float history_weight;
    float clamp_expansion;
    float sharpness;
    uint history_valid;
} pc;

void main(){
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(pc.extent)))) return;
    vec2 uv=(vec2(pixel)+0.5)*pc.inverse_extent;
    vec4 current=texelFetch(sampler2D(current_color,taa_sampler),pixel,0);
    vec3 minimum_color=current.rgb, maximum_color=current.rgb;
    vec3 average=vec3(0.0);
    float minimum_luma=dot(current.rgb,vec3(0.2126,0.7152,0.0722));
    float maximum_luma=minimum_luma;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x){
        ivec2 q=clamp(pixel+ivec2(x,y),ivec2(0),ivec2(pc.extent)-1);
        vec3 sample_color=texelFetch(sampler2D(current_color,taa_sampler),q,0).rgb;
        minimum_color=min(minimum_color,sample_color);
        maximum_color=max(maximum_color,sample_color);
        float sample_luma=dot(sample_color,vec3(0.2126,0.7152,0.0722));
        minimum_luma=min(minimum_luma,sample_luma);
        maximum_luma=max(maximum_luma,sample_luma);
        average+=sample_color/9.0;
    }
    float relative_luma_span=(maximum_luma-minimum_luma)/
        max(maximum_luma,0.08);
    float edge_support=smoothstep(0.025,0.125,relative_luma_span);
    vec4 motion=texelFetch(sampler2D(motion_surface,taa_sampler),pixel,0);
    vec2 history_uv=uv+motion.xy;
    bool inside=all(greaterThanEqual(history_uv,vec2(0.0))) &&
                all(lessThanEqual(history_uv,vec2(1.0)));
    vec3 history=texture(sampler2D(previous_history,taa_sampler),
        clamp(history_uv,vec2(0.0),vec2(1.0))).rgb;
    vec3 span=(maximum_color-minimum_color)*max(pc.clamp_expansion,0.0);
    history=clamp(history,minimum_color-span,maximum_color+span);
    float weight=pc.history_valid!=0u && inside
        ? clamp(pc.history_weight,0.0,0.98)*(1.0-clamp(motion.b,0.0,1.0))*
          (1.0-clamp(motion.a,0.0,1.0))*edge_support : 0.0;
    vec3 resolved=mix(current.rgb,history,weight);
    resolved+=clamp(pc.sharpness,0.0,1.0)*edge_support*(resolved-average);
    imageStore(output_history,pixel,vec4(max(resolved,vec3(0.0)),current.a));
}
)glsl";
}

} // namespace Fast::Oot3d
