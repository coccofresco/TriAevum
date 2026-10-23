#pragma once

// Private to the gfx_vulkan_pica*.cpp units: their includes and the helpers they share. The anonymous
// namespace keeps the helpers local to each unit, as when they lived in one file.
#ifdef ENABLE_OOT3D_VULKAN
#include <bit>
#include "fast/oot3d/pica_guide_diagnostics.h"
#include "fast/oot3d/outline_diagnostics.h"
#include "fast/oot3d/outline_occlusion_pass.h"

#include "fast/backends/gfx_vulkan.h"
#include "oot3d/renderer/ui_presentation_layout.h"
#include "fast/oot3d/pica_shadow2d.h"
#include "fast/oot3d/pica_display_composition.h"
#include "fast/oot3d/display_effect_plan.h"
#include "fast/oot3d/display_effect_resources.h"
#include "fast/oot3d/effect_graph_physical_plan.h"
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/scene_view_runtime.h"
#include "fast/oot3d/texture_catalog_runtime.h"
#include "fast/oot3d/grass_interaction_bridge.h"
#include "fast/oot3d/grass_primitive_topology.h"
#include "fast/oot3d/grass_render_telemetry.h"
#include "fast/oot3d/grass_surface_eligibility.h"
#include "fast/oot3d/grass_texture_source_cache.h"
#include "fast/oot3d/grass_scene_bridge.h"
#include "fast/oot3d/visual_clock.h"
#include "fast/oot3d/render_resolution_policy.h"
#include "fast/oot3d/pica_toon_shader.h"
#include "fast/oot3d/pica_toon_telemetry.h"
#include "fast/oot3d/pica_scanout_effects.h"
#include "fast/oot3d/pica_ambient_occlusion_guide.h"
#include "fast/oot3d/pica_scene_domain_guide.h"
#include "fast/oot3d/pica_grass_texture_coordinates.h"
#include "fast/oot3d/pica_guide_sampling_barriers.h"
#include "fast/oot3d/pica_reactive_mask.h"
#include "fast/oot3d/pica_nri_shader_contract.h"
#include "fast/oot3d/pica_uber_effect_program.h"
#include "fast/oot3d/pica_nri_vertex_input.h"
#include "fast/oot3d/pica_nri_pipeline_state.h"
#include "fast/renderer3ds/pica_pipeline_identity.h"
#include "fast/renderer3ds/pica_surface_lighting.h"
#include "fast/renderer3ds/pica_tev_program.h"
#include "fast/oot3d/pica_uniform_layout.h"
#include "fast/oot3d/pica_nri_draw_ownership.h"
#include "fast/oot3d/pica_directional_shadow_lighting.h"
#include "fast/oot3d/pica_scene_semantics.h"
#include "fast/oot3d/pica_alpha_coverage_policy.h"
#include "fast/oot3d/linear_scene_color.h"
#include "fast/oot3d/upscaler_guides.h"
#include "fast/oot3d/pica_reflection_material.h"
#include "fast/oot3d/reflection_provider.h"
#include "fast/oot3d/pica_texture_decode.h"
#include "oot3d/renderer/azahar_texture_pack.h"
#include <spdlog/spdlog.h>
#include "fast/backends/gfx_native_pica_layout.h"
#include "fast/renderer3ds/pica_ui_canvas.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include "fast/renderer3ds/slow_step_trace.h"

namespace Fast {
namespace {

constexpr uint64_t kCustomTextureUploadBudgetBytes =
    32ULL * 1024ULL * 1024ULL;

void CheckNativeVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) +
                                 " failed with VkResult " +
                                 std::to_string(static_cast<int>(result)));
    }
}

void SetNativeError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

template <typename Handle>
uintptr_t ToPicaSceneNativeHandle(Handle handle) noexcept {
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<uintptr_t>(handle);
    } else {
        return static_cast<uintptr_t>(handle);
    }
}

uint64_t HashNativeBytes(std::span<const uint8_t> bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (const uint8_t value : bytes) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

using Oot3d::ToNativeVkCompare;

using Oot3d::ToNativeVkStencil;

using Oot3d::ToNativeVkLogic;

using Oot3d::ToNativeVkTopology;

Oot3d::PicaSceneTopology ToPicaSceneTopology(
    GfxNativePicaTopology topology) {
    switch (topology) {
        case GfxNativePicaTopology::TriangleList:
        case GfxNativePicaTopology::GeometryShader:
            return Oot3d::PicaSceneTopology::TriangleList;
        case GfxNativePicaTopology::TriangleStrip:
            return Oot3d::PicaSceneTopology::TriangleStrip;
        case GfxNativePicaTopology::TriangleFan:
            throw std::runtime_error(
                "native PICA triangle fan requires index expansion");
    }
    throw std::runtime_error("invalid native PICA topology");
}

Oot3d::PicaSceneCullMode ToPicaSceneCullMode(const GfxNativePicaDrawView& draw) noexcept {
    return draw.CullMode == GfxNativeCullMode::KeepAll ? Oot3d::PicaSceneCullMode::None
        : draw.FramebufferFlipped                      ? Oot3d::PicaSceneCullMode::Front
                                                       : Oot3d::PicaSceneCullMode::Back;
}

Oot3d::PicaSceneFrontFace ToPicaSceneFrontFace(const GfxNativePicaDrawView& draw) noexcept {
    return draw.CullMode == GfxNativeCullMode::KeepCounterClockwise ? Oot3d::PicaSceneFrontFace::Clockwise
                                                                     : Oot3d::PicaSceneFrontFace::CounterClockwise;
}

// A draw composited over the completed scene (the schedule's overlay tail):
// native UI, or a scene draw the title lights as an object of its own (the
// title logo). The scene boundary before the first of them resolves the
// scene stages; what they draw is laid over that result and never read by it.
bool NativeSceneOverlayDraw(const GfxNativePicaDrawView& draw) noexcept {
    return draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Ui ||
           (draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Scene &&
            draw.Composition.IsolatedLighting);
}

// Opaque-geometry admission from the draw's own state, shared by shadow
// casters and the scene distance field: depth-writing, colour-writing and
// without an alpha cutout, which a depth-only caster would draw solid.
bool AdmitsOpaqueSceneGeometry(const GfxNativePicaDrawView& draw) noexcept {
    return draw.FragmentOperationMode == 0U && draw.DepthTestEnabled && draw.DepthWriteEnabled &&
           !draw.AlphaTestEnabled && (draw.ColorWriteMask & 0x7U) != 0U;
}

// An alpha test that can discard fragments; "Always" passes every fragment.
bool RunsDiscardingAlphaTest(const GfxNativePicaDrawView& draw) noexcept {
    return draw.AlphaTestEnabled && draw.AlphaCompare != GfxNativePicaCompareFunction::Always;
}

// Shadow-caster admission: depth-writing, colour-writing geometry. An
// alpha-tested caster runs its native alpha test in the map, which needs the
// published fragment hooks, the fragment uniforms and directly sampled native
// textures (no packed shadow image or GPU-produced copy). A test that never
// passes casts nothing.
bool AdmitsShadowCasterGeometry(const GfxNativePicaDrawView& draw) noexcept {
    if (draw.FragmentOperationMode != 0U || !draw.DepthTestEnabled || !draw.DepthWriteEnabled ||
        (draw.ColorWriteMask & 0x7U) == 0U)
        return false;
    if (!RunsDiscardingAlphaTest(draw))
        return true;
    return draw.AlphaCompare != GfxNativePicaCompareFunction::Never &&
           draw.FragmentShaderHooks.ValidFor(draw.FragmentShaderSource) &&
           draw.FragmentShaderHooks.Supports(Renderer3ds::PicaShaderHook::AfterNativeTev) &&
           draw.FragmentShaderHooks.Supports(Renderer3ds::PicaShaderHook::BeforeDepth) &&
           draw.FragmentUniformBytes.size() == Oot3d::kPicaPackedFragmentUniformSize &&
           std::all_of(draw.Textures.begin(), draw.Textures.end(), [](const auto& texture) {
               return texture.Slot < 3U && texture.CopyReferences.empty() &&
                      (texture.NativeType == 0U || (texture.Slot == 0U && texture.NativeType == 3U));
           });
}

// Native state that shapes an alpha-tested caster: TEV stages, constants and
// combiner buffer, the alpha test, texture LOD bias, the lighting and
// procedural-texture programs and the sampled contents. Light and fog values
// leave the cutout unchanged, so they do not invalidate cached pages.
uint64_t HashAlphaCasterMaterial(const GfxNativePicaDrawView& draw,
                                 const std::array<uint64_t, 3>& replacementHashes) {
    const auto bytes = draw.FragmentUniformBytes;
    uint64_t hash = HashNativeBytes(bytes.first(Oot3d::kPicaPackedFragmentDepthScaleOffset));
    const auto mix = [&hash](uint64_t value) {
        hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    };
    mix(HashNativeBytes(bytes.subspan(Oot3d::kPicaPackedFragmentTextureLodBiasOffset, 16U)));
    mix(HashNativeBytes(bytes.subspan(Oot3d::kPicaPackedFragmentTevProgramOffset,
                                      Oot3d::kPicaPackedFragmentProcTexOffset -
                                          Oot3d::kPicaPackedFragmentTevProgramOffset)));
    uint32_t textureConfig = 0;
    std::memcpy(&textureConfig, bytes.data() + Oot3d::kPicaPackedFragmentControlOffset + 8U, sizeof(textureConfig));
    if ((textureConfig & (1U << 10U)) != 0U)
        mix(HashNativeBytes(bytes.subspan(Oot3d::kPicaPackedFragmentProcTexOffset)));
    for (const auto& texture : draw.Textures) {
        mix(texture.Slot);
        mix(texture.NativeContentHash);
        mix(replacementHashes[texture.Slot]);
        mix(uint64_t{texture.NativeWrapS} | uint64_t{texture.NativeWrapT} << 8U |
            uint64_t{texture.MinLinear} << 16U | uint64_t{texture.MagLinear} << 17U |
            uint64_t{texture.MipLinear} << 18U);
    }
    return hash;
}

VkFormat ToNativeVkVertexFormat(GfxNativePicaVertexFormat format,
                                uint8_t components) {
    if (components == 0 || components > 4) {
        throw std::runtime_error("invalid native PICA vertex component count");
    }
    static constexpr std::array<VkFormat, 4> kSignedByte{
        VK_FORMAT_R8_SSCALED, VK_FORMAT_R8G8_SSCALED,
        VK_FORMAT_R8G8B8_SSCALED, VK_FORMAT_R8G8B8A8_SSCALED};
    static constexpr std::array<VkFormat, 4> kUnsignedByte{
        VK_FORMAT_R8_USCALED, VK_FORMAT_R8G8_USCALED,
        VK_FORMAT_R8G8B8_USCALED, VK_FORMAT_R8G8B8A8_USCALED};
    static constexpr std::array<VkFormat, 4> kSignedShort{
        VK_FORMAT_R16_SSCALED, VK_FORMAT_R16G16_SSCALED,
        VK_FORMAT_R16G16B16_SSCALED, VK_FORMAT_R16G16B16A16_SSCALED};
    static constexpr std::array<VkFormat, 4> kFloat{
        VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};
    switch (format) {
        case GfxNativePicaVertexFormat::SignedByte:
            return kSignedByte[components - 1U];
        case GfxNativePicaVertexFormat::UnsignedByte:
            return kUnsignedByte[components - 1U];
        case GfxNativePicaVertexFormat::SignedShort:
            return kSignedShort[components - 1U];
        case GfxNativePicaVertexFormat::Float:
            return kFloat[components - 1U];
    }
    throw std::runtime_error("invalid native PICA vertex format");
}

GfxNativeTextureWrap DecodeNativePicaWrap(uint8_t wrap) {
    switch (wrap) {
        case 0:
            return GfxNativeTextureWrap::ClampToEdge;
        case 2:
        case 6:
        case 7:
            return GfxNativeTextureWrap::Repeat;
        case 3:
            return GfxNativeTextureWrap::MirroredRepeat;
        case 1:
            throw std::runtime_error(
                "native PICA clamp-to-border requires border color state");
        case 4:
        case 5:
            throw std::runtime_error(
                "native PICA asymmetric wrap mode is unsupported");
        default:
            throw std::runtime_error("invalid native PICA texture wrap mode");
    }
}

std::optional<Oot3d::GrassTextureWrap>
DecodeNativePicaGrassWrap(uint8_t wrap) {
    switch (wrap) {
        case 0:
            return Oot3d::GrassTextureWrap::Clamp;
        case 2:
        case 6:
        case 7:
            return Oot3d::GrassTextureWrap::Repeat;
        case 3:
            return Oot3d::GrassTextureWrap::Mirror;
        default:
            return std::nullopt;
    }
}

using Oot3d::UsesTranslucentBlend;

VkFilter ToNativePicaVkFilter(GfxNativeTextureFilter filter) {
    switch (filter) {
        case GfxNativeTextureFilter::Linear:
        case GfxNativeTextureFilter::LinearMipmapNearest:
        case GfxNativeTextureFilter::LinearMipmapLinear:
            return VK_FILTER_LINEAR;
        default:
            return VK_FILTER_NEAREST;
    }
}

VkSamplerMipmapMode ToNativePicaVkMipmapMode(
    GfxNativeTextureFilter filter) {
    return filter == GfxNativeTextureFilter::NearestMipmapLinear ||
                   filter == GfxNativeTextureFilter::LinearMipmapLinear
        ? VK_SAMPLER_MIPMAP_MODE_LINEAR
        : VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

VkSamplerAddressMode ToNativePicaVkAddressMode(
    GfxNativeTextureWrap wrap) {
    switch (wrap) {
        case GfxNativeTextureWrap::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case GfxNativeTextureWrap::MirroredRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case GfxNativeTextureWrap::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

using Oot3d::ToNativeVkBlendOperation;

using Oot3d::ToNativeVkBlendFactor;

VkImageAspectFlags NativeDepthAspect(VkFormat format) {
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        format == VK_FORMAT_D24_UNORM_S8_UINT) {
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return aspect;
}

uint32_t NativePicaColorBytesPerPixel(uint8_t format) {
    return format == 0U ? 4U : (format == 1U ? 3U : 2U);
}

uint32_t NativePicaDepthBytesPerPixel(uint8_t format) {
    return format == 0U ? 2U : (format == 2U ? 3U : 4U);
}

uint32_t NativePicaFillElementSize(uint16_t control) {
    return (control & (1U << 9U)) != 0U
               ? 4U
               : ((control & (1U << 8U)) != 0U ? 3U : 2U);
}

uint8_t NativePicaFillByte(const GfxNativePicaMemoryFillView& fill,
                           uint32_t index) {
    const uint32_t shift = (index % NativePicaFillElementSize(fill.Control)) * 8U;
    return static_cast<uint8_t>(fill.Value >> shift);
}

float NativePicaUnorm(uint32_t value, uint32_t maximum) {
    return static_cast<float>(value) / static_cast<float>(maximum);
}

VkClearColorValue DecodeNativePicaClearColor(
    const GfxNativePicaMemoryFillView& fill, uint8_t format) {
    const uint8_t b0 = NativePicaFillByte(fill, 0U);
    const uint8_t b1 = NativePicaFillByte(fill, 1U);
    const uint8_t b2 = NativePicaFillByte(fill, 2U);
    const uint8_t b3 = NativePicaFillByte(fill, 3U);
    std::array<float, 4> color{};
    if (format == 0U) {
        color = {NativePicaUnorm(b3, 0xFFU), NativePicaUnorm(b2, 0xFFU),
                 NativePicaUnorm(b1, 0xFFU), NativePicaUnorm(b0, 0xFFU)};
    } else if (format == 1U) {
        color = {NativePicaUnorm(b2, 0xFFU), NativePicaUnorm(b1, 0xFFU),
                 NativePicaUnorm(b0, 0xFFU), 1.0F};
    } else {
        const uint16_t packed = static_cast<uint16_t>(b0) |
                                static_cast<uint16_t>(b1) << 8U;
        if (format == 2U) {
            color = {NativePicaUnorm((packed >> 11U) & 0x1FU, 0x1FU),
                     NativePicaUnorm((packed >> 5U) & 0x3FU, 0x3FU),
                     NativePicaUnorm(packed & 0x1FU, 0x1FU), 1.0F};
        } else if (format == 3U) {
            color = {NativePicaUnorm((packed >> 11U) & 0x1FU, 0x1FU),
                     NativePicaUnorm((packed >> 6U) & 0x1FU, 0x1FU),
                     NativePicaUnorm((packed >> 1U) & 0x1FU, 0x1FU),
                     static_cast<float>(packed & 1U)};
        } else {
            color = {NativePicaUnorm((packed >> 12U) & 0xFU, 0xFU),
                     NativePicaUnorm((packed >> 8U) & 0xFU, 0xFU),
                     NativePicaUnorm((packed >> 4U) & 0xFU, 0xFU),
                     NativePicaUnorm(packed & 0xFU, 0xFU)};
        }
    }
    VkClearColorValue result{};
    std::copy(color.begin(), color.end(), result.float32);
    return result;
}

VkClearDepthStencilValue DecodeNativePicaClearDepthStencil(
    const GfxNativePicaMemoryFillView& fill, uint8_t format) {
    const uint32_t b0 = NativePicaFillByte(fill, 0U);
    const uint32_t b1 = NativePicaFillByte(fill, 1U);
    const uint32_t b2 = NativePicaFillByte(fill, 2U);
    const uint32_t depth = format == 0U ? b0 | (b1 << 8U)
                                        : b0 | (b1 << 8U) | (b2 << 16U);
    return {NativePicaUnorm(depth, format == 0U ? 0xFFFFU : 0xFFFFFFU),
            format == 3U ? NativePicaFillByte(fill, 3U) : 0U};
}

bool BuildNativePicaMemoryFillSmoke(
    uint64_t renderTargetNamespace, uint32_t colorAddress,
    uint32_t depthAddress, uint64_t colorEnd, uint64_t depthEnd,
    GfxNativePicaMemoryFillView& fill) {
    static const bool smokeEnabled = [] {
        const char* smoke = std::getenv("OOT3D_GRAPHICS_TEST_PICA_MEMORY_FILL");
        return smoke != nullptr && std::string_view(smoke) == "1";
    }();
    if (!smokeEnabled)
        return false;
    const uint64_t start =
        std::min<uint64_t>(colorAddress, depthAddress);
    const uint64_t end = std::max(colorEnd, depthEnd);
    if (start == 0U || end > std::numeric_limits<uint32_t>::max())
        return false;
    fill = {
        renderTargetNamespace,
        static_cast<uint32_t>(start),
        static_cast<uint32_t>(end),
        0xFFFFFFFFU,
        static_cast<uint16_t>(1U | (1U << 9U)),
    };
    return true;
}


} // namespace

// Texture identity and mip helpers, defined in gfx_vulkan_pica_textures.cpp.
std::optional<uint64_t> ResolveNativeTextureBaseLevelContentHash(
    const GfxNativePicaTextureView& texture);
std::optional<size_t> NativePicaDecodedMipChainSize(
    uint32_t width, uint32_t height, uint32_t levels);
GfxNativeTextureFilter NativePicaMinFilter(
    bool minLinear, bool mipLinear, uint32_t mipLevels);

} // namespace Fast

#endif
