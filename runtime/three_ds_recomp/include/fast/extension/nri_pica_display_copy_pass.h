#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/extension/pica_display_transfer.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Extension {

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
    // Commit a fully composed scene before native UI. Exact transfer copy,
    // restoring the destination's attachment layout without storage usage.
    bool DestinationColorAttachment = false;
};

[[nodiscard]] inline bool ValidateNriPicaDisplayCopyDesc(
    const NriPicaDisplayCopyDesc& desc) {
    return desc.SourceImage != VK_NULL_HANDLE &&
           desc.DestinationImage != VK_NULL_HANDLE &&
           desc.SourceImage != desc.DestinationImage &&
           desc.Format != VK_FORMAT_UNDEFINED &&
           desc.Plan.SamplingFitsSource() &&
           (!desc.DestinationColorAttachment ||
            (desc.DestinationInitialized &&
             desc.Plan.SourceWidth == desc.Plan.DestinationWidth &&
             desc.Plan.SourceHeight == desc.Plan.DestinationHeight &&
             desc.Plan.HorizontalSamples == 1U &&
             desc.Plan.VerticalSamples == 1U));
}

// One region of a raw copy, materialized from the immutable internal-resolution
// capture of the copy's source into the texture a native draw samples. The
// destination is at the declared native layout scaled by a per-axis density that
// is at least the source's internal-per-native ratio, and each destination texel
// maps through the region's native translation onto a fractional position inside
// the capture, so internal-or-higher density is kept per axis even for
// anisotropic or non-integral source scales.
struct NriPicaDisplayCopyRegionDesc {
    uint32_t FrameIndex = 0;
    uint64_t FrameId = 0;
    VkImage SourceImage = VK_NULL_HANDLE;
    VkFormat SourceFormat = VK_FORMAT_UNDEFINED;
    uint32_t SourceWidth = 0;
    uint32_t SourceHeight = 0;
    VkImage DestinationImage = VK_NULL_HANDLE;
    VkFormat DestinationFormat = VK_FORMAT_UNDEFINED;
    uint32_t DestinationWidth = 0;
    uint32_t DestinationHeight = 0;
    bool DestinationInitialized = false;
    // Region in destination texels of the derived density.
    uint32_t DestinationX = 0;
    uint32_t DestinationY = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    // Destination-native to source-native region translation, per axis.
    int32_t NativeDeltaX = 0;
    int32_t NativeDeltaY = 0;
    // The source is a captured attachment, whose rows run bottom-up, while the
    // destination stores native memory rows (top row first): the source rows
    // are mirrored. False for a source decoded from native bytes.
    bool SourceAttachmentRows = false;
    // Destination texels per native texel, and the source surface's native
    // extent the capture was rendered from.
    uint32_t DensityX = 1;
    uint32_t DensityY = 1;
    uint32_t SourceNativeWidth = 0;
    uint32_t SourceNativeHeight = 0;
    // Native format whose channel semantics the sampled pixels must carry.
    uint8_t NativeFormat = 0;
};

[[nodiscard]] inline bool ValidateNriPicaDisplayCopyRegionDesc(
    const NriPicaDisplayCopyRegionDesc& desc) {
    if (desc.SourceImage == VK_NULL_HANDLE ||
        desc.DestinationImage == VK_NULL_HANDLE ||
        desc.SourceImage == desc.DestinationImage ||
        desc.SourceFormat == VK_FORMAT_UNDEFINED ||
        desc.DestinationFormat == VK_FORMAT_UNDEFINED ||
        desc.Width == 0U || desc.Height == 0U || desc.SourceWidth == 0U ||
        desc.SourceHeight == 0U || desc.DestinationWidth == 0U ||
        desc.DestinationHeight == 0U || desc.DensityX == 0U ||
        desc.DensityY == 0U || desc.SourceNativeWidth == 0U ||
        desc.SourceNativeHeight == 0U || desc.NativeFormat > 4U ||
        uint64_t{desc.DestinationX} + desc.Width > desc.DestinationWidth ||
        uint64_t{desc.DestinationY} + desc.Height > desc.DestinationHeight) {
        return false;
    }
    // The shader's numerators must stay inside signed 32-bit arithmetic.
    const uint64_t lastX = uint64_t{desc.DestinationX} + desc.Width - 1U;
    const uint64_t lastY = uint64_t{desc.DestinationY} + desc.Height - 1U;
    const uint64_t numeratorX =
        (2U * lastX + 1U + 2U * desc.DensityX *
                            static_cast<uint64_t>(
                                desc.NativeDeltaX < 0 ? -desc.NativeDeltaX
                                                      : desc.NativeDeltaX)) *
        desc.SourceWidth;
    // A mirrored source subtracts the row from 2*density*sourceNativeHeight;
    // bounding the sum bounds the difference.
    const uint64_t numeratorY =
        (2U * lastY + 1U + 2U * desc.DensityY *
                            static_cast<uint64_t>(
                                desc.NativeDeltaY < 0 ? -desc.NativeDeltaY
                                                      : desc.NativeDeltaY) +
         (desc.SourceAttachmentRows
              ? 2U * uint64_t{desc.DensityY} * desc.SourceNativeHeight
              : 0U)) *
        desc.SourceHeight;
    return numeratorX <= 0x7FFFFFFFull && numeratorY <= 0x7FFFFFFFull;
}

// Executes native crop/downsample semantics while preserving the layouts
// expected by the surrounding Vulkan render-target and scanout paths.
class NriPicaDisplayCopyPass final {
  public:
    NriPicaDisplayCopyPass();
    ~NriPicaDisplayCopyPass();
    NriPicaDisplayCopyPass(const NriPicaDisplayCopyPass&) = delete;
    NriPicaDisplayCopyPass& operator=(const NriPicaDisplayCopyPass&) = delete;

    bool Initialize(NriInteropContext& interop);
    bool Execute(const NriPicaDisplayCopyDesc& desc);
    // Materializes one region of a raw copy from an immutable internal-resolution
    // capture into the sampled texture, applying the native format's channel
    // semantics.
    bool ExecuteRegion(const NriPicaDisplayCopyRegionDesc& desc);
    void ForgetTexture(VkImage image);
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] uint32_t LastBarrierCount() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Extension

#endif
