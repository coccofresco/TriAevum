#pragma once
#include "fast/renderer3ds/pica_surface_lighting.h"
#include "fast/renderer3ds/pica_native_state.h"
#include <cstddef>

namespace Fast::Renderer3ds {
// Appended ONLY for the new applied variant at the existing surface binding16.
// The old2912-byte payload and every old descriptor range remain unchanged.
struct alignas(16) PicaFragmentBalanceUniforms {
    // active native-index mask, key native index, retained reference-fill mask, preserve
    std::array<uint32_t,4> Control{};
    // Per native index: diffuse gain, specular gain, ambient gain, source class.
    // class0 local, class1 scene direct, class2 key carrier, class3 retained fill.
    std::array<std::array<float,4>,8> Gains{};
    std::array<float,4> Parameters{}; // global ambient gain, reserved
};
struct alignas(16) PicaBalancedSurfaceUniforms {
    PicaSurfaceLightingUniforms Surface;
    PicaFragmentBalanceUniforms Balance;
};
static_assert(sizeof(PicaFragmentBalanceUniforms)==160);
static_assert(offsetof(PicaBalancedSurfaceUniforms,Balance)==2912);
static_assert(sizeof(PicaBalancedSurfaceUniforms)==3072);

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
        for (uint32_t j=0;j<scene.Count;++j)
            if (scene.Lights[j].SourceIdentity==source.SourceIdentity) nativeScene=&scene.Lights[j];
        if (nativeScene && !directional) return std::nullopt;
        auto& gain=out.Gains[i];
        gain={e.NativeLocalStrength,e.NativeLocalStrength,ambient,0};
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

inline constexpr std::string_view kPicaFragmentBalanceCaptureBegin=R"glsl(
    precise vec3 balance_diffuse[8];
    precise vec3 balance_ambient[8];
    precise vec3 balance_specular[8];
    precise vec3 balance_combined[8];
)glsl";
inline constexpr std::string_view kPicaFragmentBalanceCaptureDiffuse=R"glsl(
        balance_diffuse[slot] = diffuse * attenuation;
        balance_ambient[slot] = fragment_uniforms.lighting_ambient[i].rgb * attenuation;
        balance_combined[slot] = diffuse * attenuation + fragment_uniforms.lighting_ambient[i].rgb * attenuation;
)glsl";
inline constexpr std::string_view kPicaFragmentBalanceCaptureSpecular=R"glsl(
        balance_specular[slot] = specular;
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
            vec3 dg=vec3(gains.x), sg=vec3(gains.y);
            if (i==pica_surface.balance_control.y) { dg=key_visibility; sg=key_visibility; }
            bool unchanged=all(equal(dg,vec3(1.0))) && gains.z==1.0;
            balance_primary += unchanged ? balance_combined[slot] :
                balance_diffuse[slot]*dg + balance_ambient[slot]*gains.z;
            balance_secondary += balance_specular[slot]*sg;
            balance_ambient_sum += balance_ambient[slot]*gains.z;
            identity = identity && unchanged && all(equal(sg,vec3(1.0)));
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
