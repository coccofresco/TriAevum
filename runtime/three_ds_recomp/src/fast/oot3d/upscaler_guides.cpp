#include "fast/oot3d/upscaler_guides.h"

#include <algorithm>

namespace Fast::Oot3d {

UpscalerGuideSpan ResolveUpscalerGuideSpan(
    uint32_t pixel, uint32_t extent, uint32_t sourceExtent) noexcept {
    if (extent == 0U || sourceExtent == 0U) return {};
    const uint64_t first = uint64_t(pixel) * sourceExtent / extent;
    const uint64_t end = (uint64_t(pixel) + 1U) * sourceExtent + extent - 1U;
    uint64_t last = std::max<uint64_t>(end / extent, first + 1U) - 1U;
    last = std::min<uint64_t>({last, first + kUpscalerGuideMaxSpan - 1U, sourceExtent - 1U});
    return {uint32_t(std::min<uint64_t>(first, sourceExtent - 1U)), uint32_t(last)};
}

bool UpscalerGuidesNeedResample(uint32_t colorWidth, uint32_t colorHeight,
                                uint32_t guideWidth, uint32_t guideHeight) noexcept {
    return colorWidth != guideWidth || colorHeight != guideHeight;
}

std::array<float, 2> UpscalerJitterInInputPixels(
    const std::array<float, 2>& targetPixels,
    uint32_t targetWidth, uint32_t targetHeight,
    uint32_t inputWidth, uint32_t inputHeight) noexcept {
    if (targetWidth == 0U || targetHeight == 0U) return targetPixels;
    return {targetPixels[0] * float(inputWidth) / float(targetWidth),
            targetPixels[1] * float(inputHeight) / float(targetHeight)};
}

std::array<float, 2> UpscalerMotionJitterCorrection(
    const std::array<float, 2>& currentJitterUv, const std::array<float, 2>& previousJitterUv) noexcept {
    return {currentJitterUv[0] - previousJitterUv[0], currentJitterUv[1] - previousJitterUv[1]};
}

std::array<float, 4> UpscalerMotionWithoutJitter(
    const std::array<float, 4>& motion, const std::array<float, 2>& correction) noexcept {
    if (motion[2] >= 0.5F) return motion;
    return {motion[0] + correction[0], motion[1] + correction[1], motion[2], motion[3]};
}

std::string BuildUpscalerGuideResampleShader() {
    return R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D source_depth;
layout(set=0,binding=1) uniform texture2D source_motion;
layout(set=0,binding=2) uniform texture2D source_reactive;
layout(set=0,binding=3,r32f) uniform writeonly image2D depth_output;
layout(set=0,binding=4,rgba16f) uniform writeonly image2D motion_output;
layout(set=0,binding=5,r16f) uniform writeonly image2D reactive_output;
layout(set=0,binding=6) uniform sampler point_sampler;
layout(push_constant) uniform GuideState {
    uvec2 extent;
    uvec2 source_extent;
    vec2 motion_correction;
} pc;
const uint max_span=4u;
void main(){
    uvec2 pixel=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(pixel,pc.extent))) return;
    // The source texels this output texel covers (ResolveUpscalerGuideSpan).
    uvec2 first=min(pixel*pc.source_extent/pc.extent,pc.source_extent-1u);
    uvec2 last=max(((pixel+1u)*pc.source_extent+pc.extent-1u)/pc.extent,first+1u)-1u;
    last=min(min(last,first+max_span-1u),pc.source_extent-1u);
    // The nearest sample keeps its depth and motion, so a silhouette keeps
    // the motion of the surface in front; reactive coverage is the largest.
    ivec2 nearest=ivec2(first);
    float depth=texelFetch(sampler2D(source_depth,point_sampler),nearest,0).r;
    float reactive=0.0;
    for(uint y=first.y;y<=last.y;++y){
        for(uint x=first.x;x<=last.x;++x){
            ivec2 texel=ivec2(x,y);
            float candidate=texelFetch(sampler2D(source_depth,point_sampler),texel,0).r;
            if(candidate<depth){ depth=candidate; nearest=texel; }
            reactive=max(reactive,texelFetch(sampler2D(source_reactive,point_sampler),texel,0).r);
        }
    }
    imageStore(depth_output,ivec2(pixel),vec4(depth,0.0,0.0,1.0));
    // Motion without the jitter (UpscalerMotionWithoutJitter).
    vec4 motion=texelFetch(sampler2D(source_motion,point_sampler),nearest,0);
    if(motion.z<0.5) motion.xy+=pc.motion_correction;
    imageStore(motion_output,ivec2(pixel),motion);
    imageStore(reactive_output,ivec2(pixel),vec4(reactive,0.0,0.0,1.0));
}
)glsl";
}

} // namespace Fast::Oot3d
