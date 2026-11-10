#pragma once

#include "fast/extension/pica_attachment_contract.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace Fast::Extension {

// The native PICA scene attachments' formats, by PicaColorAttachment.
inline constexpr std::array<VkFormat, kPicaColorAttachmentCount> kPicaColorAttachmentFormats{
    VK_FORMAT_R8G8B8A8_UNORM,      VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R32G32B32A32_UINT,   VK_FORMAT_R16G16B16A16_UNORM};

// TRIAEVUM_THIN_GUIDES=1 (perf, default off): the geometry guide is stored as
// R32G32_UINT (8 bytes) instead of R32G32B32A32_UINT (16). Its writers keep
// their uvec4 output (an integer attachment stores the components its format
// has, unconverted), so R (native normal) and G (face normal) keep their bits
// and B (kind) and A (native depth) are dropped. MXAO reads only G: its shader
// and the values it reads are unchanged. Valid while nothing reads B or A: the
// outline passes (R/A, Toon outlines) and the GI adapter (B) are not part of
// Enhanced; with them on the switch is wrong and the runtime says so.
// Process-wide: every target, pipeline and view of the guide uses one format.
[[nodiscard]] inline bool PicaThinGuides() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TRIAEVUM_THIN_GUIDES");
        const bool on = value != nullptr && value[0] == '1';
        if (on)
            std::fprintf(stderr, "[thin-guides] enabled: geometry guide R32G32_UINT (8 B, was 16 B)\n");
        return on;
    }();
    return enabled;
}

[[nodiscard]] inline VkFormat PicaGeometryGuideFormat() noexcept {
    return PicaThinGuides() ? VK_FORMAT_R32G32_UINT : VK_FORMAT_R32G32B32A32_UINT;
}

// The attachment formats in use (kPicaColorAttachmentFormats with the
// geometry guide's format of this process).
[[nodiscard]] inline std::array<VkFormat, kPicaColorAttachmentCount> PicaColorAttachmentFormats() noexcept {
    auto formats = kPicaColorAttachmentFormats;
    formats[PicaAttachmentIndex(PicaColorAttachment::OutlineGeometryGuide)] = PicaGeometryGuideFormat();
    return formats;
}

// One warning per reader that needs a geometry-guide channel the thin layout
// dropped.
inline void WarnPicaThinGuidesReader(const char* reader) noexcept {
    static bool warned = false;
    if (!PicaThinGuides() || warned) return;
    warned = true;
    std::fprintf(stderr,
                 "[thin-guides] WARNING: %s reads geometry guide B/A, which TRIAEVUM_THIN_GUIDES=1 does not store; "
                 "its result is wrong in this run\n",
                 reader);
}

// The formats a dynamic-rendering layout binds and its pipelines declare:
// VK_FORMAT_UNDEFINED for an attachment it leaves unbound.
[[nodiscard]] inline std::array<VkFormat, kPicaColorAttachmentCount> BoundPicaColorAttachmentFormats(
    const PicaAttachmentRequirements& attachments) noexcept {
    auto formats = PicaColorAttachmentFormats();
    for (size_t index = 0; index < formats.size(); ++index)
        if (!attachments.Binds(static_cast<PicaColorAttachment>(index))) formats[index] = VK_FORMAT_UNDEFINED;
    return formats;
}

} // namespace Fast::Extension
