#include "fast/oot3d/nri_pica_render_target_owner.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"

#include <cstdlib>
#include <string_view>

namespace Fast::Oot3d {

bool NriPicaRenderTargetOwner::Initialize(
    NriInteropContext& interop) {
    Shutdown();
    if (const char* enabled =
            std::getenv("OOT3D_GRAPHICS_NRI_PICA_RENDER_TARGETS");
        enabled != nullptr && std::string_view(enabled) == "0") {
        mReason =
            "NRI PICA render-target ownership disabled by environment";
        return false;
    }
    if (!interop.Available()) {
        mReason = interop.UnavailableReason();
        return false;
    }
    mInterop = &interop;
    mReason.clear();
    return true;
}

bool NriPicaRenderTargetOwner::Create(
    const NriPicaRenderTargetDesc& desc,
    NriPicaRenderTargetImages& images) {
    images = {};
    if (!Available() || desc.Width == 0U || desc.Height == 0U ||
        desc.DepthFormat == VK_FORMAT_UNDEFINED)
        return false;

    const auto create = [&](
                            VkFormat format, VkImageUsageFlags usage,
                            VkSampleCountFlagBits samples,
                            VkImage& image) {
        NriOwnedTexture2DDesc textureDesc;
        textureDesc.Width = desc.Width;
        textureDesc.Height = desc.Height;
        textureDesc.Format = format;
        textureDesc.Usage = usage;
        textureDesc.Samples = samples;
        NriOwnedTexture2D texture;
        if (!mInterop->CreateOwnedTexture2D(textureDesc, texture))
            return false;
        image = texture.Image;
        return image != VK_NULL_HANDLE;
    };

    const VkImageUsageFlags colorUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags guideUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags depthUsage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    const bool instrumented = !desc.Attachments.NativeColorOnly();
    const bool baseCreated =
        create(VK_FORMAT_R8G8B8A8_UNORM, colorUsage, VK_SAMPLE_COUNT_1_BIT, images.Color) &&
        (!instrumented ||
         (create(VK_FORMAT_R8G8B8A8_UNORM, guideUsage, VK_SAMPLE_COUNT_1_BIT, images.NormalGuide) &&
          create(VK_FORMAT_R8G8B8A8_UNORM, guideUsage, VK_SAMPLE_COUNT_1_BIT, images.MaterialGuide) &&
          create(VK_FORMAT_R16G16B16A16_SFLOAT, guideUsage, VK_SAMPLE_COUNT_1_BIT, images.RigidMotionGuide) &&
          create(VK_FORMAT_R8G8B8A8_UNORM, guideUsage, VK_SAMPLE_COUNT_1_BIT, images.AmbientGuide) &&
          create(VK_FORMAT_R8G8B8A8_UNORM, guideUsage, VK_SAMPLE_COUNT_1_BIT, images.FogGuide) &&
          create(VK_FORMAT_R32G32B32A32_SFLOAT, guideUsage, VK_SAMPLE_COUNT_1_BIT, images.OutlineGeometryGuide))) &&
        create(VK_FORMAT_R32_UINT,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
               VK_SAMPLE_COUNT_1_BIT, images.Shadow) &&
        create(desc.DepthFormat, depthUsage, VK_SAMPLE_COUNT_1_BIT, images.Depth);
    if (!baseCreated) {
        Destroy(images);
        images = {};
        return false;
    }

    if (desc.Samples != VK_SAMPLE_COUNT_1_BIT) {
        const VkImageUsageFlags msaaColorUsage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        const bool msaaCreated =
            create(VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage, desc.Samples, images.MsaaColor) &&
            (!instrumented ||
             (create(VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage, desc.Samples, images.MsaaNormalGuide) &&
              create(VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage, desc.Samples, images.MsaaMaterialGuide) &&
              create(VK_FORMAT_R16G16B16A16_SFLOAT, msaaColorUsage, desc.Samples, images.MsaaRigidMotionGuide) &&
              create(VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage, desc.Samples, images.MsaaAmbientGuide) &&
              create(VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage, desc.Samples, images.MsaaFogGuide) &&
              create(VK_FORMAT_R32G32B32A32_SFLOAT, msaaColorUsage, desc.Samples, images.MsaaOutlineGeometryGuide))) &&
            create(desc.DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                   desc.Samples, images.MsaaDepth);
        if (!msaaCreated) {
            Destroy(images);
            images = {};
            return false;
        }
    }
    if (!ValidateNriPicaRenderTargetImages(
            images, desc.Samples, desc.Attachments)) {
        Destroy(images);
        images = {};
        return false;
    }
    return true;
}

void NriPicaRenderTargetOwner::Destroy(
    const NriPicaRenderTargetImages& images) {
    if (mInterop == nullptr) return;
    for (VkImage image : NriPicaRenderTargetImageArray(images))
        if (image != VK_NULL_HANDLE)
            mInterop->DestroyOwnedTexture(image);
}

void NriPicaRenderTargetOwner::Shutdown() {
    mInterop = nullptr;
    mReason =
        "NRI PICA render-target ownership is not initialized";
}

bool NriPicaRenderTargetOwner::Available() const {
    return mInterop != nullptr && mReason.empty() &&
           mInterop->Available();
}

const std::string&
NriPicaRenderTargetOwner::UnavailableReason() const {
    return mReason;
}

} // namespace Fast::Oot3d

#endif
