#include "fast/extension/nri_pica_memory_fill_clear_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/extension/nri_interop_context.h"
#include "fast/extension/nri_storage_clear_binding.h"
#include "fast/extension/resource_state_tracker.h"

#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"

#include <NRI.h>
#endif

#include <array>
#include <cstdlib>
#include <string_view>

namespace Fast::Extension {

struct NriPicaMemoryFillClearPass::Impl {
    NriInteropContext* Interop = nullptr;
    uint32_t LastShadowBarrierCount = 0;
    std::string Reason = "NRI PICA memory-fill clear is not initialized";
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    std::unique_ptr<NriStorageClearBinding> StorageClear;
#endif
};

NriPicaMemoryFillClearPass::NriPicaMemoryFillClearPass() : mImpl(std::make_unique<Impl>()) {
}

NriPicaMemoryFillClearPass::~NriPicaMemoryFillClearPass() {
    Shutdown();
}

bool NriPicaMemoryFillClearPass::Initialize(NriInteropContext& interop) {
    Shutdown();
    if (const char* enabled = std::getenv("OOT3D_GRAPHICS_NRI_PICA_MEMORY_FILL_CLEARS");
        enabled != nullptr && std::string_view(enabled) == "0") {
        mImpl->Reason = "NRI PICA memory-fill clears disabled by environment";
        return false;
    }
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    mImpl->Reason = "NRI PICA memory-fill clears were not compiled";
    return false;
#else
    mImpl->Interop = &interop;
    mImpl->Core = NriInteropAccess::Core(interop);
    if (mImpl->Core == nullptr) {
        mImpl->Reason = "NRI PICA memory-fill clear core is unavailable";
        return false;
    }
    mImpl->StorageClear = std::make_unique<NriStorageClearBinding>();
    if (!mImpl->StorageClear->Initialize(interop)) {
        mImpl->Reason = mImpl->StorageClear->UnavailableReason();
        return false;
    }
    mImpl->Reason.clear();
    return true;
#endif
}

bool NriPicaMemoryFillClearPass::ClearShadow(const NriPicaShadowClearDesc& desc) {
    mImpl->LastShadowBarrierCount = 0;
#ifndef ENABLE_OOT3D_NRI
    (void)desc;
    return false;
#else
    if (!Available() || !ValidateNriPicaShadowClearDesc(desc))
        return false;
    nri::CommandBuffer* command = NriInteropAccess::CommandBuffer(*mImpl->Interop, desc.FrameIndex);
    if (command == nullptr || !mImpl->Interop->WrapTexture(desc.Image, VK_FORMAT_R32_UINT, VK_IMAGE_TYPE_2D,
                                                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                                               VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                                           desc.Width, desc.Height))
        return false;
    nri::Descriptor* storage = NriInteropAccess::TextureView(*mImpl->Interop, desc.Image, true);
    if (storage == nullptr)
        return false;

    const uintptr_t key = reinterpret_cast<uintptr_t>(desc.Image);
    ResourceStateTracker states;
    ResourceTransition initial;
    initial.Resource = key;
    initial.After = { ResourceAccess::StorageReadWrite, 0 };
    states.Commit(initial);
    const auto writable = states.PlanTransition(key, { ResourceAccess::StorageClear, 0 });
    if (!NriInteropAccess::CmdTextureBarrier(*mImpl->Interop, desc.FrameIndex, desc.Image, writable))
        return false;
    states.Commit(writable);

    if (!mImpl->StorageClear->ClearUint32(desc.FrameIndex, desc.Image, desc.Value))
        return false;

    const auto readable = states.PlanTransition(key, { ResourceAccess::StorageReadWrite, 0 });
    if (!NriInteropAccess::CmdTextureBarrier(*mImpl->Interop, desc.FrameIndex, desc.Image, readable))
        return false;
    states.Commit(readable);
    mImpl->LastShadowBarrierCount = 2U;
    return true;
#endif
}

bool NriPicaMemoryFillClearPass::ClearAttachments(const NriPicaAttachmentClearDesc& desc) {
#ifndef ENABLE_OOT3D_NRI
    (void)desc;
    return false;
#else
    if (!Available() || !ValidateNriPicaAttachmentClearDesc(desc))
        return false;
    nri::CommandBuffer* command = NriInteropAccess::CommandBuffer(*mImpl->Interop, desc.FrameIndex);
    if (command == nullptr)
        return false;

    std::array<nri::ClearAttachmentDesc, kPicaColorAttachmentCount + 1U> clears{};
    uint32_t count = 0;
    if (desc.ClearColor) {
        // The scene colour and the bound guides; unbound guides are not in the pass.
        for (uint8_t index = 0; index < desc.Attachments.ColorAttachmentCount(); ++index) {
            const auto attachment = static_cast<PicaColorAttachment>(index);
            if (!desc.Attachments.Binds(attachment)) continue;
            auto& clear = clears[count++];
            clear.planes = nri::PlaneBits::COLOR;
            clear.colorAttachmentIndex = index;
            switch (attachment) {
                case PicaColorAttachment::SceneColor:
                    clear.value.color.f = { desc.Color[0], desc.Color[1], desc.Color[2], desc.Color[3] };
                    break;
                case PicaColorAttachment::NormalGuide: clear.value.color.f = { 0.5F, 0.5F, 1.0F, 0.0F }; break;
                case PicaColorAttachment::AmbientGuide: clear.value.color.f = { 1.0F, 1.0F, 1.0F, 0.0F }; break;
                case PicaColorAttachment::FogGuide:
                case PicaColorAttachment::OutlineGeometryGuide: clear.value.color.f = { 0, 0, 0, 1 }; break;
                default: break;
            }
        }
    }
    if (desc.ClearDepth) {
        auto& clear = clears[count++];
        clear.planes = desc.HasStencil ? nri::PlaneBits::DEPTH | nri::PlaneBits::STENCIL : nri::PlaneBits::DEPTH;
        clear.value.depthStencil = { desc.Depth, desc.Stencil };
    }
    const nri::Rect rect{ 0, 0, static_cast<nri::Dim_t>(desc.Width), static_cast<nri::Dim_t>(desc.Height) };
    mImpl->Core->CmdClearAttachments(*command, clears.data(), count, &rect, 1U);
    return true;
#endif
}

bool NriPicaMemoryFillClearPass::ClearColorRects(const NriPicaColorRectsClearDesc& desc) {
#ifndef ENABLE_OOT3D_NRI
    (void)desc;
    return false;
#else
    if (!Available() || !ValidateNriPicaColorRectsClearDesc(desc))
        return false;
    nri::CommandBuffer* command = NriInteropAccess::CommandBuffer(*mImpl->Interop, desc.FrameIndex);
    if (command == nullptr)
        return false;
    nri::ClearAttachmentDesc clear{};
    clear.planes = nri::PlaneBits::COLOR;
    clear.colorAttachmentIndex = 0U;
    clear.value.color.f = { desc.Color[0], desc.Color[1], desc.Color[2], desc.Color[3] };
    std::array<nri::Rect, 4> rects{};
    for (uint32_t index = 0; index < desc.RectCount; ++index) {
        const auto& rect = desc.Rects[index];
        rects[index] = { static_cast<int16_t>(rect[0]), static_cast<int16_t>(rect[1]),
                         static_cast<nri::Dim_t>(rect[2]), static_cast<nri::Dim_t>(rect[3]) };
    }
    mImpl->Core->CmdClearAttachments(*command, &clear, 1U, rects.data(), desc.RectCount);
    return true;
#endif
}

void NriPicaMemoryFillClearPass::Shutdown() {
    *mImpl = {};
    mImpl->Reason = "NRI PICA memory-fill clear is not initialized";
}

bool NriPicaMemoryFillClearPass::Available() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Reason.empty() && mImpl->Interop != nullptr && mImpl->Core != nullptr && mImpl->Interop->Available();
#else
    return false;
#endif
}

uint32_t NriPicaMemoryFillClearPass::LastShadowBarrierCount() const {
    return mImpl->LastShadowBarrierCount;
}

const std::string& NriPicaMemoryFillClearPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Extension

#endif
