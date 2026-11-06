#pragma once
#include "fast/renderer3ds/pica_surface_lighting.h"
#include "fast/renderer3ds/pica_native_state.h"
#include <cstddef>

namespace Fast::Renderer3ds {
// Appended ONLY for the new applied variant at the existing surface binding16.
// The plain surface payload (sizeof(PicaSurfaceLightingUniforms)) and every old descriptor range remain unchanged.
struct alignas(16) PicaFragmentBalanceUniforms {
    // active native-index mask, key native index, retained reference-fill mask, preserve
    std::array<uint32_t,4> Control{};
    // Per native index: diffuse gain, specular gain, ambient gain, source class.
    // class0 local, class1 scene direct, class2 key carrier, class3 retained fill.
    // class >=4: exact point row (class-4), x=intensity, y=radius scale.
    std::array<std::array<float,4>,8> Gains{};
    std::array<float,4> Parameters{}; // global ambient gain, reserved
};
struct alignas(16) PicaBalancedSurfaceUniforms {
    PicaSurfaceLightingUniforms Surface;
    PicaFragmentBalanceUniforms Balance;
};
static_assert(sizeof(PicaFragmentBalanceUniforms)==160);
static_assert(offsetof(PicaBalancedSurfaceUniforms,Balance)==sizeof(PicaSurfaceLightingUniforms));
static_assert(sizeof(PicaBalancedSurfaceUniforms)==sizeof(PicaSurfaceLightingUniforms)+sizeof(PicaFragmentBalanceUniforms));

// First balanced contract: transfer the existing scene_main source, not a
// separately evaluated/new light. A white, unit, unrotated key is essential.
// Unsupported profile shapes keep native lighting (never the additive demo).
inline bool PicaFragmentBalanceProfileSupported(const PicaEnvironmentLighting& e) noexcept {
    const auto unit = [](float x) { return std::isfinite(x) && x >= 0 && x <= 1; };
    return e.Enabled && e.KeyDirection==PicaEnvironmentKeyDirection::SceneMain &&
        e.KeyAzimuthOffset==0 && e.KeyElevationOffset==0 &&
        e.NativeDirectStrength==0 && unit(e.NativeAmbientStrength) && unit(e.NativeLocalStrength) &&
        e.AmbientIntensity==0 && e.Lights[0].Intensity==1 &&
        e.Lights[0].Color==std::array<float,3>{1,1,1} && e.Lights[1].Intensity==0 &&
        e.PlacedLightCount==0;
}

// The environment a title whose scene lighting is native fragment lighting
// renders a frame with. A profile in the supported shape applies as it is.
// Any other shape applies nowhere: its draws keep native lighting, so the
// shadow light, caster selection, receiver matching, rim and placed lights
// follow the native environment too, exactly as with the profile Off (the
// settings that are independent of it stay: scope, scene point-light groups,
// caster faces), instead of a key that no fragment-lit receiver can match.
// In the supported shape, the rim light and the fog contribution, which the
// fragment-lit contract does not apply, apply to no draw of the frame (the
// title's other draws included), so a profile acts the same on every draw.
// Other titles are unchanged.
[[nodiscard]] inline PicaEnvironmentLighting PicaFragmentLitFrameEnvironment(
    const PicaEnvironmentLighting& e, bool nativeFragmentLighting) noexcept {
    if (!nativeFragmentLighting || !e.Enabled) return e;
    if (PicaFragmentBalanceProfileSupported(e)) {
        auto applied = e;
        applied.Rim.Enabled = false;
        applied.FogStrength = 1.0F;
        return applied;
    }
    PicaEnvironmentLighting off;
    off.ScopeIdentity = e.ScopeIdentity;
    off.PointLights = e.PointLights;
    off.CasterFaces = e.CasterFaces;
    return off;
}

// The light's function decides what the profile replaces. A slot the title
// adapter classified as Scene must be one of the environment's own
// directionals (records whose owner it proved: PicaSceneDirectionalLights
// Scope Scene). The main one becomes the key; the others follow the native
// direct policy; their ambient and the draw's global ambient (the scene
// ambient) take the profile's ambient gain. Every other slot (Local or
// Unknown: lamps, actor and room lights) keeps its whole native light,
// diffuse, specular and its own ambient, at the profile's local gain, and an
// environment record the adapter did not prove is never replaced. A Scene
// slot without such a record keeps the whole draw native instead of guessing
// which term it was.
inline std::optional<PicaFragmentBalanceUniforms> BuildPicaFragmentBalanceUniforms(
    bool capability, const PicaFragmentLightingLayout& layout,
    const PicaNativeLightBinding& binding, const PicaEnvironmentLighting& e) noexcept {
    if (!capability || !PicaFragmentBalanceProfileSupported(e) || !layout.Valid() ||
        layout.ActiveLightCount==0 || !binding.SceneDirectionals ||
        binding.SceneDirectionals->Count==0 ||
        binding.SceneDirectionals->Count>binding.SceneDirectionals->Lights.size() ||
        (binding.ScenePoints && binding.ScenePoints->Count!=0)) return std::nullopt;
    const auto& scene=*binding.SceneDirectionals;
    const auto* main=FindPicaSceneMainDirectional(scene);
    if (!main || main->SourceIdentity==0 || PicaSceneDirectHorizon(*main,scene.WorldUp)!=1)
        return std::nullopt; // A dimmed/absent sun is not an identity transfer.
    for (uint32_t a=0;a<scene.Count;++a) {
        if (scene.Lights[a].SourceIdentity==0) return std::nullopt;
        for (uint32_t b=0;b<a;++b)
            if (scene.Lights[a].SourceIdentity==scene.Lights[b].SourceIdentity) return std::nullopt;
    }
    PicaFragmentBalanceUniforms out;
    out.Control[1]=8;
    out.Control[3]=e.PreserveSceneBrightness;
    const float ambient=e.PreserveSceneBrightness ? 1.0F : e.NativeAmbientStrength;
    out.Parameters[0]=ambient;
    for (uint32_t slot=0;slot<layout.ActiveLightCount;++slot) {
        const uint32_t i=layout.LightPermutation[slot];
        // Sources[] is indexed by the native hardware light number, not its
        // active iteration order. No positional, colour or direction guessing.
        if (i>=binding.Sources.size() || (out.Control[0]&(1U<<i))) return std::nullopt;
        const auto& source=binding.Sources[i];
        if (source.SourceIdentity==0 || source.Kind==PicaNativeLightSource::Type::Unavailable)
            return std::nullopt;
        for (uint32_t j=0;j<binding.Sources.size();++j)
            if (j!=i && (out.Control[0]&(1U<<j)) && binding.Sources[j].SourceIdentity==source.SourceIdentity)
                return std::nullopt;
        out.Control[0]|=1U<<i;
        const bool directional=layout.Lights[i].Directional;
        if (directional != (source.Kind==PicaNativeLightSource::Type::Directional)) return std::nullopt;
        const PicaSceneDirectionalLight* nativeScene=nullptr;
        if (source.Scope==PicaNativeLightScope::Scene) {
            if (scene.Scope!=PicaNativeLightScope::Scene) return std::nullopt;
            for (uint32_t j=0;j<scene.Count;++j)
                if (scene.Lights[j].SourceIdentity==source.SourceIdentity) nativeScene=&scene.Lights[j];
            if (!nativeScene) return std::nullopt;
        }
        if (nativeScene && !directional) return std::nullopt;
        auto& gain=out.Gains[i];
        gain={e.NativeLocalStrength,e.NativeLocalStrength,e.NativeLocalStrength,0};
        if (nativeScene) {
            if (nativeScene==main) {
                if (out.Control[1]!=8) return std::nullopt;
                out.Control[1]=i;
                gain={0,0,ambient,2}; // All existing direct moves to key slot8.
            } else if (e.PreserveSceneBrightness) {
                // MM's opposite fragment light remains a native reference
                // term. Removing it cannot satisfy native-brightness identity.
                gain={1,1,ambient,3};out.Control[2]|=1U<<i;
            } else {
                // Literal OoT non-preserving direct=0 endpoint, including fill.
                gain={0,0,ambient,1};
            }
        }
    }
    if (out.Control[1]==8) return std::nullopt;
    return out;
}

// The environment a surface draw of a title lit by native fragment lights
// takes while a profile applies (`balanceTitleDraw`: such a title, an enabled
// frame profile, not an isolated draw). True keeps the no-profile native
// reference: a fragment-lit draw the balance did not qualify must never fall
// back to adding the old demo key on top of its native lights, and outside
// the full scene-lighting model no draw of the title takes the profile. A
// draw without native fragment lighting (vertex lit or baked room colour)
// takes the profile through the ordinary vertex-lighting equations,
// baked-reference path included, in the full model only.
[[nodiscard]] constexpr bool PicaFragmentLitTitleDrawKeepsNative(
    bool balanceTitleDraw, bool balanced, bool fragmentLit, bool fullSceneLighting) noexcept {
    return balanceTitleDraw && !balanced && (fragmentLit || !fullSceneLighting);
}

// Specialized variants only: native radiance/LUTs are already per pixel.
// The exact point row supplies user tint and cube visibility, not a second
// material coefficient or the supplemental selector's exposure/fade.
inline constexpr std::string_view kPicaNativePointBalanceFunctions=R"glsl(
float pica_point_shadow(float slot, vec3 light_to_fragment, float radius);
bool pica_native_point_row(int point) {
    for (uint i = 0u; i < 8u; ++i)
        if ((pica_surface.balance_control.x & (1u << i)) != 0u &&
            pica_surface.balance_gains[i].w == float(point + 4)) return true;
    return false;
}
vec3 pica_native_point_factor(uint i) {
    PicaScenePointLight point = pica_surface.scene_points[int(pica_surface.balance_gains[i].w) - 4];
    vec3 delta = point.position_radius.xyz + pica_view;
    vec3 world_delta = vec3(dot(pica_surface.view_to_world_linear[0].xyz, delta),
        dot(pica_surface.view_to_world_linear[1].xyz, delta),
        dot(pica_surface.view_to_world_linear[2].xyz, delta));
    return point.color.rgb * pica_surface.balance_gains[i].x *
        pica_point_shadow(point.color.w, -world_delta, point.position_radius.w);
}
float pica_native_point_attenuation(uint i, uint flags, float distance_to_light,
    float spot, float native_attenuation) {
    float scale = pica_surface.balance_gains[i].y;
    if (pica_surface.balance_gains[i].w < 4.0 || scale == 1.0 || (flags & 64u) == 0u)
        return native_attenuation;
    return spot * pica_lighting_lut_unsigned(int(16u + i), clamp(
        fragment_uniforms.lighting_attenuation[i].y * distance_to_light / scale +
        fragment_uniforms.lighting_attenuation[i].x, 0.0, 1.0));
}
)glsl";

inline constexpr std::string_view kPicaFragmentBalanceCaptureBegin=R"glsl(
    precise vec3 balance_diffuse[8];
    precise vec3 balance_ambient[8];
    precise vec3 balance_specular[8];
    precise vec3 balance_combined[8];
)glsl";
inline constexpr std::string_view kPicaFragmentBalanceCaptureDiffuse=R"glsl(
        float replacement_attenuation = pica_native_point_attenuation(i, flags,
            distance_to_light, spot_attenuation, attenuation);
        balance_diffuse[slot] = diffuse * replacement_attenuation;
        balance_ambient[slot] = fragment_uniforms.lighting_ambient[i].rgb * attenuation;
        balance_combined[slot] = diffuse * attenuation + fragment_uniforms.lighting_ambient[i].rgb * attenuation;
)glsl";
inline constexpr std::string_view kPicaFragmentBalanceCaptureSpecular=R"glsl(
        balance_specular[slot] = specular;
        if (pica_surface.balance_gains[i].w >= 4.0) {
            balance_specular[slot] = (s0 + s1) * highlight * replacement_attenuation;
            if ((lp.control.w & 4u) != 0u && (flags & 16u) != 0u)
                balance_specular[slot] *= pica_shadow_factor.rgb;
        }
)glsl";
inline constexpr std::string_view kPicaFragmentBalanceBody=R"glsl(
    // Preserve native material/vertex colour. Recompose BEFORE native clamp
    // and byte-round/TEV. Never subtract a Lambert term from a clipped colour.
    pica_balance_ambient = fragment_uniforms.lighting_global_ambient.rgb;
    if (!surface_water) {
        precise vec3 balance_primary = vec3(0.0);
        precise vec3 balance_secondary = vec3(0.0);
        precise vec3 balance_ambient_sum = vec3(0.0);
        vec3 key_visibility = pica_surface_shadow(8);
        bool identity = pica_surface.balance_parameters.x == 1.0;
        for (uint slot=0u;slot<lp.control.x;++slot) {
            uint i=lp.lights[slot].x;
            vec4 gains=pica_surface.balance_gains[i];
            if (gains.w >= 4.0) {
                vec3 factor = pica_native_point_factor(i);
                balance_primary += balance_diffuse[slot] * factor + balance_ambient[slot];
                balance_secondary += balance_specular[slot] * factor;
                balance_ambient_sum += balance_ambient[slot];
                identity = false;
                continue;
            }
            vec3 dg=vec3(gains.x), sg=vec3(gains.y);
            if (i==pica_surface.balance_control.y) { dg=key_visibility; sg=key_visibility; }
            bool unchanged=all(equal(dg,vec3(1.0))) && gains.z==1.0;
            balance_primary += unchanged ? balance_combined[slot] :
                balance_diffuse[slot]*dg + balance_ambient[slot]*gains.z;
            balance_secondary += balance_specular[slot]*sg;
            balance_ambient_sum += balance_ambient[slot]*gains.z;
            identity = identity && unchanged && all(equal(sg,vec3(1.0)));
        }
        if ((pica_surface.balance_control.w & 2u) != 0u && pica_surface.control.y != 0u) {
            // Exact native matches were converted above. The scene evaluator
            // skips their rows: only unmatched/independent sources add energy.
            // Supplemental material reflectance is applied exactly once.
            balance_primary += pica_surface_scene_point_primary(
                pica_surface_unit(pica_surface_normal), false) * pica_surface.material_diffuse.rgb;
            identity = false;
        }
        balance_primary += fragment_uniforms.lighting_global_ambient.rgb * pica_surface.balance_parameters.x;
        balance_ambient_sum += fragment_uniforms.lighting_global_ambient.rgb * pica_surface.balance_parameters.x;
        pica_balance_ambient = balance_ambient_sum;
        // Exact identity bypass: also preserves native floating point grouping
        // and rounding in the no-shadow gate, not just a real-number equation.
        if (!identity) {
            primary_fragment_color.rgb=clamp(balance_primary,vec3(0.0),vec3(1.0));
            secondary_fragment_color.rgb=clamp(balance_secondary,vec3(0.0),vec3(1.0));
        }
        // Native Fresnel/shadow alpha and native TEV alpha replay remain intact.
    }
)glsl";
} // namespace Fast::Renderer3ds
