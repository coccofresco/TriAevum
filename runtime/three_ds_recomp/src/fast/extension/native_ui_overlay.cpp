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

NativeUiOverlayGrid ResolveNativeUiOverlayGrid(
    bool preserveSampling, uint32_t upscaledWidth, uint32_t upscaledHeight,
    float x, float y, float width, float height, bool flipY) noexcept {
    NativeUiOverlayGrid grid;
    grid.Extent = {upscaledWidth, upscaledHeight}; grid.Viewport = {x, y, width, height};
    if (!preserveSampling || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 ||
        std::abs(x) > 65536 || std::abs(y) > 65536 || width > 65536 || height > 65536)
        return grid;
    // Vulkan's top-left rule includes a centre on the leading edge and
    // excludes one on the trailing edge. Preserve precisely those pixels.
    const float left = std::ceil(x - 0.5F), top = std::ceil(y - 0.5F);
    const float right = std::ceil(x + width - 0.5F), bottom = std::ceil(y + height - 0.5F);
    grid.Direct = true;
    grid.Viewport = {left, top, right - left, bottom - top};
    grid.Extent = {uint32_t(bottom - top), uint32_t(right - left)};
    grid.SourceExtent = {height, width};
    grid.SourceOffset = {y + height - bottom, flipY ? x + width - right : left - x};
    return grid;
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
    vec2 source_extent;
    vec2 source_offset;
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
        vec2 source=(vec2(pixel)+0.5+pc.source_offset)*vec2(size)/pc.source_extent-0.5;
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
    vec2 position=pc.resample_upscaled!=0u
        ? (vec2(pixel)+0.5+pc.source_offset)*vec2(pc.display_extent)/pc.source_extent-0.5
        : (vec2(pixel)+0.5)*vec2(pc.display_extent)/vec2(pc.extent)-0.5;
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
