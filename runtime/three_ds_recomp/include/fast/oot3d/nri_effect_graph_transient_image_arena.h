#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/effect_graph_physical_plan.h"
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

struct NriEffectGraphTransientImageBinding {
    EffectResource Resource = EffectResource::Count;
    NriOwnedTexture2D Texture;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    uint32_t MipLevels = 0U;
    uint32_t Layers = 0U;
    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags Usage = 0U;
    uint8_t Slot = kNoEffectAliasSlot;
    ResourceStateTracker* StateTracker = nullptr;
    ResourceStateTracker* MipStateTrackers = nullptr;
    uint32_t MipStateTrackerCount = 0U;

    [[nodiscard]] ResourceStateTracker* StateTrackerForMip(
        uint32_t mip) const noexcept {
        if (mip >= MipLevels) return nullptr;
        if (MipLevels == 1U) {
            return mip == 0U ? StateTracker : nullptr;
        }
        return MipStateTrackers != nullptr &&
                       MipStateTrackerCount == MipLevels
                   ? &MipStateTrackers[mip]
                   : nullptr;
    }

    [[nodiscard]] bool Valid() const noexcept {
        const bool stateTrackingValid = MipLevels == 1U
            ? StateTracker != nullptr && MipStateTrackers == nullptr &&
                  MipStateTrackerCount == 0U
            : StateTracker == nullptr && MipStateTrackers != nullptr &&
                  MipStateTrackerCount == MipLevels;
        return Resource != EffectResource::Count &&
               Texture.Image != VK_NULL_HANDLE &&
               Format != VK_FORMAT_UNDEFINED && Width != 0U &&
               Height != 0U && MipLevels != 0U && Layers != 0U &&
               Usage != 0U && Slot != kNoEffectAliasSlot &&
               stateTrackingValid;
    }
};

struct NriEffectGraphTransientImageArenaStats {
    uint32_t ActiveResourceCount = 0U;
    uint32_t PhysicalSlotCount = 0U;
    uint32_t AliasOpportunityCount = 0U;
    uint32_t PeakLiveResourceCount = 0U;
    uint32_t LastAllocatedSlotCount = 0U;
    uint64_t TotalAllocatedSlotCount = 0U;
    uint64_t ReusedConfigurationCount = 0U;
    bool LastConfigurationReused = false;
};

// Owns transient images selected by the compiled effect graph. Providers
// borrow bindings and retain ownership only of pipelines and descriptors.
class NriEffectGraphTransientImageArena final {
  public:
    NriEffectGraphTransientImageArena();
    ~NriEffectGraphTransientImageArena();
    NriEffectGraphTransientImageArena(
        const NriEffectGraphTransientImageArena&) = delete;
    NriEffectGraphTransientImageArena& operator=(
        const NriEffectGraphTransientImageArena&) = delete;

    bool Initialize(VkDevice device, NriInteropContext& interop);
    bool Configure(const EffectTransientAllocationPlan& plan);
    [[nodiscard]] bool ConfiguredFor(
        const EffectTransientAllocationPlan& plan) const noexcept;
    [[nodiscard]] bool HasActiveResources() const noexcept;
    [[nodiscard]] const NriEffectGraphTransientImageBinding* Find(
        EffectResource resource) const noexcept;
    void InvalidateScreenResources();
    void Shutdown();

    [[nodiscard]] bool Available() const noexcept;
    [[nodiscard]] const NriEffectGraphTransientImageArenaStats& Stats()
        const noexcept;
    [[nodiscard]] const std::string& UnavailableReason() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
