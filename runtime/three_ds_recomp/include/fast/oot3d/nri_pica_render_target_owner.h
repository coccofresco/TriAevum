#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/pica_attachment_contract.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;

struct NriPicaRenderTargetDesc {
    uint32_t Width = 0;
    uint32_t Height = 0;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;
    PicaAttachmentRequirements Attachments{kAllPicaAuxiliaryOutputs};
};

struct NriPicaRenderTargetImages {
    VkImage Color = VK_NULL_HANDLE;
    VkImage NormalGuide = VK_NULL_HANDLE;
    VkImage MaterialGuide = VK_NULL_HANDLE;
    VkImage RigidMotionGuide = VK_NULL_HANDLE;
    VkImage AmbientGuide = VK_NULL_HANDLE;
    VkImage FogGuide = VK_NULL_HANDLE;
    VkImage OutlineGeometryGuide = VK_NULL_HANDLE;
    VkImage Shadow = VK_NULL_HANDLE;
    VkImage Depth = VK_NULL_HANDLE;
    VkImage MsaaColor = VK_NULL_HANDLE;
    VkImage MsaaNormalGuide = VK_NULL_HANDLE;
    VkImage MsaaMaterialGuide = VK_NULL_HANDLE;
    VkImage MsaaRigidMotionGuide = VK_NULL_HANDLE;
    VkImage MsaaAmbientGuide = VK_NULL_HANDLE;
    VkImage MsaaFogGuide = VK_NULL_HANDLE;
    VkImage MsaaOutlineGeometryGuide = VK_NULL_HANDLE;
    VkImage MsaaDepth = VK_NULL_HANDLE;
};

[[nodiscard]] inline std::array<VkImage, 17> NriPicaRenderTargetImageArray(const NriPicaRenderTargetImages& images) {
    return {
        images.Color,
        images.NormalGuide,
        images.MaterialGuide,
        images.RigidMotionGuide,
        images.AmbientGuide,
        images.FogGuide,
        images.OutlineGeometryGuide,
        images.Shadow,
        images.Depth,
        images.MsaaColor,
        images.MsaaNormalGuide,
        images.MsaaMaterialGuide,
        images.MsaaRigidMotionGuide,
        images.MsaaAmbientGuide,
        images.MsaaFogGuide,
        images.MsaaOutlineGeometryGuide,
        images.MsaaDepth,
    };
}

[[nodiscard]] inline bool ValidateNriPicaRenderTargetImages(
    const NriPicaRenderTargetImages& images,
    VkSampleCountFlagBits samples,
    PicaAttachmentRequirements attachments = {
        kAllPicaAuxiliaryOutputs}) {
    if (images.Color == VK_NULL_HANDLE ||
        images.Shadow == VK_NULL_HANDLE ||
        images.Depth == VK_NULL_HANDLE)
        return false;
    const std::array<VkImage, 6> guides{ images.NormalGuide,  images.MaterialGuide, images.RigidMotionGuide,
                                         images.AmbientGuide, images.FogGuide,      images.OutlineGeometryGuide };
    const bool instrumented = !attachments.NativeColorOnly();
    for (VkImage guide : guides)
        if ((guide != VK_NULL_HANDLE) != instrumented) return false;
    const bool multisampled = samples != VK_SAMPLE_COUNT_1_BIT;
    if ((images.MsaaColor != VK_NULL_HANDLE) != multisampled ||
        (images.MsaaDepth != VK_NULL_HANDLE) != multisampled)
        return false;
    const std::array<VkImage, 6> msaaGuides{ images.MsaaNormalGuide,      images.MsaaMaterialGuide,
                                             images.MsaaRigidMotionGuide, images.MsaaAmbientGuide,
                                             images.MsaaFogGuide,         images.MsaaOutlineGeometryGuide };
    for (VkImage guide : msaaGuides)
        if ((guide != VK_NULL_HANDLE) != (multisampled && instrumented))
            return false;
    return true;
}

[[nodiscard]] inline uint32_t CountNriPicaRenderTargetImages(
    const NriPicaRenderTargetImages& images) {
    uint32_t count = 0;
    for (VkImage image : NriPicaRenderTargetImageArray(images))
        if (image != VK_NULL_HANDLE) ++count;
    return count;
}

class NriPicaRenderTargetOwner final {
  public:
    bool Initialize(NriInteropContext& interop);
    bool Create(const NriPicaRenderTargetDesc& desc,
                NriPicaRenderTargetImages& images);
    void Destroy(const NriPicaRenderTargetImages& images);
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    NriInteropContext* mInterop = nullptr;
    std::string mReason =
        "NRI PICA render-target ownership is not initialized";
};

} // namespace Fast::Oot3d

#endif
