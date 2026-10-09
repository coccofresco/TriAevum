#pragma once

// Private to the gfx_vulkan*.cpp backend units (not the gfx_vulkan_pica*.cpp ones): their includes
// and the helpers they share. The anonymous namespace keeps the helpers local to each unit.
#ifdef ENABLE_OOT3D_VULKAN

#include "fast/backends/gfx_vulkan.h"
#include "fast/renderer3ds/pica_geometry_guide.h"
#include "fast/renderer3ds/pica_native_fragment_binaries.h"
#include "fast/oot3d/pica_temporal_fragment_binaries.h"
#include "fast/oot3d/pica_toon_fragment_artifact.h"
#include "fast/oot3d/pica_nri_shader_contract.h"
#include "fast/oot3d/pica_uber_effect_program.h"
#include "fast/oot3d/pica_uber_fragment_binaries.h"
#include "fast/oot3d/pica_caster_fragment_binaries.h"
#include "fast/renderer/framebuffer_readback.h"
#include "fast/renderer/shaderc_compiler.h"
#include "fast/renderer/compatibility_shader_sources.h"
#include "fast/renderer3ds/vulkan_pipeline_cache_store.h"
#include "fast/renderer3ds/pica_vulkan_device_profile.h"
#include "fast/renderer3ds/pica_startup_programs.h"
#include "fast/oot3d/pica_nri_pipeline_state.h"

#include "fast/backends/gfx_sdl.h"
#include "fast/interpreter.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#ifdef _WIN32
#include <SDL2/SDL_syswm.h>
#endif
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <imgui_impl_vulkan.h>
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/ambient_occlusion_diagnostics.h"
#include "fast/oot3d/display_diagnostics.h"
#include "fast/oot3d/anti_aliasing_frame_policy.h"
#include "fast/oot3d/renderer_presentation_controller.h"
#include "fast/oot3d/render_resolution_policy.h"
#include "fast/oot3d/scene_view_runtime.h"
#include "fast/oot3d/grass_scene_bridge.h"
#include "fast/oot3d/nri_ngx_vulkan_requirements.h"
#include "fast/oot3d/vulkan_adapter_policy.h"
#include "oot3d/renderer/azahar_texture_pack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#include "fast/renderer3ds/slow_step_trace.h"

namespace Fast {

namespace {

constexpr size_t kOot3dCachedVertexBufferLimit = 1024;
constexpr uint32_t kSpirvMagic = 0x07230203U;
constexpr uint64_t kMaximumPipelineCacheBytes = 64ULL * 1024ULL * 1024ULL;

void* ResolveNriNativeWindow(GfxWindowBackendSDL2* backend) {
#ifdef _WIN32
    if (backend == nullptr || backend->GetNativeWindow() == nullptr)
        return nullptr;
    SDL_SysWMinfo windowInfo{};
    SDL_VERSION(&windowInfo.version);
    if (SDL_GetWindowWMInfo(
            static_cast<SDL_Window*>(backend->GetNativeWindow()),
            &windowInfo) != SDL_TRUE)
        return nullptr;
    return windowInfo.info.win.window;
#else
    (void)backend;
    return nullptr;
#endif
}

std::filesystem::path VulkanShaderCacheDirectory() {
#ifdef _WIN32
    const auto* overridePath = _wgetenv(L"TRIAEVUM_RENDERER_CACHE_DIR");
#else
    const auto* overridePath = std::getenv("TRIAEVUM_RENDERER_CACHE_DIR");
#endif
    if (overridePath != nullptr && *overridePath != 0) {
        const std::filesystem::path directory(overridePath);
        if (!directory.is_absolute())
            throw std::runtime_error("TRIAEVUM_RENDERER_CACHE_DIR must be absolute");
        return directory;
    }
    char* prefPath = SDL_GetPrefPath(nullptr, "oot3d_native_vulkan");
    if (prefPath == nullptr) {
        return {};
    }
    const std::filesystem::path directory =
        std::filesystem::path(prefPath) / "shader_cache";
    SDL_free(prefPath);
    return directory;
}

using Renderer3ds::VulkanPipelineCacheHeader;
using Renderer3ds::MakePipelineCacheHeader;
using Renderer3ds::LoadPipelineCacheData;
using Renderer3ds::StorePipelineCacheData;
using Renderer3ds::kMaximumNriPipelineCacheBytes;

std::filesystem::path VulkanPipelineCachePath(bool nri = false) {
    const auto directory = VulkanShaderCacheDirectory();
    return directory.empty() ? std::filesystem::path{}
        : directory / (nri ? Renderer3ds::kNriPipelineCacheFilename : "pipeline_cache.bin");
}

void CheckVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(static_cast<int>(result)));
    }
}

bool HasStencil(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

using Renderer::IsOot3dPicaTexture2Shader;
using Renderer::IsOot3dShadow2dShader;

VkBlendOp ToVulkanBlendOperation(GfxNativeBlendEquation equation) {
    switch (equation) {
        case GfxNativeBlendEquation::Add:
            return VK_BLEND_OP_ADD;
        case GfxNativeBlendEquation::Subtract:
            return VK_BLEND_OP_SUBTRACT;
        case GfxNativeBlendEquation::ReverseSubtract:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case GfxNativeBlendEquation::Min:
            return VK_BLEND_OP_MIN;
        case GfxNativeBlendEquation::Max:
            return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

VkBlendFactor ToVulkanBlendFactor(GfxNativeBlendFactor factor) {
    switch (factor) {
        case GfxNativeBlendFactor::Zero:
            return VK_BLEND_FACTOR_ZERO;
        case GfxNativeBlendFactor::One:
            return VK_BLEND_FACTOR_ONE;
        case GfxNativeBlendFactor::SourceColor:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case GfxNativeBlendFactor::OneMinusSourceColor:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case GfxNativeBlendFactor::DestColor:
            return VK_BLEND_FACTOR_DST_COLOR;
        case GfxNativeBlendFactor::OneMinusDestColor:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case GfxNativeBlendFactor::SourceAlpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case GfxNativeBlendFactor::OneMinusSourceAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case GfxNativeBlendFactor::DestAlpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case GfxNativeBlendFactor::OneMinusDestAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case GfxNativeBlendFactor::ConstantColor:
            return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case GfxNativeBlendFactor::OneMinusConstantColor:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case GfxNativeBlendFactor::ConstantAlpha:
            return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case GfxNativeBlendFactor::OneMinusConstantAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case GfxNativeBlendFactor::SourceAlphaSaturate:
            return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    }
    return VK_BLEND_FACTOR_ONE;
}

VkFilter ToVulkanFilter(GfxNativeTextureFilter filter) {
    switch (filter) {
        case GfxNativeTextureFilter::Linear:
        case GfxNativeTextureFilter::LinearMipmapNearest:
        case GfxNativeTextureFilter::LinearMipmapLinear:
            return VK_FILTER_LINEAR;
        default:
            return VK_FILTER_NEAREST;
    }
}

VkSamplerMipmapMode ToVulkanMipmapMode(GfxNativeTextureFilter filter) {
    return filter == GfxNativeTextureFilter::NearestMipmapLinear ||
                   filter == GfxNativeTextureFilter::LinearMipmapLinear
               ? VK_SAMPLER_MIPMAP_MODE_LINEAR
               : VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

VkSamplerAddressMode ToVulkanAddressMode(GfxNativeTextureWrap wrap) {
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

bool SameSamplerState(const GfxNativeSamplerState& left,
                      const GfxNativeSamplerState& right) {
    return left.MinFilter == right.MinFilter && left.MagFilter == right.MagFilter &&
           left.WrapS == right.WrapS && left.WrapT == right.WrapT &&
           left.LodBias == right.LodBias && left.MaxMipLevel == right.MaxMipLevel;
}

} // namespace

} // namespace Fast

#endif
