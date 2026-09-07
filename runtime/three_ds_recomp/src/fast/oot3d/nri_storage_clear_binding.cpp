#include "fast/oot3d/nri_storage_clear_binding.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"

#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"

#include <NRI.h>
#endif

#include <unordered_map>

namespace Fast::Oot3d {
namespace {

constexpr uint32_t kMaxStorageClearBindings = 128U;

} // namespace

struct NriStorageClearBinding::Impl {
    NriInteropContext* Interop = nullptr;
    std::string Reason =
        "NRI storage-clear binding is not initialized";
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* Layout = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    struct Binding {
        nri::DescriptorSet* Set = nullptr;
        nri::Descriptor* Descriptor = nullptr;
    };
    std::unordered_map<VkImage, Binding> Bindings;
#endif
};

NriStorageClearBinding::NriStorageClearBinding()
    : mImpl(std::make_unique<Impl>()) {}

NriStorageClearBinding::~NriStorageClearBinding() {
    Shutdown();
}

bool NriStorageClearBinding::Initialize(
    NriInteropContext& interop) {
    Shutdown();
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    mImpl->Reason = "NRI storage-clear binding was not compiled";
    return false;
#else
    mImpl->Interop = &interop;
    mImpl->Core = NriInteropAccess::Core(interop);
    nri::Device* device = NriInteropAccess::Device(interop);
    if (mImpl->Core == nullptr || device == nullptr) {
        mImpl->Reason =
            "NRI storage-clear device interface is unavailable";
        return false;
    }

    const nri::DescriptorRangeDesc range{
        0U, 1U, nri::DescriptorType::STORAGE_TEXTURE,
        nri::StageBits::COMPUTE_SHADER};
    nri::DescriptorSetDesc set{};
    set.ranges = &range;
    set.rangeNum = 1U;
    nri::PipelineLayoutDesc layout{};
    layout.descriptorSets = &set;
    layout.descriptorSetNum = 1U;
    layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
    layout.flags =
        nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
    if (mImpl->Core->CreatePipelineLayout(
            *device, layout, mImpl->Layout) !=
            nri::Result::SUCCESS) {
        mImpl->Reason =
            "NRI storage-clear pipeline layout creation failed";
        return false;
    }

    nri::DescriptorPoolDesc pool{};
    pool.descriptorSetMaxNum = kMaxStorageClearBindings;
    pool.storageTextureMaxNum = kMaxStorageClearBindings;
    if (mImpl->Core->CreateDescriptorPool(
            *device, pool, mImpl->Pool) !=
            nri::Result::SUCCESS) {
        mImpl->Core->DestroyPipelineLayout(mImpl->Layout);
        mImpl->Layout = nullptr;
        mImpl->Reason =
            "NRI storage-clear descriptor pool creation failed";
        return false;
    }
    mImpl->Reason.clear();
    return true;
#endif
}

bool NriStorageClearBinding::ClearUint32(
    uint32_t frameIndex, VkImage image, uint32_t value) {
#ifndef ENABLE_OOT3D_NRI
    (void)frameIndex;
    (void)image;
    (void)value;
    return false;
#else
    if (!Available() || image == VK_NULL_HANDLE) return false;
    nri::CommandBuffer* command = NriInteropAccess::CommandBuffer(
        *mImpl->Interop, frameIndex);
    nri::Descriptor* storage = NriInteropAccess::TextureView(
        *mImpl->Interop, image, true);
    if (command == nullptr || storage == nullptr) return false;

    auto [entry, inserted] = mImpl->Bindings.try_emplace(image);
    if (inserted &&
        mImpl->Core->AllocateDescriptorSets(
            *mImpl->Pool, *mImpl->Layout, 0U,
            &entry->second.Set, 1U, 0U) !=
            nri::Result::SUCCESS) {
        mImpl->Bindings.erase(entry);
        return false;
    }
    if (entry->second.Descriptor != storage) {
        const nri::UpdateDescriptorRangeDesc update{
            entry->second.Set, 0U, 0U, &storage, 1U};
        mImpl->Core->UpdateDescriptorRanges(&update, 1U);
        entry->second.Descriptor = storage;
    }
    mImpl->Core->CmdSetDescriptorPool(
        *command, *mImpl->Pool);
    mImpl->Core->CmdSetPipelineLayout(
        *command, nri::BindPoint::COMPUTE, *mImpl->Layout);
    const nri::SetDescriptorSetDesc set{
        0U, entry->second.Set, nri::BindPoint::COMPUTE};
    mImpl->Core->CmdSetDescriptorSet(*command, set);

    nri::ClearStorageDesc clear{};
    clear.descriptor = storage;
    clear.value.ui.x = value;
    clear.setIndex = 0U;
    clear.rangeIndex = 0U;
    clear.descriptorIndex = 0U;
    mImpl->Core->CmdClearStorage(*command, clear);
    return true;
#endif
}

void NriStorageClearBinding::Shutdown() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        if (mImpl->Pool != nullptr)
            mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
        if (mImpl->Layout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->Layout);
    }
#endif
    *mImpl = {};
    mImpl->Reason =
        "NRI storage-clear binding is not initialized";
}

bool NriStorageClearBinding::Available() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Reason.empty() && mImpl->Interop != nullptr &&
           mImpl->Core != nullptr && mImpl->Layout != nullptr &&
           mImpl->Pool != nullptr &&
           mImpl->Interop->Available();
#else
    return false;
#endif
}

const std::string&
NriStorageClearBinding::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
