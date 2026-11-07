#include "fast/extension/native_ui_overlay.h"

#include <cmath>

namespace Fast::Extension {

NativeUiOverlayTap ResolveNativeUiOverlayTap(
    uint32_t pixel, uint32_t extent, uint32_t displayExtent) noexcept {
    if (extent == 0U || displayExtent == 0U) return {};
    const float position =
        (float(pixel) + 0.5F) * float(displayExtent) / float(extent) - 0.5F;
    const float first = std::floor(position);
    return {int32_t(first), position - first};
}

std::array<uint32_t, 2> ResolveNativeUiOverlayExtent(
    bool preserveSampling, uint32_t upscaledWidth, uint32_t upscaledHeight,
    float presentationWidth, float presentationHeight) noexcept {
    if (!preserveSampling || !std::isfinite(presentationWidth) || !std::isfinite(presentationHeight) ||
        presentationWidth < 1.0F || presentationHeight < 1.0F ||
        presentationWidth > 65536.0F || presentationHeight > 65536.0F ||
        presentationWidth != std::floor(presentationWidth) || presentationHeight != std::floor(presentationHeight))
        return {upscaledWidth, upscaledHeight};
    return {uint32_t(presentationWidth), uint32_t(presentationHeight)};
}

std::string BuildNativeUiOverlayShader() {
    return R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D display_ui;
layout(set=0,binding=1) uniform texture2D display_scene;
layout(set=0,binding=2) uniform texture2D upscaled;
layout(set=0,binding=3,rgba16f) uniform writeonly image2D composed;
layout(set=0,binding=4) uniform sampler point_sampler;
layout(push_constant) uniform OverlayState {
    uvec2 extent;
    uvec2 display_extent;
    uint linear_output;
    uint resample_upscaled;
    uint reserved1;
    uint reserved2;
} pc;

vec3 oot3d_srgb_to_linear(vec3 value) {
    value=max(value,vec3(0.0));
    bvec3 cutoff=lessThanEqual(value,vec3(0.04045));
    vec3 lower=value/12.92;
    vec3 higher=pow((value+0.055)/1.055,vec3(2.4));
    return mix(higher,lower,cutoff);
}

void main(){
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(pc.extent)))) return;
    vec4 color;
    if(pc.resample_upscaled!=0u){
        ivec2 size=textureSize(sampler2D(upscaled,point_sampler),0);
        vec2 source=(vec2(pixel)+0.5)*vec2(size)/vec2(pc.extent)-0.5;
        ivec2 first=ivec2(floor(source));
        vec2 weight=fract(source);
        color=vec4(0.0);
        for(int y=0;y<2;++y) for(int x=0;x<2;++x){
            float w=(x==0 ? 1.0-weight.x : weight.x)*(y==0 ? 1.0-weight.y : weight.y);
            color+=w*texelFetch(sampler2D(upscaled,point_sampler),clamp(first+ivec2(x,y),ivec2(0),size-1),0);
        }
    }else{
        // Preserve the original path and its exact texel fetch by default.
        color=texelFetch(sampler2D(upscaled,point_sampler),pixel,0);
    }
    // The bilinear footprint of this texel in the display
    // (ResolveNativeUiOverlayTap): its texels the UI changed carry the UI.
    vec2 position=(vec2(pixel)+0.5)*vec2(pc.display_extent)/vec2(pc.extent)-0.5;
    vec2 first=floor(position);
    vec2 fraction=position-first;
    ivec2 limit=ivec2(pc.display_extent)-1;
    vec3 ui=vec3(0.0);
    float coverage=0.0;
    for(int y=0;y<2;++y){
        for(int x=0;x<2;++x){
            ivec2 texel=clamp(ivec2(first)+ivec2(x,y),ivec2(0),limit);
            vec3 shown=texelFetch(sampler2D(display_ui,point_sampler),texel,0).rgb;
            vec3 scene=texelFetch(sampler2D(display_scene,point_sampler),texel,0).rgb;
            if(any(notEqual(shown,scene))){
                float weight=(x==0 ? 1.0-fraction.x : fraction.x)*(y==0 ? 1.0-fraction.y : fraction.y);
                ui+=weight*(pc.linear_output!=0u && pc.resample_upscaled==0u ? oot3d_srgb_to_linear(shown) : shown);
                coverage+=weight;
            }
        }
    }
    // Where the UI changed nothing the upscaled scene is kept as it is.
    if(coverage>0.0){
        if(pc.resample_upscaled!=0u && pc.linear_output!=0u)
            ui=coverage*oot3d_srgb_to_linear(ui/coverage);
        color.rgb=mix(color.rgb,ui/coverage,coverage);
    }
    imageStore(composed,pixel,color);
}
)glsl";
}

} // namespace Fast::Extension
