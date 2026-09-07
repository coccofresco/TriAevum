#include "fast/oot3d/pica_dynamic_rendering_scope.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"

#include <array>
#include <cstdlib>
#include <string_view>

#ifdef ENABLE_OOT3D_NRI
#include <NRI.h>
#include "nri_interop_internal.h"
#endif

namespace Fast::Oot3d {

struct PicaDynamicRenderingScope::Impl {
    PFN_vkCmdBeginRenderingKHR BeginRendering = nullptr;
    PFN_vkCmdEndRenderingKHR EndRendering = nullptr;
    NriInteropContext* Interop = nullptr;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    VkResolveModeFlagBits DepthResolveMode = VK_RESOLVE_MODE_NONE;
    bool IsActive = false;
    bool ActiveNri = false;
    bool PreferNri = false;
    bool PreferNriBarriers = false;
    uint32_t ActiveNriGlobalBarrierCount = 0;
    uint32_t LastNriGlobalBarrierCount = 0;
    std::string Reason = "PICA dynamic rendering is not initialized";

    void CmdGlobalBarrier(
        uint32_t frameIndex, VkCommandBuffer command, bool incoming);
};

namespace {
bool HasStencil(VkFormat format) {
    return format == VK_FORMAT_D16_UNORM_S8_UINT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT ||
           format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

#ifdef ENABLE_OOT3D_NRI
nri::ResolveOp ToNriResolveOp(VkResolveModeFlagBits mode) {
    switch (mode) {
        case VK_RESOLVE_MODE_MIN_BIT:
            return nri::ResolveOp::MIN;
        case VK_RESOLVE_MODE_MAX_BIT:
            return nri::ResolveOp::MAX;
        default:
            return nri::ResolveOp::AVERAGE;
    }
}

nri::AccessBits PicaVisibleMemoryAccess() {
    return nri::AccessBits::INDEX_BUFFER |
           nri::AccessBits::VERTEX_BUFFER |
           nri::AccessBits::CONSTANT_BUFFER |
           nri::AccessBits::ARGUMENT_BUFFER |
           nri::AccessBits::COLOR_ATTACHMENT |
           nri::AccessBits::DEPTH_STENCIL_ATTACHMENT |
           nri::AccessBits::INPUT_ATTACHMENT |
           nri::AccessBits::SHADER_RESOURCE |
           nri::AccessBits::SHADER_RESOURCE_STORAGE |
           nri::AccessBits::COPY_SOURCE |
           nri::AccessBits::COPY_DESTINATION |
           nri::AccessBits::RESOLVE_SOURCE |
           nri::AccessBits::RESOLVE_DESTINATION |
           nri::AccessBits::CLEAR_STORAGE;
}

bool CmdNriPicaGlobalBarrier(
    NriInteropContext& interop, uint32_t frameIndex, bool incoming) {
    const nri::AccessStage allMemory{
        PicaVisibleMemoryAccess(), nri::StageBits::ALL};
    const nri::AccessStage attachments{
        nri::AccessBits::COLOR_ATTACHMENT |
            nri::AccessBits::DEPTH_STENCIL_ATTACHMENT,
        nri::StageBits::COLOR_ATTACHMENT |
            nri::StageBits::DEPTH_STENCIL_ATTACHMENT};
    return NriInteropAccess::CmdGlobalBarrier(
        interop, frameIndex,
        incoming ? allMemory : attachments,
        incoming ? attachments : allMemory);
}
#endif

void CmdVkPicaGlobalBarrier(VkCommandBuffer command, bool incoming) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    if (incoming) {
        barrier.srcAccessMask =
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(
            command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr);
        return;
    }
    barrier.srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(
        command,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}
} // namespace

void PicaDynamicRenderingScope::Impl::CmdGlobalBarrier(
    uint32_t frameIndex, VkCommandBuffer command, bool incoming) {
#ifdef ENABLE_OOT3D_NRI
    if (PreferNriBarriers && Interop != nullptr &&
        CmdNriPicaGlobalBarrier(*Interop, frameIndex, incoming)) {
        ++ActiveNriGlobalBarrierCount;
        return;
    }
#else
    (void)frameIndex;
#endif
    CmdVkPicaGlobalBarrier(command, incoming);
}

PicaDynamicRenderingScope::PicaDynamicRenderingScope()
    : mImpl(std::make_unique<Impl>()) {}
PicaDynamicRenderingScope::~PicaDynamicRenderingScope() { Shutdown(); }

bool PicaDynamicRenderingScope::Initialize(
    VkDevice device, NriInteropContext* interop, VkFormat depthFormat,
    VkResolveModeFlagBits depthResolveMode,
    bool dynamicRenderingEnabled) {
    Shutdown();
    if (!dynamicRenderingEnabled) {
        mImpl->Reason = "VK_KHR_dynamic_rendering is unavailable";
        return false;
    }
    if (device == VK_NULL_HANDLE || depthFormat == VK_FORMAT_UNDEFINED) {
        mImpl->Reason = "PICA dynamic rendering received an invalid device";
        return false;
    }
    mImpl->BeginRendering =
        reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(
            vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR"));
    mImpl->EndRendering =
        reinterpret_cast<PFN_vkCmdEndRenderingKHR>(
            vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR"));
    if (mImpl->BeginRendering == nullptr ||
        mImpl->EndRendering == nullptr) {
        mImpl->Reason =
            "VK_KHR_dynamic_rendering commands are unavailable";
        return false;
    }
    mImpl->DepthFormat = depthFormat;
    mImpl->DepthResolveMode = depthResolveMode;
    mImpl->Interop = interop;
#ifdef ENABLE_OOT3D_NRI
    const char* nriScope =
        std::getenv("OOT3D_GRAPHICS_NRI_PICA_SCOPE");
    mImpl->PreferNri =
        interop != nullptr && interop->Available() &&
        interop->DynamicRenderingAvailable() &&
        (nriScope == nullptr || std::string_view(nriScope) != "0");
    const char* nriBarriers =
        std::getenv("OOT3D_GRAPHICS_NRI_PICA_BARRIERS");
    mImpl->PreferNriBarriers =
        interop != nullptr && interop->Available() &&
        (nriBarriers == nullptr ||
         std::string_view(nriBarriers) != "0");
#endif
    mImpl->Reason.clear();
    return true;
}

bool PicaDynamicRenderingScope::Begin(
    uint32_t frameIndex, VkCommandBuffer command,
    const PicaDynamicRenderingTarget& target) {
    if (!Available() || mImpl->IsActive ||
        command == VK_NULL_HANDLE ||
        !ValidatePicaDynamicRenderingTarget(
            target, mImpl->DepthResolveMode))
        return false;

    mImpl->ActiveNriGlobalBarrierCount = 0;
    mImpl->LastNriGlobalBarrierCount = 0;
    const bool multisampled =
        target.Samples != VK_SAMPLE_COUNT_1_BIT;

#ifdef ENABLE_OOT3D_NRI
    if (mImpl->PreferNri &&
        ValidatePicaNriRenderingTarget(
            target, mImpl->DepthResolveMode)) {
        nri::CoreInterface* core =
            NriInteropAccess::Core(*mImpl->Interop);
        nri::CommandBuffer* nriCommand =
            NriInteropAccess::CommandBuffer(
                *mImpl->Interop, frameIndex);
        std::array<nri::Descriptor*, kPicaColorAttachmentCount>
            colorViews{};
        std::array<nri::Descriptor*, kPicaColorAttachmentCount>
            resolveViews{};
        bool ready = core != nullptr && nriCommand != nullptr;
        for (size_t index = 0;
             ready && index < target.ColorAttachmentCount; ++index) {
            ready = mImpl->Interop->WrapTexture(
                        target.ColorImages[index],
                        target.ColorFormats[index],
                        VK_IMAGE_TYPE_2D,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                        target.Width, target.Height, 1U, 1U,
                        target.Samples) &&
                    (colorViews[index] =
                         NriInteropAccess::ColorAttachmentView(
                             *mImpl->Interop,
                             target.ColorImages[index])) != nullptr;
            if (ready && multisampled) {
                ready = mImpl->Interop->WrapTexture(
                            target.ResolveImages[index],
                            target.ColorFormats[index],
                            VK_IMAGE_TYPE_2D,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                            target.Width, target.Height) &&
                        (resolveViews[index] =
                             NriInteropAccess::ColorAttachmentView(
                                 *mImpl->Interop,
                                 target.ResolveImages[index])) != nullptr;
            }
        }
        nri::Descriptor* depthView = nullptr;
        nri::Descriptor* depthResolveView = nullptr;
        if (ready) {
            ready = mImpl->Interop->WrapTexture(
                        target.DepthImage, target.DepthFormat,
                        VK_IMAGE_TYPE_2D,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                        target.Width, target.Height, 1U, 1U,
                        target.Samples) &&
                    (depthView =
                         NriInteropAccess::DepthAttachmentView(
                             *mImpl->Interop,
                             target.DepthImage)) != nullptr;
        }
        if (ready && multisampled) {
            ready = mImpl->Interop->WrapTexture(
                        target.DepthResolveImage,
                        target.DepthFormat, VK_IMAGE_TYPE_2D,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                        target.Width, target.Height) &&
                    (depthResolveView =
                         NriInteropAccess::DepthAttachmentView(
                             *mImpl->Interop,
                             target.DepthResolveImage)) != nullptr;
        }
        if (ready) {
            std::array<nri::AttachmentDesc,
                       kPicaColorAttachmentCount> colors{};
            for (size_t index = 0;
                 index < target.ColorAttachmentCount; ++index) {
                colors[index].descriptor = colorViews[index];
                colors[index].loadOp = nri::LoadOp::LOAD;
                colors[index].storeOp = nri::StoreOp::STORE;
                colors[index].resolveOp = nri::ResolveOp::AVERAGE;
                colors[index].resolveDst =
                    multisampled ? resolveViews[index] : nullptr;
            }
            nri::AttachmentDesc depth{};
            depth.descriptor = depthView;
            depth.loadOp = nri::LoadOp::LOAD;
            depth.storeOp = nri::StoreOp::STORE;
            depth.resolveOp =
                ToNriResolveOp(mImpl->DepthResolveMode);
            depth.resolveDst =
                multisampled ? depthResolveView : nullptr;
            nri::AttachmentDesc stencil{};
            if (HasStencil(target.DepthFormat)) {
                stencil.descriptor = depthView;
                stencil.loadOp = nri::LoadOp::LOAD;
                stencil.storeOp = nri::StoreOp::STORE;
                stencil.resolveOp = nri::ResolveOp::AVERAGE;
            }
            mImpl->CmdGlobalBarrier(frameIndex, command, true);
            nri::RenderingDesc rendering{};
            rendering.colors = colors.data();
            rendering.colorNum = target.ColorAttachmentCount;
            rendering.depth = depth;
            rendering.stencil = stencil;
            core->CmdBeginRendering(*nriCommand, rendering);
            mImpl->IsActive = true;
            mImpl->ActiveNri = true;
            return true;
        }
    }
#else
    (void)frameIndex;
#endif

    std::array<VkRenderingAttachmentInfoKHR,
               kPicaColorAttachmentCount> colors{};
    for (uint32_t i = 0; i < target.ColorAttachmentCount; ++i) {
        colors[i].sType =
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
        colors[i].imageView = target.Colors[i];
        colors[i].imageLayout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (multisampled) {
            colors[i].resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            colors[i].resolveImageView = target.Resolves[i];
            colors[i].resolveImageLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
    }

    VkRenderingAttachmentInfoKHR depth{
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    depth.imageView = target.Depth;
    depth.imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (multisampled) {
        depth.resolveMode = mImpl->DepthResolveMode;
        depth.resolveImageView = target.DepthResolve;
        depth.resolveImageLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfoKHR stencil = depth;
    stencil.resolveMode = VK_RESOLVE_MODE_NONE;
    stencil.resolveImageView = VK_NULL_HANDLE;
    VkRenderingInfoKHR rendering{
        VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    rendering.renderArea.extent = {target.Width, target.Height};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = target.ColorAttachmentCount;
    rendering.pColorAttachments = colors.data();
    rendering.pDepthAttachment = &depth;
    if (HasStencil(mImpl->DepthFormat))
        rendering.pStencilAttachment = &stencil;
    mImpl->CmdGlobalBarrier(frameIndex, command, true);
    mImpl->BeginRendering(command, &rendering);
    mImpl->IsActive = true;
    mImpl->ActiveNri = false;
    return true;
}

void PicaDynamicRenderingScope::End(
    uint32_t frameIndex, VkCommandBuffer command) {
    if (!mImpl->IsActive) return;
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->ActiveNri) {
        nri::CoreInterface* core =
            NriInteropAccess::Core(*mImpl->Interop);
        nri::CommandBuffer* nriCommand =
            NriInteropAccess::CommandBuffer(
                *mImpl->Interop, frameIndex);
        if (core != nullptr && nriCommand != nullptr) {
            core->CmdEndRendering(*nriCommand);
            mImpl->CmdGlobalBarrier(frameIndex, command, false);
            mImpl->LastNriGlobalBarrierCount =
                mImpl->ActiveNriGlobalBarrierCount;
            mImpl->IsActive = false;
            mImpl->ActiveNri = false;
            return;
        }
    }
#else
    (void)frameIndex;
#endif
    mImpl->EndRendering(command);
    mImpl->CmdGlobalBarrier(frameIndex, command, false);
    mImpl->LastNriGlobalBarrierCount =
        mImpl->ActiveNriGlobalBarrierCount;
    mImpl->IsActive = false;
    mImpl->ActiveNri = false;
}

void PicaDynamicRenderingScope::Shutdown() {
    *mImpl = {};
    mImpl->Reason = "PICA dynamic rendering is not initialized";
}

bool PicaDynamicRenderingScope::Available() const {
    return mImpl->Reason.empty() &&
           mImpl->BeginRendering != nullptr &&
           mImpl->EndRendering != nullptr;
}
bool PicaDynamicRenderingScope::Active() const {
    return mImpl->IsActive;
}
bool PicaDynamicRenderingScope::ActiveOwnedByNri() const {
    return mImpl->IsActive && mImpl->ActiveNri;
}
uint32_t PicaDynamicRenderingScope::LastNriGlobalBarrierCount() const {
    return mImpl->LastNriGlobalBarrierCount;
}
const std::string&
PicaDynamicRenderingScope::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
