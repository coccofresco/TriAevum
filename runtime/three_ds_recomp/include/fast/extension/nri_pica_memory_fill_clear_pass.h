#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/extension/pica_attachment_contract.h"
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Extension {

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

// The native colour attachment only, in up to four rectangles of the open
// target (x, y, width, height): presentation-owned pixels such as the bars
// around a fitted full-screen picture. Guides, depth and stencil are kept.
struct NriPicaColorRectsClearDesc {
    uint32_t FrameIndex = 0;
    std::array<float, 4> Color{};
    std::array<std::array<uint32_t, 4>, 4> Rects{};
    uint32_t RectCount = 0;
    bool NriRenderingScope = false;
};

[[nodiscard]] inline bool ValidateNriPicaShadowClearDesc(const NriPicaShadowClearDesc& desc) {
    return desc.Image != VK_NULL_HANDLE && desc.Width != 0U && desc.Height != 0U;
}

[[nodiscard]] inline bool ValidateNriPicaAttachmentClearDesc(const NriPicaAttachmentClearDesc& desc) {
    return desc.Width != 0U && desc.Height != 0U && (desc.ClearColor || desc.ClearDepth) && desc.NriRenderingScope;
}

[[nodiscard]] inline bool ValidateNriPicaColorRectsClearDesc(const NriPicaColorRectsClearDesc& desc) {
    if (desc.RectCount == 0U || desc.RectCount > desc.Rects.size() || !desc.NriRenderingScope)
        return false;
    for (uint32_t index = 0; index < desc.RectCount; ++index) {
        const auto& rect = desc.Rects[index];
        // NRI rectangles carry 16-bit offsets and extents.
        if (rect[2] == 0U || rect[3] == 0U || rect[0] > 0x7FFFU || rect[1] > 0x7FFFU ||
            rect[2] > 0xFFFFU || rect[3] > 0xFFFFU)
            return false;
    }
    return true;
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
    bool ClearColorRects(const NriPicaColorRectsClearDesc& desc);
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] uint32_t LastShadowBarrierCount() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Extension

#endif
