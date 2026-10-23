#pragma once

#include "fast/oot3d/pica_attachment_contract.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>

namespace Fast::Oot3d {

// The native PICA scene attachments' formats, by PicaColorAttachment.
inline constexpr std::array<VkFormat, kPicaColorAttachmentCount> kPicaColorAttachmentFormats{
    VK_FORMAT_R8G8B8A8_UNORM,      VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R32G32B32A32_UINT,   VK_FORMAT_R16G16B16A16_UNORM};

// The formats a dynamic-rendering layout binds and its pipelines declare:
// VK_FORMAT_UNDEFINED for an attachment it leaves unbound.
[[nodiscard]] inline std::array<VkFormat, kPicaColorAttachmentCount> BoundPicaColorAttachmentFormats(
    const PicaAttachmentRequirements& attachments) noexcept {
    auto formats = kPicaColorAttachmentFormats;
    for (size_t index = 0; index < formats.size(); ++index)
        if (!attachments.Binds(static_cast<PicaColorAttachment>(index))) formats[index] = VK_FORMAT_UNDEFINED;
    return formats;
}

} // namespace Fast::Oot3d
