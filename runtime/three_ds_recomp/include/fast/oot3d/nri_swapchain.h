#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;

struct NriSwapchainDesc {
    void* NativeWindow = nullptr;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t DesiredImageCount = 0;
    uint32_t FrameCount = 0;
    bool Vsync = true;
    bool AllowTearing = false;
};

struct NriSwapchainImage {
    VkImage Image = VK_NULL_HANDLE;
    VkImageView ColorAttachmentView = VK_NULL_HANDLE;
};

enum class NriSwapchainOperationResult : uint8_t {
    Success,
    OutOfDate,
    Failure,
};

[[nodiscard]] inline bool ValidateNriSwapchainDesc(
    const NriSwapchainDesc& desc) {
    return desc.NativeWindow != nullptr && desc.Width != 0U &&
           desc.Height != 0U && desc.DesiredImageCount >= 2U &&
           desc.DesiredImageCount <= UINT8_MAX &&
           desc.FrameCount != 0U && desc.FrameCount <= UINT8_MAX;
}

[[nodiscard]] inline bool ShouldUseVulkanPresentWorker(
    bool nriSwapchainActive, bool asynchronousPresentSupported) {
    return !nriSwapchainActive && asynchronousPresentSupported;
}

class NriSwapchain final {
  public:
    NriSwapchain();
    ~NriSwapchain();
    NriSwapchain(const NriSwapchain&) = delete;
    NriSwapchain& operator=(const NriSwapchain&) = delete;

    bool Initialize(NriInteropContext& interop);
    bool Create(const NriSwapchainDesc& desc);
    void Destroy();
    void Shutdown();

    NriSwapchainOperationResult Acquire(uint32_t frameIndex,
                                        uint32_t& imageIndex);
    NriSwapchainOperationResult Present(uint32_t imageIndex);

    [[nodiscard]] bool Supported() const;
    [[nodiscard]] bool Active() const;
    [[nodiscard]] VkFormat Format() const;
    [[nodiscard]] uint32_t Width() const;
    [[nodiscard]] uint32_t Height() const;
    [[nodiscard]] std::span<const NriSwapchainImage> Images() const;
    [[nodiscard]] VkSemaphore AcquireSemaphore(uint32_t frameIndex) const;
    [[nodiscard]] VkSemaphore ReleaseSemaphore(uint32_t imageIndex) const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
