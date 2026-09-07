#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_pica_render_target_owner.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;

struct NriPicaRenderTargetInitDesc {
    uint32_t FrameIndex = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;
    PicaAttachmentRequirements Attachments{kAllPicaAuxiliaryOutputs};
    NriPicaRenderTargetImages Images;
};

[[nodiscard]] inline bool ValidateNriPicaRenderTargetInitDesc(
    const NriPicaRenderTargetInitDesc& desc) {
    return desc.Width != 0U && desc.Height != 0U &&
           desc.DepthFormat != VK_FORMAT_UNDEFINED &&
           ValidateNriPicaRenderTargetImages(
               desc.Images, desc.Samples, desc.Attachments);
}

// Establishes the attachment/storage layouts and deterministic clear values
// expected before the first PICA draw into a newly-created target bundle.
class NriPicaRenderTargetInitPass final {
  public:
    NriPicaRenderTargetInitPass();
    ~NriPicaRenderTargetInitPass();
    NriPicaRenderTargetInitPass(
        const NriPicaRenderTargetInitPass&) = delete;
    NriPicaRenderTargetInitPass& operator=(
        const NriPicaRenderTargetInitPass&) = delete;

    bool Initialize(NriInteropContext& interop);
    bool Execute(const NriPicaRenderTargetInitDesc& desc);
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] uint32_t LastBarrierCount() const;
    [[nodiscard]] uint32_t LastClearedImageCount() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
