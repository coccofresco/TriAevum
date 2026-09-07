#include "fast/oot3d/toon_outline_shader.h"

namespace Fast::Oot3d {

std::string ToonOutlineShaderLibrary() {
    return R"glsl(
float oot3d_outline_response(float value, float low, float high, float softness) {
    float midpoint = (low + high) * 0.5;
    float half_width = (high - low) * 0.5 * max(softness, 0.001);
    return smoothstep(midpoint - half_width, midpoint + half_width, value);
}

vec2 oot3d_toon_outline_geometry_edge(vec2 uv, vec2 inverse_size, float width,
                             float depth_sensitivity, float normal_sensitivity,
                             float softness) {
    if (depth_sensitivity <= 0.0 && normal_sensitivity <= 0.0) return vec2(0.0, 1.0);
    vec4 center_guide = oot3d_outline_sample_geometry(uv);
    if (dot(center_guide.xyz, center_guide.xyz) < 0.000001) return vec2(0.0, 1.0);
    float center_depth = center_guide.a;
    vec3 center_normal = normalize(center_guide.xyz);
    float depth_delta = 0.0;
    float normal_delta = 0.0;
    float depth_owner = center_depth;
    float normal_owner = center_depth;
    // Circular footprint: one shared gather for both edge detectors.
    const vec2 directions[8] = vec2[8](
        vec2(1,0), vec2(-1,0), vec2(0,1), vec2(0,-1),
        vec2(0.70710678,0.70710678), vec2(-0.70710678,0.70710678),
        vec2(0.70710678,-0.70710678), vec2(-0.70710678,-0.70710678));
    for (int index = 0; index < 8; ++index) {
        vec2 neighbor_uv = uv + directions[index] * inverse_size * width;
        vec4 guide = oot3d_outline_sample_geometry(neighbor_uv);
        // A dilated silhouette can occupy a background pixel. Its contour
        // belongs to the nearer contributing surface, not that background.
        float owner = dot(guide.xyz, guide.xyz) >= 0.000001
            ? min(center_depth, guide.a) : center_depth;
        float difference = abs(guide.a - center_depth);
        if (difference > depth_delta) depth_owner = owner;
        else if (difference == depth_delta) depth_owner = min(depth_owner, owner);
        depth_delta = max(depth_delta, difference);
        if (dot(guide.xyz, guide.xyz) >= 0.000001) {
            vec3 normal = normalize(guide.xyz);
            difference = 1.0 - clamp(dot(center_normal, normal), -1.0, 1.0);
            if (difference > normal_delta) normal_owner = owner;
            else if (difference == normal_delta) normal_owner = min(normal_owner, owner);
            normal_delta = max(normal_delta, difference);
        }
    }
    float depth_edge = oot3d_outline_response(depth_delta * depth_sensitivity, 0.0015, 0.0090, softness);
    float normal_edge = oot3d_outline_response(normal_delta * normal_sensitivity, 0.015, 0.18, softness);
    float owner = depth_edge > normal_edge ? depth_owner : normal_owner;
    if (depth_edge == normal_edge) owner = min(depth_owner, normal_owner);
    return vec2(max(depth_edge, normal_edge), owner);
}

float oot3d_toon_outline_native_edge(vec2 uv, vec2 inverse_size, float width,
                                    float depth_sensitivity, float normal_sensitivity,
                                    float softness) {
    return oot3d_toon_outline_geometry_edge(uv, inverse_size, width,
        depth_sensitivity, normal_sensitivity, softness).x;
}

bool oot3d_outline_excluded(float alpha) {
    // The normal-guide UNORM8 marker published by extension geometry.
    return alpha >= 63.5 / 255.0 && alpha < 64.5 / 255.0;
}

float oot3d_outline_world_depth(vec2 uv, float guide_alpha) {
    // Scene depth also receives UI and secondary camera canvases. Only an
    // extension sample may use it; other samples retain the published world
    // depth, so restoring the old response does not restore overlay contours.
    return oot3d_outline_excluded(guide_alpha) ? oot3d_outline_sample_scene_depth(uv)
                                             : oot3d_outline_sample_geometry(uv).a;
}

float oot3d_outline_coverage(vec2 uv, float coverage, float depth) {
    float transparent = oot3d_outline_sample_occlusion(uv);
    if (transparent > 0.0 && 1.0 - transparent >= depth - 0.00001)
        return 1.0;
    return coverage;
}

vec2 oot3d_toon_outline_scene_edge(vec2 uv, vec2 inverse_size, float width,
                                  float depth_sensitivity, float normal_sensitivity,
                                  float softness, out bool extension_in_footprint) {
    vec4 center_guide = oot3d_outline_sample_normal(uv);
    float center_depth = oot3d_outline_world_depth(uv, center_guide.a);
    extension_in_footprint = oot3d_outline_excluded(center_guide.a);
    if (extension_in_footprint ||
        oot3d_outline_coverage(uv, center_guide.a, center_depth) < 0.5)
        return vec2(0.0, 1.0);
    vec3 center_normal = normalize(center_guide.xyz * 2.0 - 1.0);
    float depth_delta = 0.0;
    float normal_delta = 0.0;
    float depth_owner = center_depth;
    float normal_owner = center_depth;
    const vec2 directions[8] = vec2[8](
        vec2(1,0), vec2(-1,0), vec2(0,1), vec2(0,-1),
        vec2(0.70710678,0.70710678), vec2(-0.70710678,0.70710678),
        vec2(0.70710678,-0.70710678), vec2(-0.70710678,-0.70710678));
    for (int index = 0; index < 8; ++index) {
        vec2 neighbor_uv = uv + directions[index] * inverse_size * width;
        vec4 guide = oot3d_outline_sample_normal(neighbor_uv);
        float depth = oot3d_outline_world_depth(neighbor_uv, guide.a);
        bool excluded = oot3d_outline_excluded(guide.a);
        extension_in_footprint = extension_in_footprint || excluded;
        float owner = excluded ? center_depth : min(center_depth, depth);
        if (!excluded || center_depth + 0.0000001 < depth) {
            float difference = abs(depth - center_depth);
            if (difference > depth_delta) depth_owner = owner;
            else if (difference == depth_delta) depth_owner = min(depth_owner, owner);
            depth_delta = max(depth_delta, difference);
        }
        if (!excluded && oot3d_outline_coverage(neighbor_uv, guide.a, depth) >= 0.5) {
            vec3 normal = normalize(guide.xyz * 2.0 - 1.0);
            float difference = 1.0 - clamp(dot(center_normal, normal), -1.0, 1.0);
            if (difference > normal_delta) normal_owner = owner;
            else if (difference == normal_delta) normal_owner = min(normal_owner, owner);
            normal_delta = max(normal_delta, difference);
        }
    }
    float depth_edge = oot3d_outline_response(depth_delta * depth_sensitivity, 0.0015, 0.0090, softness);
    float normal_edge = oot3d_outline_response(normal_delta * normal_sensitivity, 0.015, 0.18, softness);
    float owner = depth_edge > normal_edge ? depth_owner : normal_owner;
    if (depth_edge == normal_edge) owner = min(depth_owner, normal_owner);
    return vec2(max(depth_edge, normal_edge), owner);
}

float oot3d_toon_outline_edge(vec2 uv, vec2 inverse_size, float width,
                             float depth_sensitivity, float normal_sensitivity,
                             float softness) {
    if (depth_sensitivity <= 0.0 && normal_sensitivity <= 0.0) return 0.0;
    bool extension_in_footprint;
    vec2 contour = oot3d_toon_outline_scene_edge(uv, inverse_size, width,
        depth_sensitivity, normal_sensitivity, softness, extension_in_footprint);
    // Preserve the historical scene-depth/normal response. Only where extension
    // geometry intersects the gather, recover the full native contour footprint:
    // excluding its center/neighbor samples must not thin an existing outline.
    if (extension_in_footprint) {
        vec2 native_contour = oot3d_toon_outline_geometry_edge(uv, inverse_size, width,
            depth_sensitivity, normal_sensitivity, softness);
        if (native_contour.x > 0.0) contour = native_contour;
    }
    if (contour.x <= 0.0) return 0.0;
    // Visibility uses the contour's foreground owner, not its background pixel.
    float occlusion = oot3d_outline_sample_occlusion(uv);
    return occlusion > 0.0 && 1.0 - occlusion < contour.y - 0.00001 ? 0.0 : contour.x;
}
)glsl";
}

std::string ToonOutlineComputeShaderLibrary() {
    return std::string(R"glsl(
vec3 oot3d_outline_srgb_to_linear(vec3 value) {
    value=max(value,vec3(0.0));
    bvec3 cutoff=lessThanEqual(value,vec3(0.04045));
    vec3 lower=value/12.92;
    vec3 higher=pow((value+0.055)/1.055,vec3(2.4));
    return mix(higher,lower,cutoff);
}
float oot3d_outline_sample_scene_depth(vec2 uv) {
    ivec2 size=textureSize(sampler2D(depth_guide,composite_sampler),0);
    return texelFetch(sampler2D(depth_guide,composite_sampler),
        clamp(ivec2(uv*vec2(size)),ivec2(0),size-1),0).r;
}
vec4 oot3d_outline_sample_normal(vec2 uv) {
    ivec2 size=textureSize(sampler2D(normal_guide,composite_sampler),0);
    return texelFetch(sampler2D(normal_guide,composite_sampler),
        clamp(ivec2(uv*vec2(size)),ivec2(0),size-1),0);
}
vec4 oot3d_outline_sample_geometry(vec2 uv) {
    ivec2 size=textureSize(sampler2D(outline_geometry_guide,composite_sampler),0);
    return texelFetch(sampler2D(outline_geometry_guide,composite_sampler),
        clamp(ivec2(uv*vec2(size)),ivec2(0),size-1),0);
}
float oot3d_outline_sample_occlusion(vec2 uv) {
    ivec2 size=textureSize(sampler2D(transparent_depth_guide,composite_sampler),0);
    return texelFetch(sampler2D(transparent_depth_guide,composite_sampler),
        clamp(ivec2(uv*vec2(size)),ivec2(0),size-1),0).a;
}
)glsl") + ToonOutlineShaderLibrary();
}

} // namespace Fast::Oot3d
