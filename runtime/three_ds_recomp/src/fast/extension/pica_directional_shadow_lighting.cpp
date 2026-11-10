#include "fast/extension/pica_directional_shadow_lighting.h"

#include "fast/extension/pica_directional_shadow_semantics.h"
#include "fast/renderer3ds/virtual_shadow_receiver_shader.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace Fast::Extension {
namespace {

uint64_t LightingVariantKey(uint64_t original, bool fragmentPrimary) {
    constexpr uint64_t kOffset = 1469598103934665603ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    constexpr std::string_view kSalt = "OOT3D_PICA_VIRTUAL_SHADOW_LIGHTING_V12";
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
// The visibility of a fragment is looked up once, in the main prologue
// (oot3d_directional_shadow_prepare), and every shadowed term of a matched
// light reads that value: inlined at each term inside the lighting loops, the
// receiver held its registers and the loops' at once, even where no fragment
// ran it. A smooth-shaded receiver passes its vertex normal (view space) for
// the normal bias, others zero. As Godot, the contact shadows join the map by
// min().
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
// Once per fragment, before any lighting, when a light of the draw matches.
void oot3d_directional_shadow_prepare(vec3 normal) {
    if (uint(pica_vsm.receiver.w) != 0u) oot3d_directional_shadow_visibility(normal);
}
vec3 oot3d_directional_shadow_prepared() { return oot3d_directional_shadow_value; }
)glsl";
}

} // namespace

std::string PicaDirectionalShadowLightingDeclarations() { return LightingDeclarations(); }

std::string_view PicaDirectionalShadowLightingPrologue() {
    return "\n    oot3d_directional_shadow_prepare(pica_surface_normal);\n";
}

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

namespace {

// One layout of pica_vsm_layout_visibility: true when every point of the
// light-space box reads visibility exactly 1.0 (see the header).
bool LayoutFullyLit(const Renderer3ds::ShadowBounds& world, const Renderer3ds::VirtualShadowLayout& layout,
                    const std::array<uint32_t, Renderer3ds::kVirtualShadowTableEntries>& table,
                    float maximumDistance, float normalBias, float extraGuard) noexcept {
    const auto& m = layout.WorldToLight;
    // An affine (orthographic) light: the shader's divide by w is by 1.
    if (!layout.Valid() || layout.LevelCount > Renderer3ds::kVirtualShadowLevelCount || m[3] != 0.0F ||
        m[7] != 0.0F || m[11] != 0.0F || m[15] != 1.0F)
        return false;
    // The normal bias moves a point by at most NormalBias texels of the level
    // it reads (a vector of length <= 1), at most the coarsest level's texel.
    const float coarsestTexel = layout.Levels[layout.LevelCount - 1U].PageWorldSize / 128.0F;
    Renderer3ds::ShadowBounds grown = world;
    for (size_t axis = 0; axis < 3; ++axis) {
        const float extent = std::max(std::abs(world.Minimum[axis]), std::abs(world.Maximum[axis]));
        const float guard = normalBias * coarsestTexel + 1.0F + extent * 1.0e-4F + extraGuard;
        grown.Minimum[axis] -= guard;
        grown.Maximum[axis] += guard;
    }
    const auto light = Renderer3ds::TransformShadowBounds(grown, m);
    if (!light.Available)
        return false;
    for (size_t axis = 0; axis < 3; ++axis)
        if (!std::isfinite(light.Minimum[axis]) || !std::isfinite(light.Maximum[axis]))
            return false;
    const auto& camera = layout.CameraLight;
    const auto gap = [&](size_t axis) {
        return std::max({0.0F, light.Minimum[axis] - camera[axis], camera[axis] - light.Maximum[axis]});
    };
    // Distance: world_distance >= MaximumDistance returns visibility 1.0.
    const double depthScale = 2.0 * std::max(double(maximumDistance) * 4.0, 65536.0);
    const double gx = gap(0), gy = gap(1), gz = double(gap(2)) * depthScale;
    if (std::sqrt(gx * gx + gy * gy + gz * gz) >= double(maximumDistance) * 1.001 + 1.0)
        return true;
    // Pages: every level the loop may visit holds only empty or fully lit
    // entries under the box (mix of equal 1.0 values stays 1.0).
    const float finest = layout.Levels[0].PageWorldSize;
    if (!(finest > 0.0F))
        return false;
    const float planeMin = std::max(gap(0), gap(1));
    const float planeMax =
        std::max({std::abs(light.Minimum[0] - camera[0]), std::abs(light.Maximum[0] - camera[0]),
                  std::abs(light.Minimum[1] - camera[1]), std::abs(light.Maximum[1] - camera[1])});
    const int count = int(layout.LevelCount);
    int first = int(std::ceil(std::log2(std::max(planeMin * 0.999F / (3.0F * finest), 1.0F))));
    first = std::clamp(first - 1, 0, count - 1);
    for (int level = first; level < count; ++level) {
        const auto& grid = layout.Levels[size_t(level)];
        const float page = grid.PageWorldSize;
        if (!(page > 0.0F))
            return false;
        const double lowX = std::floor(double(light.Minimum[0]) / page) - grid.OriginX;
        const double highX = std::floor(double(light.Maximum[0]) / page) - grid.OriginX;
        const double lowY = std::floor(double(light.Minimum[1]) / page) - grid.OriginY;
        const double highY = std::floor(double(light.Maximum[1]) / page) - grid.OriginY;
        // Cells outside the 8x8 grid read entry 0 (skipped).
        const int x0 = int(std::clamp(lowX, 0.0, 8.0)), x1 = int(std::clamp(highX, -1.0, 7.0));
        const int y0 = int(std::clamp(lowY, 0.0, 8.0)), y1 = int(std::clamp(highY, -1.0, 7.0));
        const bool coversGrid = lowX >= 0.0 && highX <= 7.0 && lowY >= 0.0 && highY <= 7.0;
        bool allPresent = coversGrid;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const uint32_t entry = table[size_t(level * 64 + y * 8 + x)];
                if (entry != 0U && entry != 0xffffffffU)
                    return false;
                allPresent = allPresent && entry != 0U;
            }
        // Every point found a page here and none blends into a coarser level.
        if (allPresent && x0 <= x1 && y0 <= y1 && planeMax * 1.001F + 1.0F <= 2.75F * page)
            return true;
    }
    return true;
}

} // namespace

bool PicaDirectionalShadowReceiverFullyLit(const Renderer3ds::ShadowBounds& worldBounds,
                                           const PicaDirectionalShadowHistoryState& history,
                                           float extraGuard) noexcept {
    if (!worldBounds.Available || !history.Valid() || history.ContactShadows || !(extraGuard >= 0.0F) ||
        !std::isfinite(extraGuard))
        return false;
    const float normalBias = std::clamp(history.NormalBias, 0.0F, 10.0F);
    const float maximumDistance = history.Virtual.MaximumDistance;
    if (!(maximumDistance > 0.0F))
        return false;
    if (!LayoutFullyLit(worldBounds, history.Virtual, history.PageTable, maximumDistance, normalBias, extraGuard))
        return false;
    // While a dissolve runs both layouts are read and mixed.
    if (history.Dissolving() &&
        !LayoutFullyLit(worldBounds, history.Older->Virtual, history.Older->PageTable, maximumDistance, normalBias, extraGuard))
        return false;
    return true;
}

} // namespace Fast::Extension
