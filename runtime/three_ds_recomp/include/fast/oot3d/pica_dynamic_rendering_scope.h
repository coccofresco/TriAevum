#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/pica_attachment_contract.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;

struct PicaDynamicRenderingTarget {
    std::array<VkImageView, kPicaColorAttachmentCount> Colors{};
    std::array<VkImageView, kPicaColorAttachmentCount> Resolves{};
    VkImageView Depth = VK_NULL_HANDLE;
    VkImageView DepthResolve = VK_NULL_HANDLE;
    std::array<VkImage, kPicaColorAttachmentCount> ColorImages{};
    std::array<VkImage, kPicaColorAttachmentCount> ResolveImages{};
    VkImage DepthImage = VK_NULL_HANDLE;
    VkImage DepthResolveImage = VK_NULL_HANDLE;
    std::array<VkFormat, kPicaColorAttachmentCount> ColorFormats{};
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    uint32_t ColorAttachmentCount =
        static_cast<uint32_t>(kPicaColorAttachmentCount);
    uint32_t Width = 0;
    uint32_t Height = 0;
    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;
};

[[nodiscard]] inline bool ValidatePicaDynamicRenderingTarget(
    const PicaDynamicRenderingTarget& target,
    VkResolveModeFlagBits depthResolveMode) {
    if (target.Width == 0U || target.Height == 0U ||
        target.Depth == VK_NULL_HANDLE)
        return false;
    if (target.ColorAttachmentCount == 0U ||
        target.ColorAttachmentCount > kPicaColorAttachmentCount)
        return false;
    for (size_t index = 0; index < target.ColorAttachmentCount; ++index)
        if (target.Colors[index] == VK_NULL_HANDLE) return false;
    if (target.Samples == VK_SAMPLE_COUNT_1_BIT)
        return true;
    for (size_t index = 0; index < target.ColorAttachmentCount; ++index)
        if (target.Resolves[index] == VK_NULL_HANDLE) return false;
    return target.DepthResolve != VK_NULL_HANDLE &&
           depthResolveMode != VK_RESOLVE_MODE_NONE;
}

[[nodiscard]] inline bool ValidatePicaNriRenderingTarget(
    const PicaDynamicRenderingTarget& target,
    VkResolveModeFlagBits depthResolveMode) {
    if (!ValidatePicaDynamicRenderingTarget(target, depthResolveMode) ||
        target.DepthImage == VK_NULL_HANDLE ||
        target.DepthFormat == VK_FORMAT_UNDEFINED)
        return false;
    for (size_t index = 0; index < target.ColorAttachmentCount; ++index)
        if (target.ColorImages[index] == VK_NULL_HANDLE ||
            target.ColorFormats[index] == VK_FORMAT_UNDEFINED)
            return false;
    if (target.Samples == VK_SAMPLE_COUNT_1_BIT)
        return true;
    for (size_t index = 0; index < target.ColorAttachmentCount; ++index)
        if (target.ResolveImages[index] == VK_NULL_HANDLE) return false;
    return target.DepthResolveImage != VK_NULL_HANDLE;
}

class PicaDynamicRenderingScope final {
  public:
    PicaDynamicRenderingScope();
    ~PicaDynamicRenderingScope();
    PicaDynamicRenderingScope(const PicaDynamicRenderingScope&) = delete;
    PicaDynamicRenderingScope& operator=(
        const PicaDynamicRenderingScope&) = delete;

    bool Initialize(VkDevice device, NriInteropContext* interop,
                    VkFormat depthFormat,
                    VkResolveModeFlagBits depthResolveMode,
                    bool dynamicRenderingEnabled);
    bool Begin(uint32_t frameIndex, VkCommandBuffer command,
               const PicaDynamicRenderingTarget& target);
    void End(uint32_t frameIndex, VkCommandBuffer command);
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] bool Active() const;
    [[nodiscard]] bool ActiveOwnedByNri() const;
    [[nodiscard]] uint32_t LastNriGlobalBarrierCount() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
