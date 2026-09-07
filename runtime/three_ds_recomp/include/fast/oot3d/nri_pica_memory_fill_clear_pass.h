#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/pica_attachment_contract.h"
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;

struct NriPicaShadowClearDesc {
    uint32_t FrameIndex = 0;
    VkImage Image = VK_NULL_HANDLE;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Value = 0;
};

struct NriPicaAttachmentClearDesc {
    uint32_t FrameIndex = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    std::array<float, 4> Color{};
    float Depth = 1.0F;
    uint8_t Stencil = 0;
    bool ClearColor = false;
    bool ClearDepth = false;
    bool HasStencil = false;
    bool NriRenderingScope = false;
    PicaAttachmentRequirements Attachments{ kAllPicaAuxiliaryOutputs };
};

[[nodiscard]] inline bool ValidateNriPicaShadowClearDesc(const NriPicaShadowClearDesc& desc) {
    return desc.Image != VK_NULL_HANDLE && desc.Width != 0U && desc.Height != 0U;
}

[[nodiscard]] inline bool ValidateNriPicaAttachmentClearDesc(const NriPicaAttachmentClearDesc& desc) {
    return desc.Width != 0U && desc.Height != 0U && (desc.ClearColor || desc.ClearDepth) && desc.NriRenderingScope;
}

class NriPicaMemoryFillClearPass final {
  public:
    NriPicaMemoryFillClearPass();
    ~NriPicaMemoryFillClearPass();
    NriPicaMemoryFillClearPass(const NriPicaMemoryFillClearPass&) = delete;
    NriPicaMemoryFillClearPass& operator=(const NriPicaMemoryFillClearPass&) = delete;

    bool Initialize(NriInteropContext& interop);
    bool ClearShadow(const NriPicaShadowClearDesc& desc);
    bool ClearAttachments(const NriPicaAttachmentClearDesc& desc);
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] uint32_t LastShadowBarrierCount() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
