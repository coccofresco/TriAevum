#include "fast/extension/diffuse_gi_adapter.h"

// GLSL of the OOT3D adapter kernel of the screen-space diffuse GI (own code;
// precompiled in builtin_pass.spvpack as oot3d_diffuse_gi_inputs.comp). A
// translation unit of its own so the pass shader sources (pack builder, AOT
// compiler, GPU tests) take only the shader text.

#include "fast/renderer/diffuse_gi_pass_shader.h"
#include "fast/renderer3ds/pica_geometry_guide.h"

#include <string>

namespace Fast::Extension {
namespace {

constexpr const char* kAdapterHead = R"glsl(#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(push_constant) uniform AdapterParams {
    uvec4 extent; // guide width, guide height, work width, work height
    uvec4 scale;  // work factor, phase, depth encoding (1 perspective, 3 W-buffer), flags (1 guide, 2 shading)
    vec4 depth;   // near, far
    vec4 motion;  // guide UV -> work UV scale
} p;
layout(set = 0, binding = 0) uniform texture2D native_depth;
layout(set = 0, binding = 1) uniform utexture2D geometry_guide;
layout(set = 0, binding = 2) uniform texture2D surface_guide;
layout(set = 0, binding = 3) uniform texture2D motion_vectors;
layout(std430, set = 0, binding = 4) writeonly buffer DepthOut { float depth_out[]; };
layout(std430, set = 0, binding = 5) writeonly buffer ClassOut { uint class_out[]; };
layout(std430, set = 0, binding = 6) writeonly buffer RadianceOut { vec4 radiance_out[]; };
layout(std430, set = 0, binding = 7) writeonly buffer NormalOut { vec4 normal_out[]; };
layout(std430, set = 0, binding = 8) writeonly buffer MotionOut { vec2 motion_out[]; };
layout(std430, set = 0, binding = 9) writeonly buffer GuideOut { uvec2 guide_out[]; };
// M8: the renderer's normal guide (RGBA8, view space, 0.5 n + 0.5, alpha
// coverage) -> the shading normals of the work texels and of the output texels.
layout(set = 0, binding = 10) uniform texture2D normal_guide;
layout(std430, set = 0, binding = 11) writeonly buffer ShadingOut { vec4 shading_out[]; };
layout(std430, set = 0, binding = 12) writeonly buffer ShadingGuideOut { uint shading_guide_out[]; };
)glsl";

constexpr const char* kAdapterMain = R"glsl(
float srgb_to_linear(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }

void main() {
    uvec2 id = gl_GlobalInvocationID.xy;
    if (id.x >= p.extent.x || id.y >= p.extent.y) return; // no barriers in this kernel
    ivec2 t = ivec2(id);
    float d = texelFetch(native_depth, t, 0).r;
    uvec4 geometry = texelFetch(geometry_guide, t, 0);
    vec4 surface = texelFetch(surface_guide, t, 0);
    // Class: no depth (clear, 1 or more, NaN) is sky; a zero surface-guide
    // alpha (clear, HUD, replacing surfaces) is unavailable; else one-sided.
    uint cls = !(d < 1.0) ? 3u : (surface.a > 0.0 ? 1u : 0u);
    vec3 n = geometry.z != 0u ? pica_geometry_decode_normal(geometry.y) : vec3(0.0);
    float near_plane = p.depth.x;
    float far_plane = p.depth.y;
    float z = p.scale.z == 3u ? near_plane + d * (far_plane - near_plane)
                              : near_plane * far_plane / (far_plane - d * (far_plane - near_plane));
    bool receiver = cls == 1u && z > 0.0 && !isinf(z) && !isnan(z) && dot(n, n) > 0.0;
    if ((p.scale.w & 1u) != 0u)
        guide_out[id.y * p.extent.x + id.x] = receiver ? uvec2(floatBitsToUint(z), dgi_encode_normal(n)) : uvec2(0u);
    // M8 shading normal: unit, on the face normal's side; none where the guide
    // has no coverage or a degenerate (blended) vector.
    bool shading = (p.scale.w & 2u) != 0u;
    vec3 sn = vec3(0.0);
    if (shading) {
        vec4 sg = texelFetch(normal_guide, t, 0);
        vec3 raw = sg.rgb * 2.0 - 1.0;
        float length2 = dot(raw, raw);
        if (sg.a > 0.0 && length2 > 0.0625 && !isinf(length2) && !isnan(length2)) {
            sn = raw * inversesqrt(length2);
            if (dot(sn, n) < 0.0) sn = -sn;
        }
        if ((p.scale.w & 1u) != 0u)
            shading_guide_out[id.y * p.extent.x + id.x] = receiver ? dgi_encode_normal(sn) : 0u;
    }
    uint f = p.scale.x;
    uint o = p.scale.y;
    if (id.x < o || id.y < o || (id.x - o) % f != 0u || (id.y - o) % f != 0u) return;
    uint i = (id.x - o) / f;
    uint j = (id.y - o) / f;
    if (i >= p.extent.z || j >= p.extent.w) return;
    uint w = j * p.extent.z + i;
    depth_out[w] = d;
    class_out[w] = cls;
    vec3 packed_bits = floor(surface.rgb * 65535.0 + 0.5);
    vec3 direct = mod(packed_bits, 256.0) / 255.0;
    radiance_out[w] = vec4(srgb_to_linear(direct.x), srgb_to_linear(direct.y), srgb_to_linear(direct.z), 0.0);
    normal_out[w] = vec4(n, dot(n, n) > 0.0 ? 1.0 : 0.0);
    motion_out[w] = texelFetch(motion_vectors, t, 0).xy * p.motion.xy;
    if (shading) shading_out[w] = vec4(sn, dot(sn, sn) > 0.0 ? 1.0 : 0.0);
}
)glsl";

} // namespace

std::string BuildDiffuseGiAdapterShader() {
    return std::string(kAdapterHead) + std::string(Renderer3ds::PicaGeometryGuideShaderLibrary()) +
           Renderer::DiffuseGi::DiffuseGiNormalCodecGlsl() + kAdapterMain;
}

} // namespace Fast::Extension
