#include "fast/oot3d/hiz_reflection.h"
#include "fast/oot3d/pica_surface_coordinates.h"

namespace Fast::Oot3d {

namespace {
constexpr const char* kPushBlock = R"glsl(
layout(push_constant) uniform ReflectionPush {
    uvec2 extent;
    vec2 inverse_size;
    float strength;
    float max_distance;
    float thickness;
    float edge_fade;
    uint max_steps;
    uint hiz_mip_count;
    float projection_scale_x;
    float projection_scale_y;
    float projection_offset_x;
    float projection_offset_y;
    float near_plane;
    float far_plane;
    float roughness_bias;
    float normal_sigma;
    uint surface_orientation;
} pc;
)glsl";
}

std::string BuildHiZReflectionComputeShader() {
    std::string source = R"glsl(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform texture2D immutable_color;
layout(set = 0, binding = 1) uniform texture2D hiz_depth;
layout(set = 0, binding = 2) uniform texture2D normal_guide;
layout(set = 0, binding = 3) uniform texture2D material_guide;
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D reflection_output;
layout(set = 0, binding = 5) uniform sampler linear_sampler;
layout(set = 0, binding = 6) uniform sampler nearest_sampler;
)glsl";
    source += kPushBlock;
    source += EffectSurfaceCoordinateShaderLibrary;
    source += R"glsl(
vec3 view_position(vec2 uv, float view_depth) {
    vec2 ndc = oot3d_surface_view_ndc(uv, pc.surface_orientation);
    return vec3((ndc.x + pc.projection_offset_x) * view_depth /
                    pc.projection_scale_x,
                (ndc.y + pc.projection_offset_y) * view_depth /
                    pc.projection_scale_y,
                -view_depth);
}

vec2 project_view(vec3 position) {
    float inverse_w = 1.0 / max(-position.z, 1.0e-5);
    vec2 ndc = vec2(
        pc.projection_scale_x * position.x * inverse_w -
            pc.projection_offset_x,
        pc.projection_scale_y * position.y * inverse_w -
            pc.projection_offset_y);
    return oot3d_view_ndc_surface_uv(ndc, pc.surface_orientation);
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, ivec2(pc.extent)))) return;
    vec2 uv = (vec2(pixel) + 0.5) * pc.inverse_size;
    vec4 material = texture(sampler2D(material_guide,linear_sampler), uv);
    vec4 normal_sample = texture(sampler2D(normal_guide,linear_sampler), uv);
    if (material.r <= 0.001 || normal_sample.a < 0.5) {
        imageStore(reflection_output, pixel, vec4(0.0));
        return;
    }
    float surface_depth = textureLod(
        sampler2D(hiz_depth,nearest_sampler), uv, 0.0).r;
    if (isnan(surface_depth) || isinf(surface_depth) ||
        surface_depth <= pc.near_plane || surface_depth >= pc.far_plane) {
        imageStore(reflection_output, pixel, vec4(0.0));
        return;
    }
    vec3 normal = normalize(normal_sample.xyz * 2.0 - 1.0);
    vec3 surface = view_position(uv, surface_depth);
    vec3 incident = normalize(surface);
    vec3 ray_direction = normalize(reflect(incident, normal));
    if (ray_direction.z >= -0.001) {
        imageStore(reflection_output, pixel, vec4(0.0));
        return;
    }
    float step_length = max(pc.thickness, pc.max_distance /
                            max(float(pc.max_steps), 1.0));
    vec3 origin = surface + normal * pc.thickness;
    float travelled = step_length;
    vec2 hit_uv = vec2(-1.0);
    float confidence = 0.0;
    for (uint step_index = 0u; step_index < 64u; ++step_index) {
        if (step_index >= pc.max_steps || travelled > pc.max_distance) break;
        vec3 ray_position = origin + ray_direction * travelled;
        float ray_depth = -ray_position.z;
        if (ray_depth <= pc.near_plane || ray_depth >= pc.far_plane) break;
        vec2 candidate_uv = project_view(ray_position);
        if (any(lessThanEqual(candidate_uv, vec2(0.0))) ||
            any(greaterThanEqual(candidate_uv, vec2(1.0)))) break;
        float footprint = max(travelled * max(pc.inverse_size.x,
                                              pc.inverse_size.y), 1.0);
        float mip = clamp(floor(log2(footprint)), 0.0,
                          float(max(pc.hiz_mip_count, 1u) - 1u));
        float scene_depth = textureLod(
            sampler2D(hiz_depth,nearest_sampler), candidate_uv, mip).r;
        float gap = ray_depth - scene_depth;
        float tolerance = pc.thickness * (1.0 + mip * 0.5);
        if (gap >= 0.0 && gap <= tolerance) {
            hit_uv = candidate_uv;
            confidence = 1.0 - travelled / pc.max_distance;
            break;
        }
        travelled += step_length * max(1.0, exp2(mip) * 0.5);
    }
    if (confidence <= 0.0) {
        imageStore(reflection_output, pixel, vec4(0.0));
        return;
    }
    vec2 edge_distance = min(hit_uv, vec2(1.0) - hit_uv);
    float edge = smoothstep(0.0, max(pc.edge_fade, 1.0e-4),
                            min(edge_distance.x, edge_distance.y));
    float roughness = clamp(material.g + pc.roughness_bias, 0.0, 1.0);
    float grazing = 1.0 - abs(dot(normal, -incident));
    confidence *= edge * material.r * mix(0.35, 1.0, grazing) *
                  (1.0 - roughness);
    imageStore(reflection_output, pixel,
               vec4(texture(sampler2D(immutable_color,linear_sampler),
                            hit_uv).rgb, confidence));
}
)glsl";
    return source;
}

std::string BuildHiZReflectionBilateralFilterShader() {
    std::string source = R"glsl(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform texture2D raw_reflection;
layout(set = 0, binding = 1) uniform texture2D hiz_depth;
layout(set = 0, binding = 2) uniform texture2D normal_guide;
layout(set = 0, binding = 3) uniform texture2D material_guide;
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D filtered_output;
layout(set = 0, binding = 5) uniform sampler linear_sampler;
layout(set = 0, binding = 6) uniform sampler nearest_sampler;
)glsl";
    source += kPushBlock;
    source += R"glsl(
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, ivec2(pc.extent)))) return;
    vec2 uv = (vec2(pixel) + 0.5) * pc.inverse_size;
    vec4 material = texture(sampler2D(material_guide,linear_sampler), uv);
    vec4 center_normal_sample = texture(
        sampler2D(normal_guide,linear_sampler), uv);
    if (material.r <= 0.001 || center_normal_sample.a < 0.5) {
        imageStore(filtered_output, pixel, vec4(0.0));
        return;
    }
    float center_depth = textureLod(
        sampler2D(hiz_depth,nearest_sampler), uv, 0.0).r;
    vec3 center_normal = normalize(center_normal_sample.xyz * 2.0 - 1.0);
    vec3 weighted_color = vec3(0.0);
    float weighted_confidence = 0.0;
    float total_weight = 0.0;
    float valid_weight = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 sample_uv = clamp(uv + vec2(x, y) * pc.inverse_size,
                                   vec2(0.0), vec2(1.0));
            vec4 reflection = texture(
                sampler2D(raw_reflection,linear_sampler), sample_uv);
            float sample_depth = textureLod(
                sampler2D(hiz_depth,nearest_sampler), sample_uv, 0.0).r;
            vec4 sample_normal_value = texture(
                sampler2D(normal_guide,linear_sampler), sample_uv);
            vec3 sample_normal = normalize(sample_normal_value.xyz * 2.0 - 1.0);
            float spatial_weight = exp(-0.5 * float(x * x + y * y));
            float depth_weight = exp(-abs(sample_depth - center_depth) /
                                     max(pc.thickness, 1.0e-3));
            float normal_weight = pow(max(dot(center_normal, sample_normal), 0.0),
                                      max(pc.normal_sigma, 1.0));
            float weight = spatial_weight * depth_weight * normal_weight *
                           step(0.5, sample_normal_value.a);
            total_weight += weight;
            float confidence_weight = weight * reflection.a;
            weighted_color += reflection.rgb * confidence_weight;
            weighted_confidence += confidence_weight;
            valid_weight += confidence_weight;
        }
    }
    vec3 color = valid_weight > 1.0e-5
        ? weighted_color / valid_weight : vec3(0.0);
    float confidence = total_weight > 1.0e-5
        ? weighted_confidence / total_weight : 0.0;
    imageStore(filtered_output, pixel, vec4(color, confidence));
}
)glsl";
    return source;
}

} // namespace Fast::Oot3d
