#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"

#ifdef ENABLE_OOT3D_VULKAN

#include <algorithm>
#include <array>
#include <new>
#include <utility>
#include <vector>

namespace Fast::Oot3d {
namespace {

constexpr size_t kCapacity = EffectTransientAllocationPlan::kCapacity;

[[nodiscard]] bool DecodeSampleCount(
    uint32_t value, VkSampleCountFlagBits& output) noexcept {
    switch (value) {
        case VK_SAMPLE_COUNT_1_BIT:
        case VK_SAMPLE_COUNT_2_BIT:
        case VK_SAMPLE_COUNT_4_BIT:
        case VK_SAMPLE_COUNT_8_BIT:
        case VK_SAMPLE_COUNT_16_BIT:
        case VK_SAMPLE_COUNT_32_BIT:
        case VK_SAMPLE_COUNT_64_BIT:
            output = static_cast<VkSampleCountFlagBits>(value);
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool SameSlot(
    const EffectTransientAllocationSlot& lhs,
    const EffectTransientAllocationSlot& rhs) noexcept {
    return lhs.StorageClass == rhs.StorageClass &&
           lhs.Format == rhs.Format && lhs.Width == rhs.Width &&
           lhs.Height == rhs.Height && lhs.MipLevels == rhs.MipLevels &&
           lhs.Layers == rhs.Layers && lhs.Samples == rhs.Samples &&
           lhs.Usage == rhs.Usage && lhs.ResourceMask == rhs.ResourceMask &&
           lhs.ResourceCount == rhs.ResourceCount &&
           lhs.FirstUse == rhs.FirstUse && lhs.LastUse == rhs.LastUse;
}

[[nodiscard]] bool SameRequirement(
    const EffectTransientImageRequirement& lhs,
    const EffectTransientImageRequirement& rhs) noexcept {
    return lhs.Resource == rhs.Resource && lhs.Format == rhs.Format &&
           lhs.Width == rhs.Width && lhs.Height == rhs.Height &&
           lhs.MipLevels == rhs.MipLevels && lhs.Layers == rhs.Layers &&
           lhs.Samples == rhs.Samples && lhs.Usage == rhs.Usage;
}

[[nodiscard]] bool SameResource(
    const EffectTransientAllocationResource& lhs,
    const EffectTransientAllocationResource& rhs) noexcept {
    return SameRequirement(lhs.Requirement, rhs.Requirement) &&
           lhs.StorageClass == rhs.StorageClass &&
           lhs.FirstUse == rhs.FirstUse && lhs.LastUse == rhs.LastUse &&
           lhs.Slot == rhs.Slot;
}

} // namespace

struct NriEffectGraphTransientImageArena::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
    std::array<NriOwnedTexture2D, kCapacity> Textures{};
    std::array<EffectTransientAllocationSlot, kCapacity> Slots{};
    std::array<EffectTransientAllocationResource, kCapacity> Resources{};
    std::array<NriEffectGraphTransientImageBinding, kCapacity> Bindings{};
    std::array<std::vector<ResourceStateTracker>, kCapacity> MipStates{};
    ResourceStateTracker States;
    size_t SlotCount = 0U;
    size_t ResourceCount = 0U;
    bool Ready = false;
    NriEffectGraphTransientImageArenaStats Statistics{};
    std::string Reason = "effect-graph transient image arena is not initialized";

    [[nodiscard]] bool SamePlan(
        const EffectTransientAllocationPlan& plan) const noexcept {
        if (SlotCount != plan.SlotCount() ||
            ResourceCount != plan.ResourceCount()) {
            return false;
        }
        for (size_t index = 0U; index < SlotCount; ++index) {
            if (!SameSlot(Slots[index], plan.SlotAt(index))) return false;
        }
        for (size_t index = 0U; index < ResourceCount; ++index) {
            if (!SameResource(Resources[index], plan.ResourceAt(index))) {
                return false;
            }
        }
        return true;
    }

    void DestroyTextures() noexcept {
        if (Interop != nullptr) {
            for (size_t index = 0U; index < SlotCount; ++index) {
                if (Textures[index].Image != VK_NULL_HANDLE) {
                    Interop->DestroyOwnedTexture(Textures[index].Image);
                }
            }
        }
        Textures = {};
        Slots = {};
        Resources = {};
        Bindings = {};
        for (auto& states : MipStates) states.clear();
        States.Clear();
        SlotCount = 0U;
        ResourceCount = 0U;
        Statistics.ActiveResourceCount = 0U;
        Statistics.PhysicalSlotCount = 0U;
        Statistics.AliasOpportunityCount = 0U;
        Statistics.PeakLiveResourceCount = 0U;
        Statistics.LastAllocatedSlotCount = 0U;
        Statistics.LastConfigurationReused = false;
    }
};

NriEffectGraphTransientImageArena::NriEffectGraphTransientImageArena()
    : mImpl(std::make_unique<Impl>()) {}

NriEffectGraphTransientImageArena::~NriEffectGraphTransientImageArena() {
    Shutdown();
}

bool NriEffectGraphTransientImageArena::Initialize(
    VkDevice device, NriInteropContext& interop) {
    Shutdown();
    mImpl->Device = device;
    mImpl->Interop = &interop;
    if (device == VK_NULL_HANDLE || !interop.Available()) {
        mImpl->Reason = device == VK_NULL_HANDLE
            ? "effect-graph transient image arena has no Vulkan device"
            : interop.UnavailableReason();
        return false;
    }
    mImpl->Ready = true;
    mImpl->Reason.clear();
    return true;
}

bool NriEffectGraphTransientImageArena::Configure(
    const EffectTransientAllocationPlan& plan) {
    mImpl->Statistics.LastAllocatedSlotCount = 0U;
    mImpl->Statistics.LastConfigurationReused = false;
    if (!mImpl->Ready || !plan.Complete() || plan.SlotCount() > kCapacity ||
        plan.ResourceCount() > kCapacity) {
        mImpl->Reason = !mImpl->Ready
            ? "effect-graph transient image arena is unavailable"
            : "effect-graph transient allocation plan is incomplete";
        return false;
    }
    if (mImpl->SamePlan(plan)) {
        const auto& summary = plan.Summary();
        mImpl->Statistics.ActiveResourceCount =
            summary.PlannedResourceCount;
        mImpl->Statistics.PhysicalSlotCount =
            summary.PhysicalSlotCount;
        mImpl->Statistics.AliasOpportunityCount =
            summary.AliasOpportunityCount;
        mImpl->Statistics.PeakLiveResourceCount =
            summary.PeakLiveResourceCount;
        mImpl->Statistics.LastConfigurationReused = true;
        ++mImpl->Statistics.ReusedConfigurationCount;
        return true;
    }

    std::array<NriOwnedTexture2D, kCapacity> newTextures{};
    std::array<std::vector<ResourceStateTracker>, kCapacity> newMipStates{};
    try {
        for (size_t index = 0U; index < plan.SlotCount(); ++index) {
            const uint32_t mipLevels = plan.SlotAt(index).MipLevels;
            if (mipLevels > 1U) newMipStates[index].resize(mipLevels);
        }
    } catch (const std::bad_alloc&) {
        mImpl->Reason =
            "effect-graph transient mip-state allocation failed";
        return false;
    }
    size_t createdCount = 0U;
    for (size_t index = 0U; index < plan.SlotCount(); ++index) {
        const auto& slot = plan.SlotAt(index);
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        if (!DecodeSampleCount(slot.Samples, samples)) {
            mImpl->Reason = "effect-graph transient image sample count is invalid";
            break;
        }
        NriOwnedTexture2DDesc desc;
        desc.Width = slot.Width;
        desc.Height = slot.Height;
        desc.Format = static_cast<VkFormat>(slot.Format);
        desc.Usage = static_cast<VkImageUsageFlags>(slot.Usage);
        desc.MipLevels = slot.MipLevels;
        desc.Layers = slot.Layers;
        desc.Samples = samples;
        if (!mImpl->Interop->CreateOwnedTexture2D(
                desc, newTextures[index])) {
            mImpl->Reason = "NRI effect-graph transient image allocation failed";
            break;
        }
        ++createdCount;
    }
    if (createdCount != plan.SlotCount()) {
        for (size_t index = 0U; index < createdCount; ++index) {
            mImpl->Interop->DestroyOwnedTexture(newTextures[index].Image);
        }
        return false;
    }

    if (mImpl->SlotCount != 0U &&
        vkDeviceWaitIdle(mImpl->Device) != VK_SUCCESS) {
        for (size_t index = 0U; index < createdCount; ++index) {
            mImpl->Interop->DestroyOwnedTexture(newTextures[index].Image);
        }
        mImpl->Reason = "Vulkan device wait failed while replacing transient images";
        return false;
    }
    mImpl->DestroyTextures();
    mImpl->Textures = newTextures;
    mImpl->MipStates = std::move(newMipStates);
    mImpl->SlotCount = plan.SlotCount();
    mImpl->ResourceCount = plan.ResourceCount();
    for (size_t index = 0U; index < mImpl->SlotCount; ++index) {
        mImpl->Slots[index] = plan.SlotAt(index);
    }
    for (size_t index = 0U; index < mImpl->ResourceCount; ++index) {
        const auto& resource = plan.ResourceAt(index);
        mImpl->Resources[index] = resource;
        if (resource.Slot >= mImpl->SlotCount) {
            mImpl->Reason = "effect-graph transient resource slot is invalid";
            mImpl->DestroyTextures();
            return false;
        }
        const auto& requirement = resource.Requirement;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        if (!DecodeSampleCount(requirement.Samples, samples)) {
            mImpl->Reason = "effect-graph transient resource sample count is invalid";
            mImpl->DestroyTextures();
            return false;
        }
        auto& binding = mImpl->Bindings[index];
        binding.Resource = requirement.Resource;
        binding.Texture = mImpl->Textures[resource.Slot];
        binding.Format = static_cast<VkFormat>(requirement.Format);
        binding.Width = requirement.Width;
        binding.Height = requirement.Height;
        binding.MipLevels = requirement.MipLevels;
        binding.Layers = requirement.Layers;
        binding.Samples = samples;
        binding.Usage =
            static_cast<VkImageUsageFlags>(requirement.Usage);
        binding.Slot = resource.Slot;
        if (requirement.MipLevels == 1U) {
            binding.StateTracker = &mImpl->States;
        } else {
            auto& mipStates = mImpl->MipStates[resource.Slot];
            binding.MipStateTrackers = mipStates.data();
            binding.MipStateTrackerCount =
                static_cast<uint32_t>(mipStates.size());
        }
        if (!binding.Valid()) {
            mImpl->Reason =
                "effect-graph transient image binding is incomplete";
            mImpl->DestroyTextures();
            return false;
        }
    }
    const auto& summary = plan.Summary();
    mImpl->Statistics.ActiveResourceCount = summary.PlannedResourceCount;
    mImpl->Statistics.PhysicalSlotCount = summary.PhysicalSlotCount;
    mImpl->Statistics.AliasOpportunityCount = summary.AliasOpportunityCount;
    mImpl->Statistics.PeakLiveResourceCount = summary.PeakLiveResourceCount;
    mImpl->Statistics.LastAllocatedSlotCount =
        static_cast<uint32_t>(createdCount);
    mImpl->Statistics.TotalAllocatedSlotCount += createdCount;
    mImpl->Reason.clear();
    return true;
}

bool NriEffectGraphTransientImageArena::ConfiguredFor(
    const EffectTransientAllocationPlan& plan) const noexcept {
    return mImpl->Ready && plan.Complete() && mImpl->SamePlan(plan);
}

bool NriEffectGraphTransientImageArena::HasActiveResources() const noexcept {
    return mImpl->SlotCount != 0U || mImpl->ResourceCount != 0U;
}

const NriEffectGraphTransientImageBinding*
NriEffectGraphTransientImageArena::Find(
    EffectResource resource) const noexcept {
    const auto end = mImpl->Bindings.begin() + mImpl->ResourceCount;
    const auto found = std::find_if(
        mImpl->Bindings.begin(), end,
        [resource](const NriEffectGraphTransientImageBinding& binding) {
            return binding.Resource == resource;
        });
    return found == end ? nullptr : &*found;
}

void NriEffectGraphTransientImageArena::InvalidateScreenResources() {
    if (mImpl->SlotCount != 0U && mImpl->Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(mImpl->Device);
    }
    mImpl->DestroyTextures();
}

void NriEffectGraphTransientImageArena::Shutdown() {
    InvalidateScreenResources();
    *mImpl = {};
    mImpl->Reason = "effect-graph transient image arena is not initialized";
}

bool NriEffectGraphTransientImageArena::Available() const noexcept {
    return mImpl->Ready;
}

const NriEffectGraphTransientImageArenaStats&
NriEffectGraphTransientImageArena::Stats() const noexcept {
    return mImpl->Statistics;
}

const std::string&
NriEffectGraphTransientImageArena::UnavailableReason() const noexcept {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
