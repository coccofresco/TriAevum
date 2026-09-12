#pragma once

#include "fast/oot3d/graphics_settings.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace Fast::Oot3d {

// Shared std430 payload. Only appearance belongs here, never outline policy.
struct alignas(16) ToonSurfaceParameters {
    std::array<float, 4> Control{}; // band span, softness, saturation, custom bands
    std::array<float, 4> Shadow{}; // RGB tint, strength
    std::array<float, 4> Rim{}; // RGB tint, strength
    std::array<float, 4> Flags{}; // enabled, lighting available, rim width, reserved
    std::array<std::array<float, 4>, 2> Levels{};
    std::array<std::array<float, 4>, 2> Thresholds{};
};
static_assert(sizeof(ToonSurfaceParameters) == 128U);
static_assert(offsetof(ToonSurfaceParameters, Thresholds) == 96U);
static_assert(kMaximumToonLightBands == 6U);

inline ToonSurfaceParameters PackToonSurfaceParameters(
    ToonMode mode, const ToonStyleSettings& style, bool lightingAvailable) {
    ToonSurfaceParameters result;
    result.Control = {static_cast<float>(std::clamp<int>(style.LightBands, 2, 6) - 1),
        style.BandSoftness, style.Saturation, style.CustomLightBands ? 1.0F : 0.0F};
    result.Shadow = {style.ShadowTint[0], style.ShadowTint[1], style.ShadowTint[2], style.ShadowStrength};
    result.Rim = {style.RimTint[0], style.RimTint[1], style.RimTint[2], style.RimStrength};
    result.Flags = {mode != ToonMode::Off ? 1.0F : 0.0F,
        lightingAvailable ? 1.0F : 0.0F, style.RimWidth, 0.0F};
    for (size_t i = 0; i < style.LightBandLevels.size(); ++i)
        result.Levels[i / 4][i % 4] = style.LightBandLevels[i];
    for (size_t i = 0; i < style.LightBandThresholds.size(); ++i)
        result.Thresholds[i / 4][i % 4] = style.LightBandThresholds[i];
    return result;
}

inline constexpr std::string_view kToonSurfaceResponseShader = R"glsl(
struct ToonSurfaceParameters {
    vec4 control;
    vec4 shadow;
    vec4 rim;
    vec4 flags;
    vec4 levels[2];
    vec4 thresholds[2];
};

float oot3d_toon_transition(float edge, float softness, float guide) {
    // GLSL smoothstep has undefined results for equal edges (hard bands).
    return softness > 0.0 ? smoothstep(edge - softness, edge + softness, guide)
                          : step(edge, guide);
}

float oot3d_toon_band_luminance(float guide, ToonSurfaceParameters p) {
    if (p.control.w < 0.5) {
        float scaled = guide * p.control.x;
        return (floor(scaled) + oot3d_toon_transition(0.5, p.control.y, fract(scaled))) / p.control.x;
    }
    int count = int(p.control.x);
    float bandLuminance = p.levels[0][0];
    for (int i = 0; i < count; ++i) {
        float edge = p.thresholds[i / 4][i % 4];
        float spacing = 1.0;
        if (count > 1) {
            int previous = max(i - 1, 0), next = min(i + 1, count - 1);
            float before = edge - p.thresholds[previous / 4][previous % 4];
            float after = p.thresholds[next / 4][next % 4] - edge;
            spacing = i == 0 ? after : (i == count - 1 ? before : min(before, after));
        }
        float delta = p.levels[(i + 1) / 4][(i + 1) % 4] - p.levels[i / 4][i % 4];
        bandLuminance += delta * oot3d_toon_transition(edge, p.control.y * max(spacing, 0.001), guide);
    }
    return clamp(bandLuminance, 0.0, 1.0);
}

vec3 oot3d_toon_banded_color(vec3 sourceColor, vec3 lighting,
                            float hasLighting, bool shadowTint, ToonSurfaceParameters p) {
    const vec3 lumaWeights = vec3(0.2126, 0.7152, 0.0722);
    float guide = clamp(dot(lighting, lumaWeights), 0.0, 1.0);
    float bandLuminance = oot3d_toon_band_luminance(guide, p);
    float lightingScale = mix(1.0, clamp(bandLuminance / max(guide, 0.05), 0.0, 2.0), hasLighting);
    vec3 banded = sourceColor * lightingScale;
    banded = mix(vec3(dot(banded, lumaWeights)), banded, p.control.z);
    if (shadowTint) {
        float shadowMask = (1.0 - smoothstep(0.0, 0.55, bandLuminance)) * hasLighting;
        banded = mix(banded, banded * p.shadow.rgb, shadowMask * p.shadow.w);
    }
    return banded;
}

vec3 oot3d_toon_rim(vec3 normal, vec3 viewDirection, ToonSurfaceParameters p) {
    normal = dot(normal, normal) > 0.000001 ? normalize(normal) : vec3(0.0, 0.0, 1.0);
    viewDirection = dot(viewDirection, viewDirection) > 0.000001
        ? normalize(viewDirection) : vec3(0.0, 0.0, 1.0);
    float rim = pow(clamp(1.0 - abs(dot(normal, viewDirection)), 0.0, 1.0), p.flags.z);
    return p.rim.rgb * (rim * p.rim.w);
}

vec3 oot3d_toon_surface_response(vec3 sourceColor, vec3 lighting,
                                vec3 normal, vec3 viewDirection, ToonSurfaceParameters p) {
    if (p.flags.x < 0.5) return sourceColor;
    return clamp(oot3d_toon_banded_color(sourceColor, lighting, p.flags.y, true, p)
        + oot3d_toon_rim(normal, viewDirection, p), 0.0, 1.0);
}

// Terrain-cover extensions inherit diffuse bands, not an independent rim.
// The response intentionally has no normal or viewing-direction argument.
vec3 oot3d_toon_diffuse_response(vec3 sourceColor, vec3 lighting, ToonSurfaceParameters p) {
    if (p.flags.x < 0.5) return sourceColor;
    return clamp(oot3d_toon_banded_color(sourceColor, lighting, p.flags.y, true, p), 0.0, 1.0);
}
)glsl";

} // namespace Fast::Oot3d
