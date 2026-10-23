// Native PICA on Vulkan: savestate capture and restore of textures, targets and presentation state,
// completions and resets.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_pica_internal.h"
#include "fast/renderer3ds/pica_guest_extent_snapshot.h"

namespace Fast {

std::vector<uint8_t> GfxRenderingAPIVulkan::CaptureNativePicaImageBytes(
    VkImage image, uint32_t width, uint32_t height,
    uint32_t bytesPerPixel,
    VkImageLayout stableLayout, VkPipelineStageFlags stableStage,
    VkAccessFlags stableAccess, uint32_t mipLevel) {
    const uint64_t byteCount64 =
        static_cast<uint64_t>(width) * height * bytesPerPixel;
    if (image == VK_NULL_HANDLE || bytesPerPixel == 0U ||
        byteCount64 == 0U ||
        byteCount64 > std::numeric_limits<size_t>::max()) {
        throw std::runtime_error(
            "native PICA RGBA8 image has an invalid identity");
    }
    const auto byteCount = static_cast<VkDeviceSize>(byteCount64);
    auto readback = CreateBuffer(
        byteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);
    try {
        VkCommandBuffer command = BeginImmediateCommands();
        VkImageMemoryBarrier toTransfer{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransfer.oldLayout = stableLayout;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = image;
        toTransfer.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.baseMipLevel = mipLevel;
        toTransfer.subresourceRange.levelCount = 1U;
        toTransfer.subresourceRange.layerCount = 1U;
        toTransfer.srcAccessMask = stableAccess;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(
            command, stableStage, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toTransfer);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = mipLevel;
        copy.imageSubresource.layerCount = 1U;
        copy.imageExtent = {width, height, 1U};
        vkCmdCopyImageToBuffer(
            command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            readback.Buffer, 1U, &copy);
        VkImageMemoryBarrier toStable = toTransfer;
        toStable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toStable.newLayout = stableLayout;
        toStable.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toStable.dstAccessMask = stableAccess;
        vkCmdPipelineBarrier(
            command, VK_PIPELINE_STAGE_TRANSFER_BIT, stableStage,
            0, 0, nullptr, 0, nullptr, 1, &toStable);
        EndImmediateCommands(command);

        const auto* bytes = static_cast<const uint8_t*>(readback.Mapped);
        std::vector<uint8_t> result(
            bytes, bytes + static_cast<size_t>(byteCount64));
        DestroyBuffer(readback);
        return result;
    } catch (...) {
        DestroyBuffer(readback);
        throw;
    }
}

void GfxRenderingAPIVulkan::RestoreNativePicaImageBytes(
    VkImage image, uint32_t width, uint32_t height,
    uint32_t bytesPerPixel, std::span<const uint8_t> bytes,
    VkImageLayout initialLayout,
    VkPipelineStageFlags initialStage, VkAccessFlags initialAccess,
    VkImageLayout stableLayout, VkPipelineStageFlags stableStage,
    VkAccessFlags stableAccess) {
    const uint64_t expectedBytes =
        static_cast<uint64_t>(width) * height * bytesPerPixel;
    if (image == VK_NULL_HANDLE || bytesPerPixel == 0U ||
        expectedBytes == 0U || expectedBytes != bytes.size()) {
        throw std::runtime_error(
            "native PICA RGBA8 restore payload is invalid");
    }
    auto staging = CreateBuffer(
        static_cast<VkDeviceSize>(expectedBytes),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);
    std::memcpy(staging.Mapped, bytes.data(), bytes.size());
    try {
        VkCommandBuffer command = BeginImmediateCommands();
        VkImageMemoryBarrier toTransfer{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransfer.oldLayout = initialLayout;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = image;
        toTransfer.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = 1U;
        toTransfer.subresourceRange.layerCount = 1U;
        toTransfer.srcAccessMask = initialAccess;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(
            command, initialStage, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toTransfer);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1U;
        copy.imageExtent = {width, height, 1U};
        vkCmdCopyBufferToImage(
            command, staging.Buffer, image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &copy);
        VkImageMemoryBarrier toStable = toTransfer;
        toStable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toStable.newLayout = stableLayout;
        toStable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toStable.dstAccessMask = stableAccess;
        vkCmdPipelineBarrier(
            command, VK_PIPELINE_STAGE_TRANSFER_BIT, stableStage,
            0, 0, nullptr, 0, nullptr, 1, &toStable);
        EndImmediateCommands(command);
    } catch (...) {
        DestroyBuffer(staging);
        throw;
    }
    DestroyBuffer(staging);
}

void GfxRenderingAPIVulkan::ClearNativePicaColorImage(
    VkImage image, const VkClearColorValue& value,
    VkImageLayout stableLayout, VkPipelineStageFlags stableStage,
    VkAccessFlags stableAccess) {
    if (image == VK_NULL_HANDLE) {
        throw std::runtime_error("native PICA colour clear has no image");
    }
    VkCommandBuffer command = BeginImmediateCommands();
    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.oldLayout = stableLayout;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1U;
    toTransfer.subresourceRange.layerCount = 1U;
    toTransfer.srcAccessMask = stableAccess;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command, stableStage, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toTransfer);
    vkCmdClearColorImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &value, 1U, &toTransfer.subresourceRange);
    VkImageMemoryBarrier toStable = toTransfer;
    toStable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toStable.newLayout = stableLayout;
    toStable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toStable.dstAccessMask = stableAccess;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, stableStage,
                         0, 0, nullptr, 0, nullptr, 1, &toStable);
    EndImmediateCommands(command);
}

void GfxRenderingAPIVulkan::CaptureNativePicaDepthStencilImage(
    VkImage image, uint32_t width, uint32_t height,
    std::vector<float>& depthValues,
    std::vector<uint8_t>& stencilValues) {
    depthValues.clear();
    stencilValues.clear();
    const uint64_t pixelCount64 =
        static_cast<uint64_t>(width) * height;
    if (image == VK_NULL_HANDLE || pixelCount64 == 0U ||
        pixelCount64 > std::numeric_limits<size_t>::max()) {
        throw std::runtime_error(
            "native PICA depth image has an invalid identity");
    }
    const bool hasStencil =
        mDepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        mDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT;
    if (mDepthFormat != VK_FORMAT_D32_SFLOAT &&
        mDepthFormat != VK_FORMAT_D32_SFLOAT_S8_UINT &&
        mDepthFormat != VK_FORMAT_D24_UNORM_S8_UINT) {
        throw std::runtime_error(
            "native PICA depth capture format is unsupported");
    }
    const auto pixelCount = static_cast<size_t>(pixelCount64);
    const VkDeviceSize depthBytes =
        static_cast<VkDeviceSize>(pixelCount64 * sizeof(uint32_t));
    const VkDeviceSize stencilOffset = depthBytes;
    const VkDeviceSize totalBytes =
        depthBytes + (hasStencil ? pixelCount64 : 0U);
    auto readback = CreateBuffer(
        totalBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);
    try {
        VkCommandBuffer command = BeginImmediateCommands();
        VkImageMemoryBarrier toTransfer{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransfer.oldLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = image;
        toTransfer.subresourceRange.aspectMask =
            NativeDepthAspect(mDepthFormat);
        toTransfer.subresourceRange.levelCount = 1U;
        toTransfer.subresourceRange.layerCount = 1U;
        toTransfer.srcAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
            nullptr, 1, &toTransfer);
        VkBufferImageCopy depthCopy{};
        depthCopy.imageSubresource.aspectMask =
            VK_IMAGE_ASPECT_DEPTH_BIT;
        depthCopy.imageSubresource.layerCount = 1U;
        depthCopy.imageExtent = {width, height, 1U};
        vkCmdCopyImageToBuffer(
            command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            readback.Buffer, 1U, &depthCopy);
        if (hasStencil) {
            VkBufferImageCopy stencilCopy{};
            stencilCopy.bufferOffset = stencilOffset;
            stencilCopy.imageSubresource.aspectMask =
                VK_IMAGE_ASPECT_STENCIL_BIT;
            stencilCopy.imageSubresource.layerCount = 1U;
            stencilCopy.imageExtent = {width, height, 1U};
            vkCmdCopyImageToBuffer(
                command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                readback.Buffer, 1U, &stencilCopy);
        }
        VkImageMemoryBarrier toAttachment = toTransfer;
        toAttachment.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toAttachment.newLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        toAttachment.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toAttachment.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(
            command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toAttachment);
        EndImmediateCommands(command);

        depthValues.resize(pixelCount);
        const auto* bytes = static_cast<const uint8_t*>(readback.Mapped);
        if (mDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
            constexpr float scale = 1.0F / 16777215.0F;
            for (size_t index = 0; index < pixelCount; ++index) {
                uint32_t packed = 0U;
                std::memcpy(&packed,
                            bytes + index * sizeof(uint32_t),
                            sizeof(packed));
                depthValues[index] =
                    static_cast<float>(packed & 0x00FFFFFFU) * scale;
            }
        } else {
            std::memcpy(depthValues.data(), bytes,
                        static_cast<size_t>(depthBytes));
        }
        if (hasStencil) {
            stencilValues.assign(
                bytes + static_cast<size_t>(stencilOffset),
                bytes + static_cast<size_t>(stencilOffset + pixelCount64));
        }
        DestroyBuffer(readback);
    } catch (...) {
        DestroyBuffer(readback);
        throw;
    }
}

void GfxRenderingAPIVulkan::RestoreNativePicaDepthStencilImage(
    VkImage image, uint32_t width, uint32_t height,
    std::span<const float> depthValues,
    std::span<const uint8_t> stencilValues) {
    const uint64_t pixelCount64 =
        static_cast<uint64_t>(width) * height;
    if (image == VK_NULL_HANDLE || pixelCount64 == 0U ||
        pixelCount64 != depthValues.size() ||
        !std::all_of(depthValues.begin(), depthValues.end(),
                     [](float value) {
                         return std::isfinite(value) && value >= 0.0F &&
                                value <= 1.0F;
                     })) {
        throw std::runtime_error(
            "native PICA depth restore payload is invalid");
    }
    const bool hasStencil =
        mDepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        mDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT;
    if (mDepthFormat != VK_FORMAT_D32_SFLOAT &&
        mDepthFormat != VK_FORMAT_D32_SFLOAT_S8_UINT &&
        mDepthFormat != VK_FORMAT_D24_UNORM_S8_UINT) {
        throw std::runtime_error(
            "native PICA depth restore format is unsupported");
    }
    if (!stencilValues.empty() &&
        stencilValues.size() != pixelCount64) {
        throw std::runtime_error(
            "native PICA stencil restore payload is invalid");
    }
    const auto pixelCount = static_cast<size_t>(pixelCount64);
    const VkDeviceSize depthBytes =
        static_cast<VkDeviceSize>(pixelCount64 * sizeof(uint32_t));
    const VkDeviceSize stencilOffset = depthBytes;
    const VkDeviceSize totalBytes =
        depthBytes + (hasStencil ? pixelCount64 : 0U);
    auto staging = CreateBuffer(
        totalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);
    auto* bytes = static_cast<uint8_t*>(staging.Mapped);
    if (mDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
        for (size_t index = 0; index < pixelCount; ++index) {
            const float depth = std::clamp(
                depthValues[index], 0.0F, 1.0F);
            const uint32_t packed = static_cast<uint32_t>(
                std::lround(depth * 16777215.0F));
            std::memcpy(bytes + index * sizeof(uint32_t),
                        &packed, sizeof(packed));
        }
    } else {
        std::memcpy(bytes, depthValues.data(),
                    static_cast<size_t>(depthBytes));
    }
    if (hasStencil) {
        auto* stencil = bytes + static_cast<size_t>(stencilOffset);
        if (stencilValues.empty()) {
            std::memset(stencil, 0, pixelCount);
        } else {
            std::memcpy(stencil, stencilValues.data(), pixelCount);
        }
    }
    try {
        VkCommandBuffer command = BeginImmediateCommands();
        VkImageMemoryBarrier toTransfer{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransfer.oldLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = image;
        toTransfer.subresourceRange.aspectMask =
            NativeDepthAspect(mDepthFormat);
        toTransfer.subresourceRange.levelCount = 1U;
        toTransfer.subresourceRange.layerCount = 1U;
        toTransfer.srcAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
            nullptr, 1, &toTransfer);
        VkBufferImageCopy depthCopy{};
        depthCopy.imageSubresource.aspectMask =
            VK_IMAGE_ASPECT_DEPTH_BIT;
        depthCopy.imageSubresource.layerCount = 1U;
        depthCopy.imageExtent = {width, height, 1U};
        vkCmdCopyBufferToImage(
            command, staging.Buffer, image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &depthCopy);
        if (hasStencil) {
            VkBufferImageCopy stencilCopy{};
            stencilCopy.bufferOffset = stencilOffset;
            stencilCopy.imageSubresource.aspectMask =
                VK_IMAGE_ASPECT_STENCIL_BIT;
            stencilCopy.imageSubresource.layerCount = 1U;
            stencilCopy.imageExtent = {width, height, 1U};
            vkCmdCopyBufferToImage(
                command, staging.Buffer, image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U,
                &stencilCopy);
        }
        VkImageMemoryBarrier toAttachment = toTransfer;
        toAttachment.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toAttachment.newLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        toAttachment.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toAttachment.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(
            command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toAttachment);
        EndImmediateCommands(command);
    } catch (...) {
        DestroyBuffer(staging);
        throw;
    }
    DestroyBuffer(staging);
}

bool GfxRenderingAPIVulkan::CapturePicaTextureCache(
    std::vector<GfxNativePicaTextureCacheEntrySnapshot>& snapshots,
    std::string* error) {
    snapshots.clear();
    try {
        if (mDevice == VK_NULL_HANDLE || mFrameActive || mFrameSubmitted) {
            throw std::runtime_error(
                "native PICA texture capture requires an idle frame boundary");
        }
        WaitForAllPresents();
        CheckNativeVk(vkDeviceWaitIdle(mDevice),
                      "vkDeviceWaitIdle(native PICA texture capture)");
        for (const auto& [key, texture] : mNativePicaTextures) {
            // Live render-target aliases are restored with the render target.
            if (texture.LiveRenderTargetAlias) {
                continue;
            }
            // A derived raw-copy sample is a function of its producers and of the
            // snapshot's own bytes, so it is regenerated on restore instead of
            // being serialized; only the immutable producer versions travel.
            if (key.GpuCopyIdentity != 0U) {
                continue;
            }
            // A texture-pack replacement is a function of the restoring
            // renderer's texture settings, not of the game moment.
            if (key.CustomReplacement) {
                continue;
            }
            const auto expectedBytes = NativePicaDecodedMipChainSize(
                texture.Width, texture.Height, texture.MipLevels);
            if (texture.Width == 0U || texture.Height == 0U ||
                !expectedBytes.has_value()) {
                throw std::runtime_error(
                    "native PICA cached texture has an invalid identity");
            }
            GfxNativePicaTextureCacheEntrySnapshot snapshot;
            snapshot.ContentHash = key.ContentHash;
            snapshot.ReplacementGeneration = key.ReplacementGeneration;
            snapshot.PhysicalAddress = key.PhysicalAddress;
            snapshot.SourceWidth = key.Width;
            snapshot.SourceHeight = key.Height;
            snapshot.NativeFormat = key.Format;
            snapshot.NativeType = key.Type;
            snapshot.NativeWrapS = key.WrapS;
            snapshot.NativeWrapT = key.WrapT;
            snapshot.MinLinear = key.MinLinear;
            snapshot.MagLinear = key.MagLinear;
            snapshot.MipLinear = key.MipLinear;
            snapshot.LodBiasRaw = key.LodBiasRaw;
            snapshot.MinMipLevel = key.MinMipLevel;
            snapshot.MaxMipLevel = key.MaxMipLevel;
            snapshot.CustomReplacement = key.CustomReplacement;
            snapshot.ImageWidth = texture.Width;
            snapshot.ImageHeight = texture.Height;
            snapshot.MipLevels = static_cast<uint8_t>(texture.MipLevels);
            snapshot.ImageFormat =
                key.Type == 2U && !key.CustomReplacement
                    ? GfxNativePicaTextureSnapshotFormat::R32Uint
                    : GfxNativePicaTextureSnapshotFormat::Rgba8;
            snapshot.CustomReplacementHash =
                texture.CustomReplacementHash;
            snapshot.CustomReplacementPending =
                texture.CustomReplacementPending;
            snapshot.CustomReplacementReady =
                texture.CustomReplacementReady;
            if (texture.Uploaded) {
                for (uint32_t level = 0U; level < texture.MipLevels;
                     ++level) {
                    auto mip = CaptureNativePicaImageBytes(
                        texture.Image,
                        std::max(1U, texture.Width >> level),
                        std::max(1U, texture.Height >> level), 4U,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT, level);
                    snapshot.PixelBytes.insert(snapshot.PixelBytes.end(),
                                               mip.begin(), mip.end());
                }
            } else {
                snapshot.PixelBytes = texture.Rgba8;
            }
            if (snapshot.PixelBytes.size() != *expectedBytes) {
                throw std::runtime_error(
                    "native PICA cached texture payload is invalid");
            }
            snapshots.push_back(std::move(snapshot));
        }
        // Immutable copy producers travel in the same vector: each carries the
        // full captured GPU image at its internal extent plus the identity and
        // span table a resumed plan's references resolve against.
        for (const auto& [identity, surface] : mGpuTextureCopySurfaces) {
            if (surface.Image == VK_NULL_HANDLE || surface.ImageWidth == 0U ||
                surface.ImageHeight == 0U) {
                continue;
            }
            GfxNativePicaTextureCacheEntrySnapshot snapshot;
            snapshot.HasCopyProducer = true;
            snapshot.CopyProducer.RenderTargetNamespace =
                surface.RenderTargetNamespace;
            snapshot.CopyProducer.CompletionId = surface.CompletionId;
            snapshot.CopyProducer.AfterDrawSubmissionId =
                surface.AfterDrawSubmissionId;
            snapshot.CopyProducer.SourcePhysicalAddress =
                surface.SourceSurfaceAddress;
            snapshot.CopyProducer.SourceWidth = surface.SourceWidth;
            snapshot.CopyProducer.SourceHeight = surface.SourceHeight;
            snapshot.CopyProducer.SourceNativeFormat = surface.SourceFormat;
            snapshot.CopyProducer.Spans = surface.Spans;
            snapshot.CopyProducer.ColorEncoding =
                static_cast<uint8_t>(surface.Encoding);
            snapshot.CopyProducer.SceneEffectsResolved =
                surface.SceneEffectsResolved;
            // Stored at the source's guest extent; the sampler maps spans
            // through the image extent, whatever it is.
            snapshot.ImageWidth = surface.SourceWidth;
            snapshot.ImageHeight = surface.SourceHeight;
            snapshot.ImageFormat = GfxNativePicaTextureSnapshotFormat::Rgba8;
            snapshot.MipLevels = 1U;
            const auto pixels = CaptureNativePicaImageBytes(
                surface.Image, surface.ImageWidth, surface.ImageHeight, 4U,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_READ_BIT);
            if (pixels.size() !=
                static_cast<size_t>(surface.ImageWidth) * surface.ImageHeight *
                    4U) {
                throw std::runtime_error(
                    "native PICA copy producer capture is incomplete");
            }
            snapshot.PixelBytes = Renderer3ds::ResamplePicaRgba8(
                pixels, surface.ImageWidth, surface.ImageHeight,
                surface.SourceWidth, surface.SourceHeight);
            snapshots.push_back(std::move(snapshot));
        }
        return true;
    } catch (const std::exception& exception) {
        snapshots.clear();
        SetNativeError(error, exception.what());
        return false;
    }
}

bool GfxRenderingAPIVulkan::RestorePicaTextureCache(
    std::span<const GfxNativePicaTextureCacheEntrySnapshot> snapshots,
    std::string* error) {
    try {
        if (mDevice == VK_NULL_HANDLE || mFrameActive || mFrameSubmitted) {
            throw std::runtime_error(
                "native PICA texture restore requires an idle frame boundary");
        }
        if (std::any_of(
                mNativePicaTextures.begin(), mNativePicaTextures.end(),
                [](const auto& entry) {
                    return entry.second.LiveRenderTargetAlias;
                })) {
            throw std::runtime_error(
                "native PICA texture restore requires reset renderer state");
        }
        std::set<NativePicaTextureKey> identities;
        for (const auto& snapshot : snapshots) {
            if (snapshot.HasCopyProducer) {
                const auto& producer = snapshot.CopyProducer;
                // Namespace 0 is the valid default namespace, not a sentinel.
                if (producer.CompletionId == 0U || producer.SourceWidth == 0U ||
                    producer.SourceHeight == 0U ||
                    producer.SourceNativeFormat > 4U || producer.Spans.empty() ||
                    snapshot.ImageWidth == 0U || snapshot.ImageHeight == 0U ||
                    snapshot.ImageFormat !=
                        GfxNativePicaTextureSnapshotFormat::Rgba8 ||
                    snapshot.MipLevels != 1U ||
                    snapshot.PixelBytes.size() !=
                        static_cast<size_t>(snapshot.ImageWidth) *
                            snapshot.ImageHeight * 4U) {
                    throw std::runtime_error(
                        "native PICA copy producer snapshot is incompatible");
                }
                continue;
            }
            const auto expectedBytes = NativePicaDecodedMipChainSize(
                snapshot.ImageWidth, snapshot.ImageHeight,
                snapshot.MipLevels);
            const uint8_t imageFormat =
                static_cast<uint8_t>(snapshot.ImageFormat);
            NativePicaTextureKey key;
            key.ContentHash = snapshot.ContentHash;
            key.ReplacementGeneration = snapshot.ReplacementGeneration;
            key.PhysicalAddress = snapshot.PhysicalAddress;
            key.Width = snapshot.SourceWidth;
            key.Height = snapshot.SourceHeight;
            key.Format = snapshot.NativeFormat;
            key.Type = snapshot.NativeType;
            key.WrapS = snapshot.NativeWrapS;
            key.WrapT = snapshot.NativeWrapT;
            key.MinLinear = snapshot.MinLinear;
            key.MagLinear = snapshot.MagLinear;
            key.MipLinear = snapshot.MipLinear;
            key.LodBiasRaw = snapshot.LodBiasRaw;
            key.MinMipLevel = snapshot.MinMipLevel;
            key.MaxMipLevel = snapshot.MaxMipLevel;
            key.CustomReplacement = snapshot.CustomReplacement;
            if (snapshot.SourceWidth == 0U ||
                snapshot.SourceHeight == 0U ||
                snapshot.ImageWidth == 0U ||
                snapshot.ImageHeight == 0U ||
                snapshot.MipLevels == 0U ||
                !expectedBytes.has_value() ||
                imageFormat > static_cast<uint8_t>(
                                  GfxNativePicaTextureSnapshotFormat::R32Uint) ||
                (snapshot.ImageFormat ==
                         GfxNativePicaTextureSnapshotFormat::R32Uint &&
                 (snapshot.NativeType != 2U ||
                  snapshot.CustomReplacement)) ||
                (snapshot.ImageFormat ==
                         GfxNativePicaTextureSnapshotFormat::R32Uint &&
                 snapshot.MipLevels != 1U) ||
                *expectedBytes != snapshot.PixelBytes.size() ||
                !identities.insert(key).second) {
                throw std::runtime_error(
                    "native PICA texture snapshot is incompatible");
            }
        }
        for (auto& [key, texture] : mNativePicaTextures) {
            DestroyTexture(texture);
        }
        mNativePicaTextures.clear();
        // The snapshot's records keep their own texture-pack generation; the
        // next frame retires those of another generation.
        mNativePicaTexturesGeneration.reset();
        DestroyGpuTextureCopySurfaces();
        for (const auto& snapshot : snapshots) {
            if (snapshot.HasCopyProducer) {
                // Rebuild the immutable version under the same register key a
                // resumed plan's references resolve against. The image itself is
                // created in the next active frame, where an upload path exists.
                const auto& producer = snapshot.CopyProducer;
                GpuTextureCopySurface surface;
                surface.Identity = ++mGpuTextureCopySerial;
                surface.RenderTargetNamespace = producer.RenderTargetNamespace;
                surface.CompletionId = producer.CompletionId;
                surface.AfterDrawSubmissionId = producer.AfterDrawSubmissionId;
                // The declared destination surface is not part of the persisted
                // identity: the sampler's own layout defines it. Keep the bytes
                // consistent with the source geometry for the live sweep that
                // still consults these fields.
                const uint32_t restoredBpp =
                    producer.SourceNativeFormat == 0U   ? 4U
                    : producer.SourceNativeFormat == 1U ? 3U
                                                        : 2U;
                surface.DestinationAddress = producer.SourcePhysicalAddress;
                surface.DestinationBytes = producer.SourceWidth *
                                           producer.SourceHeight * restoredBpp;
                surface.TextureWidth = producer.SourceWidth;
                surface.TextureHeight = producer.SourceHeight;
                surface.TextureFormat = producer.SourceNativeFormat;
                surface.SourceSurfaceAddress = producer.SourcePhysicalAddress;
                surface.SourceWidth = producer.SourceWidth;
                surface.SourceHeight = producer.SourceHeight;
                surface.SourceFormat = producer.SourceNativeFormat;
                surface.Spans = producer.Spans;
                surface.ImageWidth = snapshot.ImageWidth;
                surface.ImageHeight = snapshot.ImageHeight;
                surface.Encoding =
                    static_cast<Oot3d::SceneColorEncoding>(
                        producer.ColorEncoding);
                surface.SceneEffectsResolved =
                    producer.SceneEffectsResolved;
                surface.PendingPixels = snapshot.PixelBytes;
                const uint64_t identity = surface.Identity;
                mGpuTextureCopySurfaces.emplace(identity, std::move(surface));
                mGpuTextureCopyProducers[{producer.RenderTargetNamespace,
                                          producer.CompletionId,
                                          producer.AfterDrawSubmissionId}] =
                    identity;
                ++mGpuTextureCopySamplerCost.ProducerRestores;
                continue;
            }
            NativePicaTextureKey key;
            key.ContentHash = snapshot.ContentHash;
            key.ReplacementGeneration = snapshot.ReplacementGeneration;
            key.PhysicalAddress = snapshot.PhysicalAddress;
            key.Width = snapshot.SourceWidth;
            key.Height = snapshot.SourceHeight;
            key.Format = snapshot.NativeFormat;
            key.Type = snapshot.NativeType;
            key.WrapS = snapshot.NativeWrapS;
            key.WrapT = snapshot.NativeWrapT;
            key.MinLinear = snapshot.MinLinear;
            key.MagLinear = snapshot.MagLinear;
            key.MipLinear = snapshot.MipLinear;
            key.LodBiasRaw = snapshot.LodBiasRaw;
            key.MinMipLevel = snapshot.MinMipLevel;
            key.MaxMipLevel = snapshot.MaxMipLevel;
            key.CustomReplacement = snapshot.CustomReplacement;
            TextureRecord texture;
            texture.Width = snapshot.ImageWidth;
            texture.Height = snapshot.ImageHeight;
            texture.MipLevels = snapshot.MipLevels;
            texture.Rgba8 = snapshot.PixelBytes;
            texture.CustomReplacementApplied = snapshot.CustomReplacement;
            if (snapshot.CustomReplacement)
                texture.CustomReplacementPixelHash = ::Oot3d::Renderer::AzaharCityHash64(texture.Rgba8);
            texture.SamplerState.MinFilter = snapshot.NativeType != 2U
                ? NativePicaMinFilter(snapshot.MinLinear,
                                      snapshot.MipLinear,
                                      snapshot.MipLevels)
                : GfxNativeTextureFilter::Nearest;
            texture.SamplerState.MagFilter =
                snapshot.NativeType != 2U && snapshot.MagLinear
                    ? GfxNativeTextureFilter::Linear
                    : GfxNativeTextureFilter::Nearest;
            texture.SamplerState.WrapS =
                DecodeNativePicaWrap(snapshot.NativeWrapS);
            texture.SamplerState.WrapT =
                DecodeNativePicaWrap(snapshot.NativeWrapT);
            texture.SamplerState.LodBias = 0.0F;
            texture.SamplerState.MinMipLevel = snapshot.NativeType != 2U
                ? snapshot.MinMipLevel : 0U;
            texture.SamplerState.MaxMipLevel = snapshot.NativeType != 2U
                ? snapshot.MaxMipLevel : 0U;
            texture.CustomReplacementHash =
                snapshot.CustomReplacementHash;
            texture.CustomReplacementPending =
                snapshot.CustomReplacementPending;
            texture.CustomReplacementReady =
                snapshot.CustomReplacementReady;
            mNativePicaTextures.emplace(key, std::move(texture));
        }
        return true;
    } catch (const std::exception& exception) {
        SetNativeError(error, exception.what());
        return false;
    }
}

bool GfxRenderingAPIVulkan::CapturePicaColorTargets(
    std::vector<GfxNativePicaRenderTargetColorSnapshot>& snapshots,
    std::string* error) {
    snapshots.clear();
    try {
        if (mDevice == VK_NULL_HANDLE || mFrameActive || mFrameSubmitted) {
            throw std::runtime_error(
                "native PICA color capture requires an idle frame boundary");
        }
        if (mNativePicaSampleCount != VK_SAMPLE_COUNT_1_BIT) {
            throw std::runtime_error(
                "native PICA color capture does not yet support multisampled targets");
        }
        WaitForAllPresents();
        CheckNativeVk(vkDeviceWaitIdle(mDevice),
                      "vkDeviceWaitIdle(native PICA color capture)");
        for (const auto& [key, target] : mNativePicaRenderTargets) {
            if (target.ColorImage == VK_NULL_HANDLE) {
                continue;
            }
            GfxNativePicaRenderTargetColorSnapshot snapshot;
            // The saved colour carries the resolved scene effects only while the
            // last write to this target is the one that produced them; a sticky
            // flag would suppress the scene stages for unrelated content.
            snapshot.SceneEffectsResolved =
                target.SceneEffectsResolvedSerial != 0U &&
                target.SceneEffectsResolvedSerial == target.ContentWriteSerial;
            snapshot.RenderTargetNamespace = key.RenderTargetNamespace;
            snapshot.ColorPhysicalAddress = key.ColorPhysicalAddress;
            snapshot.DepthPhysicalAddress = key.DepthPhysicalAddress;
            snapshot.FramebufferWidth = key.Width;
            snapshot.FramebufferHeight = key.Height;
            snapshot.FramebufferColorFormat = key.ColorFormat;
            snapshot.FramebufferDepthFormat = key.DepthFormat;
            // Saved at the guest framebuffer extent with colour, depth and
            // stencil only: the enhancement guides and the shadow mask are
            // functions of the restoring renderer's settings and start neutral.
            snapshot.RenderScalePermille =
                Renderer3ds::kPicaGuestExtentRenderScalePermille;
            snapshot.ImageWidth = key.Width;
            snapshot.ImageHeight = key.Height;
            snapshot.SampleCount = 1U;
            snapshot.WBuffering = target.WBuffering;
            const auto color = CaptureNativePicaImageBytes(
                target.ColorImage, target.Width, target.Height, 4U,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            snapshot.ColorRgba8 = Renderer3ds::ResamplePicaRgba8(
                color, target.Width, target.Height, key.Width, key.Height);
            std::vector<float> depth;
            std::vector<uint8_t> stencil;
            CaptureNativePicaDepthStencilImage(
                target.DepthImage, target.Width, target.Height, depth,
                stencil);
            snapshot.DepthValues = Renderer3ds::ResamplePicaNearest<float>(
                depth, target.Width, target.Height, key.Width, key.Height);
            if (!stencil.empty()) {
                snapshot.StencilValues =
                    Renderer3ds::ResamplePicaNearest<uint8_t>(
                        stencil, target.Width, target.Height, key.Width,
                        key.Height);
            }
            snapshots.push_back(std::move(snapshot));
        }
        return true;
    } catch (const std::exception& exception) {
        snapshots.clear();
        SetNativeError(error, exception.what());
        return false;
    }
}

bool GfxRenderingAPIVulkan::RestorePicaColorTargets(
    std::span<const GfxNativePicaRenderTargetColorSnapshot> snapshots,
    std::string* error) {
    try {
        if (mDevice == VK_NULL_HANDLE || mFrameActive || mFrameSubmitted) {
            throw std::runtime_error(
                "native PICA color restore requires an idle frame boundary");
        }
        if (!mNativePicaRenderTargets.empty()) {
            throw std::runtime_error(
                "native PICA color restore requires reset renderer state");
        }
        if (mNativePicaSampleCount != VK_SAMPLE_COUNT_1_BIT) {
            throw std::runtime_error(
                "native PICA color restore does not yet support multisampled targets");
        }
        // Snapshots at the guest extent carry no renderer scale; legacy ones
        // were saved at a renderer's internal extent.
        const bool guestExtent =
            !snapshots.empty() &&
            snapshots.front().RenderScalePermille ==
                Renderer3ds::kPicaGuestExtentRenderScalePermille;
        // A state loaded at startup is restored before the first frame applies
        // the settings, so the targets are created at the settings' scale and
        // that frame keeps them. A legacy state at another scale is restored at
        // its own scale and the first frame replaces it, as any scale change.
        const float settingsScale = Oot3d::ResolveInternalResolutionScale(
            Oot3d::GraphicsSettingsRuntime::Instance().SnapshotForRendering().Value);
        float restoreScale = mInternalResolutionScale;
        if (guestExtent) {
            restoreScale = settingsScale;
        } else if (!snapshots.empty()) {
            const uint16_t savedScale = snapshots.front().RenderScalePermille;
            restoreScale = savedScale == static_cast<uint16_t>(std::lround(settingsScale * 1000.0F))
                ? settingsScale
                : static_cast<float>(savedScale) / 1000.0F;
        }
        if (restoreScale > 0.0F && std::abs(restoreScale - mInternalResolutionScale) > 0.0005F) {
            ApplyInternalResolutionScale(restoreScale);
        }
        const uint16_t currentScale = static_cast<uint16_t>(std::lround(
            mInternalResolutionScale * 1000.0F));
        std::set<std::tuple<uint64_t, uint32_t, uint32_t, uint16_t,
                            uint16_t, uint8_t, uint8_t, uint16_t>>
            identities;
        for (const auto& snapshot : snapshots) {
            const uint64_t expectedBytes =
                static_cast<uint64_t>(snapshot.ImageWidth) *
                snapshot.ImageHeight * 4U;
            const uint64_t expectedPixels =
                static_cast<uint64_t>(snapshot.ImageWidth) *
                snapshot.ImageHeight;
            const uint16_t expectedScale =
                guestExtent ? Renderer3ds::kPicaGuestExtentRenderScalePermille
                            : currentScale;
            if (snapshot.RenderScalePermille != expectedScale) {
                throw std::runtime_error(
                    "native PICA color snapshots were saved at different render scales (" +
                    std::to_string(snapshot.RenderScalePermille) + " and " +
                    std::to_string(expectedScale) + "/1000)");
            }
            const bool guidesValid =
                guestExtent
                    ? snapshot.ImageWidth == snapshot.FramebufferWidth &&
                          snapshot.ImageHeight == snapshot.FramebufferHeight &&
                          snapshot.NormalGuideRgba8.empty() &&
                          snapshot.MaterialGuideRgba8.empty() &&
                          snapshot.RigidMotionGuideRgba16FloatLe.empty() &&
                          snapshot.AmbientGuideRgba8.empty() &&
                          snapshot.ShadowR32UintLe.empty()
                    : expectedBytes == snapshot.NormalGuideRgba8.size() &&
                          expectedBytes == snapshot.MaterialGuideRgba8.size() &&
                          expectedPixels * 8U ==
                              snapshot.RigidMotionGuideRgba16FloatLe.size() &&
                          expectedBytes == snapshot.AmbientGuideRgba8.size() &&
                          expectedBytes == snapshot.ShadowR32UintLe.size();
            if (snapshot.ColorPhysicalAddress == 0U ||
                snapshot.FramebufferWidth == 0U ||
                snapshot.FramebufferHeight == 0U ||
                snapshot.ImageWidth == 0U || snapshot.ImageHeight == 0U ||
                snapshot.SampleCount != 1U ||
                expectedBytes != snapshot.ColorRgba8.size() || !guidesValid ||
                expectedPixels != snapshot.DepthValues.size() ||
                (!snapshot.StencilValues.empty() &&
                 expectedPixels != snapshot.StencilValues.size()) ||
                !identities
                     .insert({snapshot.RenderTargetNamespace,
                              snapshot.ColorPhysicalAddress,
                              snapshot.DepthPhysicalAddress,
                              snapshot.FramebufferWidth,
                              snapshot.FramebufferHeight,
                              snapshot.FramebufferColorFormat,
                              snapshot.FramebufferDepthFormat,
                              snapshot.RenderScalePermille})
                     .second) {
                throw std::runtime_error(
                    "native PICA color snapshot is incompatible");
            }
        }
        constexpr VkImageLayout kColorLayout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        constexpr VkPipelineStageFlags kColorStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        constexpr VkAccessFlags kColorAccess =
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        constexpr VkAccessFlags kShadowAccess =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        for (const auto& snapshot : snapshots) {
            GfxNativePicaDrawView targetDescription;
            targetDescription.RenderTargetNamespace =
                snapshot.RenderTargetNamespace;
            targetDescription.FramebufferWidth = snapshot.FramebufferWidth;
            targetDescription.FramebufferHeight = snapshot.FramebufferHeight;
            targetDescription.FramebufferColorPhysicalAddress =
                snapshot.ColorPhysicalAddress;
            targetDescription.FramebufferDepthPhysicalAddress =
                snapshot.DepthPhysicalAddress;
            targetDescription.FramebufferColorFormat =
                snapshot.FramebufferColorFormat;
            targetDescription.FramebufferDepthFormat =
                snapshot.FramebufferDepthFormat;
            auto& target = guestExtent
                ? GetOrCreateNativePicaRenderTarget(targetDescription)
                : GetOrCreateNativePicaRenderTarget(
                      targetDescription, snapshot.ImageWidth,
                      snapshot.ImageHeight);
            if (!guestExtent && (target.Width != snapshot.ImageWidth ||
                                 target.Height != snapshot.ImageHeight)) {
                throw std::runtime_error(
                    "native PICA color snapshot render extent changed");
            }
            const auto restoreColorAttachment =
                [&](VkImage image, uint32_t bytesPerPixel,
                    std::span<const uint8_t> bytes) {
                    RestoreNativePicaImageBytes(
                        image, target.Width, target.Height, bytesPerPixel,
                        bytes, kColorLayout, kColorStage, kColorAccess,
                        kColorLayout, kColorStage, kColorAccess);
                };
            // The restored colour becomes this target's current content version.
            // If it was saved as resolved content, the provenance answers true
            // immediately - including the first presentation of a restored
            // checkpoint, before any further native draw - and any later draw,
            // clear or memory fill of this target invalidates it again.
            target.ContentWriteSerial = ++mGpuTextureCopyWriteSerial;
            target.SceneEffectsResolvedSerial =
                snapshot.SceneEffectsResolved ? target.ContentWriteSerial : 0U;
            if (guestExtent) {
                restoreColorAttachment(
                    target.ColorImage, 4U,
                    Renderer3ds::ResamplePicaRgba8(
                        snapshot.ColorRgba8, snapshot.ImageWidth,
                        snapshot.ImageHeight, target.Width, target.Height));
                // Neutral guides: what the canonical colour-only target
                // reports, until the next draw writes the enhanced ones.
                if (!target.Attachments.NativeColorOnly()) {
                    ClearNativePicaColorImage(
                        target.NormalGuideImage,
                        {{128.0F / 255.0F, 128.0F / 255.0F, 1.0F, 0.0F}},
                        kColorLayout, kColorStage, kColorAccess);
                    ClearNativePicaColorImage(
                        target.MaterialGuideImage, {{0.0F, 0.0F, 0.0F, 0.0F}},
                        kColorLayout, kColorStage, kColorAccess);
                    ClearNativePicaColorImage(
                        target.RigidMotionGuideImage,
                        {{0.0F, 0.0F, 0.0F, 0.0F}}, kColorLayout,
                        kColorStage, kColorAccess);
                    ClearNativePicaColorImage(
                        target.AmbientGuideImage, {{1.0F, 1.0F, 1.0F, 0.0F}},
                        kColorLayout, kColorStage, kColorAccess);
                }
                VkClearColorValue noShadow{};
                noShadow.uint32[0] = 0xFFFFFFFFU;
                ClearNativePicaColorImage(
                    target.ShadowImage, noShadow, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, kShadowAccess);
                const auto depth = Renderer3ds::ResamplePicaNearest<float>(
                    snapshot.DepthValues, snapshot.ImageWidth,
                    snapshot.ImageHeight, target.Width, target.Height);
                std::vector<uint8_t> stencil;
                if (!snapshot.StencilValues.empty()) {
                    stencil = Renderer3ds::ResamplePicaNearest<uint8_t>(
                        snapshot.StencilValues, snapshot.ImageWidth,
                        snapshot.ImageHeight, target.Width, target.Height);
                }
                RestoreNativePicaDepthStencilImage(
                    target.DepthImage, target.Width, target.Height, depth,
                    stencil);
            } else {
                restoreColorAttachment(
                    target.ColorImage, 4U, snapshot.ColorRgba8);
                if (!target.Attachments.NativeColorOnly()) {
                    restoreColorAttachment(
                        target.NormalGuideImage, 4U,
                        snapshot.NormalGuideRgba8);
                    restoreColorAttachment(
                        target.MaterialGuideImage, 4U,
                        snapshot.MaterialGuideRgba8);
                    restoreColorAttachment(
                        target.RigidMotionGuideImage, 8U,
                        snapshot.RigidMotionGuideRgba16FloatLe);
                    restoreColorAttachment(
                        target.AmbientGuideImage, 4U,
                        snapshot.AmbientGuideRgba8);
                }
                RestoreNativePicaImageBytes(
                    target.ShadowImage, snapshot.ImageWidth,
                    snapshot.ImageHeight, 4U, snapshot.ShadowR32UintLe,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, kShadowAccess,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, kShadowAccess);
                RestoreNativePicaDepthStencilImage(
                    target.DepthImage, snapshot.ImageWidth,
                    snapshot.ImageHeight, snapshot.DepthValues,
                    snapshot.StencilValues);
            }
            target.WBuffering = snapshot.WBuffering;
        }
        return true;
    } catch (const std::exception& exception) {
        SetNativeError(error, exception.what());
        return false;
    }
}

bool GfxRenderingAPIVulkan::CapturePicaPresentationState(
    GfxNativePicaPresentationStateSnapshot& snapshot,
    std::string* error) {
    snapshot = {};
    try {
        if (mDevice == VK_NULL_HANDLE || mFrameActive || mFrameSubmitted) {
            throw std::runtime_error(
                "native PICA presentation capture requires an idle frame boundary");
        }
        WaitForAllPresents();
        CheckNativeVk(vkDeviceWaitIdle(mDevice),
                      "vkDeviceWaitIdle(native PICA presentation capture)");
        for (const auto& [key, image] : mNativePicaDisplayImages) {
            GfxNativePicaDisplayImageColorSnapshot imageSnapshot;
            imageSnapshot.RenderTargetNamespace = key.first;
            imageSnapshot.OutputPhysicalAddress = key.second;
            // Stored at the guest output extent of its transfer; the next
            // transfer recreates it at the restoring renderer's extent.
            const bool guestExtentKnown =
                image.GuestWidth != 0U && image.GuestHeight != 0U;
            imageSnapshot.ImageWidth =
                guestExtentKnown ? image.GuestWidth : image.Width;
            imageSnapshot.ImageHeight =
                guestExtentKnown ? image.GuestHeight : image.Height;
            imageSnapshot.Initialized = image.Initialized;
            if (image.Initialized) {
                imageSnapshot.ColorRgba8 = Renderer3ds::ResamplePicaRgba8(
                    CaptureNativePicaImageBytes(
                        image.Image, image.Width, image.Height, 4U,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT),
                    image.Width, image.Height, imageSnapshot.ImageWidth,
                    imageSnapshot.ImageHeight);
            }
            const auto depthAssociation =
                mNativePicaDisplayDepthTargets.find(key);
            if (depthAssociation != mNativePicaDisplayDepthTargets.end()) {
                const auto& target = depthAssociation->second;
                imageSnapshot.HasDepthTarget = true;
                imageSnapshot.DepthTargetNamespace =
                    target.RenderTargetNamespace;
                imageSnapshot.DepthTargetColorPhysicalAddress =
                    target.ColorPhysicalAddress;
                imageSnapshot.DepthTargetDepthPhysicalAddress =
                    target.DepthPhysicalAddress;
                imageSnapshot.DepthTargetFramebufferWidth = target.Width;
                imageSnapshot.DepthTargetFramebufferHeight = target.Height;
                imageSnapshot.DepthTargetFramebufferColorFormat =
                    target.ColorFormat;
                imageSnapshot.DepthTargetFramebufferDepthFormat =
                    target.DepthFormat;
                // The associated target is saved at the guest extent too.
                imageSnapshot.DepthTargetRenderScalePermille =
                    Renderer3ds::kPicaGuestExtentRenderScalePermille;
            }
            snapshot.DisplayImages.push_back(std::move(imageSnapshot));
        }
        snapshot.LastPresentedTransfer =
            mLastPresentedNativePicaDisplayTransfer;
        return true;
    } catch (const std::exception& exception) {
        snapshot = {};
        SetNativeError(error, exception.what());
        return false;
    }
}

bool GfxRenderingAPIVulkan::RestorePicaPresentationState(
    const GfxNativePicaPresentationStateSnapshot& snapshot,
    std::string* error) {
    try {
        if (mDevice == VK_NULL_HANDLE || mFrameActive || mFrameSubmitted) {
            throw std::runtime_error(
                "native PICA presentation restore requires an idle frame boundary");
        }
        if (!mNativePicaDisplayImages.empty() ||
            !mNativePicaDisplayDepthTargets.empty() ||
            mLastPresentedNativePicaDisplayTransfer.has_value()) {
            throw std::runtime_error(
                "native PICA presentation restore requires reset renderer state");
        }
        // A display image's depth association names a target saved at the
        // guest extent; that target was restored at the current scale.
        const uint16_t currentScale = static_cast<uint16_t>(
            std::lround(mInternalResolutionScale * 1000.0F));
        const auto restoredScale = [currentScale](uint16_t saved) {
            return saved == Renderer3ds::kPicaGuestExtentRenderScalePermille
                       ? currentScale
                       : saved;
        };
        std::set<std::pair<uint64_t, uint32_t>> identities;
        for (const auto& image : snapshot.DisplayImages) {
            const uint64_t expectedBytes =
                static_cast<uint64_t>(image.ImageWidth) *
                image.ImageHeight * 4U;
            if (image.OutputPhysicalAddress == 0U ||
                image.ImageWidth == 0U || image.ImageHeight == 0U ||
                (image.Initialized
                     ? expectedBytes != image.ColorRgba8.size()
                     : !image.ColorRgba8.empty()) ||
                !identities
                     .insert({image.RenderTargetNamespace,
                              image.OutputPhysicalAddress})
                     .second) {
                throw std::runtime_error(
                    "native PICA display image snapshot is incompatible");
            }
            if (image.HasDepthTarget) {
                const NativePicaRenderTargetKey targetKey{
                    image.DepthTargetNamespace,
                    image.DepthTargetColorPhysicalAddress,
                    image.DepthTargetDepthPhysicalAddress,
                    image.DepthTargetFramebufferWidth,
                    image.DepthTargetFramebufferHeight,
                    image.DepthTargetFramebufferColorFormat,
                    image.DepthTargetFramebufferDepthFormat,
                    restoredScale(image.DepthTargetRenderScalePermille)};
                if (mNativePicaRenderTargets.find(targetKey) ==
                    mNativePicaRenderTargets.end()) {
                    throw std::runtime_error(
                        "native PICA display depth association is unavailable");
                }
            }
        }
        if (snapshot.LastPresentedTransfer.has_value()) {
            const auto& transfer = *snapshot.LastPresentedTransfer;
            const auto displayKey = std::make_pair(
                transfer.RenderTargetNamespace,
                transfer.OutputPhysicalAddress);
            const auto displaySnapshot = std::find_if(
                snapshot.DisplayImages.begin(),
                snapshot.DisplayImages.end(),
                [&](const auto& image) {
                    return image.RenderTargetNamespace ==
                               displayKey.first &&
                           image.OutputPhysicalAddress ==
                               displayKey.second &&
                           image.Initialized;
                });
            if (transfer.CompletionId == 0U ||
                transfer.InputPhysicalAddress == 0U ||
                transfer.OutputPhysicalAddress == 0U ||
                transfer.InputWidth == 0U || transfer.InputHeight == 0U ||
                transfer.OutputWidth == 0U || transfer.OutputHeight == 0U ||
                static_cast<uint8_t>(transfer.PresentationMode) >
                    static_cast<uint8_t>(
                        GfxNativePicaPresentationMode::AlphaOverlay) ||
                displaySnapshot == snapshot.DisplayImages.end()) {
                throw std::runtime_error(
                    "native PICA last presentation snapshot is incompatible");
            }
        }

        for (const auto& imageSnapshot : snapshot.DisplayImages) {
            auto& image = GetOrCreateNativePicaDisplayImage(
                imageSnapshot.RenderTargetNamespace,
                imageSnapshot.OutputPhysicalAddress,
                imageSnapshot.ImageWidth, imageSnapshot.ImageHeight);
            if (imageSnapshot.Initialized) {
                RestoreNativePicaImageBytes(
                    image.Image, image.Width, image.Height, 4U,
                    imageSnapshot.ColorRgba8, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0U,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT);
            }
            image.Initialized = imageSnapshot.Initialized;
            image.GuestWidth = imageSnapshot.ImageWidth;
            image.GuestHeight = imageSnapshot.ImageHeight;
            if (imageSnapshot.HasDepthTarget) {
                mNativePicaDisplayDepthTargets[{
                    imageSnapshot.RenderTargetNamespace,
                    imageSnapshot.OutputPhysicalAddress}] = {
                    imageSnapshot.DepthTargetNamespace,
                    imageSnapshot.DepthTargetColorPhysicalAddress,
                    imageSnapshot.DepthTargetDepthPhysicalAddress,
                    imageSnapshot.DepthTargetFramebufferWidth,
                    imageSnapshot.DepthTargetFramebufferHeight,
                    imageSnapshot.DepthTargetFramebufferColorFormat,
                    imageSnapshot.DepthTargetFramebufferDepthFormat,
                    restoredScale(imageSnapshot.DepthTargetRenderScalePermille)};
            }
        }
        mLastPresentedNativePicaDisplayTransfer =
            snapshot.LastPresentedTransfer;
        return true;
    } catch (const std::exception& exception) {
        for (auto& [key, image] : mNativePicaDisplayImages) {
            DestroyNativePicaDisplayImage(image);
        }
        mNativePicaDisplayImages.clear();
        DestroyNativePicaSceneBeforeUiImages();
        mNativePicaDisplayDepthTargets.clear();
        mLastPresentedNativePicaDisplayTransfer.reset();
        SetNativeError(error, exception.what());
        return false;
    }
}

bool GfxRenderingAPIVulkan::QueuePicaCompletion(
    uint64_t completionId, std::string* error) {
    if (!mFrameActive || completionId == 0U) {
        SetNativeError(
            error,
            "native PICA completion queued outside an active Vulkan frame");
        return false;
    }
    mFrameResources[mCurrentFrame].NativePicaCompletionIds.push_back(
        completionId);
    return true;
}

std::vector<uint64_t>
GfxRenderingAPIVulkan::TakePicaCompletions() {
    auto completions = std::move(mCompletedNativePicaIds);
    mCompletedNativePicaIds.clear();
    return completions;
}

bool GfxRenderingAPIVulkan::ResetPicaState(
    std::string* error) {
    try {
        EndNativePicaRenderPass();
        CheckNativeVk(vkDeviceWaitIdle(mDevice),
                      "vkDeviceWaitIdle(native PICA state reset)");
        mBrixelizerScene.Reset();
        DestroyNativePicaGeometryResources();
            ForgetNativePicaEffectNriTextures();
        mAmbientOcclusionPass.InvalidateScreenResources();
        mCacaoPass.InvalidateScreenResources();
        mMxaoPass.InvalidateScreenResources();
        mFidelityFxSssrPass.InvalidateScreenResources();
        mBrixelizerGiPass.InvalidateScreenResources();
        mScreenSpaceGi.InvalidateScreenResources();
        mReflectionMaterialResolvePass.InvalidateScreenResources();
        mReflectionIblPass.InvalidateProfile();
        mLinearSceneColorPass.InvalidateScreenResources();
        mHiZDepthPyramidPass.InvalidateScreenResources();
        if (mHiZReflectionSurface.has_value()) {
            mSceneSurfaces.Retire(*mHiZReflectionSurface);
            mHiZReflectionSurface.reset();
        }
        mHiZReflectionPass.InvalidateScreenResources();
        if (mMotionSurface.has_value()) {
            mSceneSurfaces.Retire(*mMotionSurface);
            mMotionSurface.reset();
        }
        mMotionVectorPass.InvalidateScreenResources();
        if (mLinearColorSurface.has_value()) {
            mSceneSurfaces.Retire(*mLinearColorSurface);
            mLinearColorSurface.reset();
        }
        mNriUpscalerPass.InvalidateScreenResources();
        mUpscalerGuidePass.InvalidateScreenResources();
        mNativeUiOverlayPass.InvalidateScreenResources();
        if (mCompositeSurface.has_value()) {
            mSceneSurfaces.Retire(*mCompositeSurface);
            mCompositeSurface.reset();
        }
        mSceneCompositePass.InvalidateScreenResources();
        mSmaa1xPass.InvalidateScreenResources();
        mNriEffectGraphTransientImageArena.InvalidateScreenResources();
        if (mTaaSurface.has_value()) {
            mSceneSurfaces.Retire(*mTaaSurface);
            mTaaSurface.reset();
        }
        mTemporalAaPass.InvalidateScreenResources();
        mTaaOutputValid = false;
        mHiZOutputValid = false;
        mAmbientOcclusionOutputValid = false;
        mInteractiveGrassPass.ResetTemporalState();
        Oot3d::GrassSceneBridge::Instance().Clear();
        mTemporalHistory.Reset();
        mPicaTemporalHistory.Reset();
        mNativePicaCompositionSchedule.Reset();
        for (auto texture = mNativePicaTextures.begin();
             texture != mNativePicaTextures.end();) {
            if (!texture->second.ImageBorrowed) {
                ++texture;
                continue;
            }
            DestroyTexture(texture->second);
            texture = mNativePicaTextures.erase(texture);
        }
        DestroyGpuTextureCopySurfaces();
        for (auto& [key, target] : mNativePicaRenderTargets) {
            DestroyNativePicaRenderTarget(target);
        }
        mNativePicaRenderTargets.clear();
        mNativePicaDisplayDepthTargets.clear();
        for (auto& [key, image] : mNativePicaDisplayImages) {
            DestroyNativePicaDisplayImage(image);
        }
        mNativePicaDisplayImages.clear();
        DestroyNativePicaSceneBeforeUiImages();
        mInvalidatedNativePicaRenderTargetAddresses.clear();
        mLastPresentedNativePicaDisplayTransfer.reset();
        mPendingNativePicaMemoryFills.clear();
        mPicaMemoryFillSmokeInjected = false;
        mCompletedNativePicaIds.clear();
        for (auto& frame : mFrameResources) {
            frame.NativePicaCompletionIds.clear();
        }
        mActiveNativePicaRenderTarget = nullptr;
        mNativePicaRenderPassActive = false;
        mNativePicaPresentedThisFrame = false;
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = std::string("native PICA state reset failed: ") +
                     exception.what();
        }
        return false;
    }
}

bool GfxRenderingAPIVulkan::ResetTitleState(std::string* error) {
    try {
        Oot3d::GrassSceneBridge::Instance().Clear();
        Oot3d::GrassInteractionBridge::Instance().Reset();
        Oot3d::VisualClock::Instance().Reset();
        Oot3d::SceneViewRuntime::Instance().Reset();
        mNativeSceneView.Reset();
        mPreparedShadowCasterView.Reset();
        mPicaScenePublications.Reset();
        return true;
    } catch (const std::exception& exception) {
        SetNativeError(error, exception.what());
        return false;
    }
}

} // namespace Fast

#endif
