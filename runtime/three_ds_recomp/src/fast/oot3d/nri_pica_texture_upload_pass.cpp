#include "fast/oot3d/nri_pica_texture_upload_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/pica_nri_texture_upload.h"
#include "fast/oot3d/resource_state_tracker.h"

#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"

#include <NRI.h>
#include <Extensions/NRIHelper.h>
#endif

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <map>
#include <string_view>
#include <vector>

namespace Fast::Oot3d {
namespace {

constexpr uint32_t kFrameSlots = 2U;
constexpr uint32_t kBytesPerPixel = 4U;

} // namespace

struct NriPicaTextureUploadPass::Impl {
    NriInteropContext* Interop = nullptr;
    std::map<std::pair<uintptr_t, uint32_t>, ResourceState>
        TextureMipStates;
    uint64_t Capacity = 0;
    uint32_t RowAlignment = 1;
    uint32_t SliceAlignment = 1;
    uint64_t LastUploadedBytes = 0;
    std::string Reason = "NRI PICA texture upload is not initialized";
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::Device* Device = nullptr;
    struct UploadBuffer {
        nri::Buffer* Buffer = nullptr;
        uint8_t* Mapped = nullptr;
        uint64_t Capacity = 0;
        uint64_t Used = 0;
    };
    struct FrameUpload {
        uint64_t FrameId = std::numeric_limits<uint64_t>::max();
        std::vector<UploadBuffer> Buffers;
    };
    std::array<FrameUpload, kFrameSlots> Frames{};

    bool CreateUploadBuffer(uint64_t capacity, UploadBuffer& upload) {
        if (Core == nullptr || Device == nullptr || capacity == 0U ||
            capacity > std::numeric_limits<size_t>::max())
            return false;
        nri::BufferDesc bufferDesc{};
        bufferDesc.size = capacity;
        if (Core->CreateCommittedBuffer(
                *Device, nri::MemoryLocation::HOST_UPLOAD, 0.0F,
                bufferDesc, upload.Buffer) != nri::Result::SUCCESS)
            return false;
        upload.Mapped = static_cast<uint8_t*>(
            Core->MapBuffer(*upload.Buffer, 0, capacity));
        if (upload.Mapped == nullptr) {
            Core->DestroyBuffer(upload.Buffer);
            upload.Buffer = nullptr;
            return false;
        }
        upload.Capacity = capacity;
        return true;
    }

    void DestroyUploadBuffer(UploadBuffer& upload) {
        if (Core != nullptr && upload.Buffer != nullptr) {
            if (upload.Mapped != nullptr)
                Core->UnmapBuffer(*upload.Buffer);
            Core->DestroyBuffer(upload.Buffer);
        }
        upload = {};
    }

    void BeginFrame(FrameUpload& frame, uint64_t frameId) {
        frame.FrameId = frameId;
        if (frame.Buffers.size() > 2U) {
            const auto largest = std::max_element(
                frame.Buffers.begin() + 1, frame.Buffers.end(),
                [](const UploadBuffer& left, const UploadBuffer& right) {
                    return left.Capacity < right.Capacity;
                });
            if (largest != frame.Buffers.begin() + 1)
                std::iter_swap(frame.Buffers.begin() + 1, largest);
            for (size_t index = 2U; index < frame.Buffers.size(); ++index)
                DestroyUploadBuffer(frame.Buffers[index]);
            frame.Buffers.resize(2U);
        }
        for (auto& upload : frame.Buffers)
            upload.Used = 0U;
    }
#endif
};

NriPicaTextureUploadPass::NriPicaTextureUploadPass()
    : mImpl(std::make_unique<Impl>()) {}

NriPicaTextureUploadPass::~NriPicaTextureUploadPass() {
    Shutdown();
}

bool NriPicaTextureUploadPass::Initialize(
    NriInteropContext& interop, uint64_t bytesPerFrame) {
    Shutdown();
    mImpl->Interop = &interop;
    mImpl->Capacity = bytesPerFrame;
    if (const char* enabled =
            std::getenv("OOT3D_GRAPHICS_NRI_PICA_TEXTURE_UPLOADS");
        enabled != nullptr && std::string_view(enabled) == "0") {
        mImpl->Reason =
            "NRI PICA texture uploads disabled by environment";
        return false;
    }
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
    if (bytesPerFrame == 0U) {
        mImpl->Reason = "NRI PICA texture upload arena is empty";
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    mImpl->Reason = "NRI PICA texture uploads were not compiled";
    return false;
#else
    mImpl->Core = NriInteropAccess::Core(interop);
    mImpl->Device = NriInteropAccess::Device(interop);
    if (mImpl->Core == nullptr || mImpl->Device == nullptr) {
        mImpl->Reason = "NRI PICA texture upload device is unavailable";
        return false;
    }
    const auto& deviceDesc = mImpl->Core->GetDeviceDesc(*mImpl->Device);
    mImpl->RowAlignment = std::max(
        1U, deviceDesc.memoryAlignment.uploadBufferTextureRow);
    mImpl->SliceAlignment = std::max(
        1U, deviceDesc.memoryAlignment.uploadBufferTextureSlice);

    for (auto& frame : mImpl->Frames) {
        frame.Buffers.emplace_back();
        if (!mImpl->CreateUploadBuffer(
                bytesPerFrame, frame.Buffers.back())) {
            mImpl->Reason =
                "NRI PICA texture upload buffer creation failed";
            Shutdown();
            mImpl->Reason =
                "NRI PICA texture upload buffer creation failed";
            return false;
        }
    }
    mImpl->Reason.clear();
    return true;
#endif
}

bool NriPicaTextureUploadPass::Execute(
    const NriPicaTextureUploadDesc& desc) {
    mImpl->LastUploadedBytes = 0;
#ifndef ENABLE_OOT3D_NRI
    (void)desc;
    return false;
#else
    if (!Available() || desc.Image == VK_NULL_HANDLE ||
        desc.Format == VK_FORMAT_UNDEFINED || desc.Width == 0U ||
        desc.Height == 0U || desc.MipLevels == 0U ||
        desc.MipLevel >= desc.MipLevels)
        return false;
    const uint64_t pixelCount =
        static_cast<uint64_t>(desc.Width) * desc.Height;
    if (pixelCount >
            std::numeric_limits<uint64_t>::max() / kBytesPerPixel ||
        pixelCount * kBytesPerPixel != desc.Pixels.size())
        return false;

    const uint32_t slot = desc.FrameIndex % kFrameSlots;
    auto& frame = mImpl->Frames[slot];
    if (frame.Buffers.empty())
        return false;
    if (frame.FrameId != desc.FrameId)
        mImpl->BeginFrame(frame, desc.FrameId);

    Impl::UploadBuffer* upload = nullptr;
    std::optional<PicaNriTextureUploadLayout> layout;
    for (auto& candidate : frame.Buffers) {
        const auto candidateLayout = PlanPicaNriTextureUpload(
            candidate.Used, candidate.Capacity, desc.Width, desc.Height,
            kBytesPerPixel, mImpl->RowAlignment, mImpl->SliceAlignment);
        if (candidateLayout.has_value()) {
            upload = &candidate;
            layout = candidateLayout;
            break;
        }
    }
    if (upload == nullptr) {
        const auto required = PlanPicaNriTextureUpload(
            0U, std::numeric_limits<uint64_t>::max(),
            desc.Width, desc.Height, kBytesPerPixel,
            mImpl->RowAlignment, mImpl->SliceAlignment);
        if (!required.has_value())
            return false;
        const uint64_t capacity =
            std::max(mImpl->Capacity, required->RequiredSize);
        frame.Buffers.emplace_back();
        if (!mImpl->CreateUploadBuffer(
                capacity, frame.Buffers.back())) {
            frame.Buffers.pop_back();
            return false;
        }
        upload = &frame.Buffers.back();
        layout = PlanPicaNriTextureUpload(
            0U, upload->Capacity, desc.Width, desc.Height,
            kBytesPerPixel, mImpl->RowAlignment, mImpl->SliceAlignment);
        if (!layout.has_value())
            return false;
    }
    if (!PackPicaNriTextureUpload(
            desc.Pixels, desc.Width, desc.Height, kBytesPerPixel,
            *layout, std::span<uint8_t>(
                         upload->Mapped,
                         static_cast<size_t>(upload->Capacity))))
        return false;

    nri::CommandBuffer* command = NriInteropAccess::CommandBuffer(
        *mImpl->Interop, desc.FrameIndex);
    if (command == nullptr ||
        !mImpl->Interop->WrapTexture(
            desc.Image, desc.Format, VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            desc.Width << desc.MipLevel,
            desc.Height << desc.MipLevel, desc.MipLevels))
        return false;
    nri::Texture* texture =
        NriInteropAccess::Texture(*mImpl->Interop, desc.Image);
    if (texture == nullptr)
        return false;

    const uintptr_t textureKey = reinterpret_cast<uintptr_t>(desc.Image);
    const auto stateKey = std::pair{textureKey, desc.MipLevel};
    const auto current = mImpl->TextureMipStates.find(stateKey);
    const ResourceState before = current == mImpl->TextureMipStates.end()
                                     ? ResourceState{}
                                     : current->second;
    const ResourceTransition writable{
        textureKey, before, {ResourceAccess::TransferWrite, 0}};
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, desc.FrameIndex, desc.Image, writable,
            desc.MipLevel, 1U))
        return false;
    mImpl->TextureMipStates[stateKey] = writable.After;

    nri::TextureRegionDesc region{};
    region.width = static_cast<nri::Dim_t>(desc.Width);
    region.height = static_cast<nri::Dim_t>(desc.Height);
    region.depth = 1;
    region.mipOffset = static_cast<nri::Dim_t>(desc.MipLevel);
    region.planes = nri::PlaneBits::COLOR;
    const nri::TextureDataLayoutDesc source{
        layout->Offset, layout->RowPitch, layout->SlicePitch};
    mImpl->Core->CmdUploadBufferToTexture(
        *command, *texture, region, *upload->Buffer, source);

    const ResourceTransition readable{
        textureKey, writable.After, {ResourceAccess::ShaderRead, 0}};
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, desc.FrameIndex, desc.Image, readable,
            desc.MipLevel, 1U))
        return false;
    mImpl->TextureMipStates[stateKey] = readable.After;
    upload->Used = layout->RequiredSize;
    mImpl->LastUploadedBytes = desc.Pixels.size();
    return true;
#endif
}

void NriPicaTextureUploadPass::ForgetTexture(VkImage image) {
    if (image == VK_NULL_HANDLE)
        return;
    const uintptr_t key = reinterpret_cast<uintptr_t>(image);
    for (auto it = mImpl->TextureMipStates.begin();
         it != mImpl->TextureMipStates.end();) {
        it = it->first.first == key
                 ? mImpl->TextureMipStates.erase(it)
                 : std::next(it);
    }
}

void NriPicaTextureUploadPass::Shutdown() {
    mImpl->TextureMipStates.clear();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        for (auto& frame : mImpl->Frames) {
            for (auto& upload : frame.Buffers)
                mImpl->DestroyUploadBuffer(upload);
        }
    }
#endif
    *mImpl = {};
    mImpl->Reason = "NRI PICA texture upload is not initialized";
}

bool NriPicaTextureUploadPass::Available() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Reason.empty() && mImpl->Interop != nullptr &&
           mImpl->Core != nullptr && mImpl->Device != nullptr &&
           !mImpl->Frames[0].Buffers.empty() &&
           !mImpl->Frames[1].Buffers.empty() &&
           mImpl->Frames[0].Buffers[0].Buffer != nullptr &&
           mImpl->Frames[1].Buffers[0].Buffer != nullptr;
#else
    return false;
#endif
}

uint64_t NriPicaTextureUploadPass::LastUploadedBytes() const {
    return mImpl->LastUploadedBytes;
}

const std::string&
NriPicaTextureUploadPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
