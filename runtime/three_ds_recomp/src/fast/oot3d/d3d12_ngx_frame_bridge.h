#pragma once

#if defined(_WIN32) && defined(ENABLE_OOT3D_VULKAN)

#include "fast/oot3d/d3d12_ngx_provider.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

struct ID3D12CommandQueue;
struct ID3D12Device;

namespace nri {
struct CoreInterface;
struct Device;
struct UpscalerInterface;
} // namespace nri

namespace Fast::Oot3d {

// Owns the persistent cross-API frame resources and the ordinary D3D12 DLSR
// dispatch. Vulkan records only the producer/acquire halves; D3D12 resources
// are always returned to COMMON before crossing the API boundary.
class D3d12NgxFrameBridge final {
  public:
    D3d12NgxFrameBridge();
    ~D3d12NgxFrameBridge();
    D3d12NgxFrameBridge(const D3d12NgxFrameBridge&) = delete;
    D3d12NgxFrameBridge& operator=(const D3d12NgxFrameBridge&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice vulkanDevice, uint32_t graphicsQueueFamily,
                    ID3D12Device& d3d12Device, ID3D12CommandQueue& d3d12Queue, nri::Device& nriDevice,
                    const nri::CoreInterface& core, const nri::UpscalerInterface& upscalerInterface,
                    const D3d12NgxFrameContract& initialContract);
    void Shutdown();

    [[nodiscard]] bool ConfiguredFor(const D3d12NgxFrameContract& contract) const;
    // The caller must make both APIs idle before changing a live contract.
    bool Configure(const D3d12NgxFrameContract& contract);

    // Records Vulkan input conversion/release and the matching D3D12 command
    // list. Nothing is submitted until QueuePreparedFrame is called.
    bool PrepareFrame(VkCommandBuffer vulkanCommandBuffer, uint32_t frameSlot, const D3d12NgxFrameInputs& inputs,
                      D3d12NgxFrameSynchronization& synchronization);
    // Called after the Vulkan prefix that signals VulkanInputsReady has been
    // submitted. The completion value is always signaled, including the
    // recoverable failure path, so Vulkan cannot be left waiting forever.
    bool QueuePreparedFrame(const D3d12NgxFrameSynchronization& synchronization);
    bool RecordOutputAcquire(VkCommandBuffer vulkanCommandBuffer, const D3d12NgxFrameSynchronization& synchronization);

    [[nodiscard]] bool Available() const;
    [[nodiscard]] bool ExternalMemoryReady() const;
    [[nodiscard]] bool SharedFenceReady() const;
    [[nodiscard]] VkImage OutputImage(uint32_t frameSlot) const;
    [[nodiscard]] VkImageView OutputView(uint32_t frameSlot) const;
    [[nodiscard]] const D3d12NgxFrameContract& Contract() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
