#include "fast/oot3d/nri_pica_render_target_init_pass.h"
#include "fast/oot3d/pica_attachment_contract.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/nri_storage_clear_binding.h"
#include "fast/oot3d/resource_state_tracker.h"

#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"

#include <NRI.h>
#endif

#include <array>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace Fast::Oot3d {

struct NriPicaRenderTargetInitPass::Impl {
    NriInteropContext* Interop = nullptr;
    uint32_t LastBarrierCount = 0;
    uint32_t LastClearedImageCount = 0;
    std::string Reason =
        "NRI PICA render-target initialization is not initialized";
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    std::unique_ptr<NriStorageClearBinding> StorageClear;
#endif
};

NriPicaRenderTargetInitPass::NriPicaRenderTargetInitPass()
    : mImpl(std::make_unique<Impl>()) {}

NriPicaRenderTargetInitPass::~NriPicaRenderTargetInitPass() {
    Shutdown();
}

bool NriPicaRenderTargetInitPass::Initialize(
    NriInteropContext& interop) {
    Shutdown();
    if (const char* enabled = std::getenv(
            "OOT3D_GRAPHICS_NRI_PICA_RENDER_TARGET_INITIALIZATION");
        enabled != nullptr && std::string_view(enabled) == "0") {
        mImpl->Reason =
            "NRI PICA render-target initialization disabled by environment";
        return false;
    }
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    mImpl->Reason =
        "NRI PICA render-target initialization was not compiled";
    return false;
#else
    mImpl->Interop = &interop;
    mImpl->Core = NriInteropAccess::Core(interop);
    if (mImpl->Core == nullptr) {
        mImpl->Reason =
            "NRI PICA render-target initialization core is unavailable";
        return false;
    }
    mImpl->StorageClear =
        std::make_unique<NriStorageClearBinding>();
    if (!mImpl->StorageClear->Initialize(interop)) {
        mImpl->Reason =
            mImpl->StorageClear->UnavailableReason();
        return false;
    }
    mImpl->Reason.clear();
    return true;
#endif
}

bool NriPicaRenderTargetInitPass::Execute(
    const NriPicaRenderTargetInitDesc& desc) {
    mImpl->LastBarrierCount = 0;
    mImpl->LastClearedImageCount = 0;
#ifndef ENABLE_OOT3D_NRI
    (void)desc;
    return false;
#else
    if (!Available() ||
        !ValidateNriPicaRenderTargetInitDesc(desc))
        return false;
    nri::CommandBuffer* command = NriInteropAccess::CommandBuffer(
        *mImpl->Interop, desc.FrameIndex);
    if (command == nullptr) return false;

    const auto wrap = [&](VkImage image, VkFormat format,
                          VkImageUsageFlags usage,
                          VkSampleCountFlagBits samples) {
        return mImpl->Interop->WrapTexture(
            image, format, VK_IMAGE_TYPE_2D, usage,
            desc.Width, desc.Height, 1U, 1U, samples);
    };
    const VkImageUsageFlags colorUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags resolvedColorUsage =
        colorUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const VkImageUsageFlags depthUsage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    const bool instrumented = !desc.Attachments.NativeColorOnly();
    const uint32_t colorCount = desc.Attachments.ColorAttachmentCount();
    const bool baseWrapped =
        wrap(desc.Images.Color, VK_FORMAT_R8G8B8A8_UNORM, resolvedColorUsage, VK_SAMPLE_COUNT_1_BIT) &&
        (!instrumented ||
         (wrap(desc.Images.NormalGuide, VK_FORMAT_R8G8B8A8_UNORM, colorUsage, VK_SAMPLE_COUNT_1_BIT) &&
          wrap(desc.Images.MaterialGuide, VK_FORMAT_R8G8B8A8_UNORM, colorUsage, VK_SAMPLE_COUNT_1_BIT) &&
          wrap(desc.Images.RigidMotionGuide, VK_FORMAT_R16G16B16A16_SFLOAT, colorUsage, VK_SAMPLE_COUNT_1_BIT) &&
          wrap(desc.Images.AmbientGuide, VK_FORMAT_R8G8B8A8_UNORM, colorUsage, VK_SAMPLE_COUNT_1_BIT) &&
          wrap(desc.Images.FogGuide, VK_FORMAT_R8G8B8A8_UNORM, colorUsage, VK_SAMPLE_COUNT_1_BIT) &&
          wrap(desc.Images.OutlineGeometryGuide, VK_FORMAT_R32G32B32A32_SFLOAT, colorUsage, VK_SAMPLE_COUNT_1_BIT))) &&
        wrap(desc.Images.Shadow, VK_FORMAT_R32_UINT,
             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
             VK_SAMPLE_COUNT_1_BIT) &&
        wrap(desc.Images.Depth, desc.DepthFormat, depthUsage, VK_SAMPLE_COUNT_1_BIT);
    if (!baseWrapped) return false;
    if (desc.Samples != VK_SAMPLE_COUNT_1_BIT) {
        const VkImageUsageFlags msaaColorUsage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (!wrap(desc.Images.MsaaColor, VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage, desc.Samples) ||
            (instrumented &&
             (!wrap(desc.Images.MsaaNormalGuide, VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage, desc.Samples) ||
              !wrap(desc.Images.MsaaMaterialGuide, VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage, desc.Samples) ||
              !wrap(desc.Images.MsaaRigidMotionGuide, VK_FORMAT_R16G16B16A16_SFLOAT, msaaColorUsage, desc.Samples) ||
              !wrap(desc.Images.MsaaAmbientGuide, VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage, desc.Samples) ||
              !wrap(desc.Images.MsaaFogGuide, VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage, desc.Samples) ||
              !wrap(desc.Images.MsaaOutlineGeometryGuide, VK_FORMAT_R32G32B32A32_SFLOAT, msaaColorUsage,
                    desc.Samples))) ||
            !wrap(desc.Images.MsaaDepth, desc.DepthFormat,
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, desc.Samples))
            return false;
    }

    std::array<nri::Descriptor*, kPicaColorAttachmentCount> baseColors{ {
        NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.Color),
        NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.NormalGuide),
        NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.MaterialGuide),
        NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.RigidMotionGuide),
        NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.AmbientGuide),
        NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.FogGuide),
        NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.OutlineGeometryGuide),
    } };
    nri::Descriptor* baseDepth =
        NriInteropAccess::DepthAttachmentView(
            *mImpl->Interop, desc.Images.Depth);
    nri::Descriptor* shadow =
        NriInteropAccess::TextureView(
            *mImpl->Interop, desc.Images.Shadow, true);
    for (size_t index = 0; index < colorCount; ++index)
        if (baseColors[index] == nullptr) return false;
    if (baseDepth == nullptr || shadow == nullptr) return false;

    std::array<nri::Descriptor*, kPicaColorAttachmentCount>
        msaaColors{};
    nri::Descriptor* msaaDepth = nullptr;
    if (desc.Samples != VK_SAMPLE_COUNT_1_BIT) {
        msaaColors = { {
            NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.MsaaColor),
            NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.MsaaNormalGuide),
            NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.MsaaMaterialGuide),
            NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.MsaaRigidMotionGuide),
            NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.MsaaAmbientGuide),
            NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.MsaaFogGuide),
            NriInteropAccess::ColorAttachmentView(*mImpl->Interop, desc.Images.MsaaOutlineGeometryGuide),
        } };
        msaaDepth = NriInteropAccess::DepthAttachmentView(
            *mImpl->Interop, desc.Images.MsaaDepth);
        for (size_t index = 0; index < colorCount; ++index)
            if (msaaColors[index] == nullptr) return false;
        if (msaaDepth == nullptr) return false;
    }

    ResourceStateTracker states;
    std::vector<ResourceTransition> initialTransitions;
    std::vector<NriTextureTransitionDesc> initialDescs;
    initialTransitions.reserve(
        (desc.Samples == VK_SAMPLE_COUNT_1_BIT ? 2U : 3U) +
        colorCount *
            (desc.Samples == VK_SAMPLE_COUNT_1_BIT ? 1U : 2U));
    initialDescs.reserve(initialTransitions.capacity());
    const auto plan = [&](VkImage image, ResourceAccess access) {
        const auto transition = states.PlanTransition(
            reinterpret_cast<uintptr_t>(image), {access, 0});
        initialTransitions.push_back(transition);
        initialDescs.push_back({image, transition, 0, 1});
    };
    plan(desc.Images.Color, ResourceAccess::ColorAttachment);
    if (instrumented) {
        plan(desc.Images.NormalGuide, ResourceAccess::ColorAttachment);
        plan(desc.Images.MaterialGuide, ResourceAccess::ColorAttachment);
        plan(desc.Images.RigidMotionGuide,
             ResourceAccess::ColorAttachment);
        plan(desc.Images.AmbientGuide,
             ResourceAccess::ColorAttachment);
        plan(desc.Images.FogGuide, ResourceAccess::ColorAttachment);
        plan(desc.Images.OutlineGeometryGuide, ResourceAccess::ColorAttachment);
    }
    plan(desc.Images.Shadow, ResourceAccess::StorageClear);
    plan(desc.Images.Depth, ResourceAccess::DepthAttachment);
    if (desc.Samples != VK_SAMPLE_COUNT_1_BIT) {
        plan(desc.Images.MsaaColor,
             ResourceAccess::ColorAttachment);
        if (instrumented) {
            plan(desc.Images.MsaaNormalGuide,
                 ResourceAccess::ColorAttachment);
            plan(desc.Images.MsaaMaterialGuide,
                 ResourceAccess::ColorAttachment);
            plan(desc.Images.MsaaRigidMotionGuide,
                 ResourceAccess::ColorAttachment);
            plan(desc.Images.MsaaAmbientGuide,
                 ResourceAccess::ColorAttachment);
            plan(desc.Images.MsaaFogGuide, ResourceAccess::ColorAttachment);
            plan(desc.Images.MsaaOutlineGeometryGuide, ResourceAccess::ColorAttachment);
        }
        plan(desc.Images.MsaaDepth,
             ResourceAccess::DepthAttachment);
    }
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, desc.FrameIndex,
            initialDescs.data(),
            static_cast<uint32_t>(initialDescs.size())))
        return false;
    for (const auto& transition : initialTransitions)
        states.Commit(transition);

    const auto clearAttachments = [this, command](
                                      const std::array<
                                          nri::Descriptor*,
                                          kPicaColorAttachmentCount>& colors,
                                      nri::Descriptor* depth,
                                      uint32_t activeColorCount) {
        std::array<nri::AttachmentDesc,
                   kPicaColorAttachmentCount> colorAttachments{};
        for (size_t index = 0; index < activeColorCount;
             ++index) {
            colorAttachments[index].descriptor = colors[index];
            colorAttachments[index].loadOp = nri::LoadOp::CLEAR;
            colorAttachments[index].storeOp = nri::StoreOp::STORE;
        }
        if (activeColorCount > 1U) {
            colorAttachments[1].clearValue.color.f =
                {0.5F, 0.5F, 1.0F, 0.0F};
            colorAttachments[PicaAttachmentIndex(
                PicaColorAttachment::AmbientGuide)]
                .clearValue.color.f = {1.0F, 1.0F, 1.0F, 0.0F};
            colorAttachments[PicaAttachmentIndex(PicaColorAttachment::FogGuide)]
                .clearValue.color.f = {0.0F, 0.0F, 0.0F, 1.0F};
            colorAttachments[PicaAttachmentIndex(PicaColorAttachment::OutlineGeometryGuide)].clearValue.color.f = {
                0.0F, 0.0F, 0.0F, 1.0F
            };
        }
        nri::RenderingDesc rendering{};
        rendering.colors = colorAttachments.data();
        rendering.colorNum = activeColorCount;
        rendering.depth.descriptor = depth;
        rendering.depth.clearValue.depthStencil = {1.0F, 0U};
        rendering.depth.loadOp = nri::LoadOp::CLEAR;
        rendering.depth.storeOp = nri::StoreOp::STORE;
        mImpl->Core->CmdBeginRendering(*command, rendering);
        mImpl->Core->CmdEndRendering(*command);
    };
    clearAttachments(baseColors, baseDepth, colorCount);
    if (desc.Samples != VK_SAMPLE_COUNT_1_BIT)
        clearAttachments(msaaColors, msaaDepth, colorCount);

    if (!mImpl->StorageClear->ClearUint32(
            desc.FrameIndex, desc.Images.Shadow, 0xFFFFFFFFU))
        return false;

    const auto shadowReady = states.PlanTransition(
        reinterpret_cast<uintptr_t>(desc.Images.Shadow),
        {ResourceAccess::StorageReadWrite, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, desc.FrameIndex,
            desc.Images.Shadow, shadowReady))
        return false;
    states.Commit(shadowReady);
    mImpl->LastBarrierCount =
        static_cast<uint32_t>(initialTransitions.size()) + 1U;
    mImpl->LastClearedImageCount =
        CountNriPicaRenderTargetImages(desc.Images);
    return true;
#endif
}

void NriPicaRenderTargetInitPass::Shutdown() {
    *mImpl = {};
    mImpl->Reason =
        "NRI PICA render-target initialization is not initialized";
}

bool NriPicaRenderTargetInitPass::Available() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Reason.empty() && mImpl->Interop != nullptr &&
           mImpl->Core != nullptr && mImpl->Interop->Available();
#else
    return false;
#endif
}

uint32_t NriPicaRenderTargetInitPass::LastBarrierCount() const {
    return mImpl->LastBarrierCount;
}

uint32_t NriPicaRenderTargetInitPass::LastClearedImageCount() const {
    return mImpl->LastClearedImageCount;
}

const std::string&
NriPicaRenderTargetInitPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
