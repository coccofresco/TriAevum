#pragma once

#include "fast/oot3d/display_effect_resources.h"
#include "fast/oot3d/effect_graph.h"
#include "fast/renderer/extension_resource_allocation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fast::Oot3d {

using EffectPhysicalResidency =
    ::Fast::Renderer::ExtensionResourceResidency;

// Aliasing remains conservative until the graph also owns image creation.
// Only resources in the same storage class, format and extent may share a
// candidate slot, and their declared lifetimes must not overlap.
enum class EffectTransientStorageClass : uint8_t {
    None,
    SinglePlaneImage,
    DepthPyramid,
    ShadowDepth,
};

inline constexpr uint8_t kNoEffectAliasSlot = 0xffU;

struct EffectPhysicalResource {
    EffectResource Resource = EffectResource::Count;
    EffectPhysicalResidency Residency =
        EffectPhysicalResidency::Semantic;
    EffectTransientStorageClass StorageClass =
        EffectTransientStorageClass::None;
    size_t FirstUse = 0U;
    size_t LastUse = 0U;
    uint32_t ReadCount = 0U;
    uint32_t WriteCount = 0U;
    EffectResourceBinding Binding{};
    uint8_t AliasSlot = kNoEffectAliasSlot;
    bool Resolved = false;

    [[nodiscard]] bool AliasEligible() const noexcept {
        return Resolved &&
               Residency == EffectPhysicalResidency::TransientImage &&
               StorageClass != EffectTransientStorageClass::None;
    }
};

struct EffectTransientAliasSlot {
    EffectTransientStorageClass StorageClass =
        EffectTransientStorageClass::None;
    uint32_t Format = 0U;
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    size_t FirstUse = 0U;
    size_t LastUse = 0U;
    uint32_t ResourceMask = 0U;
    uint32_t ResourceCount = 0U;
};

struct EffectPhysicalPlanSummary {
    uint32_t DeclaredResourceCount = 0U;
    uint32_t ResolvedResourceCount = 0U;
    uint32_t MissingResourceCount = 0U;
    uint32_t MissingResourceMask = 0U;
    uint32_t SemanticResourceCount = 0U;
    uint32_t ExternalImageResourceCount = 0U;
    uint32_t NativeAttachmentResourceCount = 0U;
    uint32_t TransientImageResourceCount = 0U;
    uint32_t TemporalHistoryResourceCount = 0U;
    uint32_t PhysicalImageCount = 0U;
    uint32_t AliasEligibleResourceCount = 0U;
    uint32_t AliasSlotCount = 0U;
    uint32_t AliasOpportunityCount = 0U;
    uint32_t PeakLiveTransientCount = 0U;

    [[nodiscard]] bool Complete() const noexcept {
        return MissingResourceCount == 0U &&
               ResolvedResourceCount == DeclaredResourceCount;
    }
};

class EffectGraphPhysicalPlan final {
  public:
    static constexpr size_t kResourceCapacity =
        static_cast<size_t>(EffectResource::Count);

    [[nodiscard]] const EffectPhysicalResource* Find(
        EffectResource resource) const noexcept;
    [[nodiscard]] const EffectPhysicalResource& ResourceAt(
        size_t index) const noexcept;
    [[nodiscard]] const EffectTransientAliasSlot& AliasSlotAt(
        size_t index) const noexcept;
    [[nodiscard]] size_t ResourceCount() const noexcept;
    [[nodiscard]] size_t AliasSlotCount() const noexcept;
    [[nodiscard]] const EffectPhysicalPlanSummary& Summary() const noexcept;
    [[nodiscard]] bool Complete() const noexcept;

  private:
    friend EffectGraphPhysicalPlan BuildEffectGraphPhysicalPlan(
        const CompiledEffectGraph&,
        const DisplayEffectResourceTable&) noexcept;

    std::array<EffectPhysicalResource, kResourceCapacity> mResources{};
    std::array<EffectTransientAliasSlot, kResourceCapacity> mAliasSlots{};
    size_t mResourceCount = 0U;
    size_t mAliasSlotCount = 0U;
    EffectPhysicalPlanSummary mSummary{};
};

[[nodiscard]] EffectPhysicalResidency ClassifyEffectResourceResidency(
    EffectResource resource) noexcept;

[[nodiscard]] EffectTransientStorageClass
ClassifyEffectTransientStorage(EffectResource resource) noexcept;

[[nodiscard]] EffectGraphPhysicalPlan BuildEffectGraphPhysicalPlan(
    const CompiledEffectGraph& graph,
    const DisplayEffectResourceTable& resources) noexcept;

// Pre-dispatch declaration used by the graph-owned image arena. Unlike
// EffectGraphPhysicalPlan, this plan is built before providers execute and
// therefore does not depend on already-materialized resource bindings.
struct EffectTransientImageRequirement {
    EffectResource Resource = EffectResource::Count;
    uint32_t Format = 0U;
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    uint32_t MipLevels = 1U;
    uint32_t Layers = 1U;
    uint32_t Samples = 1U;
    uint32_t Usage = 0U;

    [[nodiscard]] bool Valid() const noexcept {
        return Resource != EffectResource::Count && Format != 0U &&
               Width != 0U && Height != 0U && MipLevels != 0U &&
               Layers != 0U && Samples != 0U && Usage != 0U;
    }
};

struct EffectTransientAllocationResource {
    EffectTransientImageRequirement Requirement;
    EffectTransientStorageClass StorageClass =
        EffectTransientStorageClass::None;
    size_t FirstUse = 0U;
    size_t LastUse = 0U;
    uint8_t Slot = kNoEffectAliasSlot;
};

struct EffectTransientAllocationSlot {
    EffectTransientStorageClass StorageClass =
        EffectTransientStorageClass::None;
    uint32_t Format = 0U;
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    uint32_t MipLevels = 1U;
    uint32_t Layers = 1U;
    uint32_t Samples = 1U;
    uint32_t Usage = 0U;
    size_t FirstUse = 0U;
    size_t LastUse = 0U;
    uint32_t ResourceMask = 0U;
    uint32_t ResourceCount = 0U;
};

struct EffectTransientAllocationSummary {
    uint32_t RequestedResourceCount = 0U;
    uint32_t PlannedResourceCount = 0U;
    uint32_t PhysicalSlotCount = 0U;
    uint32_t AliasOpportunityCount = 0U;
    uint32_t MissingLifetimeCount = 0U;
    uint32_t InvalidRequirementCount = 0U;
    uint32_t MissingLifetimeMask = 0U;
    uint32_t InvalidRequirementMask = 0U;
    uint32_t PeakLiveResourceCount = 0U;

    [[nodiscard]] bool Complete() const noexcept {
        return RequestedResourceCount == PlannedResourceCount &&
               MissingLifetimeCount == 0U &&
               InvalidRequirementCount == 0U;
    }
};

class EffectTransientAllocationPlan final {
  public:
    static constexpr size_t kCapacity =
        static_cast<size_t>(EffectResource::Count);

    [[nodiscard]] const EffectTransientAllocationResource* Find(
        EffectResource resource) const noexcept;
    [[nodiscard]] const EffectTransientAllocationResource& ResourceAt(
        size_t index) const noexcept;
    [[nodiscard]] const EffectTransientAllocationSlot& SlotAt(
        size_t index) const noexcept;
    [[nodiscard]] size_t ResourceCount() const noexcept;
    [[nodiscard]] size_t SlotCount() const noexcept;
    [[nodiscard]] const EffectTransientAllocationSummary& Summary()
        const noexcept;
    [[nodiscard]] bool Complete() const noexcept;

  private:
    friend EffectTransientAllocationPlan BuildEffectTransientAllocationPlan(
        const CompiledEffectGraph&,
        std::span<const EffectTransientImageRequirement>) noexcept;

    std::array<EffectTransientAllocationResource, kCapacity> mResources{};
    std::array<EffectTransientAllocationSlot, kCapacity> mSlots{};
    size_t mResourceCount = 0U;
    size_t mSlotCount = 0U;
    EffectTransientAllocationSummary mSummary{};
};

[[nodiscard]] EffectTransientAllocationPlan
BuildEffectTransientAllocationPlan(
    const CompiledEffectGraph& graph,
    std::span<const EffectTransientImageRequirement> requirements) noexcept;

} // namespace Fast::Oot3d
