#pragma once
#include "fast/renderer3ds/pica_surface_lighting.h"
#include "fast/renderer3ds/pica_native_state.h"
#include "fast/renderer3ds/pica_tev_program.h"
#include <cstddef>

namespace Fast::Renderer3ds {
// Appended ONLY for the new applied variant at the existing surface binding16.
// The plain surface payload (sizeof(PicaSurfaceLightingUniforms)) and every old descriptor range remain unchanged.
struct alignas(16) PicaFragmentBalanceUniforms {
    // active native-index mask, proved main native index (8 = absent),
    // secondary material-factor bit, preserve
    std::array<uint32_t,4> Control{};
    // Per native index: diffuse gain, specular gain, ambient gain, source class.
    // class0 local/unknown, class1 scene direct, class2 proved main source.
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

// The codec validates the complete profile. At the balance boundary reject
// malformed gains/counts too; all authored light shapes use the shared evaluator.
inline bool PicaFragmentBalanceProfileSupported(const PicaEnvironmentLighting& e) noexcept {
    const auto gain = [](float x) { return std::isfinite(x) && x >= 0 && x <= 2; };
    if (!e.Enabled || !gain(e.NativeDirectStrength) || !gain(e.NativeAmbientStrength) ||
        !gain(e.NativeLocalStrength) || !gain(e.AmbientIntensity) ||
        !std::isfinite(e.FogStrength) || e.FogStrength < 0 || e.FogStrength > 1 ||
        !std::isfinite(e.KeyAzimuthOffset) || !std::isfinite(e.KeyElevationOffset) ||
        e.KeyDirection > PicaEnvironmentKeyDirection::SceneAmbient ||
        e.PlacedLightCount > e.PlacedLights.size()) return false;
    for (float c : e.AmbientColor) if (!gain(c)) return false;
    for (const auto& light : e.Lights) {
        if (!std::isfinite(light.Intensity) || light.Intensity < 0 || light.Intensity > 4) return false;
        for (float c : light.Color) if (!gain(c)) return false;
        for (float d : light.DirectionTowardSource) if (!std::isfinite(d)) return false;
    }
    return true;
}

// A product of native primary AND secondary RGB uses secondary as a material
// factor, not an additive highlight. Replacing primary with the profile must
// not zero that factor. Prove only direct RGB product/replay chains; no actor,
// texture, colour, normal or title identity participates. Other operations
// conservatively lose the proof (an additive secondary is NOT a factor).
[[nodiscard]] constexpr bool PicaFragmentSecondaryMaterialFactor(const PicaTevProgram& program) noexcept {
    uint32_t previous=0, buffer=0, nextBuffer=0;
    for (size_t stage=0;stage<program.Stages.size();++stage) {
        const auto& words=program.Stages[stage];
        const auto operand=[&](uint32_t input) {
            if (((words[1]>>(4*input))&15U)!=0) return 0U;
            uint32_t source=(words[0]>>(4*input))&15U;
            if (stage==0 && input<2 && source==15) source=(words[0]>>8)&15U;
            if (source==15) return previous;
            if (source==13) return buffer;
            return source==1 ? 1U : source==2 ? 2U : 0U;
        };
        const uint32_t op=words[2]&15U;
        const uint32_t factors=op==0 ? operand(0) : op==1 ? operand(0)|operand(1) : 0U;
        previous=factors;
        buffer=nextBuffer;
        if (stage<4 && (program.Control[0]&(1U<<(8+stage)))) nextBuffer=factors;
    }
    return previous==3U;
}

inline void ApplyPicaFragmentSecondaryMaterialFactor(PicaFragmentBalanceUniforms& balance,
    const PicaTevProgram& program) noexcept {
    balance.Control[2]=PicaFragmentSecondaryMaterialFactor(program) ? 1U : 0U;
    if (balance.Control[2]!=0)
        for (auto& gain:balance.Gains)
            if (gain[3]!=0) gain[1]=1; // Captured Scene material factor, not another key.
}

// Profiles now apply ambient, keys, rim, fog and placed lights on native
// fragment surfaces too. Invalid profiles retain the capability-safe native
// fallback; other titles never enter this policy.
[[nodiscard]] inline PicaEnvironmentLighting PicaFragmentLitFrameEnvironment(
    const PicaEnvironmentLighting& e, bool nativeFragmentLighting) noexcept {
    if (!nativeFragmentLighting || !e.Enabled) return e;
    if (PicaFragmentBalanceProfileSupported(e)) return e;
    PicaEnvironmentLighting off;
    off.ScopeIdentity = e.ScopeIdentity;
    off.PointLights = e.PointLights;
    off.CasterFaces = e.CasterFaces;
    return off;
}

// The light's function decides what the profile replaces. A slot the title
// adapter classified as Scene must be one of the environment's own
// directionals (records whose owner it proved: PicaSceneDirectionalLights
// Scope Scene). The main one admits the separate shared key; all Scene
// slots follow the native direct policy; their ambient and the global ambient (the scene
// ambient) take the profile's ambient gain. Every other slot (Local or
// Unknown: lamps, actor and room lights) keeps its whole native light,
// diffuse, specular and its own ambient, at the profile's local gain, and an
// environment record the adapter did not prove is never replaced. A Scene
// slot without such a record keeps the whole draw native instead of guessing
// which term it was.
inline std::optional<PicaFragmentBalanceUniforms> BuildPicaFragmentBalanceUniforms(
    bool capability, const PicaFragmentLightingLayout& layout,
    const PicaNativeLightBinding& binding, const PicaEnvironmentLighting& e) noexcept {
    if (!capability || !PicaFragmentBalanceProfileSupported(e) || !layout.Valid()) return std::nullopt;
    const auto* scene=binding.SceneDirectionals.get();
    if (scene && scene->Count>scene->Lights.size()) return std::nullopt;
    const auto* main=scene ? FindPicaSceneMainDirectional(*scene) : nullptr;
    for (uint32_t a=0;scene && a<scene->Count;++a) {
        if (scene->Lights[a].SourceIdentity==0) return std::nullopt;
        for (uint32_t b=0;b<a;++b)
            if (scene->Lights[a].SourceIdentity==scene->Lights[b].SourceIdentity) return std::nullopt;
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
        for (uint32_t j=0;j<binding.Sources.size();++j)
            if (source.SourceIdentity!=0 && j!=i && (out.Control[0]&(1U<<j)) &&
                binding.Sources[j].SourceIdentity==source.SourceIdentity)
                return std::nullopt;
        out.Control[0]|=1U<<i;
        const bool directional=layout.Lights[i].Directional;
        if (source.Kind!=PicaNativeLightSource::Type::Unavailable &&
            directional != (source.Kind==PicaNativeLightSource::Type::Directional)) return std::nullopt;
        const PicaSceneDirectionalLight* nativeScene=nullptr;
        if (source.Scope==PicaNativeLightScope::Scene) {
            if (!scene || scene->Scope!=PicaNativeLightScope::Scene) return std::nullopt;
            for (uint32_t j=0;j<scene->Count;++j)
                if (scene->Lights[j].SourceIdentity==source.SourceIdentity) nativeScene=&scene->Lights[j];
            if (!nativeScene) return std::nullopt;
        }
        if (nativeScene && !directional) return std::nullopt;
        auto& gain=out.Gains[i];
        gain={e.NativeLocalStrength,e.NativeLocalStrength,e.NativeLocalStrength,0};
        if (nativeScene) {
            gain={e.NativeDirectStrength,e.NativeDirectStrength,ambient,1};
            if (nativeScene==main) {
                if (out.Control[1]!=8) return std::nullopt;
                out.Control[1]=i;
                gain[3]=2;
            }
        }
    }
    return out;
}

// Prepare only the balance variant. The captured native ledger owns all
// native ambient/direct; shared helpers own the explicit additions, exactly
// once. A scene-following key must not invent sunlight on a local-only room.
inline void PreparePicaFragmentProfileSurface(PicaSurfaceLightingUniforms& u,
    const PicaFragmentBalanceUniforms& balance, const PicaEnvironmentLighting& e) noexcept {
    u.Control[2]=0;
    u.SceneDirections={};
    u.SceneDirectionalColors={};
    u.ResolvedAmbient=u.EnvironmentAmbient;
    if (balance.Control[1]==8 && e.KeyDirection!=PicaEnvironmentKeyDirection::Authored) {
        u.EnvironmentDirections[0]={};
        u.EnvironmentColors[0]={};
    }
}

// Native points already present on this draw remain in the captured ledger.
// Never add them twice. The dynamic-light adapter can instead remove the
// native contribution when it supplies a renderer-owned replacement.
inline PicaNativeLightBinding PicaFragmentProfileBinding(const PicaNativeLightBinding& binding,
    const PicaFragmentLightingLayout& layout) {
    auto out=binding;
    if (!binding.ScenePoints) return out;
    auto points=std::make_shared<PicaScenePointLights>(*binding.ScenePoints);
    points->Count=0;
    for (uint32_t n=0;n<std::min<uint32_t>(binding.ScenePoints->Count,binding.ScenePoints->Lights.size());++n) {
        const auto& point=binding.ScenePoints->Lights[n];
        bool native=false;
        for (uint32_t s=0;s<layout.ActiveLightCount;++s) {
            const auto i=layout.LightPermutation[s];
            native |= i<binding.Sources.size() && point.SourceIdentity!=0 &&
                binding.Sources[i].SourceIdentity==point.SourceIdentity;
        }
        if (!native) points->Lights[points->Count++]=point;
    }
    out.ScenePoints=std::move(points);
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
        // Native fragment normal after bump decoding and quaternion rotation,
        // not the unrotated tangent-space pica_surface_normal local.
        vec3 normal = pica_surface_unit(pica_lighting_normal);
        vec3 unused;
        vec3 added = pica_surface_native_response(normal, 0u, unused);
        bool identity = pica_surface.balance_parameters.x == 1.0 && all(equal(added,vec3(0.0)));
        for (uint slot=0u;slot<lp.control.x;++slot) {
            uint i=lp.lights[slot].x;
            vec4 gains=pica_surface.balance_gains[i];
            vec3 dg=vec3(gains.x), sg=vec3(gains.y);
            vec3 visibility=pica_surface_shadow(int(i));
            dg*=visibility;
            // A Scene secondary material factor must not shadow the primary
            // response a second time. Local-light gains/visibility still apply.
            if ((pica_surface.balance_control.z & 1u) == 0u || gains.w == 0.0)
                sg*=visibility;
            bool unchanged=all(equal(dg,vec3(1.0))) && gains.z==1.0;
            balance_primary += unchanged ? balance_combined[slot] :
                balance_diffuse[slot]*dg + balance_ambient[slot]*gains.z;
            balance_secondary += balance_specular[slot]*sg;
            balance_ambient_sum += balance_ambient[slot]*gains.z;
            identity = identity && unchanged && all(equal(sg,vec3(1.0)));
        }
        balance_primary += fragment_uniforms.lighting_global_ambient.rgb * pica_surface.balance_parameters.x + added;
        balance_ambient_sum += fragment_uniforms.lighting_global_ambient.rgb * pica_surface.balance_parameters.x +
            pica_surface.environment_ambient.rgb;
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
