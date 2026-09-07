#include "fast/backends/gfx_vulkan.h"

#ifdef ENABLE_OOT3D_VULKAN

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace Fast {
namespace {
void CheckAttachmentVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
} // namespace

// Native color/depth/Shadow2D and display-transfer images outlive effect modes.
// Grow only auxiliary storage, and never attach it to a canonical-only draw.
void GfxRenderingAPIVulkan::PrepareNativePicaTargetAttachments(NativePicaRenderTarget& target) {
    if (mFramePicaAttachmentRequirements.NativeColorOnly())
        return;
    const bool allocate = target.Attachments.NativeColorOnly();
    if (!allocate && !target.AuxiliaryNeedsClear)
        return;
    EndNativePicaRenderPass();
    if (mRenderPassActive) {
        vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
        mRenderPassActive = false;
        mOverlayRenderPassActive = false;
    }

    struct Slot {
        VkImage& Image;
        VkDeviceMemory& Memory;
        VkImageView& View;
        VkFormat Format;
        VkClearColorValue Clear;
        Oot3d::SceneSurfaceKind Kind;
    };
    const VkClearColorValue black{};
    const VkClearColorValue normal{ { 0.5F, 0.5F, 1.0F, 0.0F } };
    const VkClearColorValue ambient{ { 1.0F, 1.0F, 1.0F, 0.0F } };
    const VkClearColorValue invalid{ { 0.0F, 0.0F, 0.0F, 1.0F } };
    using Kind = Oot3d::SceneSurfaceKind;
    std::array<Slot, 12> slots{ {
        { target.NormalGuideImage, target.NormalGuideMemory, target.NormalGuideView, VK_FORMAT_R8G8B8A8_UNORM, normal,
          Kind::NormalGuide },
        { target.MaterialGuideImage, target.MaterialGuideMemory, target.MaterialGuideView, VK_FORMAT_R8G8B8A8_UNORM,
          black, Kind::MaterialGuide },
        { target.RigidMotionGuideImage, target.RigidMotionGuideMemory, target.RigidMotionGuideView,
          VK_FORMAT_R16G16B16A16_SFLOAT, black, Kind::RigidMotionGuide },
        { target.AmbientGuideImage, target.AmbientGuideMemory, target.AmbientGuideView, VK_FORMAT_R8G8B8A8_UNORM,
          ambient, Kind::AmbientGuide },
        { target.FogGuideImage, target.FogGuideMemory, target.FogGuideView, VK_FORMAT_R8G8B8A8_UNORM, invalid,
          Kind::FogGuide },
        { target.OutlineGeometryGuideImage, target.OutlineGeometryGuideMemory, target.OutlineGeometryGuideView,
          VK_FORMAT_R32G32B32A32_SFLOAT, invalid, Kind::OutlineGeometryGuide },
        { target.MsaaNormalGuideImage, target.MsaaNormalGuideMemory, target.MsaaNormalGuideView,
          VK_FORMAT_R8G8B8A8_UNORM, normal, Kind::NormalGuide },
        { target.MsaaMaterialGuideImage, target.MsaaMaterialGuideMemory, target.MsaaMaterialGuideView,
          VK_FORMAT_R8G8B8A8_UNORM, black, Kind::MaterialGuide },
        { target.MsaaRigidMotionGuideImage, target.MsaaRigidMotionGuideMemory, target.MsaaRigidMotionGuideView,
          VK_FORMAT_R16G16B16A16_SFLOAT, black, Kind::RigidMotionGuide },
        { target.MsaaAmbientGuideImage, target.MsaaAmbientGuideMemory, target.MsaaAmbientGuideView,
          VK_FORMAT_R8G8B8A8_UNORM, ambient, Kind::AmbientGuide },
        { target.MsaaFogGuideImage, target.MsaaFogGuideMemory, target.MsaaFogGuideView, VK_FORMAT_R8G8B8A8_UNORM,
          invalid, Kind::FogGuide },
        { target.MsaaOutlineGeometryGuideImage, target.MsaaOutlineGeometryGuideMemory,
          target.MsaaOutlineGeometryGuideView, VK_FORMAT_R32G32B32A32_SFLOAT, invalid, Kind::OutlineGeometryGuide },
    } };
    const uint32_t count = mNativePicaSampleCount == VK_SAMPLE_COUNT_1_BIT ? 6U : 12U;
    std::vector<VkImageMemoryBarrier> barriers;
    barriers.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
        auto& slot = slots[index];
        const bool msaa = index >= 6U;
        const auto samples = msaa ? mNativePicaSampleCount : VK_SAMPLE_COUNT_1_BIT;
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        (msaa ? 0U : VK_IMAGE_USAGE_SAMPLED_BIT);
        if (slot.Image == VK_NULL_HANDLE) {
            if (target.NriOwned) {
                Oot3d::NriOwnedTexture2DDesc desc;
                desc.Width = target.Width;
                desc.Height = target.Height;
                desc.Format = slot.Format;
                desc.Usage = usage;
                desc.Samples = samples;
                Oot3d::NriOwnedTexture2D texture;
                if (!mNriInterop.CreateOwnedTexture2D(desc, texture))
                    throw std::runtime_error("NRI auxiliary PICA image allocation failed");
                slot.Image = texture.Image;
            } else {
                VkImageCreateInfo desc{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
                desc.imageType = VK_IMAGE_TYPE_2D;
                desc.format = slot.Format;
                desc.extent = { target.Width, target.Height, 1 };
                desc.mipLevels = desc.arrayLayers = 1;
                desc.samples = samples;
                desc.tiling = VK_IMAGE_TILING_OPTIMAL;
                desc.usage = usage;
                CheckAttachmentVk(vkCreateImage(mDevice, &desc, nullptr, &slot.Image), "create PICA auxiliary image");
                VkMemoryRequirements requirements{};
                vkGetImageMemoryRequirements(mDevice, slot.Image, &requirements);
                VkMemoryAllocateInfo memory{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
                memory.allocationSize = requirements.size;
                memory.memoryTypeIndex =
                    FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                CheckAttachmentVk(vkAllocateMemory(mDevice, &memory, nullptr, &slot.Memory),
                                  "allocate PICA auxiliary memory");
                CheckAttachmentVk(vkBindImageMemory(mDevice, slot.Image, slot.Memory, 0), "bind PICA auxiliary memory");
            }
            VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            view.image = slot.Image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = slot.Format;
            view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            CheckAttachmentVk(vkCreateImageView(mDevice, &view, nullptr, &slot.View), "create PICA auxiliary view");
            mNriInterop.WrapTexture(slot.Image, slot.Format, VK_IMAGE_TYPE_2D, usage, target.Width, target.Height, 1, 1,
                                    samples);
            if (!msaa) {
                mSceneSurfaces.Publish(
                    { { target.Key.RenderTargetNamespace, target.Key.ColorPhysicalAddress, slot.Kind },
                      reinterpret_cast<uintptr_t>(slot.Image),
                      target.Width,
                      target.Height,
                      static_cast<uint32_t>(slot.Format),
                      0,
                      true });
            }
        }
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        // Discard only guides. Synchronize prior uses without a device-wide idle.
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = slot.Image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barriers.push_back(barrier);
    }
    auto command = mFrameActive ? mCommandBuffers[mCurrentFrame] : BeginImmediateCommands();
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, count, barriers.data());
    for (uint32_t index = 0; index < count; ++index) {
        vkCmdClearColorImage(command, slots[index].Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &slots[index].Clear, 1,
                             &barriers[index].subresourceRange);
        auto& barrier = barriers[index];
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        mResourceStates.Transition(reinterpret_cast<uintptr_t>(slots[index].Image),
                                   { Oot3d::ResourceAccess::ColorAttachment, mGraphicsQueueFamily });
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         nullptr, 0, nullptr, count, barriers.data());
    if (!mFrameActive)
        EndImmediateCommands(command);
    target.Attachments = { Oot3d::kAllPicaAuxiliaryOutputs };
    target.AuxiliaryNeedsClear = false;
    if (allocate)
        mDiagnostics.RecordNriPicaRenderTarget(target.NriOwned, count);
}

VkFramebuffer GfxRenderingAPIVulkan::NativePicaFramebuffer(NativePicaRenderTarget& target, bool canonical) {
    auto& cached = canonical ? target.CanonicalFramebuffer : target.Framebuffer;
    if (cached != VK_NULL_HANDLE)
        return cached;
    const bool msaa = mNativePicaSampleCount != VK_SAMPLE_COUNT_1_BIT;
    const std::array<VkImageView, 8> resolved{
        { target.ColorView, target.NormalGuideView, target.MaterialGuideView, target.RigidMotionGuideView,
          target.AmbientGuideView, target.FogGuideView, target.OutlineGeometryGuideView, target.DepthView }
    };
    const std::array<VkImageView, 8> sampled{ { target.MsaaColorView, target.MsaaNormalGuideView,
                                                target.MsaaMaterialGuideView, target.MsaaRigidMotionGuideView,
                                                target.MsaaAmbientGuideView, target.MsaaFogGuideView,
                                                target.MsaaOutlineGeometryGuideView, target.MsaaDepthView } };
    std::vector<VkImageView> views;
    const auto append = [&](const auto& source) {
        if (canonical) {
            views.push_back(source.front());
            views.push_back(source.back());
        } else
            views.insert(views.end(), source.begin(), source.end());
    };
    if (msaa)
        append(sampled);
    append(resolved);
    VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    info.renderPass = canonical ? mNativePicaCanonicalRenderPass : mNativePicaRenderPass;
    info.attachmentCount = static_cast<uint32_t>(views.size());
    info.pAttachments = views.data();
    info.width = target.Width;
    info.height = target.Height;
    info.layers = 1;
    CheckAttachmentVk(vkCreateFramebuffer(mDevice, &info, nullptr, &cached), "create PICA framebuffer variant");
    return cached;
}
} // namespace Fast
#endif
