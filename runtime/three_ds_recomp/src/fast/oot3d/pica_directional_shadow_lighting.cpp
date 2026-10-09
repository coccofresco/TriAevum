#include "fast/oot3d/pica_directional_shadow_lighting.h"

#include "fast/oot3d/pica_directional_shadow_semantics.h"
#include "fast/renderer3ds/virtual_shadow_receiver_shader.h"

#include <algorithm>
#include <string_view>

namespace Fast::Oot3d {
namespace {

uint64_t LightingVariantKey(uint64_t original, bool fragmentPrimary) {
    constexpr uint64_t kOffset = 1469598103934665603ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    constexpr std::string_view kSalt = "OOT3D_PICA_VIRTUAL_SHADOW_LIGHTING_V10";
    uint64_t hash = kOffset;
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        hash ^= static_cast<uint8_t>(original >> shift);
        hash *= kPrime;
    }
    for (const char value : kSalt) {
        hash ^= static_cast<uint8_t>(value);
        hash *= kPrime;
    }
    hash ^= fragmentPrimary ? 1U : 0U;
    return hash * kPrime;
}

std::string LightingDeclarations() {
    return std::string(Renderer3ds::VirtualShadowReceiverShader()) + R"glsl(
// Contact shadows of the shadow light (screen space, this sample), with the
// light's shadow strength as the map's (Godot's light shadow opacity).
layout(set=0,binding=29) uniform sampler2D pica_contact_shadow;
float oot3d_contact_shadow_visibility(vec3 position) {
    if (pica_vsm.receiver.y < 0.5) return 1.0;
    vec4 clip = pica_vsm.contact_projection * vec4(position, 1.0);
    if (clip.w <= 0.0) return 1.0;
    vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;
    float contact = textureLod(pica_contact_shadow, uv, 0.0).r;
    return max(0.0, 1.0 - (1.0 - contact) * clamp(pica_vsm.shadow_state.x, 0.0, 4.0));
}
// The lighting asks once per shadowed term of each matched light; the
// visibility of a fragment is looked up once. A smooth-shaded receiver passes
// its vertex normal (view space) for the normal bias, others zero. As Godot,
// the contact shadows join the map by min().
// The filter gets no receiver-plane depth slope: the slope of a flat triangle
// is constant across it and breaks at its edges, so on a smooth-shaded
// low-poly receiver the shadows showed its facets. The depth and normal
// biases cover the filter's footprint instead.
bool oot3d_directional_shadow_ready = false;
vec3 oot3d_directional_shadow_value = vec3(1.0);
vec3 oot3d_directional_shadow_visibility(vec3 normal) {
    if ((pica_vsm.extent.w & 1u) == 0u) return vec3(1.0);
    if (!oot3d_directional_shadow_ready) {
        oot3d_directional_shadow_value =
            vec3(min(pica_vsm_visibility(-pica_view, vec3(0.0), vec3(0.0), normal),
                     oot3d_contact_shadow_visibility(-pica_view)));
        oot3d_directional_shadow_ready = true;
    }
    return oot3d_directional_shadow_value;
}
)glsl";
}

} // namespace

std::string PicaDirectionalShadowLightingDeclarations() { return LightingDeclarations(); }

PicaDirectionalShadowReceiverUniforms PackPicaDirectionalShadowReceiverUniforms(
    const DirectionalShadowMatrix& viewToWorld, const PicaDirectionalShadowHistoryState& history) noexcept {
    PicaDirectionalShadowReceiverUniforms uniforms;
    PackPicaDirectionalShadowReceiverUniforms(viewToWorld, history, uniforms);
    return uniforms;
}

void PackPicaDirectionalShadowReceiverUniforms(const DirectionalShadowMatrix& viewToWorld,
                                               const PicaDirectionalShadowHistoryState& history,
                                               PicaDirectionalShadowReceiverUniforms& uniforms) noexcept {
    const auto& layout = history.Virtual;
    const bool dissolving = history.Dissolving();
    const uint32_t regions = std::max<uint32_t>(history.AtlasRegions, 1U);
    uniforms.ShadowState = { std::clamp(history.Strength, 0.0F, 4.0F),
                             1.0F / float(layout.AtlasResolution * regions), layout.MaximumDistance,
                             std::max(history.DepthBias, 0.0F) };
    uniforms.Receiver = { std::clamp(history.NormalBias, 0.0F, 10.0F), history.ContactShadows ? 1.0F : 0.0F, 0.0F,
                          0.0F };
    uniforms.ContactProjection = history.ContactShadows
        ? MultiplyDirectionalShadowMatrices(history.WorldToContactClip, viewToWorld) : DirectionalShadowMatrix{};
    uniforms.Extent = { layout.AtlasResolution, layout.AtlasResolution, std::min<uint32_t>(history.PcfRadius, 2U),
                        kPicaDirectionalShadowBoundFlag | (dissolving ? kPicaDirectionalShadowDissolveFlag : 0U) };
    uniforms.Dissolve = { dissolving ? std::clamp(history.NewerWeight, 0.0F, 1.0F) : 1.0F,
                          1.0F / float(layout.AtlasResolution), float(history.Region * layout.AtlasResolution),
                          dissolving ? float(history.Older->Region * layout.AtlasResolution) : 0.0F };
    const auto pack = [&](PicaDirectionalShadowReceiverLayout& destination,
                          const Renderer3ds::VirtualShadowLayout& source,
                          const std::array<uint32_t, Renderer3ds::kVirtualShadowTableEntries>& table) {
        destination.ViewToLight = MultiplyDirectionalShadowMatrices(source.WorldToLight, viewToWorld);
        destination.CameraLight = { source.CameraLight[0], source.CameraLight[1], source.CameraLight[2],
                                    float(source.LevelCount) };
        for (size_t level = 0; level < source.LevelCount; ++level) {
            const auto& grid = source.Levels[level];
            destination.Levels[level] = { float(grid.OriginX), float(grid.OriginY), grid.PageWorldSize, 0 };
        }
        for (size_t level = source.LevelCount; level < destination.Levels.size(); ++level)
            destination.Levels[level] = {};
        destination.PageTable = table;
    };
    pack(uniforms.Layouts[0], layout, history.PageTable);
    if (dissolving)
        pack(uniforms.Layouts[1], history.Older->Virtual, history.Older->PageTable);
}


PicaDirectionalShadowLightingInstrumentation
BuildPicaDirectionalShadowLightingInstrumentation(uint64_t originalFragmentKey, bool enabled,
                                                  const ::Oot3d::Renderer::PicaShaderHookLayout& hooks) {
    using ::Oot3d::Renderer::PicaShaderHook;
    using ::Oot3d::Renderer::PicaShaderSemantic;

    PicaDirectionalShadowLightingInstrumentation result;
    result.FragmentKey = originalFragmentKey;
    if (!enabled) {
        return result;
    }
    if (!hooks.Valid() || !hooks.Supports(PicaShaderHook::PicaLighting)) {
        result.Eligibility = PicaDirectionalShadowLightingEligibility::UnsupportedShader;
        return result;
    }

    const bool fragmentPrimary =
        hooks.Has(PicaShaderSemantic::MaterialLightingPoint) && hooks.Has(PicaShaderSemantic::SecondaryFragmentColor);
    const bool vertexPrimary = hooks.Has(PicaShaderSemantic::VertexLightingPoint) &&
                               hooks.Has(PicaShaderSemantic::PrimaryColorInput) &&
                               hooks.Has(PicaShaderSemantic::PrimaryColorConsumed);
    if (!fragmentPrimary && !vertexPrimary) {
        result.Eligibility = PicaDirectionalShadowLightingEligibility::UnsupportedShader;
        return result;
    }

    result.Declarations = LightingDeclarations();
    result.FragmentKey = LightingVariantKey(originalFragmentKey, fragmentPrimary);
    result.Eligibility = fragmentPrimary ? PicaDirectionalShadowLightingEligibility::FragmentPrimary
                                         : PicaDirectionalShadowLightingEligibility::VertexPrimary;
    return result;
}

PicaDirectionalShadowReceiverBinding
BuildPicaSurfaceShadowReceiverBinding(const DirectionalShadowMatrix& currentViewToWorld,
                                      std::span<const PicaSurfaceShadowLight> lights,
                                      const PicaDirectionalShadowHistoryState& history) noexcept {
    PicaDirectionalShadowReceiverBinding result;
    if (!history.Valid())
        return result;
    const uint16_t mask =
        ResolvePicaSurfaceDirectionalShadowMask(lights, currentViewToWorld, history.WorldLightDirectionTowardSource);
    result.Classification = mask != 0U ? PicaDirectionalShadowReceiverClass::DirectMatched
                                       : PicaDirectionalShadowReceiverClass::DirectUnmatched;
    if (mask == 0U)
        return result;
    PackPicaDirectionalShadowReceiverUniforms(currentViewToWorld, history, result.Uniforms);
    result.Uniforms.Receiver[3] = static_cast<float>(mask);
    return result;
}

} // namespace Fast::Oot3d
