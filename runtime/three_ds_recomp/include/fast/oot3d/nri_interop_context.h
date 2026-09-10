#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include <vulkan/vulkan.h>

#include "fast/oot3d/linear_scene_color.h"
#include "fast/oot3d/renderer_validation_telemetry.h"
#include "fast/oot3d/upscaler_contract.h"
#include "fast/renderer/shaderc_compiler.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

class NriInteropAccess;

struct NriOwnedTexture2D {
    VkImage Image = VK_NULL_HANDLE;
    VkImageView SampledView = VK_NULL_HANDLE;
    VkImageView StorageView = VK_NULL_HANDLE;
    VkImageView ColorAttachmentView = VK_NULL_HANDLE;
    VkImageView DepthAttachmentView = VK_NULL_HANDLE;
};

struct NriOwnedTexture2DDesc {
    uint32_t Width = 0;
    uint32_t Height = 0;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags Usage = 0;
    uint32_t MipLevels = 1;
    uint32_t Layers = 1;
    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;
    bool CubeSampledView = false;
};

// Shared scene inputs for temporal NRI upscalers. Provider-specific settings
// are consumed only by providers that need them.
struct NriTemporalUpscaleDispatchDesc {
    uint32_t FrameIndex = 0;
    VkImage InputImage = VK_NULL_HANDLE;
    VkFormat InputFormat = VK_FORMAT_UNDEFINED;
    VkImage DepthImage = VK_NULL_HANDLE;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    VkImage MotionImage = VK_NULL_HANDLE;
    VkFormat MotionFormat = VK_FORMAT_UNDEFINED;
    VkImage ReactiveImage = VK_NULL_HANDLE;
    VkFormat ReactiveFormat = VK_FORMAT_UNDEFINED;
    VkImage OutputImage = VK_NULL_HANDLE;
    VkFormat OutputFormat = VK_FORMAT_UNDEFINED;
    uint32_t InputWidth = 0;
    uint32_t InputHeight = 0;
    uint32_t OutputWidth = 0;
    uint32_t OutputHeight = 0;
    UpscalerQuality Quality = UpscalerQuality::Quality;
    std::array<float, 2> JitterPixels{};
    float NearPlane = 0.1F;
    float FarPlane = 1000.0F;
    float VerticalFovRadians = 1.0F;
    float FrameTimeMilliseconds = 16.6667F;
    float ViewUnitsPerMeter = 1.0F;
    float Sharpness = 0.0F;
    bool ResetHistory = false;
    SceneColorEncoding InputEncoding = SceneColorEncoding::Unknown;
};

// Owns NRI wrapper objects plus resources explicitly created through the
// owned-resource API. Wrapped Vulkan objects remain owned by the backend.
class NriInteropContext final {
  public:
    NriInteropContext();
    ~NriInteropContext();
    NriInteropContext(const NriInteropContext&) = delete;
    NriInteropContext& operator=(const NriInteropContext&) = delete;

    bool Initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                    uint32_t graphicsQueueFamily, bool synchronization2Enabled,
                    bool dynamicRenderingEnabled,
                    bool nisDeviceFeaturesEnabled,
                    bool fsrDeviceFeaturesEnabled,
                    bool swapchainExtensionsEnabled = false,
                    RendererValidationTelemetry* validationTelemetry = nullptr);
    void Shutdown();
    Renderer::CachedPassShaderCompiler& Shaders() { return mShaders; }

    bool WrapFrameCommandBuffer(uint32_t frameIndex, VkCommandBuffer commandBuffer);
    bool WrapTexture(VkImage image, VkFormat format, VkImageType type,
                     VkImageUsageFlags usage, uint32_t width, uint32_t height,
                     uint32_t mipLevels = 1, uint32_t layers = 1,
                     VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    bool WrapBuffer(VkBuffer buffer, uint64_t size,
                    uint8_t* mappedMemory = nullptr);
    bool CreateOwnedTexture2D(uint32_t width, uint32_t height, VkFormat format,
                              NriOwnedTexture2D& texture,
                              uint32_t mipLevels = 1,
                              bool createStorageView = true);
    bool CreateOwnedTexture2D(const NriOwnedTexture2DDesc& desc,
                              NriOwnedTexture2D& texture);
    [[nodiscard]] bool OwnsTexture(VkImage image) const;
    [[nodiscard]] bool DynamicRenderingAvailable() const;
    void DestroyOwnedTexture(VkImage image);
    void ForgetTexture(VkImage image);
    void ForgetBuffer(VkBuffer buffer);

    [[nodiscard]] bool NisAvailable() const;
    [[nodiscard]] const std::string& NisUnavailableReason() const;
    bool DispatchNis(uint32_t frameIndex,
                     VkImage inputImage, VkFormat inputFormat,
                     uint32_t inputWidth, uint32_t inputHeight,
                     VkImage outputImage, VkFormat outputFormat,
                     uint32_t outputWidth, uint32_t outputHeight,
                     UpscalerQuality quality, float sharpness,
                     SceneColorEncoding inputEncoding);
    [[nodiscard]] bool FsrAvailable() const;
    [[nodiscard]] const std::string& FsrUnavailableReason() const;
    bool DispatchFsr(const NriTemporalUpscaleDispatchDesc& desc);
    [[nodiscard]] bool DlssAvailable() const;
    [[nodiscard]] const std::string& DlssUnavailableReason() const;
    bool DispatchDlss(const NriTemporalUpscaleDispatchDesc& desc);

    [[nodiscard]] bool Available() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    friend class NriInteropAccess;
    struct Impl;
    std::unique_ptr<Impl> mImpl;
    Renderer::CachedPassShaderCompiler mShaders;
};

} // namespace Fast::Oot3d

#endif
