#pragma once

#if defined(_WIN32) && defined(ENABLE_OOT3D_VULKAN)

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "fast/oot3d/upscaler_contract.h"

namespace Fast::Oot3d {

struct D3d12NgxProviderStatus {
    bool AdapterMatched = false;
    bool DeviceReady = false;
    bool NriReady = false;
    bool DlssFeatureReady = false;
    bool DlssContractResourcesReady = false;
    bool DlssEvaluateReady = false;
    bool ExternalMemoryReady = false;
    bool SharedFenceReady = false;
    bool ZeroCopyInteropReady = false;
    uint32_t VendorId = 0;
    uint32_t DeviceId = 0;
    uint32_t ProbeInputWidth = 0;
    uint32_t ProbeInputHeight = 0;
    uint32_t ProbeOutputWidth = 0;
    uint32_t ProbeOutputHeight = 0;
    std::string AdapterName;
    std::string Detail;
    bool FrameBridgeReady = false;
    bool FrameDispatchRequested = false;
    uint64_t FrameDispatchCount = 0;
};

struct D3d12NgxFrameContract {
    uint32_t InputWidth = 0;
    uint32_t InputHeight = 0;
    uint32_t OutputWidth = 0;
    uint32_t OutputHeight = 0;
    uint32_t FrameSlotCount = 0;
    UpscalerQuality Quality = UpscalerQuality::Quality;
    bool InputSrgb = false;

    [[nodiscard]] bool Valid() const noexcept;
    auto operator<=>(const D3d12NgxFrameContract&) const = default;
};

struct D3d12NgxFrameInputs {
    VkImageView Color = VK_NULL_HANDLE;
    VkImageView Depth = VK_NULL_HANDLE;
    VkImageView Motion = VK_NULL_HANDLE;
    VkImageView Reactive = VK_NULL_HANDLE;
    VkImageLayout ColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageLayout DepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkImageLayout MotionLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageLayout ReactiveLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<float, 2> JitterPixels{};
    bool ResetHistory = false;

    [[nodiscard]] bool Valid() const noexcept;
};

struct D3d12NgxFrameSynchronization {
    VkSemaphore Semaphore = VK_NULL_HANDLE;
    uint64_t VulkanInputsReady = 0;
    uint64_t D3d12OutputReady = 0;
    uint32_t FrameSlot = 0;
    uint64_t Serial = 0;

    [[nodiscard]] bool Valid() const noexcept;
};

// Owns the optional D3D12 side of NGX while Vulkan remains the rendering API.
// Availability requires a real same-adapter resource and fence interop plane;
// creating an NGX feature alone is deliberately not sufficient.
class D3d12NgxProvider final {
  public:
    D3d12NgxProvider();
    ~D3d12NgxProvider();
    D3d12NgxProvider(const D3d12NgxProvider&) = delete;
    D3d12NgxProvider& operator=(const D3d12NgxProvider&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t graphicsQueueFamily,
                    uint32_t frameSlotCount, bool externalInteropExtensionsEnabled);
    void Shutdown();

    [[nodiscard]] bool FrameDispatchRequested() const;
    [[nodiscard]] bool ConfiguredForFrameContract(const D3d12NgxFrameContract& contract) const;
    bool ConfigureFrameContract(const D3d12NgxFrameContract& contract);
    bool PrepareFrame(VkCommandBuffer vulkanCommandBuffer, uint32_t frameSlot, const D3d12NgxFrameInputs& inputs,
                      D3d12NgxFrameSynchronization& synchronization);
    bool QueuePreparedFrame(const D3d12NgxFrameSynchronization& synchronization);
    bool RecordOutputAcquire(VkCommandBuffer vulkanCommandBuffer, const D3d12NgxFrameSynchronization& synchronization);
    [[nodiscard]] VkImage FrameOutputImage(uint32_t frameSlot) const;
    [[nodiscard]] VkImageView FrameOutputView(uint32_t frameSlot) const;

    [[nodiscard]] bool Available() const;
    [[nodiscard]] const D3d12NgxProviderStatus& Status() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
