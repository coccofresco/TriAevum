#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"
#include "fast/oot3d/effect_transient_configuration_cache.h"

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

} // namespace

// The images of one allocation plan. Bindings point at its state trackers,
// so a configuration never moves.
struct EffectTransientConfiguration {
    std::array<NriOwnedTexture2D, kCapacity> Textures{};
    std::array<NriEffectGraphTransientImageBinding, kCapacity> Bindings{};
    std::array<std::vector<ResourceStateTracker>, kCapacity> MipStates{};
    ResourceStateTracker States;
    size_t SlotCount = 0U;
    size_t ResourceCount = 0U;
};

struct NriEffectGraphTransientImageArena::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
    std::array<EffectTransientConfiguration, EffectTransientConfigurationCache::kCapacity> Configurations{};
    EffectTransientConfigurationCache Cache;
    EffectTransientConfiguration* Current = nullptr;
    bool Ready = false;
    NriEffectGraphTransientImageArenaStats Statistics{};
    std::string Reason = "effect-graph transient image arena is not initialized";

    void Destroy(EffectTransientConfiguration& configuration) noexcept {
        if (Interop != nullptr) {
            for (size_t index = 0U; index < configuration.SlotCount; ++index) {
                if (configuration.Textures[index].Image != VK_NULL_HANDLE) {
                    Interop->DestroyOwnedTexture(configuration.Textures[index].Image);
                }
            }
        }
        configuration.Textures = {};
        configuration.Bindings = {};
        for (auto& states : configuration.MipStates) states.clear();
        configuration.States.Clear();
        configuration.SlotCount = 0U;
        configuration.ResourceCount = 0U;
        if (Current == &configuration) Current = nullptr;
    }

    void DestroyAll() noexcept {
        for (auto& configuration : Configurations) Destroy(configuration);
        Cache.Clear();
        Statistics.ActiveResourceCount = 0U;
        Statistics.PhysicalSlotCount = 0U;
        Statistics.AliasOpportunityCount = 0U;
        Statistics.PeakLiveResourceCount = 0U;
        Statistics.LastAllocatedSlotCount = 0U;
        Statistics.LastConfigurationReused = false;
    }

    void Summarize(const EffectTransientAllocationPlan& plan) noexcept {
        const auto& summary = plan.Summary();
        Statistics.ActiveResourceCount = summary.PlannedResourceCount;
        Statistics.PhysicalSlotCount = summary.PhysicalSlotCount;
        Statistics.AliasOpportunityCount = summary.AliasOpportunityCount;
        Statistics.PeakLiveResourceCount = summary.PeakLiveResourceCount;
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
    const auto selection = mImpl->Cache.Select(plan);
    auto& configuration = mImpl->Configurations[selection.Index];
    if (selection.Hit) {
        if (selection.Covers) mImpl->Cache.Touch(selection.Index);
        else mImpl->Cache.Commit(selection.Index, plan);
        mImpl->Current = &configuration;
        mImpl->Summarize(plan);
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

    // Recorded commands may still use the replaced configuration's images.
    if (selection.Evicts && !mImpl->Interop->CompleteRecordedWork()) {
        for (size_t index = 0U; index < createdCount; ++index) {
            mImpl->Interop->DestroyOwnedTexture(newTextures[index].Image);
        }
        mImpl->Reason = "Vulkan device wait failed while replacing transient images";
        return false;
    }
    mImpl->Destroy(configuration);
    mImpl->Cache.Remove(selection.Index);
    const auto fail = [&](const char* reason) {
        mImpl->Reason = reason;
        mImpl->Destroy(configuration);
        return false;
    };
    configuration.Textures = newTextures;
    configuration.MipStates = std::move(newMipStates);
    configuration.SlotCount = plan.SlotCount();
    configuration.ResourceCount = plan.ResourceCount();
    for (size_t index = 0U; index < configuration.ResourceCount; ++index) {
        const auto& resource = plan.ResourceAt(index);
        if (resource.Slot >= configuration.SlotCount) {
            return fail("effect-graph transient resource slot is invalid");
        }
        const auto& requirement = resource.Requirement;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        if (!DecodeSampleCount(requirement.Samples, samples)) {
            return fail("effect-graph transient resource sample count is invalid");
        }
        auto& binding = configuration.Bindings[index];
        binding.Resource = requirement.Resource;
        binding.Texture = configuration.Textures[resource.Slot];
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
            binding.StateTracker = &configuration.States;
        } else {
            auto& mipStates = configuration.MipStates[resource.Slot];
            binding.MipStateTrackers = mipStates.data();
            binding.MipStateTrackerCount =
                static_cast<uint32_t>(mipStates.size());
        }
        if (!binding.Valid()) {
            return fail("effect-graph transient image binding is incomplete");
        }
    }
    mImpl->Cache.Commit(selection.Index, plan);
    mImpl->Current = &configuration;
    mImpl->Summarize(plan);
    mImpl->Statistics.LastAllocatedSlotCount =
        static_cast<uint32_t>(createdCount);
    mImpl->Statistics.TotalAllocatedSlotCount += createdCount;
    mImpl->Reason.clear();
    return true;
}

bool NriEffectGraphTransientImageArena::ConfiguredFor(
    const EffectTransientAllocationPlan& plan) const noexcept {
    return mImpl->Ready && plan.Complete() && mImpl->Cache.Select(plan).Hit;
}

bool NriEffectGraphTransientImageArena::ReplacesConfigurationFor(
    const EffectTransientAllocationPlan& plan) const noexcept {
    return mImpl->Ready && plan.Complete() && mImpl->Cache.Select(plan).Evicts;
}


const NriEffectGraphTransientImageBinding*
NriEffectGraphTransientImageArena::Find(
    EffectResource resource) const noexcept {
    if (mImpl->Current == nullptr) return nullptr;
    const auto& bindings = mImpl->Current->Bindings;
    const auto end = bindings.begin() + mImpl->Current->ResourceCount;
    const auto found = std::find_if(
        bindings.begin(), end,
        [resource](const NriEffectGraphTransientImageBinding& binding) {
            return binding.Resource == resource;
        });
    return found == end ? nullptr : &*found;
}

void NriEffectGraphTransientImageArena::InvalidateScreenResources() {
    if (mImpl->Cache.Count() != 0U && mImpl->Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(mImpl->Device);
    }
    mImpl->DestroyAll();
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
