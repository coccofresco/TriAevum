#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/pica_display_transfer.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;

struct NriPicaDisplayCopyDesc {
    uint32_t FrameIndex = 0;
    uint64_t FrameId = 0;
    VkImage SourceImage = VK_NULL_HANDLE;
    VkImage DestinationImage = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    PicaDisplayTransferPlan Plan{};
    bool SourceShaderRead = false;
    bool DestinationInitialized = false;
};

[[nodiscard]] inline bool ValidateNriPicaDisplayCopyDesc(
    const NriPicaDisplayCopyDesc& desc) {
    return desc.SourceImage != VK_NULL_HANDLE &&
           desc.DestinationImage != VK_NULL_HANDLE &&
           desc.SourceImage != desc.DestinationImage &&
           desc.Format != VK_FORMAT_UNDEFINED &&
           desc.Plan.SourceWidth != 0U &&
           desc.Plan.SourceHeight != 0U &&
           desc.Plan.DestinationWidth != 0U &&
           desc.Plan.DestinationHeight != 0U &&
           (desc.Plan.HorizontalSamples == 1U ||
            desc.Plan.HorizontalSamples == 2U) &&
           (desc.Plan.VerticalSamples == 1U ||
            desc.Plan.VerticalSamples == 2U) &&
           static_cast<uint64_t>(desc.Plan.DestinationWidth) *
                   desc.Plan.HorizontalSamples <=
               desc.Plan.SourceWidth &&
           static_cast<uint64_t>(desc.Plan.DestinationHeight) *
                   desc.Plan.VerticalSamples <=
               desc.Plan.SourceHeight;
}

// Executes native crop/downsample semantics while preserving the layouts
// expected by the surrounding Vulkan render-target and scanout paths.
class NriPicaDisplayCopyPass final {
  public:
    NriPicaDisplayCopyPass();
    ~NriPicaDisplayCopyPass();
    NriPicaDisplayCopyPass(const NriPicaDisplayCopyPass&) = delete;
    NriPicaDisplayCopyPass& operator=(
        const NriPicaDisplayCopyPass&) = delete;

    bool Initialize(NriInteropContext& interop);
    bool Execute(const NriPicaDisplayCopyDesc& desc);
    void ForgetTexture(VkImage image);
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] uint32_t LastBarrierCount() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
