#pragma once
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
#include <string>
#include <string_view>
#include <utility>
namespace Fast::Renderer3ds {
// Reject a stale source contract atomically. Default variants never call this.
inline bool InstrumentPicaShadowProbe(std::string& declarations) {
    std::string result = declarations;
    const auto replace = [&](std::string_view from, std::string_view to) {
        const auto at = result.find(from);
        if (at == std::string::npos || result.find(from, at + from.size()) != std::string::npos) return false;
        result.replace(at, from.size(), to);
        return true;
    };
    if (!replace(R"probe(    if (entry == 0xffffffffu) return 1.0;)probe", R"probe(    bool probe_capture = layout_index == 0 && !pica_probe_found;
    if (probe_capture) {
        pica_probe_found = true;
        pica_probe_reason = entry == 0xffffffffu ? 5u : 6u;
        pica_probe_level = uint(level);
        pica_probe_entry = entry;
    }
    if (entry == 0xffffffffu) return 1.0;)probe")) return false;
    if (!replace(R"probe(            visibility += dot(step(sample_depth, textureGather(pica_vsm_atlas, uv, 0)), weights);)probe", R"probe(            vec4 probe_gather = textureGather(pica_vsm_atlas, uv, 0);
            if (probe_capture && ix == 0 && iy == 0) {
                pica_probe_comparison = sample_depth.x;
                pica_probe_atlas = probe_gather.x;
                pica_probe_texel = uvec2(ivec2(floor(uv / inverse_atlas - vec2(0.5))) + ivec2(0, 1));
            }
            visibility += dot(step(sample_depth, probe_gather), weights);)probe")) return false;
    if (!replace(R"probe(    if ((pica_vsm.extent.w & 1u) == 0u || light.z < 0.0 || light.z > 1.0) return vec2(0.0);)probe", R"probe(    if (layout_index == 0) pica_probe_reason = 2u;
    if ((pica_vsm.extent.w & 1u) == 0u || light.z < 0.0 || light.z > 1.0) return vec2(0.0);)probe")) return false;
    if (!replace(R"probe(    if (world_distance >= maximum_distance) return vec2(0.0);)probe", R"probe(    if (layout_index == 0) pica_probe_reason = 3u;
    if (world_distance >= maximum_distance) return vec2(0.0);
    if (layout_index == 0) pica_probe_reason = 4u;)probe")) return false;
    if (!replace(R"probe(    if (abs(light.w) <= 1.0e-7) return 1.0;)probe", R"probe(    if (layout_index == 0) pica_probe_reason = 1u;
    if (abs(light.w) <= 1.0e-7) return 1.0;)probe")) return false;
    declarations = R"probe(bool pica_probe_found = false;
uint pica_probe_reason = 0u, pica_probe_level = 255u, pica_probe_entry = 0u;
float pica_probe_comparison = 0.0, pica_probe_atlas = 0.0;
uvec2 pica_probe_texel = uvec2(0u);
)probe" + std::move(result);
    return true;
}

inline constexpr std::string_view kPicaShadowProbeEpilogue = R"glsl(
    // RGB only: native alpha/discard/depth/order are preserved.
    uint probe_code = uint(pica_vsm.receiver.z);
    uint probe_mode = probe_code / 1024u;
    uint probe_ordinal = probe_code % 1024u;
    if (probe_mode == 1u) {
        uint id = probe_ordinal + 1u;
        pica_color.rgb = vec3(id & 255u, (id >> 8u) & 255u, 165u) / 255.0;
    } else if (probe_mode >= 2u && probe_mode <= 9u) {
        bool probe_was_sampled = oot3d_directional_shadow_ready;
        // Reuses the production cache when lighting already asked for visibility.
        float probe_visibility = oot3d_directional_shadow_visibility(pica_surface_normal).r;
        bool probe_slot8 = (uint(pica_vsm.receiver.w) & 256u) != 0u;
        uint probe_bits = 0u;
        if (probe_mode == 2u)
            pica_color.rgb = vec3(probe_visibility, probe_slot8 ? probe_visibility : 1.0,
                                 probe_was_sampled ? 1.0 : 0.0);
        else if (probe_mode == 3u)
            pica_color.rgb = vec3(pica_probe_reason, pica_probe_level, probe_slot8 ? 255u : 0u) / 255.0;
        else if (probe_mode == 7u)
            pica_color.rgb = vec3(floatBitsToUint(pica_probe_comparison) >> 24u,
                                 floatBitsToUint(pica_probe_atlas) >> 24u,
                                 (pica_probe_texel.y >> 8u) & 255u) / 255.0;
        else if (probe_mode == 8u)
            pica_color.rgb = vec3(float(uint(gl_FragCoord.x) & 255u) / 255.0);
        else {
            probe_bits = probe_mode == 4u ? pica_probe_entry : (probe_mode == 9u ?
                ((pica_probe_texel.y & 65535u) << 16u) | (pica_probe_texel.x & 65535u) :
                floatBitsToUint(probe_mode == 5u ? pica_probe_comparison : pica_probe_atlas));
            pica_color.rgb = vec3(probe_bits & 255u, (probe_bits >> 8u) & 255u,
                                 (probe_bits >> 16u) & 255u) / 255.0;
        }
    }
)glsl";
} // namespace Fast::Renderer3ds
#endif
