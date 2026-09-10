#ifdef ENABLE_OOT3D_VULKAN

#include "fast/backends/gfx_vulkan.h"
#include "fast/renderer/framebuffer_readback.h"
#include "fast/renderer/shaderc_compiler.h"
#include "fast/renderer3ds/vulkan_pipeline_cache_store.h"
#include "fast/renderer3ds/pica_vulkan_device_profile.h"
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
#include "fast/oot3d/cacao_diagnostics.h"
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

bool IsOot3dPicaTexture2Shader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_TEXTURE2_MULT_ADD;
}

bool IsOot3dShadow2dShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_TEXTURE_ENV_SHADOW2D;
}

bool IsPicaTextureEnvShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_TEXTURE_ENV ||
           shaderId == SHADER_ID_PICA_TEXTURE_ENV_SHADOW2D ||
           shaderId == SHADER_ID_PICA_TEXTURE_ENV_POST_MULTIPLY ||
           shaderId == SHADER_ID_PICA_TEXTURE2_MULT_ADD;
}

bool IsPicaTextureEnvPostMultiplyShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_TEXTURE_ENV_POST_MULTIPLY;
}

bool IsOot3dPicaFogShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_FOG || IsPicaTextureEnvShader(shaderId);
}

bool IsOot3dPicaAlphaTestShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_ALPHA_TEST || IsOot3dPicaFogShader(shaderId);
}

std::string ShaderInputName(uint32_t item) {
    if (item >= SHADER_INPUT_1 && item <= SHADER_INPUT_7) {
        return "input" + std::to_string(item - SHADER_INPUT_1 + 1);
    }
    return {};
}

std::string ShaderItemExpression(uint32_t item, bool withAlpha, bool onlyAlpha,
                                 bool inputsHaveAlpha, bool firstCycle,
                                 bool hintSingleElement) {
    const std::string input = ShaderInputName(item);
    if (!input.empty()) {
        if (onlyAlpha) {
            return input + ".a";
        }
        return withAlpha || !inputsHaveAlpha ? input : input + ".rgb";
    }

    if (onlyAlpha) {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_1:
                return "1.0";
            case SHADER_TEXEL0:
            case SHADER_TEXEL0A:
                return firstCycle ? "texVal0.a" : "texVal1.a";
            case SHADER_TEXEL1:
            case SHADER_TEXEL1A:
                return firstCycle ? "texVal1.a" : "texVal0.a";
            case SHADER_COMBINED:
                return "texel.a";
            case SHADER_NOISE:
                return "drawRandom";
            default:
                return "0.0";
        }
    }

    const char* zero = withAlpha ? "vec4(0.0)" : "vec3(0.0)";
    const char* one = withAlpha ? "vec4(1.0)" : "vec3(1.0)";
    switch (item) {
        case SHADER_0:
            return zero;
        case SHADER_1:
            return one;
        case SHADER_TEXEL0:
            return firstCycle ? (withAlpha ? "texVal0" : "texVal0.rgb")
                              : (withAlpha ? "texVal1" : "texVal1.rgb");
        case SHADER_TEXEL1:
            return firstCycle ? (withAlpha ? "texVal1" : "texVal1.rgb")
                              : (withAlpha ? "texVal0" : "texVal0.rgb");
        case SHADER_TEXEL0A: {
            const char* source = firstCycle ? "texVal0.a" : "texVal1.a";
            if (hintSingleElement) {
                return source;
            }
            return std::string(withAlpha ? "vec4(" : "vec3(") + source + ")";
        }
        case SHADER_TEXEL1A: {
            const char* source = firstCycle ? "texVal1.a" : "texVal0.a";
            if (hintSingleElement) {
                return source;
            }
            return std::string(withAlpha ? "vec4(" : "vec3(") + source + ")";
        }
        case SHADER_COMBINED:
            return withAlpha ? "texel" : "texel.rgb";
        case SHADER_NOISE:
            return withAlpha ? "vec4(drawRandom)" : "vec3(drawRandom)";
        default:
            return zero;
    }
}

std::string BuildCombinerFormula(const CCFeatures& features, int cycle, bool alphaChannel,
                                 bool withAlpha) {
    const int channel = alphaChannel ? 1 : 0;
    const int* combine = features.c[cycle][channel];
    const bool onlyAlpha = alphaChannel;
    const bool firstCycle = cycle == 0;
    const auto item = [&](int index, bool hintSingleElement = false) {
        return ShaderItemExpression(combine[index], withAlpha, onlyAlpha, features.opt_alpha,
                                    firstCycle, hintSingleElement);
    };
    if (features.do_single[cycle][channel]) {
        return item(3);
    }
    if (features.do_multiply[cycle][channel]) {
        return item(0) + " * " + item(2, true);
    }
    if (features.do_mix[cycle][channel]) {
        return "mix(" + item(1) + ", " + item(0) + ", " + item(2, true) + ")";
    }
    return "(" + item(0) + " - " + item(1) + ") * " + item(2, true) + " + " +
           item(3);
}

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

bool GfxRenderingAPIVulkan::QueueFamilies::Complete() const {
    return Graphics.has_value() && Present.has_value();
}

GfxRenderingAPIVulkan::GfxRenderingAPIVulkan(GfxWindowBackendSDL2* windowBackend)
    : mWindowBackend(windowBackend),
      mSceneSurfaces([this](const Oot3d::SceneSurface& surface) {
          if (surface.NativeImage != 0)
              mNriInterop.ForgetTexture(reinterpret_cast<VkImage>(surface.NativeImage));
          mResourceStates.Forget(surface.NativeImage);
      }) {
}

GfxRenderingAPIVulkan::~GfxRenderingAPIVulkan() {
    Shutdown();
}

const char* GfxRenderingAPIVulkan::GetName() {
    return "Vulkan";
}

int GfxRenderingAPIVulkan::GetMaxTextureSize() {
    if (mPhysicalDevice == VK_NULL_HANDLE) {
        return 4096;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
    return static_cast<int>(properties.limits.maxImageDimension2D);
}

GfxClipParameters GfxRenderingAPIVulkan::GetClipParameters() {
    return { true, true };
}

void GfxRenderingAPIVulkan::UnloadShader(ShaderProgram*) {
    mCurrentShader = nullptr;
}

void GfxRenderingAPIVulkan::LoadShader(ShaderProgram* newPrg) {
    mCurrentShader = reinterpret_cast<VulkanShaderProgram*>(newPrg);
}

void GfxRenderingAPIVulkan::ClearShaderCache() {
    if (mDevice != VK_NULL_HANDLE) {
        CheckVk(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle(clear shader cache)");
        DestroyGraphicsPipelines();
        for (auto& [key, shader] : mShaders) {
            if (shader.VertexShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(mDevice, shader.VertexShader, nullptr);
            }
            if (shader.FragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(mDevice, shader.FragmentShader, nullptr);
            }
        }
    }
    mCurrentShader = nullptr;
    mShaders.clear();
}

ShaderProgram* GfxRenderingAPIVulkan::CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) {
    auto& shader = mShaders[{ shaderId0, shaderId1 }];
    shader = BuildShaderProgram(shaderId0, shaderId1);
    const std::string vertexSource = BuildVertexShaderSource(shader);
    const std::string fragmentSource = BuildFragmentShaderSource(shaderId0, shaderId1);
    const std::string sourceStem = "oot3d_" + std::to_string(shaderId0) + "_" +
                                   std::to_string(shaderId1);
    shader.VertexShader =
        CompileShaderModule(vertexSource, true, (sourceStem + ".vert").c_str());
    shader.FragmentShader =
        CompileShaderModule(fragmentSource, false, (sourceStem + ".frag").c_str());
    mCurrentShader = &shader;
    return reinterpret_cast<ShaderProgram*>(&shader);
}

ShaderProgram* GfxRenderingAPIVulkan::LookupShader(uint64_t shaderId0, uint64_t shaderId1) {
    const auto it = mShaders.find({ shaderId0, shaderId1 });
    return it == mShaders.end() ? nullptr : reinterpret_cast<ShaderProgram*>(&it->second);
}

void GfxRenderingAPIVulkan::ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) {
    const auto* shader = reinterpret_cast<const VulkanShaderProgram*>(prg);
    *numInputs = shader != nullptr ? shader->NumInputs : 0;
    usedTextures[0] = shader != nullptr && shader->UsedTextures[0];
    usedTextures[1] = shader != nullptr && shader->UsedTextures[1];
}

uint32_t GfxRenderingAPIVulkan::NewTexture() {
    const uint32_t id = mNextTextureId++;
    mTextures.emplace(id, TextureRecord{});
    return id;
}

void GfxRenderingAPIVulkan::SelectTexture(int tile, uint32_t textureId) {
    if (tile < 0 || static_cast<size_t>(tile) >= mSelectedTextures.size()) {
        return;
    }
    mCurrentTextureUnit = static_cast<uint32_t>(tile);
    mSelectedTextures[mCurrentTextureUnit] = textureId;
}

void GfxRenderingAPIVulkan::UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) {
    if (rgba32Buf == nullptr || width == 0 || height == 0 ||
        mSelectedTextures[mCurrentTextureUnit] == 0) {
        return;
    }
    auto& texture = mTextures[mSelectedTextures[mCurrentTextureUnit]];
    const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    texture.Rgba8.assign(rgba32Buf, rgba32Buf + byteCount);
    CreateTextureImage(texture, rgba32Buf, width, height);
}

void GfxRenderingAPIVulkan::SetSamplerParameters(int sampler, bool linearFilter, uint32_t cms,
                                                  uint32_t cmt) {
    if (sampler < 0 || static_cast<size_t>(sampler) >= mSelectedTextures.size()) {
        return;
    }
    const auto it = mTextures.find(mSelectedTextures[static_cast<size_t>(sampler)]);
    if (it == mTextures.end()) {
        return;
    }
    auto& state = it->second.SamplerState;
    state.MinFilter = linearFilter ? GfxNativeTextureFilter::Linear
                                   : GfxNativeTextureFilter::Nearest;
    state.MagFilter = state.MinFilter;
    state.WrapS = (cms & G_TX_CLAMP) != 0
                      ? GfxNativeTextureWrap::ClampToEdge
                      : (cms & G_TX_MIRROR) != 0 ? GfxNativeTextureWrap::MirroredRepeat
                                                 : GfxNativeTextureWrap::Repeat;
    state.WrapT = (cmt & G_TX_CLAMP) != 0
                      ? GfxNativeTextureWrap::ClampToEdge
                      : (cmt & G_TX_MIRROR) != 0 ? GfxNativeTextureWrap::MirroredRepeat
                                                 : GfxNativeTextureWrap::Repeat;
    if (!it->second.SamplerStateApplied ||
        !SameSamplerState(state, it->second.AppliedSamplerState)) {
        RecreateSampler(it->second);
    }
}

bool GfxRenderingAPIVulkan::UploadTextureMipLevel(uint32_t level, const uint8_t* rgba32Buf,
                                                  uint32_t width, uint32_t height) {
    if (level == 0 || rgba32Buf == nullptr || width == 0 || height == 0 ||
        mSelectedTextures[mCurrentTextureUnit] == 0) {
        return false;
    }
    auto it = mTextures.find(mSelectedTextures[mCurrentTextureUnit]);
    if (it == mTextures.end() || !it->second.Uploaded || level >= it->second.MipLevels ||
        level > it->second.UploadedMipLevels) {
        return false;
    }
    const uint32_t uploadedMipLevels = it->second.UploadedMipLevels;
    UploadTextureLevel(it->second, level, rgba32Buf, width, height);
    if (it->second.UploadedMipLevels != uploadedMipLevels) {
        RecreateSampler(it->second);
    }
    return true;
}

bool GfxRenderingAPIVulkan::SetNativeSamplerParameters(int sampler,
                                                       const GfxNativeSamplerState& state) {
    if (sampler < 0 || static_cast<size_t>(sampler) >= mSelectedTextures.size()) {
        return false;
    }
    const auto it = mTextures.find(mSelectedTextures[static_cast<size_t>(sampler)]);
    if (it == mTextures.end()) {
        return false;
    }
    it->second.SamplerState = state;
    if (!it->second.SamplerStateApplied ||
        !SameSamplerState(state, it->second.AppliedSamplerState)) {
        RecreateSampler(it->second);
    }
    return true;
}

void GfxRenderingAPIVulkan::SetDepthTestAndMask(bool depthTest, bool zUpd) {
    mCurrentDepthTest = depthTest;
    mCurrentDepthMask = zUpd;
}

void GfxRenderingAPIVulkan::SetZmodeDecal(bool decal) {
    mCurrentZmodeDecal = decal;
}

void GfxRenderingAPIVulkan::SetViewport(int x, int y, int width, int height) {
    mViewport = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
                  static_cast<float>(height), 0.0f, 1.0f };
    mViewportSet = true;
    ApplyDynamicViewportAndScissor();
}

void GfxRenderingAPIVulkan::SetScissor(int x, int y, int width, int height) {
    mScissor.offset = { x, y };
    mScissor.extent = { static_cast<uint32_t>(std::max(0, width)),
                        static_cast<uint32_t>(std::max(0, height)) };
    mScissorSet = true;
    ApplyDynamicViewportAndScissor();
}

void GfxRenderingAPIVulkan::SetUseAlpha(bool useAlpha) {
    mBlendState = {};
    mBlendState.Enabled = useAlpha;
    if (useAlpha) {
        mBlendState.SourceRgb = GfxNativeBlendFactor::SourceAlpha;
        mBlendState.DestRgb = GfxNativeBlendFactor::OneMinusSourceAlpha;
        mBlendState.SourceAlpha = GfxNativeBlendFactor::SourceAlpha;
        mBlendState.DestAlpha = GfxNativeBlendFactor::OneMinusSourceAlpha;
    }
}

bool GfxRenderingAPIVulkan::SetNativeBlendState(const GfxNativeBlendState& state) {
    mBlendState = state;
    return true;
}

bool GfxRenderingAPIVulkan::SetNativeCullMode(GfxNativeCullMode mode) {
    mCullMode = mode;
    return true;
}

bool GfxRenderingAPIVulkan::SupportsOot3dPicaFogLut() const {
    return true;
}

bool GfxRenderingAPIVulkan::SetOot3dPicaFogShaderParameters(const uint32_t* lutWords,
                                                            size_t lutWordCount,
                                                            bool fogFlip,
                                                            uint64_t stateKey) {
    if (lutWords == nullptr || lutWordCount < mDrawUniforms.FogLut.size()) {
        return false;
    }
    if (!mFogStateValid || mFogStateKey != stateKey) {
        std::copy_n(lutWords, mDrawUniforms.FogLut.size(), mDrawUniforms.FogLut.begin());
        mFogStateKey = stateKey;
        mFogStateValid = true;
    }
    mDrawUniforms.FogFlip = fogFlip ? 1 : 0;
    return true;
}

bool GfxRenderingAPIVulkan::SetOot3dPicaAlphaTestShaderParameters(bool enabled,
                                                                 uint32_t function,
                                                                 uint8_t reference) {
    mDrawUniforms.AlphaTestEnabled = enabled ? 1 : 0;
    mDrawUniforms.AlphaTestFunction = static_cast<int32_t>(function);
    mDrawUniforms.AlphaTestReference = reference;
    return true;
}

bool GfxRenderingAPIVulkan::SupportsOot3dPicaTexture2() const {
    return true;
}

bool GfxRenderingAPIVulkan::SetOot3dNativeTransform(const float* rowMajorMatrix) {
    if (rowMajorMatrix == nullptr) {
        return false;
    }
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            mTransformPushConstants.ModelViewProjection[column * 4 + row] =
                rowMajorMatrix[row * 4 + column];
        }
    }
    return true;
}

bool GfxRenderingAPIVulkan::DrawTrianglesCached(uint64_t cacheId, uint64_t contentVersion, const float* bufVbo,
                                                 size_t bufVboLen, size_t bufVboNumTris) {
    if (!mFrameActive || mCurrentFramebuffer != 0 || mCurrentShader == nullptr ||
        cacheId == 0 || bufVbo == nullptr || bufVboLen == 0 || bufVboNumTris == 0) {
        return false;
    }

    auto cacheIt = mOot3dCachedVertexBuffers.find(cacheId);
    if (cacheIt == mOot3dCachedVertexBuffers.end()) {
        if (mOot3dCachedVertexBuffers.size() >= kOot3dCachedVertexBufferLimit) {
            const auto oldest = std::min_element(
                mOot3dCachedVertexBuffers.begin(), mOot3dCachedVertexBuffers.end(),
                [](const auto& left, const auto& right) {
                    return left.second.LastUsedFrame < right.second.LastUsedFrame;
                });
            if (oldest != mOot3dCachedVertexBuffers.end()) {
                CheckVk(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle(evict cached vertex buffer)");
                for (auto& buffer : oldest->second.Buffers) {
                    DestroyBuffer(buffer);
                }
                mOot3dCachedVertexBuffers.erase(oldest);
            }
        }
        cacheIt = mOot3dCachedVertexBuffers.emplace(cacheId, CachedVertexBuffer{}).first;
    }

    auto& cached = cacheIt->second;
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(bufVboLen * sizeof(float));
    auto& buffer = cached.Buffers[mCurrentFrame];
    if (buffer.Buffer == VK_NULL_HANDLE || buffer.Size < byteCount) {
        DestroyBuffer(buffer);
        buffer = CreateBuffer(byteCount, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              true);
        cached.ContentVersions[mCurrentFrame] = 0;
    }
    if (cached.ContentVersions[mCurrentFrame] != contentVersion ||
        cached.FloatCount != bufVboLen || cached.TriangleCount != bufVboNumTris) {
        std::memcpy(buffer.Mapped, bufVbo, static_cast<size_t>(byteCount));
        cached.ContentVersions[mCurrentFrame] = contentVersion;
    }
    cached.FloatCount = bufVboLen;
    cached.TriangleCount = bufVboNumTris;
    cached.LastUsedFrame = mFrameCounter;
    DrawTrianglesFromBuffer(buffer.Buffer, 0, bufVboLen, bufVboNumTris);
    return true;
}

void GfxRenderingAPIVulkan::DrawTriangles(float bufVbo[], size_t bufVboLen,
                                           size_t bufVboNumTris) {
    if (!mFrameActive || mCurrentFramebuffer != 0 || mCurrentShader == nullptr ||
        bufVbo == nullptr || bufVboLen == 0 || bufVboNumTris == 0) {
        return;
    }
    auto& frame = mFrameResources[mCurrentFrame];
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(bufVboLen * sizeof(float));
    const VkDeviceSize alignedOffset = (frame.VertexBytesUsed + 15) & ~VkDeviceSize(15);
    if (alignedOffset + byteCount > frame.VertexBuffer.Size) {
        throw std::runtime_error("OOT3D Vulkan per-frame vertex arena exhausted");
    }
    std::memcpy(static_cast<uint8_t*>(frame.VertexBuffer.Mapped) + alignedOffset, bufVbo,
                static_cast<size_t>(byteCount));
    frame.VertexBytesUsed = alignedOffset + byteCount;

    DrawTrianglesFromBuffer(frame.VertexBuffer.Buffer, alignedOffset, bufVboLen,
                            bufVboNumTris);
}

void GfxRenderingAPIVulkan::DrawTrianglesFromBuffer(VkBuffer vertexBuffer,
                                                     VkDeviceSize vertexOffset,
                                                     size_t bufVboLen,
                                                     size_t bufVboNumTris) {
    if (!mFrameActive || mCurrentFramebuffer != 0 || mCurrentShader == nullptr ||
        vertexBuffer == VK_NULL_HANDLE || bufVboLen == 0 || bufVboNumTris == 0) {
        return;
    }
    const uint32_t drawCallIndex = mDrawCallCountThisFrame++;
    const size_t vertexCount = bufVboNumTris * 3;
    if (vertexCount == 0 || bufVboLen % vertexCount != 0 ||
        (bufVboLen / vertexCount) * sizeof(float) != mCurrentShader->VertexStride) {
        throw std::runtime_error(
            "OOT3D Vulkan packed vertex stride mismatch at draw " +
            std::to_string(drawCallIndex) + ": shader=" +
            std::to_string(mCurrentShader->ShaderId0) + "/" +
            std::to_string(mCurrentShader->ShaderId1) + ", packed=" +
            std::to_string(vertexCount == 0 ? 0 : bufVboLen / vertexCount) +
            " floats, pipeline=" +
            std::to_string(mCurrentShader->VertexStride / sizeof(float)) + " floats");
    }
    BeginRenderPassIfNeeded();
    if (!mRenderPassActive) {
        return;
    }

    auto& frame = mFrameResources[mCurrentFrame];
    VkCommandBuffer commandBuffer = mCommandBuffers[mCurrentFrame];
    const VkPipeline pipeline = GetOrCreatePipeline(*mCurrentShader);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
    ApplyDynamicViewportAndScissor();
    vkCmdSetBlendConstants(commandBuffer, mBlendState.ConstantColor);

    const auto resolveTexture = [&](uint32_t unit) -> TextureRecord* {
        const auto textureIt = mTextures.find(mSelectedTextures[unit]);
        return textureIt != mTextures.end() && textureIt->second.Uploaded
                   ? &textureIt->second
                   : &mFallbackTexture;
    };
    std::array<TextureRecord*, 3> drawTextures = {
        mCurrentShader->UsedTextures[0] ? resolveTexture(0) : &mFallbackTexture,
        mCurrentShader->UsedTextures[1] ? resolveTexture(1) : &mFallbackTexture,
        mCurrentShader->UsesTexture2 ? resolveTexture(2) : &mFallbackTexture,
    };
    for (size_t unit = 0; unit < drawTextures.size(); ++unit) {
        mDrawUniforms.TextureWidth[unit] = static_cast<int32_t>(drawTextures[unit]->Width);
        mDrawUniforms.TextureHeight[unit] = static_cast<int32_t>(drawTextures[unit]->Height);
        mDrawUniforms.TextureFiltering[unit] = static_cast<int32_t>(mFilterMode);
    }
    const OffscreenFramebufferRecord* shadowFramebuffer = nullptr;
    if (IsOot3dShadow2dShader(mCurrentShader->NativeShaderId)) {
        const auto shadowIt =
            mOffscreenFramebuffers.find(mBoundOot3dShadow2dFramebufferId);
        if (shadowIt == mOffscreenFramebuffers.end() ||
            shadowIt->second.ColorFormat != GfxFramebufferColorFormat::R32ui ||
            shadowIt->second.ColorView == VK_NULL_HANDLE ||
            shadowIt->second.ColorSampler == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "OOT3D Vulkan Shadow2D shader has no bound R32UI shadow target");
        }
        shadowFramebuffer = &shadowIt->second;
        mDrawUniforms.TextureWidth[3] = static_cast<int32_t>(shadowFramebuffer->Width);
        mDrawUniforms.TextureHeight[3] = static_cast<int32_t>(shadowFramebuffer->Height);
    } else {
        mDrawUniforms.TextureWidth[3] = static_cast<int32_t>(mFallbackTexture.Width);
        mDrawUniforms.TextureHeight[3] = static_cast<int32_t>(mFallbackTexture.Height);
    }
    mDrawUniforms.TextureFiltering[3] = static_cast<int32_t>(FILTER_NONE);
    mDrawUniforms.FrameCount = mFrameCounter;
    mDrawUniforms.NoiseScale = mCurrentNoiseScale;

    const VkDeviceSize uniformOffset =
        (frame.UniformBytesUsed + mStorageBufferAlignment - 1) &
        ~(mStorageBufferAlignment - 1);
    if (uniformOffset + sizeof(mDrawUniforms) > frame.UniformBuffer.Size) {
        throw std::runtime_error("OOT3D Vulkan per-frame uniform arena exhausted");
    }
    std::memcpy(static_cast<uint8_t*>(frame.UniformBuffer.Mapped) + uniformOffset,
                &mDrawUniforms, sizeof(mDrawUniforms));
    frame.UniformBytesUsed = uniformOffset + sizeof(mDrawUniforms);

    std::array<VkDescriptorImageInfo, 4> imageInfos{};
    for (uint32_t unit = 0; unit < 3; ++unit) {
        imageInfos[unit].sampler = drawTextures[unit]->Sampler;
        imageInfos[unit].imageView = drawTextures[unit]->View;
        imageInfos[unit].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    imageInfos[3].sampler = shadowFramebuffer != nullptr
                                ? shadowFramebuffer->ColorSampler
                                : mFallbackTexture.Sampler;
    imageInfos[3].imageView = shadowFramebuffer != nullptr
                                 ? shadowFramebuffer->ColorView
                                 : mFallbackTexture.View;
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    TextureDescriptorKey descriptorKey;
    for (size_t unit = 0; unit < imageInfos.size(); ++unit) {
        descriptorKey.Samplers[unit] = imageInfos[unit].sampler;
        descriptorKey.ImageViews[unit] = imageInfos[unit].imageView;
    }
    auto descriptorIt = frame.TextureDescriptorSets.find(descriptorKey);
    if (descriptorIt == frame.TextureDescriptorSets.end()) {
        const VkDescriptorSet descriptorSet = AllocateTextureDescriptorSet();
        std::array<VkWriteDescriptorSet, 5> descriptorWrites{};
        for (uint32_t unit = 0; unit < 4; ++unit) {
            descriptorWrites[unit].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[unit].dstSet = descriptorSet;
            descriptorWrites[unit].dstBinding = unit;
            descriptorWrites[unit].descriptorCount = 1;
            descriptorWrites[unit].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[unit].pImageInfo = &imageInfos[unit];
        }
        VkDescriptorBufferInfo uniformInfo{};
        uniformInfo.buffer = frame.UniformBuffer.Buffer;
        uniformInfo.offset = 0;
        uniformInfo.range = sizeof(mDrawUniforms);
        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = descriptorSet;
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        descriptorWrites[4].pBufferInfo = &uniformInfo;
        vkUpdateDescriptorSets(mDevice, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
        descriptorIt = frame.TextureDescriptorSets.emplace(descriptorKey, descriptorSet).first;
    }
    if (uniformOffset > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("OOT3D Vulkan dynamic uniform offset exceeds uint32_t");
    }
    const VkDescriptorSet descriptorSet = descriptorIt->second;
    const uint32_t dynamicUniformOffset = static_cast<uint32_t>(uniformOffset);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0,
                            1, &descriptorSet, 1, &dynamicUniformOffset);
    TransformPushConstants drawTransform = mTransformPushConstants;
    if (mTemporalJitterEnabled && mCurrentDepthTest != 0 &&
        !mOot3dShadow2dPassActive &&
        Oot3d::HasPerspectiveClipW(drawTransform.ModelViewProjection)) {
        drawTransform.ModelViewProjection =
            Oot3d::ApplyTemporalJitterToClipMatrix(
                drawTransform.ModelViewProjection, mTemporalJitterPixels,
                std::abs(mViewport.width), std::abs(mViewport.height));
    }
    vkCmdPushConstants(commandBuffer, mPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(drawTransform), &drawTransform);
    vkCmdDraw(commandBuffer, static_cast<uint32_t>(bufVboNumTris * 3), 1, 0, 0);
}

void GfxRenderingAPIVulkan::Init() {
    if (mInitialized) {
        return;
    }
    if (mWindowBackend == nullptr || !mWindowBackend->UsesVulkan()) {
        throw std::runtime_error("Vulkan renderer was paired with a non-Vulkan SDL backend");
    }
    if (mWindowBackend->GetNativeWindow() == nullptr) {
        throw std::runtime_error(std::string("SDL Vulkan window creation failed: ") + SDL_GetError());
    }

    mDiagnostics.ReloadFromEnvironment();
    mValidationTelemetry.Reset();
    ConfigureNativePicaAotShaders();

    CreateInstance();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreatePipelineCache();
    CreateCommandResources();
    if (mNriInterop.Initialize(mInstance, mPhysicalDevice, mDevice, mGraphicsQueueFamily,
                               mSynchronization2Enabled,
                               mDynamicRenderingEnabled,
                               mNisVulkanFeaturesEnabled,
                               mFsrVulkanFeaturesEnabled,
                               mNriSwapchainExtensionsEnabled,
                               mVulkanValidation.Enabled()
                                   ? &mValidationTelemetry : nullptr)) {
        SPDLOG_INFO("OOT3D Vulkan NRI v180 interop enabled");
    } else {
        SPDLOG_INFO("OOT3D Vulkan NRI interop unavailable: {}",
                    mNriInterop.UnavailableReason());
    }
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
    const bool d3d12NgxReady = mD3d12NgxProvider.Initialize(mPhysicalDevice, mDevice, mGraphicsQueueFamily,
                                                            kFramesInFlight, mD3d12InteropExtensionsEnabled);
    const auto& d3d12NgxStatus = mD3d12NgxProvider.Status();
    mDiagnostics.SetD3d12NgxProvider({
        d3d12NgxStatus.AdapterMatched,
        d3d12NgxStatus.DeviceReady,
        d3d12NgxStatus.NriReady,
        d3d12NgxStatus.DlssFeatureReady,
        d3d12NgxStatus.DlssContractResourcesReady,
        d3d12NgxStatus.DlssEvaluateReady,
        d3d12NgxStatus.ExternalMemoryReady,
        d3d12NgxStatus.SharedFenceReady,
        d3d12NgxStatus.ZeroCopyInteropReady,
        d3d12NgxStatus.VendorId,
        d3d12NgxStatus.DeviceId,
        d3d12NgxStatus.ProbeInputWidth,
        d3d12NgxStatus.ProbeInputHeight,
        d3d12NgxStatus.ProbeOutputWidth,
        d3d12NgxStatus.ProbeOutputHeight,
        d3d12NgxStatus.AdapterName,
        d3d12NgxStatus.Detail,
        d3d12NgxStatus.FrameBridgeReady,
        d3d12NgxStatus.FrameDispatchRequested,
        d3d12NgxStatus.FrameDispatchCount,
    });
    if (d3d12NgxReady) {
        const auto& status = mD3d12NgxProvider.Status();
        SPDLOG_INFO("OOT3D D3D12 NGX provider ready on {} (vendor={:#06x}, device={:#06x}): {}", status.AdapterName,
                    status.VendorId, status.DeviceId, status.Detail);
    } else {
        SPDLOG_INFO("OOT3D D3D12 NGX provider unavailable: {}", mD3d12NgxProvider.UnavailableReason());
    }
#endif
    if (!mNriSwapchainExtensionsEnabled) {
        constexpr const char* reason =
            "VK_KHR_get_surface_capabilities2 is unavailable";
        mDiagnostics.SetVulkanPresentationFallback(
            Oot3dVulkanPresentationFallbackReason::
                MissingSurfaceCapabilities2,
            reason);
        SPDLOG_INFO("OOT3D Vulkan NRI swapchain unavailable: {}",
                    reason);
    } else if (!mNriSwapchainQueueEligible) {
        constexpr const char* reason =
            "graphics and presentation use separate queue families";
        mDiagnostics.SetVulkanPresentationFallback(
            Oot3dVulkanPresentationFallbackReason::
                SeparateQueueFamilies,
            reason);
        SPDLOG_INFO("OOT3D Vulkan NRI swapchain unavailable: {}",
                    reason);
    } else if (!mNriSwapchain.Initialize(mNriInterop)) {
        mDiagnostics.SetVulkanPresentationFallback(
            Oot3dVulkanPresentationFallbackReason::
                NriInitializationFailed,
            mNriSwapchain.UnavailableReason());
        SPDLOG_INFO("OOT3D Vulkan NRI swapchain unavailable: {}",
                    mNriSwapchain.UnavailableReason());
    } else {
        mDiagnostics.SetVulkanPresentationFallback(
            Oot3dVulkanPresentationFallbackReason::None, {});
    }
    const char* nriPicaPipelines =
        std::getenv("OOT3D_GRAPHICS_NRI_PICA_PIPELINES");
    const bool nriPicaPipelinesEnabled =
        nriPicaPipelines == nullptr ||
        std::string_view(nriPicaPipelines) != "0";
    if (!nriPicaPipelinesEnabled ||
        !mNriPicaPipelineBridge.Initialize(mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan NRI PICA pipeline bridge unavailable: {}",
                    nriPicaPipelinesEnabled
                        ? mNriPicaPipelineBridge.UnavailableReason()
                        : "disabled by environment");
    } else {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
        std::vector<uint8_t> data;
        LoadPipelineCacheData(VulkanPipelineCachePath(true),
                              MakePipelineCacheHeader(properties), data);
        const bool initialized = mNriPicaPipelineBridge.InitializePipelineCache(data);
        SPDLOG_INFO("NRI PICA pipeline cache: initialized={}, accepted {} bytes", initialized,
                    mNriPicaPipelineBridge.PipelineStatistics().InitialCacheBytes);
    }
    if (!mNriPicaTextureImageOwner.Initialize(mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA texture images unavailable: {}",
            mNriPicaTextureImageOwner.UnavailableReason());
    }
    if (!mNriPicaDisplayImageOwner.Initialize(
            mNriInterop,
            "OOT3D_GRAPHICS_NRI_PICA_DISPLAY_IMAGES",
            "NRI PICA display image ownership")) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA display images unavailable: {}",
            mNriPicaDisplayImageOwner.UnavailableReason());
    }
    if (!mNriPicaRenderTargetOwner.Initialize(mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA render targets unavailable: {}",
            mNriPicaRenderTargetOwner.UnavailableReason());
    }
    if (!mNriPicaRenderTargetInitPass.Initialize(mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA render-target initialization unavailable: {}",
            mNriPicaRenderTargetInitPass.UnavailableReason());
    }
    if (!mNriPicaDisplayCopyPass.Initialize(mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA display copies unavailable: {}",
            mNriPicaDisplayCopyPass.UnavailableReason());
    }
    if (!mNriPicaMemoryFillClearPass.Initialize(mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA memory-fill clears unavailable: {}",
            mNriPicaMemoryFillClearPass.UnavailableReason());
    }
    constexpr uint64_t kNriPicaTextureUploadBytesPerFrame =
        64ULL * 1024ULL * 1024ULL;
    if (!mNriPicaTextureUploadPass.Initialize(
            mNriInterop, kNriPicaTextureUploadBytesPerFrame)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA texture uploads unavailable: {}",
            mNriPicaTextureUploadPass.UnavailableReason());
    }
    if (!mCacaoPass.Initialize(mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan CACAO unavailable: {}", mCacaoPass.UnavailableReason());
    }
    if (!mHiZDepthPyramidPass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan Hi-Z unavailable: {}",
                    mHiZDepthPyramidPass.UnavailableReason());
    }
    if (!mHiZReflectionPass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan SSR unavailable: {}",
                    mHiZReflectionPass.UnavailableReason());
    }
    if (!mFidelityFxSssrPass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan FidelityFX SSSR unavailable: {}",
                    mFidelityFxSssrPass.UnavailableReason());
    }
    if (!mReflectionIblPass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan reflection IBL unavailable: {}",
                    mReflectionIblPass.UnavailableReason());
    }
    if (!mReflectionMaterialResolvePass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan reflection material resolve unavailable: {}",
            mReflectionMaterialResolvePass.UnavailableReason());
    }
    if (!mLinearSceneColorPass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan linear scene color unavailable: {}",
                    mLinearSceneColorPass.UnavailableReason());
    }
    if (!mMotionVectorPass.Initialize(mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan motion vectors unavailable: {}",
                    mMotionVectorPass.UnavailableReason());
    }
    if (!mNriDirectionalShadowPass.Initialize(mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D NRI directional shadows unavailable: {}", mNriDirectionalShadowPass.UnavailableReason());
    }
    if (!mTemporalAaPass.Initialize(mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan TAA unavailable: {}",
                    mTemporalAaPass.UnavailableReason());
    }
    if (!mNriEffectGraphTransientImageArena.Initialize(
            mDevice, mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan effect-graph transient image arena unavailable: {}",
            mNriEffectGraphTransientImageArena.UnavailableReason());
    }
    if (!mSceneCompositePass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan scene composite unavailable: {}",
                    mSceneCompositePass.UnavailableReason());
    }
    if (!mSmaa1xPass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan SMAA 1x unavailable: {}",
                    mSmaa1xPass.UnavailableReason());
    }
    if (!mNriUpscalerPass.Initialize(mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan NIS unavailable: {}",
                    mNriUpscalerPass.UnavailableReason());
    }
    auto& settingsRuntime = Oot3d::GraphicsSettingsRuntime::Instance();
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::NriInterop,
                                  mNriInterop.Available(), mNriInterop.UnavailableReason());
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::NriDirectionalShadows,
        mNriDirectionalShadowPass.Available(),
        mNriDirectionalShadowPass.UnavailableReason());
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::SampledSceneColor, true);
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::SampledDepth, true);
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::NormalGuide, true);
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::MaterialGuide, true);
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::MotionVectors,
                                  mMotionVectorPass.Available(),
                                  mMotionVectorPass.UnavailableReason());
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::TemporalHistory,
                                  mMotionVectorPass.Available() &&
                                      mTemporalAaPass.Available(),
                                  mTemporalAaPass.Available()
                                      ? mMotionVectorPass.UnavailableReason()
                                       : mTemporalAaPass.UnavailableReason());
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::LinearHdrWorkingColor,
        mLinearSceneColorPass.Available(),
        mLinearSceneColorPass.UnavailableReason());
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::Smaa1x,
        mSmaa1xPass.Available(),
        mSmaa1xPass.UnavailableReason());
    const bool fidelityFxSssrAvailable =
        mFidelityFxSssrPass.Available() &&
        mMotionVectorPass.Available() &&
        mLinearSceneColorPass.Available() &&
        mReflectionIblPass.Available() &&
        mReflectionMaterialResolvePass.Available();
    std::string fidelityFxSssrReason;
    if (!mFidelityFxSssrPass.Available())
        fidelityFxSssrReason =
            mFidelityFxSssrPass.UnavailableReason();
    else if (!mMotionVectorPass.Available())
        fidelityFxSssrReason =
            mMotionVectorPass.UnavailableReason();
    else if (!mLinearSceneColorPass.Available())
        fidelityFxSssrReason =
            mLinearSceneColorPass.UnavailableReason();
    else if (!mReflectionIblPass.Available())
        fidelityFxSssrReason =
            mReflectionIblPass.UnavailableReason();
    else if (!mReflectionMaterialResolvePass.Available())
        fidelityFxSssrReason =
            mReflectionMaterialResolvePass.UnavailableReason();
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::FidelityFxSssr,
        fidelityFxSssrAvailable, fidelityFxSssrReason);
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::NriNisUpscaler,
        mNriUpscalerPass.Available(), mNriUpscalerPass.UnavailableReason());
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::NriFsrUpscaler,
        mNriUpscalerPass.FsrAvailable(),
        mNriInterop.FsrUnavailableReason());
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::NriXessUpscaler, false,
        "NRI XeSS is unavailable on the Vulkan backend");
    const bool dlssAvailable = mNgxVulkanRequirementsSatisfied &&
        mNriUpscalerPass.DlssAvailable();
    const auto& ngxRequirements =
        Oot3d::GetNriNgxVulkanRequirements();
    std::string dlssCapabilityDetail;
    if (!mNgxVulkanRequirementsSatisfied) {
        dlssCapabilityDetail = mNgxVulkanUnavailableReason.empty()
            ? "Required NGX Vulkan extensions are unavailable"
            : mNgxVulkanUnavailableReason;
    } else if (!mNriUpscalerPass.DlssAvailable()) {
        dlssCapabilityDetail = mNriInterop.DlssUnavailableReason();
    } else {
        dlssCapabilityDetail = "Official NVIDIA NGX " +
            ngxRequirements.RuntimeVersion + " (DLSS SR / DLAA)";
    }
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
    dlssCapabilityDetail += mD3d12NgxProvider.Available()
        ? "; D3D12 NGX persistent per-frame bridge ready"
        : "; D3D12 NGX provider unavailable: " +
              mD3d12NgxProvider.UnavailableReason();
#endif
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::NriDlssUpscaler,
        dlssAvailable, dlssCapabilityDetail);
    if (dlssAvailable) {
        SPDLOG_INFO("OOT3D Vulkan DLSS ready: {}",
                    dlssCapabilityDetail);
    } else {
        SPDLOG_INFO("OOT3D Vulkan DLSS unavailable: {}",
                    dlssCapabilityDetail);
    }
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::Msaa2x,
        (mNativePicaSupportedSampleCounts & VK_SAMPLE_COUNT_2_BIT) != 0U,
        "2x color/depth sampling is unavailable");
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::Msaa4x,
        (mNativePicaSupportedSampleCounts & VK_SAMPLE_COUNT_4_BIT) != 0U,
        "4x color/depth sampling is unavailable");
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::Msaa8x,
        (mNativePicaSupportedSampleCounts & VK_SAMPLE_COUNT_8_BIT) != 0U,
        "8x color/depth sampling is unavailable");
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::MultisampledDepthResolve,
        mNativePicaDepthResolveMode != VK_RESOLVE_MODE_NONE,
        "multisampled depth resolve is unavailable");
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::PresentTearing,
                                  mWindowBackend->CanDisableVsync());
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::ExclusiveFullscreen,
                                  true);
    CreateSyncObjects();
    CreateSwapchainResources();
    if (!mNriPicaScanoutPass.Initialize(mDevice, mNriInterop) ||
        !mNriPicaScanoutPass.Configure(mSwapchainFormat)) {
        SPDLOG_INFO("OOT3D Vulkan NRI PICA scanout unavailable: {}",
                    mNriPicaScanoutPass.UnavailableReason());
    }
    CreateShaderResources();
    bool picaDynamicRenderingEnabled = mDynamicRenderingEnabled;
    if (const char* overrideValue =
            std::getenv("OOT3D_GRAPHICS_PICA_DYNAMIC_RENDERING");
        overrideValue != nullptr &&
        std::string_view(overrideValue) == "0")
        picaDynamicRenderingEnabled = false;
    if (!mPicaDynamicRenderingScope.Initialize(
            mDevice, &mNriInterop, mDepthFormat,
            mNativePicaDepthResolveMode,
            picaDynamicRenderingEnabled)) {
        SPDLOG_INFO("OOT3D Vulkan PICA dynamic rendering unavailable: {}",
                    mPicaDynamicRenderingScope.UnavailableReason());
    }
    if (!mInteractiveGrassPass.Initialize(mPhysicalDevice, mDevice,
                                          mNriInterop.Shaders(),
                                          mNativePicaCanonicalRenderPass,
                                          mNativePicaRenderPass,
                                          mNativePicaSampleCount,
                                          mPicaDynamicRenderingScope.Available(),
                                          mDepthFormat)) {
        SPDLOG_INFO("OOT3D Vulkan interactive grass unavailable: {}",
                    mInteractiveGrassPass.UnavailableReason());
    }
    CreateFrameResources();
    mScanoutProbe.Initialize(mPhysicalDevice, mDevice, kFramesInFlight);
    mGpuProfiler.Initialize(mPhysicalDevice, mDevice, mGraphicsQueueFamily,
                            mDiagnostics.Enabled());
    CreateFallbackTexture();
    StartPresentWorker();
    mInitialized = true;
    // Emit the compatibility contract as soon as the opted-in backend is live;
    // subsequent frames replace it with bounded measurements.
    mDiagnostics.Flush();
}

void GfxRenderingAPIVulkan::Shutdown() {
    FinishNativePicaAotShaders();
    if (mInstance == VK_NULL_HANDLE) {
        return;
    }
    if (mDevice != VK_NULL_HANDLE) {
        WaitForAllPresents();
        StopPresentWorker();
        vkDeviceWaitIdle(mDevice);
        StorePipelineCache();
        ShutdownImGuiBackend();
        mSceneSurfaces.Clear();
        mResourceStates.Clear();
        mCacaoPass.Shutdown();
        mFidelityFxSssrPass.Shutdown();
        mReflectionMaterialResolvePass.Shutdown();
        mReflectionIblPass.Shutdown();
        mLinearSceneColorPass.Shutdown();
        mHiZDepthPyramidPass.Shutdown();
        mHiZReflectionPass.Shutdown();
        mMotionVectorPass.Shutdown();
        mNriDirectionalShadowPass.Shutdown();
        mNriPicaScanoutPass.Shutdown();
        mNriUpscalerPass.Shutdown();
        mSceneCompositePass.Shutdown();
        mSmaa1xPass.Shutdown();
        mNriEffectGraphTransientImageArena.Shutdown();
        mTemporalAaPass.Shutdown();
        mInteractiveGrassPass.Shutdown();
        mPicaDynamicRenderingScope.Shutdown();
        mNriPicaTextureUploadPass.Shutdown();
        mNriPicaPipelineBridge.Shutdown();
        DestroyNativePicaGeometryResources();
        DestroyNativePicaShaderResources();
        mNriPicaDisplayCopyPass.Shutdown();
        mNriPicaMemoryFillClearPass.Shutdown();
        mNriPicaRenderTargetInitPass.Shutdown();
        mNriPicaRenderTargetOwner.Shutdown();
        mNriPicaDisplayImageOwner.Shutdown();
        mNriPicaTextureImageOwner.Shutdown();
        DestroySwapchainResources();
        mNriSwapchain.Shutdown();
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
        for (uint32_t slot = 0U; slot < kFramesInFlight; ++slot) {
            const VkImage output =
                mD3d12NgxProvider.FrameOutputImage(slot);
            if (output != VK_NULL_HANDLE)
                mNriInterop.ForgetTexture(output);
        }
        mD3d12NgxProvider.Shutdown();
#endif
        mNriInterop.Shutdown();
        mGpuProfiler.Shutdown();
        mScanoutProbe.Shutdown();
        DestroyGraphicsPipelines();
        for (auto& [id, framebuffer] : mOffscreenFramebuffers) {
            DestroyOffscreenFramebuffer(framebuffer);
        }
        mOffscreenFramebuffers.clear();
        for (auto& [id, texture] : mTextures) {
            DestroyTexture(texture);
        }
        mTextures.clear();
        for (auto& [id, cached] : mOot3dCachedVertexBuffers) {
            for (auto& buffer : cached.Buffers) {
                DestroyBuffer(buffer);
            }
        }
        mOot3dCachedVertexBuffers.clear();
        DestroyTexture(mFallbackTexture);
        for (VkSampler sampler : mRetiredSamplers) {
            vkDestroySampler(mDevice, sampler, nullptr);
        }
        mRetiredSamplers.clear();
        for (auto& frame : mFrameResources) {
            if (frame.DescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(mDevice, frame.DescriptorPool, nullptr);
                frame.DescriptorPool = VK_NULL_HANDLE;
            }
            DestroyBuffer(frame.VertexBuffer);
            DestroyBuffer(frame.UniformBuffer);
        }
        for (auto& [key, shader] : mShaders) {
            if (shader.VertexShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(mDevice, shader.VertexShader, nullptr);
            }
            if (shader.FragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(mDevice, shader.FragmentShader, nullptr);
            }
        }
        mShaders.clear();
        if (mPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
            mPipelineLayout = VK_NULL_HANDLE;
        }
        if (mTextureDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(mDevice, mTextureDescriptorSetLayout, nullptr);
            mTextureDescriptorSetLayout = VK_NULL_HANDLE;
        }
        for (uint32_t index = 0; index < kFramesInFlight; ++index) {
            if (mImageAvailableSemaphores[index] != VK_NULL_HANDLE) {
                vkDestroySemaphore(mDevice, mImageAvailableSemaphores[index], nullptr);
                mImageAvailableSemaphores[index] = VK_NULL_HANDLE;
            }
            if (mInFlightFences[index] != VK_NULL_HANDLE) {
                vkDestroyFence(mDevice, mInFlightFences[index], nullptr);
                mInFlightFences[index] = VK_NULL_HANDLE;
            }
        }
        if (mOot3dShadow2dRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(mDevice, mOot3dShadow2dRenderPass, nullptr);
            mOot3dShadow2dRenderPass = VK_NULL_HANDLE;
        }
        if (mCommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
        }
        if (mPipelineCache != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(mDevice, mPipelineCache, nullptr);
            mPipelineCache = VK_NULL_HANDLE;
        }
        vkDestroyDevice(mDevice, nullptr);
    }
    mDiagnostics.EndFrame();
    mDiagnostics.Flush();
    if (mSurface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
    }
    mVulkanValidation.Shutdown();
    vkDestroyInstance(mInstance, nullptr);

    mInstance = VK_NULL_HANDLE;
    mSurface = VK_NULL_HANDLE;
    mPhysicalDevice = VK_NULL_HANDLE;
    mDevice = VK_NULL_HANDLE;
    mCommandPool = VK_NULL_HANDLE;
    mInitialized = false;
    mFrameActive = false;
    mRenderPassActive = false;
    mOverlayRenderPassActive = false;
    mFrameSubmitted = false;
    mOot3dShadow2dPassActive = false;
    mActiveOot3dShadow2dFramebufferId = -1;
    mBoundOot3dShadow2dFramebufferId = -1;
}

bool GfxRenderingAPIVulkan::InitImGuiBackend() {
    if (mImGuiInitialized) return true;
    if (mDevice == VK_NULL_HANDLE || mOverlayRenderPass == VK_NULL_HANDLE ||
        mSwapchainImages.size() < 2) return false;
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_2;
    info.Instance = mInstance;
    info.PhysicalDevice = mPhysicalDevice;
    info.Device = mDevice;
    info.QueueFamily = mGraphicsQueueFamily;
    info.Queue = mGraphicsQueue;
    info.RenderPass = mOverlayRenderPass;
    info.MinImageCount = 2;
    info.ImageCount = static_cast<uint32_t>(mSwapchainImages.size());
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.PipelineCache = mPipelineCache;
    info.DescriptorPoolSize = 128;
    mImGuiInitialized = ImGui_ImplVulkan_Init(&info);
    return mImGuiInitialized;
}

void GfxRenderingAPIVulkan::ShutdownImGuiBackend() {
    if (!mImGuiInitialized) return;
    ImGui_ImplVulkan_Shutdown();
    mImGuiInitialized = false;
}

void GfxRenderingAPIVulkan::NewImGuiFrame() {
    if (InitImGuiBackend()) ImGui_ImplVulkan_NewFrame();
}

void GfxRenderingAPIVulkan::RenderImGuiDrawData(ImDrawData* drawData) {
    if (!mImGuiInitialized || drawData == nullptr || !mFrameActive) return;
    if (!mRenderPassActive) {
        std::string error;
        if (!PrepareOverlay(&error)) return;
        // Presentation-only host frames can precede the first guest display
        // transfer at 60 Hz or uncapped rates. In that case the overlay
        // preparation has no scene to rescan, so establish the ordinary
        // swapchain pass before ImGui emits indexed draws.
        if (!mRenderPassActive) {
            BeginRenderPassIfNeeded();
        }
    }
    if (!mRenderPassActive) return;
    ImGui_ImplVulkan_RenderDrawData(drawData, mCommandBuffers[mCurrentFrame]);
}

void GfxRenderingAPIVulkan::OnResize() {
    mSwapchainDirty = true;
}

bool GfxRenderingAPIVulkan::PublishSceneView(
    const Oot3d::TitleSceneViewSubmission& view) {
    if (view.CameraAvailable) {
        Oot3d::SceneViewRuntime::Instance().PublishPerspectiveCamera(
            view.GuestFunction, view.GuestReturnAddress,
            view.Left, view.Right, view.Bottom, view.Top,
            view.NearPlane, view.FarPlane, view.Eye, view.At);
    } else {
        Oot3d::SceneViewRuntime::Instance().PublishPerspective(
            view.GuestFunction, view.GuestReturnAddress,
            view.Left, view.Right, view.Bottom, view.Top,
            view.NearPlane, view.FarPlane);
    }
    return true;
}

bool GfxRenderingAPIVulkan::PublishPicaFrameTemporalSample(
    const ::Fast::Renderer3ds::PicaFrameTemporalSample& sample) {
    return mPicaSceneFrame.SetTemporalSample(sample);
}

bool GfxRenderingAPIVulkan::ApplyPresentationSettings(
    const Oot3d::TitlePresentationSettingsView& settings,
    std::string* error) {
    if (!mInitialized || mFrameActive || settings.WindowMode > 2U ||
        settings.Width < 320U || settings.Height < 240U) {
        if (error != nullptr) {
            *error =
                "invalid or unsafe OOT3D presentation-settings transaction";
        }
        mDiagnostics.RecordPresentationTransaction(
            settings.TransactionKind, false, true);
        return false;
    }
    if (mPresentationSettingsApplied &&
        mAppliedWindowMode == settings.WindowMode &&
        mAppliedOutputWidth == settings.Width &&
        mAppliedOutputHeight == settings.Height &&
        mAppliedVsync == settings.VSync) {
        mDiagnostics.SetPresentationState({
            true,
            mAppliedWindowMode,
            mAppliedOutputWidth,
            mAppliedOutputHeight,
            mSwapchainExtent.width,
            mSwapchainExtent.height,
            mAppliedVsync,
        });
        mDiagnostics.RecordPresentationTransaction(
            settings.TransactionKind, true, false);
        return true;
    }

    Oot3d::TitlePresentationSettingsView previous;
    if (mPresentationSettingsApplied) {
        previous = {
            mAppliedWindowMode,
            mAppliedOutputWidth,
            mAppliedOutputHeight,
            mAppliedVsync,
        };
    } else {
        int32_t x = 0;
        int32_t y = 0;
        mWindowBackend->GetDimensions(
            &previous.Width, &previous.Height, &x, &y);
        const bool borderless =
            mWindowBackend->IsWindowedFullscreen();
        previous.WindowMode = !mWindowBackend->IsFullscreen()
            ? 0U
            : borderless ? 1U : 2U;
        previous.VSync = mVsyncEnabled;
    }

    const auto apply = [&](const Oot3d::TitlePresentationSettingsView& value,
                           bool allowInjectedFailure) {
        const bool fullscreen = value.WindowMode != 0U;
        const bool borderless = value.WindowMode == 1U;
        const bool currentlyBorderless =
            mWindowBackend->IsWindowedFullscreen();
        const uint8_t currentMode = !mWindowBackend->IsFullscreen()
            ? 0U
            : currentlyBorderless ? 1U : 2U;
        const bool fullscreenFlavorChanged =
            fullscreen && currentMode != 0U &&
            currentMode != value.WindowMode;
        const bool exclusiveModeChanged =
            currentMode == 2U && value.WindowMode == 2U;
        const bool leavingFullscreen =
            !fullscreen && currentMode != 0U;
        if (fullscreenFlavorChanged || exclusiveModeChanged ||
            leavingFullscreen) {
            mWindowBackend->SetFullscreen(false);
        }

        int32_t x = 0;
        int32_t y = 0;
        uint32_t oldWidth = 0;
        uint32_t oldHeight = 0;
        mWindowBackend->GetDimensions(
            &oldWidth, &oldHeight, &x, &y);
        mWindowBackend->SetWindowedFullscreen(borderless);
        if (!fullscreen || !borderless) {
            mWindowBackend->SetDimensions(
                value.Width, value.Height, x, y);
        }
        if (value.WindowMode == 2U &&
            !mWindowBackend->SetExclusiveFullscreenDisplayMode(
                value.Width, value.Height)) {
            throw std::runtime_error(
                "SDL rejected the requested exclusive display mode");
        }
        if (mWindowBackend->IsFullscreen() != fullscreen) {
            mWindowBackend->SetFullscreen(fullscreen);
        } else if (fullscreenFlavorChanged || exclusiveModeChanged) {
            mWindowBackend->SetFullscreen(true);
        }
        if (mWindowBackend->IsFullscreen() != fullscreen) {
            throw std::runtime_error(
                "SDL rejected the requested fullscreen mode");
        }
        if (fullscreen &&
            mWindowBackend->IsWindowedFullscreen() != borderless) {
            throw std::runtime_error(
                "SDL rejected the requested fullscreen flavor");
        }
        mVsyncEnabled = value.VSync;
        mSwapchainDirty = true;
        RecreateSwapchain();
        if (mSwapchainDirty || mSwapchainImages.size() < 2U) {
            throw std::runtime_error(
                "swapchain recreation did not produce usable images");
        }
        if (allowInjectedFailure) {
            const char* inject = std::getenv(
                "OOT3D_GRAPHICS_TEST_FAIL_PRESENTATION_APPLY");
            if (inject != nullptr &&
                std::string_view(inject) == "1") {
                throw std::runtime_error(
                    "injected presentation transaction failure");
            }
        }
    };

    WaitForAllPresents();
    CheckVk(vkDeviceWaitIdle(mDevice),
            "vkDeviceWaitIdle(OOT3D presentation settings)");
    try {
        apply(settings, mPresentationSettingsApplied);
    } catch (const std::exception& applyException) {
        try {
            apply(previous, false);
        } catch (const std::exception& rollbackException) {
            throw std::runtime_error(
                std::string("OOT3D presentation apply failed: ") +
                applyException.what() +
                "; rollback failed: " + rollbackException.what());
        }
        mAppliedWindowMode = previous.WindowMode;
        mAppliedOutputWidth = previous.Width;
        mAppliedOutputHeight = previous.Height;
        mAppliedVsync = previous.VSync;
        mPresentationSettingsApplied = true;
        mDiagnostics.SetPresentationState({
            true,
            mAppliedWindowMode,
            mAppliedOutputWidth,
            mAppliedOutputHeight,
            mSwapchainExtent.width,
            mSwapchainExtent.height,
            mAppliedVsync,
        });
        mDiagnostics.RecordPresentationTransaction(
            settings.TransactionKind, false, true);
        if (error != nullptr) {
            *error = std::string(applyException.what()) +
                "; previous presentation settings restored";
        }
        return false;
    }

    mAppliedWindowMode = settings.WindowMode;
    mAppliedOutputWidth = settings.Width;
    mAppliedOutputHeight = settings.Height;
    mAppliedVsync = settings.VSync;
    mPresentationSettingsApplied = true;
    mDiagnostics.SetPresentationState({
        true,
        mAppliedWindowMode,
        mAppliedOutputWidth,
        mAppliedOutputHeight,
        mSwapchainExtent.width,
        mSwapchainExtent.height,
        mAppliedVsync,
    });
    mDiagnostics.RecordPresentationTransaction(
        settings.TransactionKind, true, false);
    return true;
}

void GfxRenderingAPIVulkan::StartFrame() {
    if (!mInitialized || mFrameActive || mFrameSubmitted) {
        return;
    }
    auto& settingsRuntime = Oot3d::GraphicsSettingsRuntime::Instance();
    Oot3d::TickCacaoDiagnostics(settingsRuntime, mFrameCounter);
    settingsRuntime.TickPresentation();
    const auto initialGraphicsSettings = settingsRuntime.Snapshot();
    const auto presentationRequest =
        Oot3d::ResolveRendererPresentationRequest(
            initialGraphicsSettings,
            settingsRuntime.PresentationStatus(),
            mPresentationSettingsApplied);
    if (presentationRequest.has_value()) {
        const auto& value = presentationRequest->Value;
        const Oot3d::TitlePresentationSettingsView view{
            static_cast<uint8_t>(value.Window),
            value.Width,
            value.Height,
            value.VSync,
            static_cast<uint8_t>(presentationRequest->Kind),
        };
        std::string presentationError;
        if (ApplyPresentationSettings(view, &presentationError)) {
            if (presentationRequest->RequiresAcknowledgement() &&
                !settingsRuntime.AcknowledgePresentationApplied(
                    initialGraphicsSettings)) {
                SPDLOG_ERROR(
                    "OOT3D renderer applied display settings but the "
                    "transaction could not be acknowledged");
            }
        } else {
            if (presentationRequest->Kind ==
                Oot3d::RendererPresentationRequestKind::Candidate) {
                settingsRuntime.RejectPresentationApply(
                    initialGraphicsSettings, presentationError);
            }
            SPDLOG_ERROR("OOT3D display settings were not applied: {}",
                         presentationError);
        }
    }
    mCacaoExecutedThisFrame = false;
    mHiZExecutedThisFrame = false;
    mReflectionExecutedThisFrame = false;
    mReflectionProviderThisFrame =
        Oot3d::ReflectionProvider::Off;
    mReflectionOutputEncodingThisFrame =
        Oot3d::SceneColorEncoding::Unknown;
    mGrassExecutedThisFrame = false;
    mLinearColorExecutedThisFrame = false;
    mMotionExecutedThisFrame = false;
    mUpscalerExecutedThisFrame = false;
    mUpscalerUsedD3d12ThisFrame = false;
    mUpscalerOutputEncodingThisFrame =
        Oot3d::SceneColorEncoding::Unknown;
    mUpscalerEffectsCompositedThisFrame = false;
    mCompositeExecutedThisFrame = false;
    mCompositeOutputEncodingThisFrame =
        Oot3d::SceneColorEncoding::Unknown;
    mTaaExecutedThisFrame = false;
    mTaaOutputEncodingThisFrame =
        Oot3d::SceneColorEncoding::Unknown;
    mTaaEffectsCompositedThisFrame = false;
    mSmaaExecutedThisFrame = false;
    mSmaaEffectsCompositedThisFrame = false;
    mSmaaOutputEncodingThisFrame =
        Oot3d::SceneColorEncoding::Unknown;
    mSmaaRenderTargetNamespace = 0;
    mSmaaDisplayPhysicalAddress = 0;
    mSmaaWidth = 0;
    mSmaaHeight = 0;
    mGrassRenderedTargetsThisFrame.clear();
    mGrassInsertionAttemptedTargetsThisFrame.clear();
    mGrassWorldTargetsThisFrame.clear();
    mDirectionalShadowInsertionAttemptedTargetsThisFrame.clear();
    mGrassSurfaceOccurrencesThisFrame.clear();
    mRigidMotionOccurrencesThisFrame.clear();
    mNativePicaDepthWritingDrawsThisFrame = 0;
    mCustomTextureUploadBytesThisFrame = 0;
    if (mFrameCounter > 3U) {
        Oot3d::GrassSceneBridge::Instance().PruneBeforeFrame(mFrameCounter - 2U);
        mRigidMotionTracker.PruneBeforeFrame(mFrameCounter - 2U);
        std::erase_if(mPreviousPicaVertexUniforms,
            [this](const auto& item) {
                return item.second.FrameId + 2U < mFrameCounter;
            });
    }
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::ValidViewMetadata,
        Oot3d::SceneViewRuntime::Instance().LatestPerspective().has_value(),
        "No valid perspective frustum has been published by the game runtime");
    if (const char* grassSmoke = std::getenv("OOT3D_GRAPHICS_GRASS_AUTO");
        grassSmoke != nullptr && std::string_view(grassSmoke) == "1") {
        auto candidate = settingsRuntime.Snapshot();
        if (candidate.Grass.Rules.empty()) {
            const auto selectors =
                Oot3d::GrassSceneBridge::Instance().AvailableTextures();
            static bool grassSmokeSourceReported = false;
            if (!selectors.empty() && !grassSmokeSourceReported) {
                std::fprintf(stderr,
                    "OOT3D grass smoke: %zu source meshes, %zu texture selectors\n",
                    Oot3d::GrassSceneBridge::Instance().Size(), selectors.size());
                grassSmokeSourceReported = true;
            }
            uint64_t requestedHash = 0U;
            if (const char* hash = std::getenv("OOT3D_GRAPHICS_GRASS_HASH");
                hash != nullptr) {
                char* end = nullptr;
                requestedHash = std::strtoull(hash, &end, 16);
                if (end == hash || *end != '\0') requestedHash = 0U;
            }
            size_t addedRules = 0U;
            for (size_t index = 0;
                 index < selectors.size() && addedRules < 128U; ++index) {
                if (requestedHash != 0U &&
                    selectors[index].Rgba8Hash != requestedHash) continue;
                Oot3d::GrassPlacementRule rule;
                rule.RuleId = static_cast<uint32_t>(addedRules + 1U);
                rule.Target = selectors[index];
                rule.InputBlack = 0.0F;
                candidate.Grass.Rules.push_back(rule);
                ++addedRules;
            }
            if (!candidate.Grass.Rules.empty()) {
                candidate.Preset = Oot3d::GraphicsPreset::Custom;
                candidate.Grass.Quality = Oot3d::GrassQuality::Medium;
                candidate.Grass.Generation.InstancesPerSquareMeter =
                    3.0F;
                candidate.Grass.MaxInstancesPerRoom = 30000U;
                candidate.Grass.DrawDistance = 1200.0F;
                candidate.Grass.WindDirectionDegrees = 35.0F;
                candidate.Grass.WindStrength = 0.5F;
                candidate.Grass.WindSpeed = 1.2F;
                settingsRuntime.Apply(std::move(candidate));
            }
        }
    }
    auto graphicsSnapshot = settingsRuntime.SnapshotForRendering();
    const auto& nextGraphicsSettings = graphicsSnapshot.Value;
    if (mFrameGraphicsSettingsRevision != 0 &&
        (mFrameGraphicsSettings.Grass.Quality != nextGraphicsSettings.Grass.Quality ||
         mFrameGraphicsSettings.Effects.Toon != nextGraphicsSettings.Effects.Toon ||
         mFrameGraphicsSettings.Effects.ToonStyle.OutlineEnabled != nextGraphicsSettings.Effects.ToonStyle.OutlineEnabled ||
         mFrameGraphicsSettings.Effects.AmbientOcclusion != nextGraphicsSettings.Effects.AmbientOcclusion ||
         mFrameGraphicsSettings.Effects.Reflections != nextGraphicsSettings.Effects.Reflections)) {
        // Never blend the old enhanced/native presentation into the newly selected one.
        mTemporalAaPass.ResetHistory();
        mTaaOutputValid = false;
    }
    mFrameGraphicsSettings = std::move(graphicsSnapshot.Value);
    mFrameGraphicsSettingsRevision = graphicsSnapshot.Revision;
    const auto& graphicsSettings = mFrameGraphicsSettings;
    mPicaShaderPipelineCache.BeginFrame(
        mFrameGraphicsSettingsRevision);
    auto& azaharTexturePacks =
        ::Oot3d::Renderer::AzaharTexturePackRuntime::Instance();
    if (mAzaharConfiguredSettingsRevision !=
        mFrameGraphicsSettingsRevision) {
        azaharTexturePacks.Configure({
            .DumpTextures =
                graphicsSettings.TexturePacks.Azahar.DumpTextures,
            .LoadCustomTextures =
                graphicsSettings.TexturePacks.Azahar
                    .LoadCustomTextures,
            .LoadDirectory =
                graphicsSettings.TexturePacks.Azahar.LoadDirectory,
            .DumpDirectory =
                graphicsSettings.TexturePacks.Azahar.DumpDirectory,
        });
        mAzaharConfiguredSettingsRevision =
            mFrameGraphicsSettingsRevision;
    }
    mFrameAzaharTextureGeneration =
        azaharTexturePacks.Generation();
    const char* taaEnvironment = std::getenv("OOT3D_GRAPHICS_TAA");
    const char* motionEnvironment = std::getenv("OOT3D_GRAPHICS_MOTION");
    const Oot3d::AntiAliasingFrameCapabilities aaCapabilities{
        mNativePicaDepthResolveMode != VK_RESOLVE_MODE_NONE,
        (mNativePicaSupportedSampleCounts & VK_SAMPLE_COUNT_2_BIT) != 0U,
        (mNativePicaSupportedSampleCounts & VK_SAMPLE_COUNT_4_BIT) != 0U,
        (mNativePicaSupportedSampleCounts & VK_SAMPLE_COUNT_8_BIT) != 0U};
    const auto aaPolicy = Oot3d::ResolveAntiAliasingFramePolicy(
        graphicsSettings, aaCapabilities,
        taaEnvironment != nullptr &&
            std::string_view(taaEnvironment) == "1",
        motionEnvironment != nullptr &&
            std::string_view(motionEnvironment) == "1");
    mNativeFidelityProfile =
        graphicsSettings.Preset == Oot3d::GraphicsPreset::Authentic;
    mTemporalJitterEnabled =
        !mNativeFidelityProfile && aaPolicy.TemporalJitter;
    mTemporalMotionEnabled =
        !mNativeFidelityProfile && aaPolicy.TemporalMotion;
    const Oot3d::PicaAttachmentFeatureRequests extensionRequests{
        graphicsSettings.Effects.DirectionalShadows.Mode ==
            Oot3d::DirectionalShadowMode::SingleCascade,
        graphicsSettings.Effects.AmbientOcclusion ==
            Oot3d::AmbientOcclusionMode::Cacao,
        graphicsSettings.Effects.Toon != Oot3d::ToonMode::Off &&
            graphicsSettings.Effects.ToonStyle.OutlineEnabled,
        graphicsSettings.Effects.Reflections !=
            Oot3d::ReflectionMode::Off,
        mTemporalMotionEnabled,
        graphicsSettings.Grass.Quality != Oot3d::GrassQuality::Off,
    };
    if (!mPicaExtensionRequests.has_value() ||
        *mPicaExtensionRequests != extensionRequests) {
        auto graph = Oot3d::BuildPicaExtensionGraph(extensionRequests);
        if (!graph.Valid()) {
            throw std::runtime_error(
                "PICA extension graph is invalid: " + graph.Error);
        }
        auto directionalShadowMapBarriers =
            Oot3d::BuildEffectPassBarrierPlan(
                graph,
                Oot3d::PicaExtensionPassName(
                    Oot3d::PicaExtensionPass::DirectionalShadowMap));
        auto directionalShadowSchedule =
            Oot3d::BuildPicaEffectPassSchedulePlan(
                graph,
                Oot3d::PicaExtensionPassName(
                    Oot3d::PicaExtensionPass::DirectionalShadowMap),
                Oot3d::EffectContractKind::AuxiliaryOutput);
        auto interactiveGrassProvider =
            Oot3d::BuildEffectGeometryProviderPlan(
                graph,
                Oot3d::PicaExtensionPassName(
                    Oot3d::PicaExtensionPass::InteractiveGrass));
        if (extensionRequests.DirectionalShadows &&
            (!directionalShadowMapBarriers.Valid() ||
             !directionalShadowSchedule.Valid() ||
             !Oot3d::PicaExtensionPassEnabled(
                 graph,
                 Oot3d::PicaExtensionPass::DirectionalShadowLighting))) {
            throw std::runtime_error(
                "PICA extension graph did not compile directional shadow contracts");
        }
        if (extensionRequests.InteractiveGrass &&
            !interactiveGrassProvider.Valid()) {
            throw std::runtime_error(
                "PICA extension graph did not compile the interactive grass geometry-provider contract");
        }
        mPicaExtensionRequests = extensionRequests;
        mPicaExtensionGraph = std::move(graph);
        mInteractiveGrassProviderPlan =
            std::move(interactiveGrassProvider);
        mDirectionalShadowMapBarrierPlan =
            std::move(directionalShadowMapBarriers);
        mDirectionalShadowSchedulePlan =
            std::move(directionalShadowSchedule);
    }
    mInteractiveGrassProviderExecution.Reset(
        mInteractiveGrassProviderPlan);
    if (mFramePicaAttachmentRequirements != mPicaExtensionGraph.Attachments) {
        // Effect activation is not a native framebuffer invalidation. Retain
        // color/depth, transfer snapshots and cached framebuffer variants.
        for (auto& [key, target] : mNativePicaRenderTargets) {
            target.AuxiliaryNeedsClear = true;
        }
    }
    mFramePicaAttachmentRequirements = mPicaExtensionGraph.Attachments;
    if (mNativeFidelityProfile &&
        !mFramePicaAttachmentRequirements.NativeColorOnly()) {
        throw std::runtime_error(
            "Native Fidelity requested auxiliary PICA attachments");
    }
    mTemporalJitterPixels = mTemporalJitterEnabled
        ? Oot3d::TemporalJitterForFrame(
              mFrameCounter,
              aaPolicy.TemporalJitterPhases).PixelOffset
        : std::array<float, 2>{};
    if (!mTemporalMotionEnabled) {
        mRigidMotionTracker.Reset();
        mPreviousPicaVertexUniforms.clear();
    }
    if (!mTemporalJitterEnabled) {
        mTaaOutputValid = false;
        mTemporalAaPass.ResetHistory();
    }
    if (graphicsSettings.Effects.AmbientOcclusion !=
        Oot3d::AmbientOcclusionMode::Cacao) {
        // The image may remain allocated for cheap re-enabling, but it must no
        // longer be eligible for scanout composition as soon as AO is Off.
        mCacaoOutputValid = false;
    }
    const float requestedInternalResolutionScale =
        Oot3d::ResolveInternalResolutionScale(graphicsSettings);
    mSpatialAaMode = aaPolicy.SpatialAaMode;
    if (mVsyncEnabled != graphicsSettings.VSync) {
        mVsyncEnabled = graphicsSettings.VSync;
        mSwapchainDirty = true;
    }
    WaitForFramePresent(mCurrentFrame);
    const VkSampleCountFlagBits requestedSampleCount =
        aaPolicy.MsaaSamples >= 8U ? VK_SAMPLE_COUNT_8_BIT
        : aaPolicy.MsaaSamples >= 4U ? VK_SAMPLE_COUNT_4_BIT
        : aaPolicy.MsaaSamples >= 2U ? VK_SAMPLE_COUNT_2_BIT
                                    : VK_SAMPLE_COUNT_1_BIT;
    if (requestedSampleCount != mNativePicaSampleCount) {
        ApplyNativePicaSampleCount(requestedSampleCount);
    }
    if (mPresentSwapchainDirty.exchange(false)) {
        mSwapchainDirty = true;
    }
    if (mSwapchainSuboptimal.exchange(false) && !mSwapchainDirty) {
        mSwapchainDirty = SwapchainSurfaceChanged();
    }
    const auto presentError = static_cast<VkResult>(mPresentError.exchange(VK_SUCCESS));
    if (presentError != VK_SUCCESS) {
        CheckVk(presentError, "asynchronous vkQueuePresentKHR");
    }
    if (std::abs(requestedInternalResolutionScale -
                 mInternalResolutionScale) > 0.0005F) {
        ApplyInternalResolutionScale(requestedInternalResolutionScale);
    }
    if (mSwapchainDirty) {
        RecreateSwapchain();
        if (mSwapchainDirty) {
            return;
        }
    }
    PrewarmNativePicaPipelines();

    CheckVk(vkWaitForFences(mDevice, 1, &mInFlightFences[mCurrentFrame], VK_TRUE,
                            std::numeric_limits<uint64_t>::max()),
            "vkWaitForFences");
    mScanoutProbe.Consume(mCurrentFrame);
    ReleaseRetiredNativePicaGeometryBuffers(mCurrentFrame);
    auto& frame = mFrameResources[mCurrentFrame];
    mCompletedNativePicaIds.insert(mCompletedNativePicaIds.end(),
                                   frame.NativePicaCompletionIds.begin(),
                                   frame.NativePicaCompletionIds.end());
    frame.NativePicaCompletionIds.clear();
    bool nriAcquire = false;
    VkResult acquire = VK_NOT_READY;
    for (;;) {
        if (mNriSwapchain.Active()) {
            const auto result =
                mNriSwapchain.Acquire(mCurrentFrame, mCurrentImage);
            if (result ==
                Oot3d::NriSwapchainOperationResult::OutOfDate) {
                mSwapchainDirty = true;
                RecreateSwapchain();
                if (mSwapchainDirty)
                    return;
                continue;
            }
            if (result !=
                Oot3d::NriSwapchainOperationResult::Success) {
                throw std::runtime_error(
                    "NRI AcquireNextTexture failed: " +
                    mNriSwapchain.UnavailableReason());
            }
            nriAcquire = true;
            acquire = VK_SUCCESS;
            break;
        }
        {
            std::lock_guard swapchainLock(mSwapchainCallMutex);
            acquire = vkAcquireNextImageKHR(
                mDevice, mSwapchain, std::numeric_limits<uint64_t>::max(),
                mImageAvailableSemaphores[mCurrentFrame], VK_NULL_HANDLE,
                &mCurrentImage);
        }
        if (acquire == VK_NOT_READY || acquire == VK_TIMEOUT) {
            SDL_Delay(1);
            continue;
        }
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            mSwapchainDirty = true;
            RecreateSwapchain();
            if (mSwapchainDirty) {
                return;
            }
            continue;
        }
        break;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        CheckVk(acquire, "vkAcquireNextImageKHR");
    }
    if (acquire == VK_SUBOPTIMAL_KHR) {
        mSwapchainSuboptimal.store(true);
    }

    if (mImagesInFlight[mCurrentImage] != VK_NULL_HANDLE) {
        CheckVk(vkWaitForFences(mDevice, 1, &mImagesInFlight[mCurrentImage], VK_TRUE,
                                std::numeric_limits<uint64_t>::max()),
                "vkWaitForFences(image)");
    }
    mImagesInFlight[mCurrentImage] = mInFlightFences[mCurrentFrame];
    frame.VertexBytesUsed = 0;
    frame.UniformBytesUsed = 0;
    CheckVk(vkResetDescriptorPool(mDevice, frame.DescriptorPool, 0), "vkResetDescriptorPool");
    frame.TextureDescriptorSets.clear();
    CheckVk(vkResetFences(mDevice, 1, &mInFlightFences[mCurrentFrame]), "vkResetFences");
    CheckVk(vkResetCommandBuffer(mCommandBuffers[mCurrentFrame], 0), "vkResetCommandBuffer");

    mFrameExternalComputeSplit = false;
    mFrameExternalWaitSemaphore = VK_NULL_HANDLE;
    mFrameExternalWaitValue = 0U;

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(mCommandBuffers[mCurrentFrame], &beginInfo), "vkBeginCommandBuffer");
    if (mNriInterop.Available() &&
        !mNriInterop.WrapFrameCommandBuffer(mCurrentFrame, mCommandBuffers[mCurrentFrame])) {
        SPDLOG_WARN("OOT3D Vulkan NRI could not wrap frame command buffer {}", mCurrentFrame);
    }
    mFrameActive = true;
    ++mFrameCounter;
    mDrawCallCountThisFrame = 0;
    mRenderPassActive = false;
    mOverlayRenderPassActive = false;
    mNativePicaRenderPassActive = false;
    mActiveNativePicaRenderTarget = nullptr;
    mNativePicaPresentedThisFrame = false;
    mPicaSceneFrame.BeginFrame(mCurrentFrame, mFrameCounter);
    mPicaScenePublications.Reset();
    mDiagnostics.BeginFrame(mFrameCounter);
    mDiagnostics.RecordPicaShaderProfile(mNativeFidelityProfile);
    mDiagnostics.RecordPicaAttachmentContract(
        mFramePicaAttachmentRequirements.ColorAttachmentCount(),
        mFramePicaAttachmentRequirements.Key());
    const auto diagnosticCount = [](uint64_t value) {
        return static_cast<uint32_t>(std::min<uint64_t>(
            value, std::numeric_limits<uint32_t>::max()));
    };
    mDiagnostics.RecordPicaPipelinePrewarm(
        diagnosticCount(mNativePicaPipelines.size()),
        diagnosticCount(mPicaPipelineManifest.Entries().size()),
        mPicaPipelinePrewarmEnabled,
        diagnosticCount(mPicaPipelinePrewarmCreated),
        diagnosticCount(mPicaPipelinePrewarmReused),
        diagnosticCount(mPicaPipelinePrewarmSkipped));
    mDiagnostics.RecordEffectGraph(
        static_cast<uint32_t>(mPicaExtensionGraph.Passes.size()),
        static_cast<uint32_t>(
            mPicaExtensionGraph.ResourceLifetimes.size()),
        static_cast<uint32_t>(std::min<size_t>(
            mPicaExtensionGraph.DeclaredBindingCount(),
            std::numeric_limits<uint32_t>::max())),
        static_cast<uint32_t>(mPicaExtensionGraph.Barriers.size()));
    mDiagnostics.RecordRendererValidation(
        mValidationTelemetry.Snapshot());
    mDiagnostics.RecordNriSwapchainAcquire(
        nriAcquire,
        static_cast<uint32_t>(mSwapchainImages.size()),
        nriAcquire,
        nriAcquire);
    if (const auto timings = mGpuProfiler.BeginFrame(
            mCurrentFrame, mFrameCounter, mCommandBuffers[mCurrentFrame]);
        timings.has_value() && mFrameCounter > kFramesInFlight) {
        mDiagnostics.SetGpuTimings(mFrameCounter - kFramesInFlight, *timings);
    }
    mViewport = { 0.0f, 0.0f, static_cast<float>(mSwapchainExtent.width),
                  static_cast<float>(mSwapchainExtent.height), 0.0f, 1.0f };
    mScissor = { { 0, 0 }, mSwapchainExtent };
    mViewportSet = true;
    mScissorSet = true;
}

void GfxRenderingAPIVulkan::EndFrame() {
    if (!mFrameActive) {
        return;
    }
    if (mOot3dShadow2dPassActive) {
        EndOot3dShadow2dDepthEncodePass();
    }
    EndNativePicaRenderPass();
    if (!mNativePicaPresentedThisFrame &&
        mLastPresentedNativePicaDisplayTransfer.has_value()) {
        std::string error;
        GfxNativePicaDisplayTransferView transfer =
            *mLastPresentedNativePicaDisplayTransfer;
        transfer.Present = true;
        if (!SubmitPicaDisplayTransfer(
                transfer, &error)) {
            throw std::runtime_error(
                "persistent native PICA scanout failed: " + error);
        }
    }
    if (!mNativePicaPresentedThisFrame) {
        BeginRenderPassIfNeeded();
    }
    if (mRenderPassActive) {
        vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
        mRenderPassActive = false;
        mOverlayRenderPassActive = false;
    }
    const auto sceneFrameStats = mPicaSceneFrame.Stats();
    mScanoutProbe.Record(mCommandBuffers[mCurrentFrame], mSwapchainImages[mCurrentImage],
                         mSwapchainFormat, mSwapchainExtent, mCurrentFrame, mCurrentImage,
                         mFrameCounter, mNativePicaPresentedThisFrame);
    mDiagnostics.RecordPicaSceneFrame(sceneFrameStats);
    mGpuProfiler.EndFrame(mCurrentFrame, mCommandBuffers[mCurrentFrame]);
    CheckVk(vkEndCommandBuffer(mCommandBuffers[mCurrentFrame]), "vkEndCommandBuffer");

    const std::array<VkSemaphore, 2> waitSemaphores{
        mImageAvailableSemaphores[mCurrentFrame],
        mFrameExternalWaitSemaphore};
    const std::array<VkPipelineStageFlags, 2> waitStages{
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
    const std::array<uint64_t, 2> waitValues{
        0U, mFrameExternalWaitValue};
    const uint64_t signalValue = 0U;
    VkTimelineSemaphoreSubmitInfo timeline{
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    if (mFrameExternalComputeSplit) {
        timeline.waitSemaphoreValueCount =
            static_cast<uint32_t>(waitValues.size());
        timeline.pWaitSemaphoreValues = waitValues.data();
        timeline.signalSemaphoreValueCount = 1U;
        timeline.pSignalSemaphoreValues = &signalValue;
    }
    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.pNext = mFrameExternalComputeSplit ? &timeline : nullptr;
    submitInfo.waitSemaphoreCount = mFrameExternalComputeSplit ? 2U : 1U;
    submitInfo.pWaitSemaphores = waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &mCommandBuffers[mCurrentFrame];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &mRenderFinishedSemaphores[mCurrentImage];
    CheckVk(vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, mInFlightFences[mCurrentFrame]),
            "vkQueueSubmit");
    mFrameActive = false;
    mFrameSubmitted = true;
}

void GfxRenderingAPIVulkan::FinishRender() {
    if (!mFrameSubmitted) {
        return;
    }
    bool nriPresented = false;
    if (mNriSwapchain.Active()) {
        const auto result = mNriSwapchain.Present(mCurrentImage);
        if (result == Oot3d::NriSwapchainOperationResult::OutOfDate) {
            mSwapchainDirty = true;
        } else if (result !=
                   Oot3d::NriSwapchainOperationResult::Success) {
            throw std::runtime_error(
                "NRI QueuePresent failed: " +
                mNriSwapchain.UnavailableReason());
        } else {
            nriPresented = true;
        }
    } else if (mAsyncPresentSupported) {
        PresentRequest request;
        request.Swapchain = mSwapchain;
        request.WaitSemaphore = mRenderFinishedSemaphores[mCurrentImage];
        request.ImageIndex = mCurrentImage;
        request.FrameIndex = mCurrentFrame;
        {
            std::lock_guard lock(mPresentMutex);
            request.Serial = mNextPresentSerial++;
            mFramePresentSerials[mCurrentFrame] = request.Serial;
            mPresentRequests.push_back(request);
        }
        mPresentRequestCondition.notify_one();
    } else {
        VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &mRenderFinishedSemaphores[mCurrentImage];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &mSwapchain;
        presentInfo.pImageIndices = &mCurrentImage;
        VkResult present;
        {
            std::lock_guard swapchainLock(mSwapchainCallMutex);
            present = vkQueuePresentKHR(mPresentQueue, &presentInfo);
        }
        if (present == VK_ERROR_OUT_OF_DATE_KHR) {
            mSwapchainDirty = true;
        } else if (present == VK_SUBOPTIMAL_KHR) {
            mSwapchainSuboptimal.store(true);
        } else if (present != VK_SUCCESS) {
            CheckVk(present, "vkQueuePresentKHR");
        }
    }
    mDiagnostics.RecordNriSwapchainPresent(nriPresented);
    mDiagnostics.RecordEffectGeometryProvider(
        mInteractiveGrassProviderExecution.Summary());
    mDiagnostics.RecordRendererValidation(
        mValidationTelemetry.Snapshot());
    mDiagnostics.EndFrame();
    mFrameSubmitted = false;
    mCurrentFrame = (mCurrentFrame + 1) % kFramesInFlight;
}

int GfxRenderingAPIVulkan::CreateFramebuffer() {
    const int id = mFramebufferCount++;
    mOffscreenFramebuffers.emplace(id, OffscreenFramebufferRecord{});
    return id;
}

void GfxRenderingAPIVulkan::UpdateFramebufferParameters(int fbId, uint32_t width, uint32_t height, uint32_t,
                                                        bool, bool, bool, bool) {
    if (fbId != 0) {
        return;
    }
    if (width == mRequestedWidth && height == mRequestedHeight) {
        return;
    }
    mRequestedWidth = width;
    mRequestedHeight = height;
    if (mInitialized) {
        // A fullscreen/mobile surface can grant a different extent from the
        // logical render request. Recreating it cannot change that constraint.
        // Real window changes still invalidate through OnResize/OUT_OF_DATE.
        const auto extent = ChooseExtent(QuerySwapchainSupport(mPhysicalDevice).Capabilities);
        if (extent.width != mSwapchainExtent.width || extent.height != mSwapchainExtent.height) {
            mSwapchainDirty = true;
        }
    }
}

bool GfxRenderingAPIVulkan::UpdateFramebufferParametersWithColorFormat(
    int fbId, uint32_t width, uint32_t height, uint32_t msaaLevel, bool openglInvertY,
    bool renderTarget, bool hasDepthBuffer, bool canExtractDepth,
    GfxFramebufferColorFormat colorFormat) {
    if (colorFormat == GfxFramebufferColorFormat::Rgba8) {
        if (fbId != 0) {
            return false;
        }
        UpdateFramebufferParameters(fbId, width, height, msaaLevel, openglInvertY,
                                    renderTarget, hasDepthBuffer, canExtractDepth);
        return true;
    }
    if (colorFormat != GfxFramebufferColorFormat::R32ui || fbId <= 0 || !renderTarget ||
        msaaLevel > 1 || !SupportsOot3dShadow2dR32uiPipeline()) {
        return false;
    }
    auto it = mOffscreenFramebuffers.find(fbId);
    if (it == mOffscreenFramebuffers.end()) {
        return false;
    }
    width = std::max(width, 1U);
    height = std::max(height, 1U);
    auto& framebuffer = it->second;
    if (framebuffer.Framebuffer != VK_NULL_HANDLE && framebuffer.Width == width &&
        framebuffer.Height == height && framebuffer.HasDepthBuffer == hasDepthBuffer &&
        framebuffer.ColorFormat == colorFormat) {
        return true;
    }
    if (mFrameSubmitted) {
        CheckVk(vkWaitForFences(mDevice, 1, &mInFlightFences[mCurrentFrame], VK_TRUE,
                                std::numeric_limits<uint64_t>::max()),
                "vkWaitForFences(recreate OOT3D Shadow2D framebuffer)");
    } else {
        CheckVk(vkDeviceWaitIdle(mDevice),
                "vkDeviceWaitIdle(recreate OOT3D Shadow2D framebuffer)");
    }
    DestroyOffscreenFramebuffer(framebuffer);
    framebuffer.ColorFormat = colorFormat;
    return CreateOot3dShadow2dFramebuffer(framebuffer, width, height, hasDepthBuffer);
}

bool GfxRenderingAPIVulkan::SupportsOot3dShadow2dR32uiPipeline() const {
    if (mPhysicalDevice == VK_NULL_HANDLE) {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, VK_FORMAT_R32_UINT, &properties);
    constexpr VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    return (properties.optimalTilingFeatures & required) == required;
}

bool GfxRenderingAPIVulkan::BindOot3dShadow2dTexture(int fbId, uint32_t textureUnit) {
    constexpr uint32_t kOot3dShadow2dTextureUnit = 6;
    if (textureUnit != kOot3dShadow2dTextureUnit) {
        return false;
    }
    const auto it = mOffscreenFramebuffers.find(fbId);
    if (it == mOffscreenFramebuffers.end() ||
        it->second.ColorFormat != GfxFramebufferColorFormat::R32ui ||
        it->second.ColorView == VK_NULL_HANDLE || it->second.ColorSampler == VK_NULL_HANDLE) {
        return false;
    }
    mBoundOot3dShadow2dFramebufferId = fbId;
    return true;
}

bool GfxRenderingAPIVulkan::SetOot3dShadow2dShaderParameters(uint32_t textureBias,
                                                             bool orthographic,
                                                             bool invert) {
    if (mCurrentShader == nullptr ||
        !IsOot3dShadow2dShader(mCurrentShader->NativeShaderId)) {
        return false;
    }
    mDrawUniforms.ShadowTextureBias = static_cast<int32_t>(textureBias);
    mDrawUniforms.ShadowOrthographic = orthographic ? 1 : 0;
    mDrawUniforms.ShadowInvert = invert ? 1 : 0;
    return true;
}

bool GfxRenderingAPIVulkan::StartOot3dShadow2dDepthEncodePass(int fbId,
                                                              uint32_t clearValue) {
    if (!mFrameActive || mOot3dShadow2dPassActive ||
        !SupportsOot3dShadow2dR32uiPipeline()) {
        return false;
    }
    const auto it = mOffscreenFramebuffers.find(fbId);
    if (it == mOffscreenFramebuffers.end() || it->second.Framebuffer == VK_NULL_HANDLE ||
        it->second.ColorFormat != GfxFramebufferColorFormat::R32ui ||
        !it->second.HasDepthBuffer) {
        return false;
    }
    if (mRenderPassActive) {
        if (mDrawCallCountThisFrame != 0) {
            return false;
        }
        vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
        mRenderPassActive = false;
        mOverlayRenderPassActive = false;
    }
    CreateOot3dShadow2dDepthEncodePipeline();
    if (mOot3dShadow2dDepthEncodePipeline == VK_NULL_HANDLE) {
        return false;
    }

    mViewportBeforeOot3dShadow2d = mViewport;
    mScissorBeforeOot3dShadow2d = mScissor;
    mViewportSetBeforeOot3dShadow2d = mViewportSet;
    mScissorSetBeforeOot3dShadow2d = mScissorSet;
    mFramebufferBeforeOot3dShadow2d = mCurrentFramebuffer;
    mCurrentFramebuffer = fbId;
    mActiveOot3dShadow2dFramebufferId = fbId;

    VkClearValue clearValues[2]{};
    clearValues[0].color.uint32[0] = clearValue;
    clearValues[1].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo beginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    beginInfo.renderPass = mOot3dShadow2dRenderPass;
    beginInfo.framebuffer = it->second.Framebuffer;
    beginInfo.renderArea.extent = { it->second.Width, it->second.Height };
    beginInfo.clearValueCount = 2;
    beginInfo.pClearValues = clearValues;
    vkCmdBeginRenderPass(mCommandBuffers[mCurrentFrame], &beginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    mOot3dShadow2dPassActive = true;
    mViewport = { 0.0f, 0.0f, static_cast<float>(it->second.Width),
                  static_cast<float>(it->second.Height), 0.0f, 1.0f };
    mScissor = { { 0, 0 }, { it->second.Width, it->second.Height } };
    mViewportSet = true;
    mScissorSet = true;
    vkCmdSetViewport(mCommandBuffers[mCurrentFrame], 0, 1, &mViewport);
    vkCmdSetScissor(mCommandBuffers[mCurrentFrame], 0, 1, &mScissor);
    return true;
}

void GfxRenderingAPIVulkan::EndOot3dShadow2dDepthEncodePass() {
    if (!mFrameActive || !mOot3dShadow2dPassActive) {
        return;
    }
    vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
    mOot3dShadow2dPassActive = false;
    mActiveOot3dShadow2dFramebufferId = -1;
    mCurrentFramebuffer = mFramebufferBeforeOot3dShadow2d;
    mViewport = mViewportBeforeOot3dShadow2d;
    mScissor = mScissorBeforeOot3dShadow2d;
    mViewportSet = mViewportSetBeforeOot3dShadow2d;
    mScissorSet = mScissorSetBeforeOot3dShadow2d;
}

bool GfxRenderingAPIVulkan::DrawOot3dShadow2dDepthEncodedTriangles(
    float bufVbo[], size_t bufVboLen, size_t bufVboNumTris) {
    if (!mFrameActive || !mOot3dShadow2dPassActive || bufVbo == nullptr ||
        bufVboNumTris == 0 || bufVboLen != bufVboNumTris * 3 * 4 ||
        mOot3dShadow2dDepthEncodePipeline == VK_NULL_HANDLE) {
        return false;
    }
    auto& frame = mFrameResources[mCurrentFrame];
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(bufVboLen * sizeof(float));
    const VkDeviceSize alignedOffset = (frame.VertexBytesUsed + 15) & ~VkDeviceSize(15);
    if (alignedOffset + byteCount > frame.VertexBuffer.Size) {
        throw std::runtime_error("OOT3D Vulkan Shadow2D vertex arena exhausted");
    }
    std::memcpy(static_cast<uint8_t*>(frame.VertexBuffer.Mapped) + alignedOffset, bufVbo,
                static_cast<size_t>(byteCount));
    frame.VertexBytesUsed = alignedOffset + byteCount;

    VkCommandBuffer commandBuffer = mCommandBuffers[mCurrentFrame];
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      mOot3dShadow2dDepthEncodePipeline);
    const VkBuffer vertexBuffer = frame.VertexBuffer.Buffer;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &alignedOffset);
    vkCmdSetViewport(commandBuffer, 0, 1, &mViewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &mScissor);
    vkCmdDraw(commandBuffer, static_cast<uint32_t>(bufVboNumTris * 3), 1, 0, 0);
    return true;
}

void GfxRenderingAPIVulkan::StartDrawToFramebuffer(int fbId, float noiseScale) {
    mCurrentFramebuffer = fbId;
    mCurrentNoiseScale = noiseScale;
}

void GfxRenderingAPIVulkan::CopyFramebuffer(int, int, int, int, int, int, int, int, int, int) {
}

void GfxRenderingAPIVulkan::ClearFramebuffer(bool, bool) {
    if (mCurrentFramebuffer == 0) {
        BeginRenderPassIfNeeded();
    }
}

void GfxRenderingAPIVulkan::ReadFramebufferToCPU(int, uint32_t width, uint32_t height, uint16_t* rgba16Buf) {
    if (rgba16Buf == nullptr || width == 0 || height == 0 ||
        mSwapchainExtent.width == 0 || mSwapchainExtent.height == 0 || mDevice == VK_NULL_HANDLE ||
        (mSwapchain == VK_NULL_HANDLE && !mNriSwapchain.Active()) ||
        mCurrentFramebuffer != 0) {
        return;
    }
    if (mFrameActive) {
        EndFrame();
    }
    if (!mFrameSubmitted) {
        return;
    }
    CheckVk(vkWaitForFences(mDevice, 1, &mInFlightFences[mCurrentFrame], VK_TRUE,
                            std::numeric_limits<uint64_t>::max()),
            "vkWaitForFences(readback)");

    const uint32_t copyWidth = mSwapchainExtent.width;
    const uint32_t copyHeight = mSwapchainExtent.height;
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(copyWidth) * copyHeight * 4;
    auto readback = CreateBuffer(byteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 true);
    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    VkImageMemoryBarrier toTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = mSwapchainImages[mCurrentImage];
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toTransfer);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { copyWidth, copyHeight, 1 };
    vkCmdCopyImageToBuffer(commandBuffer, mSwapchainImages[mCurrentImage],
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.Buffer, 1, &copy);
    VkImageMemoryBarrier toPresent{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = mSwapchainImages[mCurrentImage];
    toPresent.subresourceRange = toTransfer.subresourceRange;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toPresent);
    EndImmediateCommands(commandBuffer);

    const auto* rgba8 = static_cast<const uint8_t*>(readback.Mapped);
    const bool bgra = mSwapchainFormat == VK_FORMAT_B8G8R8A8_UNORM ||
                      mSwapchainFormat == VK_FORMAT_B8G8R8A8_SRGB;
    const bool copied = Renderer::CopyScaledFramebufferRgba5551(
        {rgba8, static_cast<size_t>(byteCount)}, copyWidth, copyHeight, bgra,
        {rgba16Buf, static_cast<size_t>(width) * height}, width, height);
    DestroyBuffer(readback);
    if (!copied) throw std::runtime_error("invalid framebuffer readback extent");
}

void GfxRenderingAPIVulkan::ResolveMSAAColorBuffer(int, int) {
}

std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
GfxRenderingAPIVulkan::GetPixelDepth(int, const std::set<std::pair<float, float>>& coordinates) {
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff> result;
    for (const auto& coordinate : coordinates) {
        result.emplace(coordinate, UINT16_MAX);
    }
    return result;
}

void* GfxRenderingAPIVulkan::GetFramebufferTextureId(int) {
    return nullptr;
}

void GfxRenderingAPIVulkan::SelectTextureFb(int) {
}

void GfxRenderingAPIVulkan::DeleteTexture(uint32_t texId) {
    const auto it = mTextures.find(texId);
    if (it == mTextures.end()) {
        return;
    }
    if (mDevice != VK_NULL_HANDLE) {
        CheckVk(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle(delete texture)");
        DestroyTexture(it->second);
    }
    mTextures.erase(it);
}

void GfxRenderingAPIVulkan::SetTextureFilter(FilteringMode mode) {
    mFilterMode = mode;
}

FilteringMode GfxRenderingAPIVulkan::GetTextureFilter() {
    return mFilterMode;
}

void GfxRenderingAPIVulkan::SetSrgbMode() {
    mSrgbMode = true;
}

ImTextureID GfxRenderingAPIVulkan::GetTextureById(int id) {
    return reinterpret_cast<ImTextureID>(static_cast<intptr_t>(id));
}

void GfxRenderingAPIVulkan::SetCurrentPrimDepth(float depth) {
    mCurrentPrimDepth = depth;
    mDrawUniforms.PrimDepth = depth;
    mPrimDepthDirty = true;
}

void GfxRenderingAPIVulkan::CreateInstance() {
    auto* window = static_cast<SDL_Window*>(mWindowBackend->GetNativeWindow());
    std::vector<const char*> extensions;
#ifdef _WIN32
    extensions = {VK_KHR_SURFACE_EXTENSION_NAME,
                  VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
#else
    unsigned int extensionCount = 0;
    if (SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr) != SDL_TRUE) {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }
    extensions.resize(extensionCount);
    if (SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions.data()) != SDL_TRUE) {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }
#endif

    const auto& ngxRequirements = Oot3d::GetNriNgxVulkanRequirements();
    mNgxVulkanRequirementsSatisfied = ngxRequirements.Available;
    mNgxVulkanUnavailableReason = ngxRequirements.Reason;
    const auto recordMissingNgxRequirement = [this](
        std::string_view category, const std::string& extension) {
        if (!mNgxVulkanUnavailableReason.empty())
            mNgxVulkanUnavailableReason += "; ";
        mNgxVulkanUnavailableReason += "missing NGX ";
        mNgxVulkanUnavailableReason += category;
        mNgxVulkanUnavailableReason += " extension ";
        mNgxVulkanUnavailableReason += extension;
    };
    uint32_t availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    vkEnumerateInstanceExtensionProperties(
        nullptr, &availableCount, available.data());
    for (const std::string& required : ngxRequirements.InstanceExtensions) {
        const bool supported = std::any_of(
            available.begin(), available.end(), [&](const auto& extension) {
                return required == extension.extensionName;
            });
        mNgxVulkanRequirementsSatisfied &= supported;
        if (!supported)
            recordMissingNgxRequirement("instance", required);
        if (supported && std::none_of(
                extensions.begin(), extensions.end(), [&](const char* name) {
                    return required == name;
                }))
            extensions.push_back(required.c_str());
    }
    constexpr const char* kSurfaceCapabilities2 =
        "VK_KHR_get_surface_capabilities2";
    mNriSwapchainExtensionsEnabled = std::any_of(
        available.begin(), available.end(), [&](const auto& extension) {
            return std::string_view(extension.extensionName) ==
                   kSurfaceCapabilities2;
        });
    if (mNriSwapchainExtensionsEnabled &&
        std::none_of(
            extensions.begin(), extensions.end(), [&](const char* name) {
                return std::string_view(name) == kSurfaceCapabilities2;
            })) {
        extensions.push_back(kSurfaceCapabilities2);
    }
    mVulkanValidation.ConfigureFromEnvironment(
        extensions, mValidationTelemetry);

    const auto applicationInfo = Renderer3ds::PicaVulkanApplicationInfo();

    VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount =
        static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    mVulkanValidation.ApplyTo(createInfo);
    CheckVk(vkCreateInstance(&createInfo, nullptr, &mInstance), "vkCreateInstance");
    mVulkanValidation.Initialize(mInstance);
}

void GfxRenderingAPIVulkan::CreateSurface() {
    auto* window = static_cast<SDL_Window*>(mWindowBackend->GetNativeWindow());
#ifdef _WIN32
    SDL_SysWMinfo windowInfo{};
    SDL_VERSION(&windowInfo.version);
    if (SDL_GetWindowWMInfo(window, &windowInfo) != SDL_TRUE) {
        throw std::runtime_error(std::string("SDL_GetWindowWMInfo failed: ") + SDL_GetError());
    }
    VkWin32SurfaceCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    createInfo.hinstance = GetModuleHandleW(nullptr);
    createInfo.hwnd = windowInfo.info.win.window;
    CheckVk(vkCreateWin32SurfaceKHR(mInstance, &createInfo, nullptr, &mSurface),
            "vkCreateWin32SurfaceKHR");
#else
    if (SDL_Vulkan_CreateSurface(window, mInstance, &mSurface) != SDL_TRUE) {
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
#endif
}

void GfxRenderingAPIVulkan::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    CheckVk(vkEnumeratePhysicalDevices(mInstance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices");
    if (deviceCount == 0) {
        throw std::runtime_error("no Vulkan physical device is available");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    CheckVk(vkEnumeratePhysicalDevices(mInstance, &deviceCount, devices.data()),
            "vkEnumeratePhysicalDevices");

    std::vector<Oot3d::VulkanAdapterCandidate> candidates;
    candidates.reserve(devices.size());
    for (uint32_t index = 0; index < devices.size(); ++index) {
        const VkPhysicalDevice device = devices[index];
        const QueueFamilies queues = FindQueueFamilies(device);
        const bool hasSwapchain = DeviceSupportsSwapchain(device);
        bool hasFormats = false;
        bool hasPresentModes = false;
        if (queues.Complete() && hasSwapchain) {
            const auto support = QuerySwapchainSupport(device);
            hasFormats = !support.Formats.empty();
            hasPresentModes = !support.PresentModes.empty();
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        Oot3d::VulkanAdapterClass adapterClass =
            Oot3d::VulkanAdapterClass::Other;
        switch (properties.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                adapterClass =
                    Oot3d::VulkanAdapterClass::Integrated;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                adapterClass =
                    Oot3d::VulkanAdapterClass::Discrete;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                adapterClass =
                    Oot3d::VulkanAdapterClass::Virtual;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                adapterClass = Oot3d::VulkanAdapterClass::Cpu;
                break;
            default:
                break;
        }
        candidates.push_back({
            index,
            properties.deviceName,
            adapterClass,
            properties.limits.maxImageDimension2D,
            queues.Graphics.has_value(),
            queues.Present.has_value(),
            hasSwapchain,
            hasFormats,
            hasPresentModes,
        });
        SPDLOG_INFO(
            "OOT3D Vulkan adapter [{}]: {} (suitable={}, score={})",
            index, properties.deviceName,
            Oot3d::IsVulkanAdapterSuitable(candidates.back()),
            Oot3d::ScoreVulkanAdapter(candidates.back()));
    }

    std::optional<uint32_t> requestedAdapter;
    if (const char* requested =
            std::getenv("OOT3D_GRAPHICS_VULKAN_ADAPTER");
        requested != nullptr && *requested != '\0') {
        const auto parsed =
            Oot3d::ParseVulkanAdapterIndex(requested);
        if (!parsed.Valid())
            throw std::runtime_error(parsed.Reason);
        requestedAdapter = parsed.EnumerationIndex;
    }
    const auto selection =
        Oot3d::SelectVulkanAdapter(candidates, requestedAdapter);
    if (!selection.CandidatePosition.has_value()) {
        throw std::runtime_error(selection.Reason);
    }
    const size_t selectedPosition =
        *selection.CandidatePosition;
    mPhysicalDevice = devices[selectedPosition];
    mVulkanAdapterCount =
        static_cast<uint32_t>(devices.size());
    mVulkanAdapterIndex =
        candidates[selectedPosition].EnumerationIndex;

    const auto queues = FindQueueFamilies(mPhysicalDevice);
    mGraphicsQueueFamily = *queues.Graphics;
    mPresentQueueFamily = *queues.Present;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
    mVulkanAdapterVendorId = properties.vendorID;
    mVulkanAdapterDeviceId = properties.deviceID;
    mNativePicaSupportedSampleCounts =
        properties.limits.framebufferColorSampleCounts &
        properties.limits.framebufferDepthSampleCounts;
    VkPhysicalDeviceDepthStencilResolveProperties resolveProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES};
    VkPhysicalDeviceProperties2 properties2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties2.pNext = &resolveProperties;
    vkGetPhysicalDeviceProperties2(mPhysicalDevice, &properties2);
    if ((resolveProperties.supportedDepthResolveModes &
         VK_RESOLVE_MODE_MIN_BIT) != 0U) {
        mNativePicaDepthResolveMode = VK_RESOLVE_MODE_MIN_BIT;
    } else if ((resolveProperties.supportedDepthResolveModes &
                VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) != 0U) {
        mNativePicaDepthResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    }
    mStorageBufferAlignment =
        std::max<VkDeviceSize>(16, properties.limits.minStorageBufferOffsetAlignment);
    mUniformBufferAlignment =
        std::max<VkDeviceSize>(16, properties.limits.minUniformBufferOffsetAlignment);
    SPDLOG_INFO("OOT3D Vulkan device [{}]: {} (api {}.{}.{})",
                 mVulkanAdapterIndex, properties.deviceName,
                 VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion),
                 VK_VERSION_PATCH(properties.apiVersion));
}

void GfxRenderingAPIVulkan::CreateLogicalDevice() {
    const std::array<float, 2> priorities = { 1.0f, 1.0f };
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice, &queueFamilyCount,
                                             queueFamilyProperties.data());
    const uint32_t graphicsQueueCount =
        mGraphicsQueueFamily < queueFamilyProperties.size()
            ? queueFamilyProperties[mGraphicsQueueFamily].queueCount
            : 0U;
    const char* presentDispatch = std::getenv("TRIAEVUM_VULKAN_PRESENT_DISPATCH");
    const char* videoDriver = SDL_GetCurrentVideoDriver();
    const auto queuePlan = Oot3d::ResolveVulkanQueueTopology(
        mGraphicsQueueFamily, mPresentQueueFamily,
        graphicsQueueCount, Oot3d::ParseVulkanPresentDispatchMode(
            presentDispatch ? presentDispatch : ""), videoDriver ? videoDriver : "");
    mNriSwapchainQueueEligible =
        queuePlan.NriSwapchainEligible;
    std::set<uint32_t> families = { mGraphicsQueueFamily, mPresentQueueFamily };
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t family : families) {
        VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = family == mGraphicsQueueFamily
            ? queuePlan.RequestedGraphicsQueueCount
            : 1U;
        queueInfo.pQueuePriorities = priorities.data();
        queueInfos.push_back(queueInfo);
    }
    uint32_t extensionCount = 0;
    CheckVk(vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &extensionCount, nullptr),
            "vkEnumerateDeviceExtensionProperties");
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    CheckVk(vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &extensionCount,
                                                  availableExtensions.data()),
            "vkEnumerateDeviceExtensionProperties");
    const auto hasExtension = [&](const char* name) {
        return std::any_of(availableExtensions.begin(), availableExtensions.end(),
                           [&](const VkExtensionProperties& extension) {
                               return std::strcmp(extension.extensionName, name) == 0;
                           });
    };
    std::vector<const char*> extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &deviceProperties);
    const bool hasVulkan12 = deviceProperties.apiVersion >= VK_API_VERSION_1_2;
    const bool hasSynchronization2Extension =
        hasExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    const bool hasDynamicRenderingExtension =
        hasExtension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR
    };
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR
    };
    VkPhysicalDeviceVulkan12Features vulkan12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
    };
    vulkan12.pNext = &synchronization2;
    synchronization2.pNext = &dynamicRendering;
    VkPhysicalDeviceFeatures2 supportedFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2
    };
    supportedFeatures.pNext = &vulkan12;
    vkGetPhysicalDeviceFeatures2(
        mPhysicalDevice, &supportedFeatures);
    mSynchronization2Enabled =
        hasVulkan12 && hasSynchronization2Extension &&
        synchronization2.synchronization2 == VK_TRUE;
    mDynamicRenderingEnabled =
        hasVulkan12 && hasDynamicRenderingExtension &&
        dynamicRendering.dynamicRendering == VK_TRUE;
    if (mSynchronization2Enabled)
        extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    if (mDynamicRenderingEnabled)
        extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
    constexpr const char* kExternalMemoryWin32 =
        "VK_KHR_external_memory_win32";
    constexpr const char* kExternalSemaphoreWin32 =
        "VK_KHR_external_semaphore_win32";
    const bool hasExternalMemoryWin32 =
        hasExtension(kExternalMemoryWin32);
    const bool hasExternalSemaphoreWin32 =
        hasExtension(kExternalSemaphoreWin32);
    mD3d12InteropExtensionsEnabled =
        hasExternalMemoryWin32 && hasExternalSemaphoreWin32 &&
        vulkan12.timelineSemaphore == VK_TRUE;
    if (hasExternalMemoryWin32)
        extensions.push_back(kExternalMemoryWin32);
    if (hasExternalSemaphoreWin32)
        extensions.push_back(kExternalSemaphoreWin32);
#endif
    const auto& ngxRequirements = Oot3d::GetNriNgxVulkanRequirements();
    for (const std::string& required : ngxRequirements.DeviceExtensions) {
        const bool promotedBufferDeviceAddress =
            hasVulkan12 &&
            required == VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
        const bool supported =
            promotedBufferDeviceAddress ||
            hasExtension(required.c_str());
        mNgxVulkanRequirementsSatisfied &= supported;
        if (!supported) {
            if (!mNgxVulkanUnavailableReason.empty())
                mNgxVulkanUnavailableReason += "; ";
            mNgxVulkanUnavailableReason +=
                "missing NGX device extension " + required;
        }
        if (supported && !promotedBufferDeviceAddress && std::none_of(
                extensions.begin(), extensions.end(), [&](const char* name) {
                    return required == name;
                }))
            extensions.push_back(required.c_str());
    }
    const auto features = Renderer3ds::PicaVulkanCoreFeatures(supportedFeatures.features);
    // NRI's Vulkan helper and NIS implementation create timeline fences,
    // FP16 shader permutations and update-after-bind image descriptors.
    // Advertise to NRI only features that are also enabled on the wrapped
    // device; otherwise IsUpscalerSupported can return a false positive.
    vulkan12 = Renderer3ds::PicaVulkan12Features(vulkan12);
    mNisVulkanFeaturesEnabled =
        features.shaderStorageImageWriteWithoutFormat == VK_TRUE &&
        vulkan12.timelineSemaphore == VK_TRUE &&
        vulkan12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE &&
        vulkan12.descriptorBindingStorageImageUpdateAfterBind == VK_TRUE;
    mFsrVulkanFeaturesEnabled =
        features.shaderInt16 == VK_TRUE &&
        vulkan12.timelineSemaphore == VK_TRUE;
    VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.pEnabledFeatures = &features;
    void* featureChain = nullptr;
    vulkan12.pNext = featureChain;
    featureChain = &vulkan12;
    if (mSynchronization2Enabled) {
        synchronization2.synchronization2 = VK_TRUE;
        synchronization2.pNext = featureChain;
        featureChain = &synchronization2;
    }
    if (mDynamicRenderingEnabled) {
        dynamicRendering.dynamicRendering = VK_TRUE;
        dynamicRendering.pNext = featureChain;
        featureChain = &dynamicRendering;
    }
    createInfo.pNext = featureChain;
    CheckVk(vkCreateDevice(mPhysicalDevice, &createInfo, nullptr, &mDevice), "vkCreateDevice");
    vkGetDeviceQueue(mDevice, mGraphicsQueueFamily, 0, &mGraphicsQueue);
    vkGetDeviceQueue(mDevice, mPresentQueueFamily,
                     queuePlan.PresentQueueIndex, &mPresentQueue);
    mAsyncPresentSupported =
        queuePlan.AsynchronousPresent &&
        mPresentQueue != mGraphicsQueue;
    mDiagnostics.SetVulkanAdapter({
        mVulkanAdapterCount, mVulkanAdapterIndex,
        mVulkanAdapterVendorId, mVulkanAdapterDeviceId,
        mGraphicsQueueFamily, mPresentQueueFamily,
        graphicsQueueCount,
        queuePlan.RequestedGraphicsQueueCount,
        queuePlan.PresentQueueIndex,
        mAsyncPresentSupported,
        mNriSwapchainQueueEligible,
    });
}

void GfxRenderingAPIVulkan::CreatePipelineCache() {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
    const VulkanPipelineCacheHeader expected =
        MakePipelineCacheHeader(properties);
    const std::filesystem::path path = VulkanPipelineCachePath();
    std::vector<uint8_t> cachedData;
    bool loaded = LoadPipelineCacheData(path, expected, cachedData);

    VkPipelineCacheCreateInfo createInfo{
        VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
    };
    createInfo.initialDataSize = cachedData.size();
    createInfo.pInitialData = cachedData.empty() ? nullptr : cachedData.data();
    VkResult result = vkCreatePipelineCache(mDevice, &createInfo, nullptr,
                                            &mPipelineCache);
    if (result != VK_SUCCESS && !cachedData.empty()) {
        SPDLOG_WARN(
            "OOT3D Vulkan rejected the persisted pipeline cache (VkResult {}); "
            "rebuilding it",
            static_cast<int>(result));
        createInfo.initialDataSize = 0;
        createInfo.pInitialData = nullptr;
        cachedData.clear();
        loaded = false;
        mPipelineCache = VK_NULL_HANDLE;
        result = vkCreatePipelineCache(mDevice, &createInfo, nullptr,
                                       &mPipelineCache);
    }
    CheckVk(result, "vkCreatePipelineCache");
    SPDLOG_INFO("OOT3D Vulkan pipeline cache: {}{}",
                loaded ? "loaded " : "new",
                loaded ? "(" + std::to_string(cachedData.size()) +
                             " bytes)"
                       : "");
}

void GfxRenderingAPIVulkan::StorePipelineCache() {
    const auto statistics = mNriPicaPipelineBridge.PipelineStatistics();
    mDiagnostics.SetNriPipelineStatistics(statistics.InitialCacheBytes,
        statistics.CreationAttempts, statistics.Created, statistics.CreationNanoseconds);
    mDiagnostics.Flush();
    SPDLOG_INFO("NRI PICA pipeline statistics: {}", nlohmann::json({
        {"initial_cache_bytes", statistics.InitialCacheBytes},
        {"creation_attempts", statistics.CreationAttempts}, {"created", statistics.Created},
        {"creation_nanoseconds", statistics.CreationNanoseconds}}).dump());
    const auto nriData = mNriPicaPipelineBridge.GetPipelineCacheData();
    if (!nriData.empty()) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
        std::string error;
        if (StorePipelineCacheData(VulkanPipelineCachePath(true),
                                  MakePipelineCacheHeader(properties), nriData, &error))
            SPDLOG_INFO("NRI PICA pipeline cache: stored {} bytes", nriData.size());
        else
            SPDLOG_WARN("NRI PICA pipeline cache: {}", error);
    }
    if (mPipelineCache == VK_NULL_HANDLE) {
        return;
    }

    size_t dataSize = 0;
    VkResult result =
        vkGetPipelineCacheData(mDevice, mPipelineCache, &dataSize, nullptr);
    if (result != VK_SUCCESS || dataSize == 0 ||
        dataSize > kMaximumPipelineCacheBytes) {
        SPDLOG_WARN(
            "OOT3D Vulkan could not size the pipeline cache (VkResult {}, {} "
            "bytes)",
            static_cast<int>(result), dataSize);
        return;
    }

    std::vector<uint8_t> data(dataSize);
    result = vkGetPipelineCacheData(mDevice, mPipelineCache, &dataSize,
                                    data.data());
    if (result != VK_SUCCESS || dataSize == 0 || dataSize > data.size()) {
        SPDLOG_WARN(
            "OOT3D Vulkan could not read the pipeline cache (VkResult {}, {} "
            "bytes)",
            static_cast<int>(result), dataSize);
        return;
    }
    data.resize(dataSize);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
    const std::filesystem::path path = VulkanPipelineCachePath();
    std::string error;
    if (StorePipelineCacheData(path, MakePipelineCacheHeader(properties), data, &error))
        SPDLOG_INFO("OOT3D Vulkan pipeline cache: stored {} bytes", data.size());
    else
        SPDLOG_WARN("OOT3D Vulkan pipeline cache: {}", error);
}

void GfxRenderingAPIVulkan::StartPresentWorker() {
    if (!Oot3d::ShouldUseVulkanPresentWorker(
            mNriSwapchain.Active(), mAsyncPresentSupported) ||
        mPresentThread.joinable()) {
        return;
    }
    mPresentWorkerStop = false;
    mPresentThread = std::thread(&GfxRenderingAPIVulkan::PresentWorkerMain, this);
}

void GfxRenderingAPIVulkan::StopPresentWorker() {
    if (!mPresentThread.joinable()) {
        return;
    }
    {
        std::lock_guard lock(mPresentMutex);
        mPresentWorkerStop = true;
    }
    mPresentRequestCondition.notify_all();
    mPresentThread.join();
}

void GfxRenderingAPIVulkan::WaitForFramePresent(uint32_t frameIndex) {
    if (!Oot3d::ShouldUseVulkanPresentWorker(
            mNriSwapchain.Active(), mAsyncPresentSupported) ||
        frameIndex >= kFramesInFlight) {
        return;
    }
    std::unique_lock lock(mPresentMutex);
    const uint64_t serial = mFramePresentSerials[frameIndex];
    mPresentCompleteCondition.wait(lock, [&] {
        return mCompletedPresentSerials[frameIndex] >= serial;
    });
}

void GfxRenderingAPIVulkan::WaitForAllPresents() {
    if (!Oot3d::ShouldUseVulkanPresentWorker(
            mNriSwapchain.Active(), mAsyncPresentSupported) ||
        !mPresentThread.joinable()) {
        return;
    }
    std::unique_lock lock(mPresentMutex);
    mPresentCompleteCondition.wait(lock, [&] {
        for (uint32_t frameIndex = 0; frameIndex < kFramesInFlight; ++frameIndex) {
            if (mCompletedPresentSerials[frameIndex] < mFramePresentSerials[frameIndex]) {
                return false;
            }
        }
        return mPresentRequests.empty();
    });
}

void GfxRenderingAPIVulkan::PresentWorkerMain() {
    for (;;) {
        PresentRequest request;
        {
            std::unique_lock lock(mPresentMutex);
            mPresentRequestCondition.wait(lock, [&] {
                return mPresentWorkerStop || !mPresentRequests.empty();
            });
            if (mPresentWorkerStop && mPresentRequests.empty()) {
                return;
            }
            request = mPresentRequests.front();
            mPresentRequests.pop_front();
        }

        VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &request.WaitSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &request.Swapchain;
        presentInfo.pImageIndices = &request.ImageIndex;
        VkResult present;
        {
            std::lock_guard swapchainLock(mSwapchainCallMutex);
            present = vkQueuePresentKHR(mPresentQueue, &presentInfo);
        }
        if (present == VK_ERROR_OUT_OF_DATE_KHR) {
            mPresentSwapchainDirty.store(true);
        } else if (present == VK_SUBOPTIMAL_KHR) {
            mSwapchainSuboptimal.store(true);
        } else if (present != VK_SUCCESS) {
            mPresentError.store(static_cast<int32_t>(present));
        }

        {
            std::lock_guard lock(mPresentMutex);
            mCompletedPresentSerials[request.FrameIndex] = request.Serial;
        }
        mPresentCompleteCondition.notify_all();
    }
}

void GfxRenderingAPIVulkan::CreateCommandResources() {
    VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = mGraphicsQueueFamily;
    CheckVk(vkCreateCommandPool(mDevice, &poolInfo, nullptr, &mCommandPool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocateInfo.commandPool = mCommandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = kFramesInFlight;
    CheckVk(vkAllocateCommandBuffers(mDevice, &allocateInfo, mCommandBuffers.data()),
            "vkAllocateCommandBuffers");
    CheckVk(vkAllocateCommandBuffers(
                mDevice, &allocateInfo,
                mContinuationCommandBuffers.data()),
            "vkAllocateCommandBuffers(continuation)");
}

VkCommandBuffer GfxRenderingAPIVulkan::SplitFrameForExternalCompute(
    VkSemaphore timelineSemaphore, uint64_t vulkanSignalValue,
    uint64_t externalCompletionValue) {
    if (!mFrameActive || mFrameExternalComputeSplit ||
        mRenderPassActive || mNativePicaRenderPassActive ||
        timelineSemaphore == VK_NULL_HANDLE ||
        vulkanSignalValue == 0U ||
        externalCompletionValue <= vulkanSignalValue) {
        return VK_NULL_HANDLE;
    }

    VkCommandBuffer prefix = mCommandBuffers[mCurrentFrame];
    CheckVk(vkEndCommandBuffer(prefix),
            "vkEndCommandBuffer(external compute prefix)");
    VkTimelineSemaphoreSubmitInfo timeline{
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    timeline.signalSemaphoreValueCount = 1U;
    timeline.pSignalSemaphoreValues = &vulkanSignalValue;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.pNext = &timeline;
    submit.commandBufferCount = 1U;
    submit.pCommandBuffers = &prefix;
    submit.signalSemaphoreCount = 1U;
    submit.pSignalSemaphores = &timelineSemaphore;
    CheckVk(vkQueueSubmit(mGraphicsQueue, 1U, &submit, VK_NULL_HANDLE),
            "vkQueueSubmit(external compute prefix)");

    std::swap(mCommandBuffers[mCurrentFrame],
              mContinuationCommandBuffers[mCurrentFrame]);
    VkCommandBuffer continuation = mCommandBuffers[mCurrentFrame];
    CheckVk(vkResetCommandBuffer(continuation, 0U),
            "vkResetCommandBuffer(external compute continuation)");
    VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(continuation, &begin),
            "vkBeginCommandBuffer(external compute continuation)");
    if (mNriInterop.Available() &&
        !mNriInterop.WrapFrameCommandBuffer(
            mCurrentFrame, continuation)) {
        SPDLOG_WARN(
            "OOT3D Vulkan NRI could not wrap external-compute continuation {}",
            mCurrentFrame);
    }
    mFrameExternalComputeSplit = true;
    mFrameExternalWaitSemaphore = timelineSemaphore;
    mFrameExternalWaitValue = externalCompletionValue;
    return continuation;
}

void GfxRenderingAPIVulkan::CreateSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t index = 0; index < kFramesInFlight; ++index) {
        if (!mNriSwapchain.Supported()) {
            CheckVk(vkCreateSemaphore(
                        mDevice, &semaphoreInfo, nullptr,
                        &mImageAvailableSemaphores[index]),
                    "vkCreateSemaphore(image available)");
        }
        CheckVk(vkCreateFence(mDevice, &fenceInfo, nullptr, &mInFlightFences[index]),
                "vkCreateFence");
    }
}

VulkanShaderProgram GfxRenderingAPIVulkan::BuildShaderProgram(uint64_t shaderId0,
                                                              uint64_t shaderId1) {
    CCFeatures features{};
    gfx_cc_get_features(shaderId0, shaderId1, &features);

    VulkanShaderProgram shader{};
    shader.ShaderId0 = shaderId0;
    shader.ShaderId1 = shaderId1;
    shader.NumInputs = static_cast<uint8_t>(features.numInputs);
    shader.UsedTextures[0] = features.usedTextures[0];
    shader.UsedTextures[1] = features.usedTextures[1];
    shader.UsesTexture2 = IsOot3dPicaTexture2Shader(features.shader_id);
    shader.HasAlpha = features.opt_alpha;
    shader.HasFog = features.opt_fog;
    shader.HasGrayscale = features.opt_grayscale;
    shader.NativeShaderId = features.shader_id;

    uint32_t floatOffset = 4;
    for (size_t textureIndex = 0; textureIndex < 2; ++textureIndex) {
        if (!features.usedTextures[textureIndex]) {
            continue;
        }
        shader.TextureCoordinateOffsets[textureIndex] = floatOffset * sizeof(float);
        floatOffset += 2;
        for (size_t axis = 0; axis < 2; ++axis) {
            if (features.clamp[textureIndex][axis]) {
                ++floatOffset;
            }
        }
    }
    if (shader.UsesTexture2) {
        shader.TextureCoordinateOffsets[2] = floatOffset * sizeof(float);
        floatOffset += 2;
    }
    if (features.opt_fog) {
        shader.FogOffset = floatOffset * sizeof(float);
        floatOffset += 4;
    }
    if (features.opt_grayscale) {
        shader.GrayscaleOffset = floatOffset * sizeof(float);
        floatOffset += 4;
    }
    const uint32_t inputComponentCount = features.opt_alpha ? 4 : 3;
    for (int inputIndex = 0; inputIndex < features.numInputs; ++inputIndex) {
        if (static_cast<size_t>(inputIndex) < shader.InputOffsets.size()) {
            shader.InputOffsets[static_cast<size_t>(inputIndex)] = floatOffset * sizeof(float);
        }
        floatOffset += inputComponentCount;
    }
    if (IsOot3dShadow2dShader(features.shader_id)) {
        shader.ShadowCoordinateOffset = floatOffset * sizeof(float);
        floatOffset += 3;
    }
    shader.VertexStride = floatOffset * sizeof(float);
    return shader;
}

std::string GfxRenderingAPIVulkan::BuildVertexShaderSource(
    const VulkanShaderProgram& shader) const {
    std::ostringstream source;
    source << "#version 450\n"
              "layout(location = 0) in vec4 aPosition;\n"
              "layout(push_constant) uniform TransformState {\n"
              "    mat4 modelViewProjection;\n"
              "} transformState;\n";
    if (shader.UsedTextures[0]) {
        source << "layout(location = 1) in vec2 aTexCoord0;\n"
                  "layout(location = 0) out vec2 vTexCoord0;\n";
    }
    if (shader.UsedTextures[1]) {
        source << "layout(location = 2) in vec2 aTexCoord1;\n"
                  "layout(location = 1) out vec2 vTexCoord1;\n";
    }
    if (shader.UsesTexture2) {
        source << "layout(location = 3) in vec2 aTexCoord2;\n"
                  "layout(location = 2) out vec2 vTexCoord2;\n";
    }
    if (shader.HasFog) {
        source << "layout(location = 4) in vec4 aFog;\n"
                  "layout(location = 3) out vec4 vFog;\n";
    }
    if (shader.HasGrayscale) {
        source << "layout(location = 5) in vec4 aGrayscale;\n"
                  "layout(location = 4) out vec4 vGrayscale;\n";
    }
    for (uint32_t input = 0; input < shader.NumInputs; ++input) {
        source << "layout(location = " << (6 + input) << ") in vec"
               << (shader.HasAlpha ? 4 : 3) << " aInput" << (input + 1) << ";\n"
               << "layout(location = " << (5 + input) << ") out vec"
               << (shader.HasAlpha ? 4 : 3) << " vInput" << (input + 1) << ";\n";
    }
    if (shader.ShadowCoordinateOffset != VulkanShaderProgram::kAbsentAttribute) {
        source << "layout(location = 14) in vec3 aShadowCoordinate;\n"
                  "layout(location = 13) out vec3 vShadowCoordinate;\n";
    }
    source << "void main() {\n"
              "    gl_Position = transformState.modelViewProjection * aPosition;\n";
    if (shader.UsedTextures[0]) {
        source << "    vTexCoord0 = aTexCoord0;\n";
    }
    if (shader.UsedTextures[1]) {
        source << "    vTexCoord1 = aTexCoord1;\n";
    }
    if (shader.UsesTexture2) {
        source << "    vTexCoord2 = aTexCoord2;\n";
    }
    if (shader.HasFog) {
        source << "    vFog = aFog;\n";
    }
    if (shader.HasGrayscale) {
        source << "    vGrayscale = aGrayscale;\n";
    }
    for (uint32_t input = 0; input < shader.NumInputs; ++input) {
        source << "    vInput" << (input + 1) << " = aInput" << (input + 1) << ";\n";
    }
    if (shader.ShadowCoordinateOffset != VulkanShaderProgram::kAbsentAttribute) {
        source << "    vShadowCoordinate = aShadowCoordinate;\n";
    }
    source << "}\n";
    return source.str();
}

std::string GfxRenderingAPIVulkan::BuildFragmentShaderSource(uint64_t shaderId0,
                                                             uint64_t shaderId1) const {
    CCFeatures features{};
    gfx_cc_get_features(shaderId0, shaderId1, &features);
    const bool picaTextureEnv = IsPicaTextureEnvShader(features.shader_id);
    const bool picaPostMultiply =
        IsPicaTextureEnvPostMultiplyShader(features.shader_id);
    const bool picaTexture2 = IsOot3dPicaTexture2Shader(features.shader_id);
    const bool picaShadow2d = IsOot3dShadow2dShader(features.shader_id);
    const bool picaFog = features.opt_fog && IsOot3dPicaFogShader(features.shader_id);
    const bool picaAlphaTest =
        features.opt_alpha_threshold && IsOot3dPicaAlphaTestShader(features.shader_id);

    std::ostringstream source;
    source << "#version 450\n";
    if (features.usedTextures[0]) {
        source << "layout(set = 0, binding = 0) uniform sampler2D uTex0;\n"
                  "layout(location = 0) in vec2 vTexCoord0;\n";
    }
    if (features.usedTextures[1]) {
        source << "layout(set = 0, binding = 1) uniform sampler2D uTex1;\n"
                  "layout(location = 1) in vec2 vTexCoord1;\n";
    }
    if (picaTexture2) {
        source << "layout(set = 0, binding = 2) uniform sampler2D uTex2;\n"
                  "layout(location = 2) in vec2 vTexCoord2;\n";
    }
    if (picaShadow2d) {
        source << "layout(set = 0, binding = 3) uniform usampler2D uOot3dShadow2d;\n";
    }
    if (features.opt_fog) {
        source << "layout(location = 3) in vec4 vFog;\n";
    }
    if (features.opt_grayscale) {
        source << "layout(location = 4) in vec4 vGrayscale;\n";
    }
    for (int input = 0; input < features.numInputs; ++input) {
        source << "layout(location = " << (5 + input) << ") in vec"
               << (features.opt_alpha ? 4 : 3) << " vInput" << (input + 1) << ";\n";
    }
    if (IsOot3dShadow2dShader(features.shader_id)) {
        source << "layout(location = 13) in vec3 vShadowCoordinate;\n";
    }
    source << R"glsl(
layout(std430, set = 0, binding = 4) readonly buffer DrawUniformState {
    ivec4 textureWidth;
    ivec4 textureHeight;
    ivec4 textureFiltering;
    uint frameCount;
    float noiseScale;
    float primDepth;
    int fogFlip;
    int alphaTestEnabled;
    int alphaTestFunction;
    int alphaTestReference;
    int shadowTextureBias;
    int shadowOrthographic;
    int shadowInvert;
    int reserved;
    uint fogLut[128];
} draw;
layout(location = 0) out vec4 outColor;

float randomValue(vec3 value) {
    float randomSeed = dot(sin(value), vec3(12.9898, 78.233, 37.719));
    return fract(sin(randomSeed) * 143758.5453);
}

vec3 picaByteRound(vec3 value) {
    return round(value * 255.0) * (1.0 / 255.0);
}

vec4 picaByteRound(vec4 value) {
    return round(value * 255.0) * (1.0 / 255.0);
}

vec4 fromLinear(vec4 linearRgb) {
    bvec3 cutoff = lessThan(linearRgb.rgb, vec3(0.0031308));
    vec3 higher = vec3(1.055) * pow(linearRgb.rgb, vec3(1.0 / 2.4)) - vec3(0.055);
    vec3 lower = linearRgb.rgb * vec3(12.92);
    return vec4(mix(higher, lower, cutoff), linearRgb.a);
}

float decodePicaFogValue(uint word) {
    return float((word >> 13u) & 0x7FFu) * (1.0 / 2048.0);
}

float decodePicaFogDiff(uint word) {
    uint raw = word & 0x1FFFu;
    uint encoded = raw < 4096u ? raw + 4096u : raw - 4096u;
    return float(encoded) * (1.0 / 2048.0) - 2.0;
}

float samplePicaFogFactor() {
    float depth = clamp(gl_FragCoord.z, 0.0, 1.0);
    float lutIndex = (draw.fogFlip != 0 ? 1.0 - depth : depth) * 128.0;
    float lutFloor = clamp(floor(lutIndex), 0.0, 127.0);
    uint word = draw.fogLut[int(lutFloor)];
    return clamp(decodePicaFogValue(word) +
                 decodePicaFogDiff(word) * (lutIndex - lutFloor), 0.0, 1.0);
}
)glsl";
    if (picaShadow2d) {
        source << R"glsl(

float compareOot3dShadow2d(uint pixel, uint z) {
    uint depth24 = pixel >> 8u;
    uint alpha8 = pixel & 0xFFu;
    return depth24 <= z ? 0.0 : float(alpha8) * (1.0 / 255.0);
}

float sampleOot3dShadow2dTap(ivec2 uv, uint z) {
    ivec2 size = textureSize(uOot3dShadow2d, 0);
    if (any(lessThan(uv, ivec2(0))) || any(greaterThanEqual(uv, size))) {
        return 1.0;
    }
    return compareOot3dShadow2d(texelFetch(uOot3dShadow2d, uv, 0).x, z);
}

float mixOot3dShadow2d(vec4 samples, vec2 factor) {
    vec2 vertical = mix(samples.xy, samples.zw, factor.yy);
    return mix(vertical.x, vertical.y, factor.x);
}

vec3 sampleOot3dShadow2d(vec2 uv, float w) {
    if (draw.shadowOrthographic == 0) {
        uv /= w;
    }
    uint z = uint(max(0, int(min(abs(w), 1.0) * 16777215.0) -
                          draw.shadowTextureBias));
    ivec2 size = textureSize(uOot3dShadow2d, 0);
    vec2 coordinate = vec2(size) * uv - vec2(0.5);
    vec2 coordinateFloor = floor(coordinate);
    vec2 factor = coordinate - coordinateFloor;
    ivec2 base = ivec2(coordinateFloor);
    vec4 samples = vec4(
        sampleOot3dShadow2dTap(base, z),
        sampleOot3dShadow2dTap(base + ivec2(1, 0), z),
        sampleOot3dShadow2dTap(base + ivec2(0, 1), z),
        sampleOot3dShadow2dTap(base + ivec2(1, 1), z));
    float value = mixOot3dShadow2d(samples, factor);
    if (draw.shadowInvert != 0) {
        value = 1.0 - value;
    }
    return vec3(value);
}
)glsl";
    }
    source << R"glsl(

void main() {
    float drawRandom = (randomValue(vec3(floor(gl_FragCoord.xy * draw.noiseScale),
                                           float(draw.frameCount))) + 1.0) / 2.0;
)glsl";
    if (features.usedTextures[0]) {
        source << "    vec4 texVal0 = texture(uTex0, vTexCoord0);\n";
    }
    if (features.usedTextures[1]) {
        source << "    vec4 texVal1 = texture(uTex1, vTexCoord1);\n";
    }
    for (int input = 0; input < features.numInputs; ++input) {
        if (input == 0 && picaShadow2d) {
            if (features.opt_alpha) {
                source << "    vec4 shadowedInput1 = vec4(vInput1.rgb * "
                          "sampleOot3dShadow2d(vShadowCoordinate.xy, vShadowCoordinate.z), "
                          "vInput1.a);\n";
            } else {
                source << "    vec3 shadowedInput1 = vInput1 * "
                          "sampleOot3dShadow2d(vShadowCoordinate.xy, vShadowCoordinate.z);\n";
            }
            source << "    vec" << (features.opt_alpha ? 4 : 3)
                   << " input1 = picaByteRound(shadowedInput1);\n";
        } else {
            source << "    vec" << (features.opt_alpha ? 4 : 3) << " input" << (input + 1)
                   << " = " << (picaTextureEnv ? "picaByteRound(" : "") << "vInput"
                   << (input + 1) << (picaTextureEnv ? ")" : "") << ";\n";
        }
    }
    source << "    " << (features.opt_alpha ? "vec4" : "vec3") << " texel;\n";
    const int cycleCount = features.opt_2cyc ? 2 : 1;
    for (int cycle = 0; cycle < cycleCount; ++cycle) {
        if (cycle == 1) {
            if (features.opt_alpha) {
                if (picaTextureEnv) {
                    source << "    texel.a = picaByteRound(vec4(clamp(texel.a, 0.0, 1.0))).a;\n";
                } else {
                    const bool combined = features.c[cycle][1][2] == SHADER_COMBINED;
                    source << "    texel.a = mod(texel.a - (" << (combined ? "-1.01" : "-0.51")
                           << "), " << (combined ? "2.02" : "2.02") << ") + ("
                           << (combined ? "-1.01" : "-0.51") << ");\n";
                }
            }
            if (picaTextureEnv) {
                source << "    texel.rgb = picaByteRound(clamp(texel.rgb, 0.0, 1.0));\n";
            } else {
                const bool combined = features.c[cycle][0][2] == SHADER_COMBINED;
                source << "    texel.rgb = mod(texel.rgb - vec3("
                       << (combined ? "-1.01" : "-0.51") << "), vec3(2.02)) + vec3("
                       << (combined ? "-1.01" : "-0.51") << ");\n";
            }
        }
        if (features.opt_alpha && !features.color_alpha_same[cycle]) {
            source << "    texel = vec4(" << BuildCombinerFormula(features, cycle, false, false)
                   << ", " << BuildCombinerFormula(features, cycle, true, true) << ");\n";
        } else {
            source << "    texel = "
                   << BuildCombinerFormula(features, cycle, false, features.opt_alpha) << ";\n";
        }
    }
    if (picaTexture2) {
        source << "    texel.rgb = clamp(texel.rgb, 0.0, 1.0);\n"
                  "    texel.rgb = clamp(texture(uTex2, vTexCoord2).rgb * texVal1.rgb + "
                  "texel.rgb, 0.0, 1.0);\n";
    }
    if (picaPostMultiply) {
        source << "    texel.rgb *= input1.rgb;\n";
    }
    if (picaTextureEnv) {
        source << "    texel = picaByteRound(clamp(texel, 0.0, 1.0));\n";
    } else {
        source << "    texel = clamp(mod(texel - "
               << (features.opt_alpha ? "vec4(-0.51)" : "vec3(-0.51)") << ", "
               << (features.opt_alpha ? "vec4(2.02)" : "vec3(2.02)") << ") + "
               << (features.opt_alpha ? "vec4(-0.51)" : "vec3(-0.51)")
               << ", 0.0, 1.0);\n";
    }
    if (features.opt_fog) {
        if (picaFog) {
            source << "    float fogFactor = samplePicaFogFactor();\n";
            if (features.opt_alpha) {
                source << "    texel = vec4(mix(vFog.rgb, texel.rgb, fogFactor), texel.a);\n";
            } else {
                source << "    texel = mix(vFog.rgb, texel, fogFactor);\n";
            }
        } else if (features.opt_alpha) {
            source << "    texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);\n";
        } else {
            source << "    texel = mix(texel, vFog.rgb, vFog.a);\n";
        }
    }
    if (features.opt_texture_edge && features.opt_alpha) {
        source << "    if (texel.a > 0.19) texel.a = 1.0; else discard;\n";
    }
    if (features.opt_alpha && features.opt_noise) {
        source << "    texel.a *= floor(clamp(randomValue(vec3(floor(gl_FragCoord.xy * "
                  "draw.noiseScale), float(draw.frameCount))) + texel.a, 0.0, 1.0));\n";
    }
    if (features.opt_grayscale) {
        source << "    float intensity = (texel.r + texel.g + texel.b) / 3.0;\n"
                  "    texel.rgb = mix(texel.rgb, vGrayscale.rgb * intensity, vGrayscale.a);\n";
    }
    if (features.opt_alpha && features.opt_alpha_threshold) {
        if (picaAlphaTest) {
            source << R"glsl(
    if (draw.alphaTestEnabled != 0) {
        int alphaU8 = int(clamp(texel.a, 0.0, 1.0) * 255.0);
        bool alphaPass = false;
        if (draw.alphaTestFunction == 1) alphaPass = true;
        else if (draw.alphaTestFunction == 2) alphaPass = alphaU8 == draw.alphaTestReference;
        else if (draw.alphaTestFunction == 3) alphaPass = alphaU8 != draw.alphaTestReference;
        else if (draw.alphaTestFunction == 4) alphaPass = alphaU8 < draw.alphaTestReference;
        else if (draw.alphaTestFunction == 5) alphaPass = alphaU8 <= draw.alphaTestReference;
        else if (draw.alphaTestFunction == 6) alphaPass = alphaU8 > draw.alphaTestReference;
        else if (draw.alphaTestFunction == 7) alphaPass = alphaU8 >= draw.alphaTestReference;
        if (!alphaPass) discard;
    } else if (texel.a < 8.0 / 256.0) {
        discard;
    }
)glsl";
        } else {
            source << "    if (texel.a < 8.0 / 256.0) discard;\n";
        }
    }
    if (features.opt_alpha && features.opt_invisible) {
        source << "    texel.a = 0.0;\n";
    }
    source << "    outColor = " << (features.opt_alpha ? "texel" : "vec4(texel, 1.0)")
           << ";\n";
    if (mSrgbMode) {
        source << "    outColor = fromLinear(outColor);\n";
    }
    if (features.opt_prim_depth) {
        source << "    gl_FragDepth = draw.primDepth;\n";
    }
    source << "}\n";
    return source.str();
}

std::vector<uint32_t> GfxRenderingAPIVulkan::CompileShaderSpirv(
    const std::string& source, bool vertexShader, const char* sourceName) {
    const auto stage = vertexShader ? Renderer::SpirvStage::Vertex : Renderer::SpirvStage::Fragment;
    const auto failures = mCompiledShaderCache.Stats().WriteFailures;
    auto spirv = mCompiledShaderCache.Resolve(source, stage, [&] {
        return Renderer::CompileShadercSpirv(source, stage, sourceName);
    });
    if (failures == 0 && mCompiledShaderCache.Stats().WriteFailures != 0)
        SPDLOG_WARN("Shader cache persistence unavailable; rendering continues: {}",
                    mCompiledShaderCache.LastWriteError());
    return spirv;
}

void GfxRenderingAPIVulkan::ConfigureNativePicaAotShaders() {
    mCompiledShaderCache.Configure(VulkanShaderCacheDirectory(), Renderer::ShadercCompilerContract());
    mNriInterop.Shaders().Configure(VulkanShaderCacheDirectory());
    mCompiledShaderCacheSummaryLogged = false;
    if (!mCompiledShaderCache.Enabled())
        SPDLOG_WARN("SPIR-V disk reuse disabled: cache directory or compiler identity unavailable");
    mPicaAotShaderPack.Clear();
    mPicaEffectiveShaderInventory.Clear();
    mPicaPipelineInventory.Clear();
    mPicaPipelineManifest.Clear();
    mPicaPipelinePrewarmedProfiles.clear();
    mPicaAotShaderMissesLogged.clear();
    mPicaAotShaderHits = 0;
    mPicaAotShaderMisses = 0;
    mPicaAotShaderSummaryLogged = false;
    mPicaPipelinePrewarmSummaryLogged = false;
    mPicaPipelinePrewarmCreated = 0U;
    mPicaPipelinePrewarmReused = 0U;
    mPicaPipelinePrewarmSkipped = 0U;
    const char* strict = std::getenv("OOT3D_PICA_AOT_SHADER_STRICT");
    mPicaAotShaderStrict =
        strict != nullptr && std::string_view(strict) == "1";
    const char* prewarm = std::getenv("OOT3D_PICA_PIPELINE_PREWARM");
    mPicaPipelinePrewarmEnabled =
        prewarm != nullptr && std::string_view(prewarm) == "1";

    if (const char* path = std::getenv("OOT3D_PICA_AOT_SHADER_PACK");
        path != nullptr && path[0] != '\0') {
        std::string error;
        if (!mPicaAotShaderPack.Load(path, &error)) {
            throw std::runtime_error(
                "could not load native PICA AOT shader pack: " + error);
        }
        SPDLOG_INFO(
            "Native PICA AOT shader pack loaded: {} entries, schema {}, {}",
            mPicaAotShaderPack.EntryCount(),
            mPicaAotShaderPack.DescriptorSchemaVersion(),
            mPicaAotShaderPack.Path().string());
    } else if (mPicaAotShaderStrict) {
        throw std::runtime_error(
            "OOT3D_PICA_AOT_SHADER_STRICT requires "
            "OOT3D_PICA_AOT_SHADER_PACK");
    }

    if (const char* path =
            std::getenv("OOT3D_PICA_EFFECTIVE_SHADER_INVENTORY");
        path != nullptr && path[0] != '\0') {
        std::string error;
        if (!mPicaEffectiveShaderInventory.Configure(path, &error)) {
            throw std::runtime_error(
                "could not configure native PICA shader inventory: " +
                error);
        }
        SPDLOG_INFO("Native PICA effective shader inventory enabled: {}",
                    mPicaEffectiveShaderInventory.Path().string());
    }
    if (const char* path =
            std::getenv("OOT3D_PICA_PIPELINE_INVENTORY");
        path != nullptr && path[0] != '\0') {
        std::string error;
        if (!mPicaPipelineInventory.Configure(path, &error)) {
            throw std::runtime_error(
                "could not configure native PICA pipeline inventory: " +
                error);
        }
        SPDLOG_INFO("Native PICA pipeline inventory enabled: {}",
                    mPicaPipelineInventory.Path().string());
    }
    if (const char* path =
            std::getenv("OOT3D_PICA_PIPELINE_MANIFEST");
        path != nullptr && path[0] != '\0') {
        std::string error;
        if (!mPicaPipelineManifest.Load(path, &error)) {
            throw std::runtime_error(
                "could not load native PICA pipeline manifest: " + error);
        }
        if (mPicaAotShaderPack.Loaded() &&
            mPicaPipelineManifest.DescriptorSchemaVersion() !=
                mPicaAotShaderPack.DescriptorSchemaVersion()) {
            throw std::runtime_error(
                "native PICA pipeline manifest and AOT shader pack use "
                "different descriptor schemas");
        }
        SPDLOG_INFO(
            "Native PICA pipeline manifest loaded: {} entries, schema {}, {}",
            mPicaPipelineManifest.Entries().size(),
            mPicaPipelineManifest.DescriptorSchemaVersion(),
            mPicaPipelineManifest.Path().string());
    }
    if (mPicaPipelinePrewarmEnabled &&
        (!mPicaPipelineManifest.Loaded() ||
         !mPicaAotShaderPack.Loaded())) {
        throw std::runtime_error(
            "OOT3D_PICA_PIPELINE_PREWARM requires both a pipeline manifest "
            "and an AOT shader pack");
    }
}

void GfxRenderingAPIVulkan::FinishNativePicaAotShaders() {
    const auto& cache = mCompiledShaderCache.Stats();
    const auto passes = mNriInterop.Shaders().Stats();
    if ((cache.Requests || passes.Requests || mPicaAotShaderPack.Loaded()) && !mCompiledShaderCacheSummaryLogged) {
        std::fprintf(stderr,
            "TRIAEVUM_PASS_SHADER_CACHE requests=%llu hits=%llu compiled=%llu compile_failed=%llu "
            "writes=%llu write_failed=%llu compile_ms=%.3f enabled=%d\n",
            static_cast<unsigned long long>(passes.Requests), static_cast<unsigned long long>(passes.Hits),
            static_cast<unsigned long long>(passes.Compilations), static_cast<unsigned long long>(passes.CompilationFailures),
            static_cast<unsigned long long>(passes.Writes), static_cast<unsigned long long>(passes.WriteFailures),
            passes.CompileNanoseconds / 1e6, mNriInterop.Shaders().Enabled() ? 1 : 0);
        std::fprintf(stderr,
            "TRIAEVUM_SPIRV_CACHE requests=%llu hits=%llu misses=%llu rejected=%llu "
            "compiled=%llu compile_failed=%llu writes=%llu write_failed=%llu "
            "compile_ms=%.3f read_ms=%.3f write_ms=%.3f enabled=%d\n",
            static_cast<unsigned long long>(cache.Requests), static_cast<unsigned long long>(cache.Hits),
            static_cast<unsigned long long>(cache.Misses), static_cast<unsigned long long>(cache.Rejected),
            static_cast<unsigned long long>(cache.Compilations), static_cast<unsigned long long>(cache.CompilationFailures),
            static_cast<unsigned long long>(cache.Writes), static_cast<unsigned long long>(cache.WriteFailures),
            cache.CompileNanoseconds / 1e6, cache.ReadNanoseconds / 1e6, cache.WriteNanoseconds / 1e6,
            mCompiledShaderCache.Enabled() ? 1 : 0);
        mCompiledShaderCacheSummaryLogged = true;
    }
    if (mPicaEffectiveShaderInventory.Enabled()) {
        std::string error;
        if (!mPicaEffectiveShaderInventory.Finish(&error)) {
            SPDLOG_WARN("Native PICA shader inventory was not written: {}",
                        error);
        } else {
            SPDLOG_INFO(
                "Native PICA effective shader inventory wrote {} modules to {}",
                mPicaEffectiveShaderInventory.EntryCount(),
                mPicaEffectiveShaderInventory.Path().string());
            std::fprintf(
                stderr,
                "OOT3D_PICA_EFFECTIVE_SHADER_INVENTORY modules=%zu path=%s\n",
                mPicaEffectiveShaderInventory.EntryCount(),
                mPicaEffectiveShaderInventory.Path().string().c_str());
        }
    }
    if (mPicaPipelineInventory.Enabled()) {
        std::string error;
        if (!mPicaPipelineInventory.Finish(&error)) {
            SPDLOG_WARN("Native PICA pipeline inventory was not written: {}",
                        error);
        } else {
            SPDLOG_INFO(
                "Native PICA pipeline inventory wrote {} pipelines to {}",
                mPicaPipelineInventory.EntryCount(),
                mPicaPipelineInventory.Path().string());
            std::fprintf(
                stderr,
                "OOT3D_PICA_PIPELINE_INVENTORY pipelines=%zu path=%s\n",
                mPicaPipelineInventory.EntryCount(),
                mPicaPipelineInventory.Path().string().c_str());
        }
    }
    if (mPicaPipelinePrewarmEnabled &&
        !mPicaPipelinePrewarmSummaryLogged) {
        SPDLOG_INFO(
            "Native PICA pipeline prewarm: {} created, {} reused, {} skipped",
            mPicaPipelinePrewarmCreated, mPicaPipelinePrewarmReused,
            mPicaPipelinePrewarmSkipped);
        std::fprintf(
            stderr,
            "OOT3D_PICA_PIPELINE_PREWARM created=%llu reused=%llu "
            "skipped=%llu profiles=%zu\n",
            static_cast<unsigned long long>(mPicaPipelinePrewarmCreated),
            static_cast<unsigned long long>(mPicaPipelinePrewarmReused),
            static_cast<unsigned long long>(mPicaPipelinePrewarmSkipped),
            mPicaPipelinePrewarmedProfiles.size());
        mPicaPipelinePrewarmSummaryLogged = true;
    }
    if (!mPicaAotShaderSummaryLogged &&
        (mPicaAotShaderPack.Loaded() || mPicaAotShaderHits != 0U ||
         mPicaAotShaderMisses != 0U)) {
        SPDLOG_INFO("Native PICA AOT shader resolution: {} hits, {} misses",
                    mPicaAotShaderHits, mPicaAotShaderMisses);
        std::fprintf(stderr,
                     "OOT3D_PICA_AOT_SHADER_RESOLUTION hits=%llu misses=%llu "
                     "strict=%u entries=%zu\n",
                     static_cast<unsigned long long>(mPicaAotShaderHits),
                     static_cast<unsigned long long>(mPicaAotShaderMisses),
                     mPicaAotShaderStrict ? 1U : 0U,
                     mPicaAotShaderPack.EntryCount());
        mPicaAotShaderSummaryLogged = true;
    }
}

std::vector<uint32_t>
GfxRenderingAPIVulkan::ResolveNativePicaShaderSpirv(
    std::string_view source, Oot3d::PicaAotShaderStage stage,
    bool vertexShader, const char* sourceName) {
    if (!mPicaAotShaderPack.Loaded()) {
        return CompileShaderSpirv(std::string(source), vertexShader,
                                  sourceName);
    }
    const auto binary = mPicaAotShaderPack.Find(stage, source);
    if (!binary.empty()) {
        ++mPicaAotShaderHits;
        return {binary.begin(), binary.end()};
    }

    ++mPicaAotShaderMisses;
    const auto identity = Oot3d::IdentifyPicaAotShaderSource(source);
    if (mPicaAotShaderMissesLogged.insert({stage, identity.Id}).second) {
        SPDLOG_WARN(
            "Native PICA AOT shader miss: stage={}, source={}, name={}",
            Oot3d::PicaAotShaderStageName(stage),
            Oot3d::FormatPicaAotShaderId(identity.Id), sourceName);
    }
    if (mPicaAotShaderStrict) {
        throw std::runtime_error(
            "native PICA AOT shader pack has no " +
            std::string(Oot3d::PicaAotShaderStageName(stage)) +
            " module for " + Oot3d::FormatPicaAotShaderId(identity.Id));
    }
    return CompileShaderSpirv(std::string(source), vertexShader,
                              sourceName);
}

VkShaderModule GfxRenderingAPIVulkan::CreateShaderModuleFromSpirv(
    std::span<const uint32_t> spirv) {
    if (spirv.empty() || spirv.front() != kSpirvMagic) {
        throw std::runtime_error("native PICA SPIR-V module is invalid");
    }
    VkShaderModuleCreateInfo moduleInfo{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleInfo.codeSize = spirv.size_bytes();
    moduleInfo.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    CheckVk(vkCreateShaderModule(mDevice, &moduleInfo, nullptr, &module),
            "vkCreateShaderModule");
    return module;
}

VkShaderModule GfxRenderingAPIVulkan::CompileShaderModule(
    const std::string& source, bool vertexShader, const char* sourceName,
    std::vector<uint32_t>* spirvOutput) {
    std::vector<uint32_t> spirv =
        CompileShaderSpirv(source, vertexShader, sourceName);
    const VkShaderModule module = CreateShaderModuleFromSpirv(spirv);
    if (spirvOutput != nullptr)
        *spirvOutput = std::move(spirv);
    return module;
}

void GfxRenderingAPIVulkan::CreateShaderResources() {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    for (uint32_t binding = 0; binding < 4; ++binding) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[binding].descriptorCount = 1;
        bindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    descriptorLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    descriptorLayoutInfo.pBindings = bindings.data();
    CheckVk(vkCreateDescriptorSetLayout(mDevice, &descriptorLayoutInfo, nullptr,
                                        &mTextureDescriptorSetLayout),
            "vkCreateDescriptorSetLayout");

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    VkPushConstantRange transformPushRange{};
    transformPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    transformPushRange.size = sizeof(TransformPushConstants);
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mTextureDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &transformPushRange;
    CheckVk(vkCreatePipelineLayout(mDevice, &pipelineLayoutInfo, nullptr, &mPipelineLayout),
            "vkCreatePipelineLayout");
    CreateNativePicaShaderResources();
}

void GfxRenderingAPIVulkan::CreateFrameResources() {
    constexpr VkDeviceSize kVertexArenaSize = 64ull * 1024ull * 1024ull;
    constexpr VkDeviceSize kUniformArenaSize = 4ull * 1024ull * 1024ull;
    for (auto& frame : mFrameResources) {
        frame.VertexBuffer =
            CreateBuffer(kVertexArenaSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         true);
        frame.UniformBuffer =
            CreateBuffer(kUniformArenaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         true);
        const std::array<VkDescriptorPoolSize, 4> poolSizes = {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 * 5 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 4096 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096 * 4 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4096 },
        };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets = 4096;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        CheckVk(vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &frame.DescriptorPool),
                "vkCreateDescriptorPool");
    }
}

void GfxRenderingAPIVulkan::CreateFallbackTexture() {
    constexpr std::array<uint8_t, 4> white = { 255, 255, 255, 255 };
    CreateTextureImage(mFallbackTexture, white.data(), 1, 1);
}

void GfxRenderingAPIVulkan::CreateOot3dShadow2dRenderPass() {
    if (mOot3dShadow2dRenderPass != VK_NULL_HANDLE) {
        return;
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R32_UINT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = mDepthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthReference{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    const std::array<VkAttachmentDescription, 2> attachments = {
        colorAttachment, depthAttachment
    };
    VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    CheckVk(vkCreateRenderPass(mDevice, &renderPassInfo, nullptr,
                               &mOot3dShadow2dRenderPass),
            "vkCreateRenderPass(OOT3D Shadow2D)");
}

void GfxRenderingAPIVulkan::DestroyOffscreenFramebuffer(
    OffscreenFramebufferRecord& framebuffer) {
    if (mDevice == VK_NULL_HANDLE) {
        framebuffer = {};
        return;
    }
    if (framebuffer.Framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(mDevice, framebuffer.Framebuffer, nullptr);
    }
    if (framebuffer.ColorSampler != VK_NULL_HANDLE) {
        vkDestroySampler(mDevice, framebuffer.ColorSampler, nullptr);
    }
    if (framebuffer.ColorView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, framebuffer.ColorView, nullptr);
    }
    if (framebuffer.ColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(mDevice, framebuffer.ColorImage, nullptr);
    }
    if (framebuffer.ColorMemory != VK_NULL_HANDLE) {
        vkFreeMemory(mDevice, framebuffer.ColorMemory, nullptr);
    }
    if (framebuffer.DepthView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, framebuffer.DepthView, nullptr);
    }
    if (framebuffer.DepthImage != VK_NULL_HANDLE) {
        vkDestroyImage(mDevice, framebuffer.DepthImage, nullptr);
    }
    if (framebuffer.DepthMemory != VK_NULL_HANDLE) {
        vkFreeMemory(mDevice, framebuffer.DepthMemory, nullptr);
    }
    framebuffer = {};
}

bool GfxRenderingAPIVulkan::CreateOot3dShadow2dFramebuffer(
    OffscreenFramebufferRecord& framebuffer, uint32_t width, uint32_t height,
    bool hasDepthBuffer) {
    if (!hasDepthBuffer || !SupportsOot3dShadow2dR32uiPipeline()) {
        return false;
    }
    CreateOot3dShadow2dRenderPass();

    const auto createImage = [&](VkFormat format, VkImageUsageFlags usage, VkImage& image,
                                 VkDeviceMemory& memory) {
        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVk(vkCreateImage(mDevice, &imageInfo, nullptr, &image),
                "vkCreateImage(OOT3D Shadow2D)");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(mDevice, image, &requirements);
        VkMemoryAllocateInfo allocationInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocationInfo.allocationSize = requirements.size;
        allocationInfo.memoryTypeIndex =
            FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckVk(vkAllocateMemory(mDevice, &allocationInfo, nullptr, &memory),
                "vkAllocateMemory(OOT3D Shadow2D)");
        CheckVk(vkBindImageMemory(mDevice, image, memory, 0),
                "vkBindImageMemory(OOT3D Shadow2D)");
    };

    createImage(VK_FORMAT_R32_UINT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                framebuffer.ColorImage, framebuffer.ColorMemory);
    VkImageViewCreateInfo colorViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    colorViewInfo.image = framebuffer.ColorImage;
    colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorViewInfo.format = VK_FORMAT_R32_UINT;
    colorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorViewInfo.subresourceRange.levelCount = 1;
    colorViewInfo.subresourceRange.layerCount = 1;
    CheckVk(vkCreateImageView(mDevice, &colorViewInfo, nullptr, &framebuffer.ColorView),
            "vkCreateImageView(OOT3D Shadow2D color)");

    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    CheckVk(vkCreateSampler(mDevice, &samplerInfo, nullptr, &framebuffer.ColorSampler),
            "vkCreateSampler(OOT3D Shadow2D)");

    TransitionImageLayout(framebuffer.ColorImage, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkCommandBuffer clearCommand = BeginImmediateCommands();
    VkClearColorValue initialClear{};
    initialClear.uint32[0] = UINT32_MAX;
    VkImageSubresourceRange colorRange{};
    colorRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorRange.levelCount = 1;
    colorRange.layerCount = 1;
    vkCmdClearColorImage(clearCommand, framebuffer.ColorImage,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &initialClear, 1,
                         &colorRange);
    EndImmediateCommands(clearCommand);
    TransitionImageLayout(framebuffer.ColorImage, 0, 1,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    createImage(mDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                framebuffer.DepthImage, framebuffer.DepthMemory);
    VkImageViewCreateInfo depthViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    depthViewInfo.image = framebuffer.DepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = mDepthFormat;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (HasStencil(mDepthFormat)) {
        depthViewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.layerCount = 1;
    CheckVk(vkCreateImageView(mDevice, &depthViewInfo, nullptr, &framebuffer.DepthView),
            "vkCreateImageView(OOT3D Shadow2D depth)");

    const std::array<VkImageView, 2> attachments = {
        framebuffer.ColorView, framebuffer.DepthView
    };
    VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    framebufferInfo.renderPass = mOot3dShadow2dRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;
    CheckVk(vkCreateFramebuffer(mDevice, &framebufferInfo, nullptr,
                                &framebuffer.Framebuffer),
            "vkCreateFramebuffer(OOT3D Shadow2D)");
    framebuffer.Width = width;
    framebuffer.Height = height;
    framebuffer.ColorFormat = GfxFramebufferColorFormat::R32ui;
    framebuffer.HasDepthBuffer = true;
    return true;
}

void GfxRenderingAPIVulkan::CreateOot3dShadow2dDepthEncodePipeline() {
    if (mOot3dShadow2dDepthEncodePipeline != VK_NULL_HANDLE) {
        return;
    }
    CreateOot3dShadow2dRenderPass();
    static constexpr const char* vertexSource = R"glsl(
#version 450
layout(location = 0) in vec4 aPosition;
void main() {
    gl_Position = aPosition;
}
)glsl";
    static constexpr const char* fragmentSource = R"glsl(
#version 450
layout(location = 0) out uint outShadow;
void main() {
    uint depth24 = uint(clamp(gl_FragCoord.z, 0.0, 1.0) * 16777215.0);
    outShadow = (depth24 << 8u) | 255u;
}
)glsl";

    VkShaderModule vertexShader =
        CompileShaderModule(vertexSource, true, "oot3d_shadow2d_depth_encode.vert");
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    try {
        fragmentShader =
            CompileShaderModule(fragmentSource, false, "oot3d_shadow2d_depth_encode.frag");
    } catch (...) {
        vkDestroyShaderModule(mDevice, vertexShader, nullptr);
        throw;
    }
    const VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
          VK_SHADER_STAGE_VERTEX_BIT, vertexShader, "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
          VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader, "main", nullptr },
    };
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 4 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
    };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
    };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_FALSE;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
    };
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;
    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
    };
    dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
    };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mPipelineLayout;
    pipelineInfo.renderPass = mOot3dShadow2dRenderPass;
    pipelineInfo.subpass = 0;
    const VkResult result = vkCreateGraphicsPipelines(
        mDevice, mPipelineCache, 1, &pipelineInfo, nullptr,
        &mOot3dShadow2dDepthEncodePipeline);
    vkDestroyShaderModule(mDevice, vertexShader, nullptr);
    vkDestroyShaderModule(mDevice, fragmentShader, nullptr);
    CheckVk(result, "vkCreateGraphicsPipelines(OOT3D Shadow2D depth encode)");
}

GfxRenderingAPIVulkan::BufferAllocation GfxRenderingAPIVulkan::CreateBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
    bool persistentlyMapped) {
    BufferAllocation buffer{};
    buffer.Size = size;
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateBuffer(mDevice, &bufferInfo, nullptr, &buffer.Buffer), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(mDevice, buffer.Buffer, &requirements);
    VkMemoryAllocateInfo allocationInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, properties);
    CheckVk(vkAllocateMemory(mDevice, &allocationInfo, nullptr, &buffer.Memory),
            "vkAllocateMemory(buffer)");
    CheckVk(vkBindBufferMemory(mDevice, buffer.Buffer, buffer.Memory, 0),
            "vkBindBufferMemory");
    if (persistentlyMapped) {
        CheckVk(vkMapMemory(mDevice, buffer.Memory, 0, size, 0, &buffer.Mapped),
                "vkMapMemory(buffer)");
    }
    return buffer;
}

void GfxRenderingAPIVulkan::DestroyBuffer(BufferAllocation& buffer) {
    if (mDevice == VK_NULL_HANDLE) {
        return;
    }
    if (buffer.Buffer != VK_NULL_HANDLE) {
        mNriInterop.ForgetBuffer(buffer.Buffer);
    }
    if (buffer.Mapped != nullptr) {
        vkUnmapMemory(mDevice, buffer.Memory);
    }
    if (buffer.Buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(mDevice, buffer.Buffer, nullptr);
    }
    if (buffer.Memory != VK_NULL_HANDLE) {
        vkFreeMemory(mDevice, buffer.Memory, nullptr);
    }
    buffer = {};
}

VkCommandBuffer GfxRenderingAPIVulkan::BeginImmediateCommands() {
    VkCommandBufferAllocateInfo allocationInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
    };
    allocationInfo.commandPool = mCommandPool;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    CheckVk(vkAllocateCommandBuffers(mDevice, &allocationInfo, &commandBuffer),
            "vkAllocateCommandBuffers(immediate)");
    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
            "vkBeginCommandBuffer(immediate)");
    return commandBuffer;
}

void GfxRenderingAPIVulkan::EndImmediateCommands(VkCommandBuffer commandBuffer) {
    CheckVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(immediate)");
    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    CheckVk(vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE),
            "vkQueueSubmit(immediate)");
    CheckVk(vkQueueWaitIdle(mGraphicsQueue), "vkQueueWaitIdle(immediate)");
    vkFreeCommandBuffers(mDevice, mCommandPool, 1, &commandBuffer);
}

void GfxRenderingAPIVulkan::TransitionImageLayout(VkImage image, uint32_t baseMipLevel,
                                                   uint32_t levelCount,
                                                   VkImageLayout oldLayout,
                                                   VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.layerCount = 1;
    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::runtime_error("unsupported OOT3D Vulkan image layout transition");
    }
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    EndImmediateCommands(commandBuffer);
}

void GfxRenderingAPIVulkan::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                                               uint32_t height, uint32_t mipLevel) {
    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = mipLevel;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    EndImmediateCommands(commandBuffer);
}

void GfxRenderingAPIVulkan::UploadTextureLevel(TextureRecord& texture, uint32_t level,
                                                const uint8_t* rgba32Buf, uint32_t width,
                                                uint32_t height) {
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(width) * height * 4;
    auto staging = CreateBuffer(byteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                true);
    std::memcpy(staging.Mapped, rgba32Buf, static_cast<size_t>(byteCount));

    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    const bool firstUpload = level == 0 && texture.UploadedMipLevels == 0;
    VkImageMemoryBarrier toTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toTransfer.oldLayout = firstUpload ? VK_IMAGE_LAYOUT_UNDEFINED
                                       : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = texture.Image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel = level;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = firstUpload ? 0 : VK_ACCESS_SHADER_READ_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, firstUpload ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                     : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toTransfer);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = level;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(commandBuffer, staging.Buffer, texture.Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier toShaderRead{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.image = texture.Image;
    toShaderRead.subresourceRange = toTransfer.subresourceRange;
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toShaderRead);
    EndImmediateCommands(commandBuffer);
    DestroyBuffer(staging);
    if (level == texture.UploadedMipLevels) {
        ++texture.UploadedMipLevels;
    }
}

void GfxRenderingAPIVulkan::CreateTextureImage(TextureRecord& texture,
                                                const uint8_t* rgba32Buf, uint32_t width,
                                                uint32_t height) {
    if (texture.Uploaded) {
        CheckVk(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle(replace texture)");
        DestroyTexture(texture);
    }
    texture.Width = width;
    texture.Height = height;
    texture.MipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    texture.UploadedMipLevels = 0;
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = texture.MipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateImage(mDevice, &imageInfo, nullptr, &texture.Image),
            "vkCreateImage(texture)");
    texture.NriImage = texture.Image;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(mDevice, texture.Image, &requirements);
    VkMemoryAllocateInfo allocationInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex =
        FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CheckVk(vkAllocateMemory(mDevice, &allocationInfo, nullptr, &texture.Memory),
            "vkAllocateMemory(texture)");
    CheckVk(vkBindImageMemory(mDevice, texture.Image, texture.Memory, 0),
            "vkBindImageMemory(texture)");

    UploadTextureLevel(texture, 0, rgba32Buf, width, height);
    if (texture.MipLevels > 1) {
        TransitionImageLayout(texture.Image, 1, texture.MipLevels - 1,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = texture.Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = texture.MipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    CheckVk(vkCreateImageView(mDevice, &viewInfo, nullptr, &texture.View),
            "vkCreateImageView(texture)");
    texture.Uploaded = true;
    RecreateSampler(texture);
}

void GfxRenderingAPIVulkan::RecreateSampler(TextureRecord& texture) {
    if (!texture.Uploaded && texture.Image == VK_NULL_HANDLE) {
        return;
    }
    if (texture.Sampler != VK_NULL_HANDLE) {
        mRetiredSamplers.push_back(texture.Sampler);
    }
    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = ToVulkanFilter(texture.SamplerState.MagFilter);
    samplerInfo.minFilter = ToVulkanFilter(texture.SamplerState.MinFilter);
    samplerInfo.mipmapMode = ToVulkanMipmapMode(texture.SamplerState.MinFilter);
    samplerInfo.addressModeU = ToVulkanAddressMode(texture.SamplerState.WrapS);
    samplerInfo.addressModeV = ToVulkanAddressMode(texture.SamplerState.WrapT);
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.mipLodBias = texture.SamplerState.LodBias;
    const uint32_t highestUploadedMip = texture.UploadedMipLevels > 0
                                            ? texture.UploadedMipLevels - 1
                                            : 0;
    samplerInfo.minLod = static_cast<float>(
        std::min(texture.SamplerState.MinMipLevel, highestUploadedMip));
    samplerInfo.maxLod = static_cast<float>(
        std::min(texture.SamplerState.MaxMipLevel, highestUploadedMip));
    CheckVk(vkCreateSampler(mDevice, &samplerInfo, nullptr, &texture.Sampler),
            "vkCreateSampler");
    texture.AppliedSamplerState = texture.SamplerState;
    texture.SamplerStateApplied = true;
}

void GfxRenderingAPIVulkan::DestroyTexture(TextureRecord& texture) {
    if (mDevice == VK_NULL_HANDLE) {
        return;
    }
    if (texture.NriImage != VK_NULL_HANDLE) {
        mNriPicaTextureUploadPass.ForgetTexture(texture.NriImage);
        if (texture.NriOwnedImage) {
            mNriPicaTextureImageOwner.Destroy(texture.NriImage);
        } else {
            mNriInterop.ForgetTexture(texture.NriImage);
        }
    }
    if (texture.Sampler != VK_NULL_HANDLE) {
        vkDestroySampler(mDevice, texture.Sampler, nullptr);
    }
    if (!texture.NriOwnedImage) {
        if (texture.View != VK_NULL_HANDLE) {
            vkDestroyImageView(mDevice, texture.View, nullptr);
        }
        if (texture.Image != VK_NULL_HANDLE) {
            vkDestroyImage(mDevice, texture.Image, nullptr);
        }
        if (texture.Memory != VK_NULL_HANDLE) {
            vkFreeMemory(mDevice, texture.Memory, nullptr);
        }
    }
    texture.Image = VK_NULL_HANDLE;
    texture.NriImage = VK_NULL_HANDLE;
    texture.Memory = VK_NULL_HANDLE;
    texture.View = VK_NULL_HANDLE;
    texture.Sampler = VK_NULL_HANDLE;
    texture.UploadedMipLevels = 0;
    texture.SamplerStateApplied = false;
    texture.NriOwnedImage = false;
    texture.Uploaded = false;
}

VkDescriptorSet GfxRenderingAPIVulkan::AllocateTextureDescriptorSet() {
    VkDescriptorSetAllocateInfo allocationInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
    };
    allocationInfo.descriptorPool = mFrameResources[mCurrentFrame].DescriptorPool;
    allocationInfo.descriptorSetCount = 1;
    allocationInfo.pSetLayouts = &mTextureDescriptorSetLayout;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    CheckVk(vkAllocateDescriptorSets(mDevice, &allocationInfo, &descriptorSet),
            "vkAllocateDescriptorSets");
    return descriptorSet;
}

GfxRenderingAPIVulkan::PipelineKey GfxRenderingAPIVulkan::BuildPipelineKey(
    const VulkanShaderProgram& shader) const {
    PipelineKey key{};
    key.ShaderId0 = shader.ShaderId0;
    key.ShaderId1 = shader.ShaderId1;
    key.VertexStride = shader.VertexStride;
    key.TextureCoordinateOffsets = shader.TextureCoordinateOffsets;
    key.ColorOffset = shader.InputOffsets[0] == VulkanShaderProgram::kAbsentAttribute
                          ? 0
                          : shader.InputOffsets[0];
    key.ColorComponents = shader.InputOffsets[0] == VulkanShaderProgram::kAbsentAttribute
                              ? 0
                              : shader.HasAlpha ? 4 : 3;
    key.UsesTexture0 = shader.UsedTextures[0];
    key.UsesVertexColor = key.ColorComponents != 0;
    key.DepthTest = mCurrentDepthTest != 0;
    key.DepthWrite = mCurrentDepthMask != 0;
    key.Decal = mCurrentZmodeDecal != 0;
    key.CullMode = mCullMode;
    key.BlendEnabled = mBlendState.Enabled;
    key.EquationRgb = mBlendState.EquationRgb;
    key.EquationAlpha = mBlendState.EquationAlpha;
    key.SourceRgb = mBlendState.SourceRgb;
    key.DestRgb = mBlendState.DestRgb;
    key.SourceAlpha = mBlendState.SourceAlpha;
    key.DestAlpha = mBlendState.DestAlpha;
    return key;
}

VkPipeline GfxRenderingAPIVulkan::GetOrCreatePipeline(const VulkanShaderProgram& shader) {
    const PipelineKey key = BuildPipelineKey(shader);
    const auto found = mPipelines.find(key);
    if (found != mPipelines.end()) {
        return found->second;
    }

    const VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
          VK_SHADER_STAGE_VERTEX_BIT, shader.VertexShader, "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
          VK_SHADER_STAGE_FRAGMENT_BIT, shader.FragmentShader, "main", nullptr },
    };
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = key.VertexStride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::vector<VkVertexInputAttributeDescription> attributes;
    attributes.push_back({ 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0 });
    if (shader.UsedTextures[0]) {
        attributes.push_back({ 1, 0, VK_FORMAT_R32G32_SFLOAT,
                               shader.TextureCoordinateOffsets[0] });
    }
    if (shader.UsedTextures[1]) {
        attributes.push_back({ 2, 0, VK_FORMAT_R32G32_SFLOAT,
                               shader.TextureCoordinateOffsets[1] });
    }
    if (shader.UsesTexture2) {
        attributes.push_back({ 3, 0, VK_FORMAT_R32G32_SFLOAT,
                               shader.TextureCoordinateOffsets[2] });
    }
    if (shader.HasFog) {
        attributes.push_back({ 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                               shader.FogOffset });
    }
    if (shader.HasGrayscale) {
        attributes.push_back({ 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                               shader.GrayscaleOffset });
    }
    for (uint32_t input = 0; input < shader.NumInputs; ++input) {
        if (input >= shader.InputOffsets.size() ||
            shader.InputOffsets[input] == VulkanShaderProgram::kAbsentAttribute) {
            throw std::runtime_error("OOT3D Vulkan shader exceeds supported vertex input count");
        }
        attributes.push_back({ 6 + input, 0,
                               shader.HasAlpha ? VK_FORMAT_R32G32B32A32_SFLOAT
                                               : VK_FORMAT_R32G32B32_SFLOAT,
                               shader.InputOffsets[input] });
    }
    if (shader.ShadowCoordinateOffset != VulkanShaderProgram::kAbsentAttribute) {
        attributes.push_back({ 14, 0, VK_FORMAT_R32G32B32_SFLOAT,
                               shader.ShadowCoordinateOffset });
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
    };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.lineWidth = 1.0f;
    rasterization.cullMode = key.CullMode == GfxNativeCullMode::KeepAll
                                 ? VK_CULL_MODE_NONE
                                 : VK_CULL_MODE_BACK_BIT;
    if (key.CullMode == GfxNativeCullMode::KeepClockwise) {
        rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
    } else {
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
    rasterization.depthBiasEnable = key.Decal ? VK_TRUE : VK_FALSE;
    rasterization.depthBiasConstantFactor = key.Decal ? -2.0f : 0.0f;
    rasterization.depthBiasSlopeFactor = key.Decal ? -2.0f : 0.0f;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
    };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };
    depthStencil.depthTestEnable = key.DepthTest || key.DepthWrite;
    depthStencil.depthWriteEnable = key.DepthWrite;
    depthStencil.depthCompareOp = key.DepthTest
                                      ? key.Decal ? VK_COMPARE_OP_LESS_OR_EQUAL
                                                  : VK_COMPARE_OP_LESS
                                      : VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = key.BlendEnabled;
    blendAttachment.colorBlendOp = ToVulkanBlendOperation(key.EquationRgb);
    blendAttachment.alphaBlendOp = ToVulkanBlendOperation(key.EquationAlpha);
    blendAttachment.srcColorBlendFactor = ToVulkanBlendFactor(key.SourceRgb);
    blendAttachment.dstColorBlendFactor = ToVulkanBlendFactor(key.DestRgb);
    blendAttachment.srcAlphaBlendFactor = ToVulkanBlendFactor(key.SourceAlpha);
    blendAttachment.dstAlphaBlendFactor = ToVulkanBlendFactor(key.DestAlpha);
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
    };
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;
    const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT,
                                             VK_DYNAMIC_STATE_SCISSOR,
                                             VK_DYNAMIC_STATE_BLEND_CONSTANTS };
    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
    };
    dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
    };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mPipelineLayout;
    pipelineInfo.renderPass = mRenderPass;
    pipelineInfo.subpass = 0;
    VkPipeline pipeline = VK_NULL_HANDLE;
    CheckVk(vkCreateGraphicsPipelines(mDevice, mPipelineCache, 1, &pipelineInfo, nullptr,
                                      &pipeline),
            "vkCreateGraphicsPipelines");
    mPipelines.emplace(key, pipeline);
    return pipeline;
}

void GfxRenderingAPIVulkan::DestroyPresentationPipelines() {
    if (mDevice == VK_NULL_HANDLE) {
        return;
    }
    for (const auto& [key, pipeline] : mPipelines) {
        vkDestroyPipeline(mDevice, pipeline, nullptr);
    }
    mPipelines.clear();
    if (mNativePicaScanoutPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(mDevice, mNativePicaScanoutPipeline, nullptr);
        mNativePicaScanoutPipeline = VK_NULL_HANDLE;
    }
    if (mNativePicaScanoutOverlayPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(mDevice, mNativePicaScanoutOverlayPipeline, nullptr);
        mNativePicaScanoutOverlayPipeline = VK_NULL_HANDLE;
    }
}

void GfxRenderingAPIVulkan::DestroyGraphicsPipelines() {
    DestroyPresentationPipelines();
    if (mDevice == VK_NULL_HANDLE) return;
    for (const auto& [key, pipeline] : mNativePicaPipelines) {
        mNriPicaPipelineBridge.Forget(pipeline);
        vkDestroyPipeline(mDevice, pipeline, nullptr);
    }
    mNativePicaPipelines.clear();
    mPicaPipelinePrewarmedProfiles.clear();
    if (mOot3dShadow2dDepthEncodePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(mDevice, mOot3dShadow2dDepthEncodePipeline, nullptr);
        mOot3dShadow2dDepthEncodePipeline = VK_NULL_HANDLE;
    }
}

void GfxRenderingAPIVulkan::CreateSwapchainResources() {
    const auto support = QuerySwapchainSupport(mPhysicalDevice);
    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(support.Formats);
    const VkPresentModeKHR presentMode = ChoosePresentMode(support.PresentModes);
    const VkExtent2D extent = ChooseExtent(support.Capabilities);
    if (extent.width == 0 || extent.height == 0) {
        mSwapchainDirty = true;
        return;
    }

    uint32_t imageCount = support.Capabilities.minImageCount + 1;
    if (support.Capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, support.Capabilities.maxImageCount);
    }
    const bool nriCreated =
        mNriSwapchain.Supported() &&
        mNriSwapchain.Create({
            ResolveNriNativeWindow(mWindowBackend),
            extent.width,
            extent.height,
            imageCount,
            kFramesInFlight,
            mVsyncEnabled,
            !mVsyncEnabled && mWindowBackend->CanDisableVsync(),
        });
    if (nriCreated) {
        mDiagnostics.SetVulkanPresentationFallback(
            Oot3dVulkanPresentationFallbackReason::None, {});
        for (VkSemaphore& semaphore : mImageAvailableSemaphores) {
            if (semaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(mDevice, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
        mSwapchainFormat = mNriSwapchain.Format();
        mSwapchainExtent = {
            mNriSwapchain.Width(), mNriSwapchain.Height()};
        const auto images = mNriSwapchain.Images();
        imageCount = static_cast<uint32_t>(images.size());
        mSwapchainImages.reserve(imageCount);
        mSwapchainImageViews.reserve(imageCount);
        for (const auto& image : images) {
            mSwapchainImages.push_back(image.Image);
            mSwapchainImageViews.push_back(
                image.ColorAttachmentView);
        }
        for (uint32_t index = 0; index < kFramesInFlight; ++index)
            mImageAvailableSemaphores[index] =
                mNriSwapchain.AcquireSemaphore(index);
        mRenderFinishedSemaphores.resize(
            imageCount, VK_NULL_HANDLE);
        for (uint32_t index = 0; index < imageCount; ++index)
            mRenderFinishedSemaphores[index] =
                mNriSwapchain.ReleaseSemaphore(index);
    } else {
        if (mNriSwapchain.Supported()) {
            mDiagnostics.SetVulkanPresentationFallback(
                Oot3dVulkanPresentationFallbackReason::
                    NriCreationFailed,
                mNriSwapchain.UnavailableReason());
            SPDLOG_WARN(
                "OOT3D NRI swapchain creation failed; using Vulkan: {}",
                mNriSwapchain.UnavailableReason());
        }
        VkSemaphoreCreateInfo semaphoreInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (VkSemaphore& semaphore : mImageAvailableSemaphores) {
            if (semaphore == VK_NULL_HANDLE) {
                CheckVk(vkCreateSemaphore(
                            mDevice, &semaphoreInfo, nullptr, &semaphore),
                        "vkCreateSemaphore(image available fallback)");
            }
        }

        VkSwapchainCreateInfoKHR createInfo{
            VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        createInfo.surface = mSurface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        const uint32_t queueFamilies[] = {
            mGraphicsQueueFamily, mPresentQueueFamily};
        if (mGraphicsQueueFamily != mPresentQueueFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilies;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        // Scanout is in logical window coordinates, not pre-rotated display
        // coordinates. Let the compositor apply the surface transform.
        createInfo.preTransform =
            (support.Capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                : support.Capabilities.currentTransform;
        SPDLOG_INFO("Vulkan surface: {}x{}, transform {}, preTransform {}",
                    extent.width, extent.height,
                    static_cast<uint32_t>(support.Capabilities.currentTransform),
                    static_cast<uint32_t>(createInfo.preTransform));
        createInfo.compositeAlpha =
            (support.Capabilities.supportedCompositeAlpha &
             VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                : static_cast<VkCompositeAlphaFlagBitsKHR>(
                      support.Capabilities.supportedCompositeAlpha &
                      (~support.Capabilities.supportedCompositeAlpha + 1));
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        CheckVk(vkCreateSwapchainKHR(
                    mDevice, &createInfo, nullptr, &mSwapchain),
                "vkCreateSwapchainKHR");

        CheckVk(vkGetSwapchainImagesKHR(
                    mDevice, mSwapchain, &imageCount, nullptr),
                "vkGetSwapchainImagesKHR(count)");
        mSwapchainImages.resize(imageCount);
        CheckVk(vkGetSwapchainImagesKHR(
                    mDevice, mSwapchain, &imageCount,
                    mSwapchainImages.data()),
                "vkGetSwapchainImagesKHR");
        mRenderFinishedSemaphores.resize(
            imageCount, VK_NULL_HANDLE);
        VkSemaphoreCreateInfo renderFinishedInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (VkSemaphore& semaphore : mRenderFinishedSemaphores) {
            CheckVk(vkCreateSemaphore(
                        mDevice, &renderFinishedInfo, nullptr, &semaphore),
                    "vkCreateSemaphore(render finished image)");
        }
        mSwapchainFormat = surfaceFormat.format;
        mSwapchainExtent = extent;
        mSwapchainImageViews.resize(imageCount);
        for (size_t index = 0;
             index < mSwapchainImages.size(); ++index) {
            VkImageViewCreateInfo viewInfo{
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = mSwapchainImages[index];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = mSwapchainFormat;
            viewInfo.subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            CheckVk(vkCreateImageView(
                        mDevice, &viewInfo, nullptr,
                        &mSwapchainImageViews[index]),
                    "vkCreateImageView(swapchain)");
        }
    }

    mDepthFormat = FindDepthFormat();
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = mSwapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = mDepthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthReference{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = dependency.srcStageMask;
    // Discarding depth contents does not discard prior depth writes. Both the
    // clear pass and the load-color overlay reuse this attachment in one frame.
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    const VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };
    VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    CheckVk(vkCreateRenderPass(mDevice, &renderPassInfo, nullptr, &mRenderPass), "vkCreateRenderPass");

    VkAttachmentDescription overlayColorAttachment = colorAttachment;
    overlayColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    overlayColorAttachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentDescription overlayDepthAttachment = depthAttachment;
    overlayDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    overlayDepthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    const VkAttachmentDescription overlayAttachments[] = {
        overlayColorAttachment, overlayDepthAttachment
    };
    VkRenderPassCreateInfo overlayRenderPassInfo = renderPassInfo;
    overlayRenderPassInfo.pAttachments = overlayAttachments;
    CheckVk(vkCreateRenderPass(mDevice, &overlayRenderPassInfo, nullptr,
                               &mOverlayRenderPass),
            "vkCreateRenderPass(overlay)");

    CreateDepthResources();
    mSwapchainFramebuffers.resize(imageCount);
    for (size_t index = 0; index < imageCount; ++index) {
        const VkImageView attachmentsForFramebuffer[] = {
            mSwapchainImageViews[index], mDepthImageViews[index]
        };
        VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        framebufferInfo.renderPass = mRenderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachmentsForFramebuffer;
        framebufferInfo.width = mSwapchainExtent.width;
        framebufferInfo.height = mSwapchainExtent.height;
        framebufferInfo.layers = 1;
        CheckVk(vkCreateFramebuffer(mDevice, &framebufferInfo, nullptr,
                                    &mSwapchainFramebuffers[index]),
                "vkCreateFramebuffer");
    }
    mImagesInFlight.assign(imageCount, VK_NULL_HANDLE);
    mSwapchainSurfaceCapabilities = support.Capabilities;
    mSwapchainSurfaceFormat = surfaceFormat;
    mSwapchainSuboptimal.store(false);
    mSwapchainDirty = false;
    SPDLOG_INFO(
        "OOT3D {} swapchain: {}x{}, {} images, format {}, present mode {}",
        nriCreated ? "NRI" : "Vulkan",
        mSwapchainExtent.width, mSwapchainExtent.height, imageCount,
        static_cast<int>(mSwapchainFormat),
        nriCreated ? -1 : static_cast<int>(presentMode));
}

void GfxRenderingAPIVulkan::DestroySwapchainResources() {
    if (mDevice == VK_NULL_HANDLE) {
        return;
    }
    ShutdownImGuiBackend();
    DestroyPresentationPipelines();
    for (VkFramebuffer framebuffer : mSwapchainFramebuffers) {
        vkDestroyFramebuffer(mDevice, framebuffer, nullptr);
    }
    mSwapchainFramebuffers.clear();
    const bool nriOwned = mNriSwapchain.Active();
    if (!nriOwned) {
        for (VkSemaphore semaphore : mRenderFinishedSemaphores) {
            if (semaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(mDevice, semaphore, nullptr);
        }
    }
    mRenderFinishedSemaphores.clear();
    if (mOverlayRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mOverlayRenderPass, nullptr);
        mOverlayRenderPass = VK_NULL_HANDLE;
    }
    if (mRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mRenderPass, nullptr);
        mRenderPass = VK_NULL_HANDLE;
    }
    for (VkImageView view : mDepthImageViews) {
        vkDestroyImageView(mDevice, view, nullptr);
    }
    for (VkImage image : mDepthImages) {
        vkDestroyImage(mDevice, image, nullptr);
    }
    for (VkDeviceMemory memory : mDepthMemories) {
        vkFreeMemory(mDevice, memory, nullptr);
    }
    mDepthImageViews.clear();
    mDepthImages.clear();
    mDepthMemories.clear();
    mNriPicaScanoutPass.ResetTargets();
    for (VkImage image : mSwapchainImages)
        mNriInterop.ForgetTexture(image);
    if (!nriOwned) {
        for (VkImageView view : mSwapchainImageViews)
            vkDestroyImageView(mDevice, view, nullptr);
    }
    mSwapchainImageViews.clear();
    mSwapchainImages.clear();
    if (mSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
        mSwapchain = VK_NULL_HANDLE;
    }
    if (nriOwned) {
        mImageAvailableSemaphores.fill(VK_NULL_HANDLE);
        mNriSwapchain.Destroy();
    }
    mImagesInFlight.clear();
}

bool GfxRenderingAPIVulkan::SwapchainSurfaceChanged() const {
    const auto support = QuerySwapchainSupport(mPhysicalDevice);
    const auto extent = ChooseExtent(support.Capabilities);
    const auto format = ChooseSurfaceFormat(support.Formats);
    // SUBOPTIMAL still permits presentation. In particular, compositor rotation
    // can report it forever; rebuilding an identical configuration cannot help.
    return extent.width != mSwapchainExtent.width || extent.height != mSwapchainExtent.height ||
           format.format != mSwapchainSurfaceFormat.format ||
           format.colorSpace != mSwapchainSurfaceFormat.colorSpace ||
           support.Capabilities.currentTransform != mSwapchainSurfaceCapabilities.currentTransform ||
           support.Capabilities.supportedTransforms != mSwapchainSurfaceCapabilities.supportedTransforms ||
           support.Capabilities.minImageCount != mSwapchainSurfaceCapabilities.minImageCount ||
           support.Capabilities.maxImageCount != mSwapchainSurfaceCapabilities.maxImageCount;
}

void GfxRenderingAPIVulkan::RecreateSwapchain() {
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t x = 0;
    int32_t y = 0;
    mWindowBackend->GetDimensions(&width, &height, &x, &y);
    if (width == 0 || height == 0) {
        mSwapchainDirty = true;
        return;
    }
    WaitForAllPresents();
    StopPresentWorker();
    CheckVk(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle(recreate swapchain)");
    // Native PICA targets are offscreen scene resources, not swapchain
    // resources. Preserve them (and CACAO's depth bindings) across presentation
    // mode changes; their own reset path handles genuine scene invalidation.
    DestroySwapchainResources();
    CreateSwapchainResources();
    StartPresentWorker();
    if (mNriPicaScanoutPass.Available() &&
        !mNriPicaScanoutPass.Configure(mSwapchainFormat)) {
        SPDLOG_WARN("OOT3D NRI scanout reconfigure failed: {}",
                    mNriPicaScanoutPass.UnavailableReason());
    }
}

GfxRenderingAPIVulkan::QueueFamilies
GfxRenderingAPIVulkan::FindQueueFamilies(VkPhysicalDevice device) const {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
    std::vector<Oot3d::VulkanQueueFamilyCandidate> candidates;
    candidates.reserve(properties.size());
    for (uint32_t index = 0; index < count; ++index) {
        VkBool32 present = VK_FALSE;
        CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, mSurface, &present),
                "vkGetPhysicalDeviceSurfaceSupportKHR");
        candidates.push_back({
            index,
            properties[index].queueCount,
            (properties[index].queueFlags &
             VK_QUEUE_GRAPHICS_BIT) != 0,
            present == VK_TRUE,
        });
    }
    const auto selected =
        Oot3d::SelectVulkanQueueFamilies(candidates);
    QueueFamilies result;
    result.Graphics = selected.GraphicsFamily;
    result.Present = selected.PresentFamily;
    return result;
}

GfxRenderingAPIVulkan::SwapchainSupport
GfxRenderingAPIVulkan::QuerySwapchainSupport(VkPhysicalDevice device) const {
    SwapchainSupport result;
    CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, mSurface, &result.Capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    uint32_t formatCount = 0;
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(device, mSurface, &formatCount, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    result.Formats.resize(formatCount);
    if (formatCount > 0) {
        CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(device, mSurface, &formatCount,
                                                     result.Formats.data()),
                "vkGetPhysicalDeviceSurfaceFormatsKHR");
    }
    uint32_t modeCount = 0;
    CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(device, mSurface, &modeCount, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
    result.PresentModes.resize(modeCount);
    if (modeCount > 0) {
        CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(device, mSurface, &modeCount,
                                                          result.PresentModes.data()),
                "vkGetPhysicalDeviceSurfacePresentModesKHR");
    }
    return result;
}

bool GfxRenderingAPIVulkan::DeviceSupportsSwapchain(VkPhysicalDevice device) const {
    uint32_t extensionCount = 0;
    CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr),
            "vkEnumerateDeviceExtensionProperties(count)");
    std::vector<VkExtensionProperties> extensions(extensionCount);
    CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()),
            "vkEnumerateDeviceExtensionProperties");
    return std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
    });
}

VkSurfaceFormatKHR GfxRenderingAPIVulkan::ChooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats) const {
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR GfxRenderingAPIVulkan::ChoosePresentMode(
    const std::vector<VkPresentModeKHR>& modes) const {
    if (mVsyncEnabled) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D GfxRenderingAPIVulkan::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t x = 0;
    int32_t y = 0;
    mWindowBackend->GetDimensions(&width, &height, &x, &y);
    return {
        std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
    };
}

VkFormat GfxRenderingAPIVulkan::FindDepthFormat() const {
    return Oot3d::FindPicaDepthFormat(mPhysicalDevice);
}

uint32_t GfxRenderingAPIVulkan::FindMemoryType(uint32_t typeFilter,
                                               VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &memoryProperties);
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if ((typeFilter & (1u << index)) != 0 &&
            (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties) {
            return index;
        }
    }
    throw std::runtime_error("Vulkan memory type was not found");
}

void GfxRenderingAPIVulkan::CreateDepthResources() {
    const size_t count = mSwapchainImages.size();
    mDepthImages.resize(count);
    mDepthMemories.resize(count);
    mDepthImageViews.resize(count);
    for (size_t index = 0; index < count; ++index) {
        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { mSwapchainExtent.width, mSwapchainExtent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = mDepthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVk(vkCreateImage(mDevice, &imageInfo, nullptr, &mDepthImages[index]),
                "vkCreateImage(depth)");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(mDevice, mDepthImages[index], &requirements);
        VkMemoryAllocateInfo allocationInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocationInfo.allocationSize = requirements.size;
        allocationInfo.memoryTypeIndex =
            FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckVk(vkAllocateMemory(mDevice, &allocationInfo, nullptr, &mDepthMemories[index]),
                "vkAllocateMemory(depth)");
        CheckVk(vkBindImageMemory(mDevice, mDepthImages[index], mDepthMemories[index], 0),
                "vkBindImageMemory(depth)");
        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = mDepthImages[index];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = mDepthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (HasStencil(mDepthFormat)) {
            viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        CheckVk(vkCreateImageView(mDevice, &viewInfo, nullptr, &mDepthImageViews[index]),
                "vkCreateImageView(depth)");
    }
}

void GfxRenderingAPIVulkan::BeginRenderPassIfNeeded() {
    if (!mFrameActive || mRenderPassActive || mOot3dShadow2dPassActive ||
        mSwapchainFramebuffers.empty()) {
        return;
    }
    VkClearValue clearValues[2]{};
    clearValues[0].color.float32[0] = mClearColor[0];
    clearValues[0].color.float32[1] = mClearColor[1];
    clearValues[0].color.float32[2] = mClearColor[2];
    clearValues[0].color.float32[3] = mClearColor[3];
    clearValues[1].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo beginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    beginInfo.renderPass = mRenderPass;
    beginInfo.framebuffer = mSwapchainFramebuffers[mCurrentImage];
    beginInfo.renderArea.extent = mSwapchainExtent;
    beginInfo.clearValueCount = 2;
    beginInfo.pClearValues = clearValues;
    vkCmdBeginRenderPass(mCommandBuffers[mCurrentFrame], &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    mRenderPassActive = true;
    mOverlayRenderPassActive = false;
    ApplyDynamicViewportAndScissor();
}

void GfxRenderingAPIVulkan::ApplyDynamicViewportAndScissor() {
    if (!mFrameActive || !mRenderPassActive) {
        return;
    }
    if (mViewportSet) {
        vkCmdSetViewport(mCommandBuffers[mCurrentFrame], 0, 1, &mViewport);
    }
    if (mScissorSet) {
        VkRect2D clamped = mScissor;
        clamped.extent.width = std::min(clamped.extent.width, mSwapchainExtent.width);
        clamped.extent.height = std::min(clamped.extent.height, mSwapchainExtent.height);
        vkCmdSetScissor(mCommandBuffers[mCurrentFrame], 0, 1, &clamped);
    }
}

} // namespace Fast

#endif
