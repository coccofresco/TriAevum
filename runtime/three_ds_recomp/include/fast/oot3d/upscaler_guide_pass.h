#pragma once
#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/effect_graph_pass_barriers.h"
#include <vulkan/vulkan.h>
#include <array>
#include <memory>
#include <string>

namespace Fast::Oot3d {
class NriInteropContext;

// Prepares a temporal upscaler's declared guides (depth, motion, reactive) at
// its colour input's extent, resampled when the extents differ, with the
// jitter taken out of the motion; see upscaler_guides.h. It runs inside the
// upscaler pass for every dispatch, and its outputs are private images the
// upscaler dispatch reads in the same frame.
class UpscalerGuidePass final {
  public:
    struct Guides {
        VkImage Depth = VK_NULL_HANDLE;
        VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
        VkImage Motion = VK_NULL_HANDLE;
        VkFormat MotionFormat = VK_FORMAT_UNDEFINED;
        VkImage Reactive = VK_NULL_HANDLE;
        VkFormat ReactiveFormat = VK_FORMAT_UNDEFINED;
        uint32_t Width = 0U;
        uint32_t Height = 0U;
    };

    UpscalerGuidePass();
    ~UpscalerGuidePass();
    UpscalerGuidePass(const UpscalerGuidePass&) = delete;
    UpscalerGuidePass& operator=(const UpscalerGuidePass&) = delete;
    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, NriInteropContext& interop);
    // Reads the declared guides in their shader-read state and leaves the
    // prepared images (R32F depth, RGBA16F motion, R16F reactive) readable.
    // `motionCorrection`: UpscalerMotionJitterCorrection.
    bool Execute(uint32_t frameSlot, const Guides& guides, uint32_t width, uint32_t height,
                 const std::array<float, 2>& motionCorrection);
    void InvalidateScreenResources();
    void Shutdown();
    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage DepthImage() const;
    [[nodiscard]] VkImageView DepthView() const;
    [[nodiscard]] VkImage MotionImage() const;
    [[nodiscard]] VkImageView MotionView() const;
    [[nodiscard]] VkImage ReactiveImage() const;
    [[nodiscard]] VkImageView ReactiveView() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
} // namespace Fast::Oot3d
#endif
