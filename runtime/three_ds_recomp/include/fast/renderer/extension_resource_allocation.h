#pragma once

#include "fast/renderer/extension_resource_graph.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace Fast::Renderer {

enum class ExtensionResourceResidency : uint8_t {
    Semantic,
    ExternalImage,
    NativeAttachment,
    TransientImage,
    TemporalHistory,
};

struct ExtensionStorageClassIdentity {
    uint64_t Namespace = 0U;
    uint64_t Class = 0U;
    uint32_t SchemaVersion = 0U;

    [[nodiscard]] bool Valid() const noexcept {
        return Namespace != 0U && Class != 0U && SchemaVersion != 0U;
    }

    bool operator==(const ExtensionStorageClassIdentity&) const = default;
};

inline constexpr uint32_t kNoExtensionAllocationSlot =
    std::numeric_limits<uint32_t>::max();

struct ExtensionTransientImageRequirement {
    ExtensionResourceIdentity Resource;
    ExtensionResourceResidency Residency =
        ExtensionResourceResidency::Semantic;
    ExtensionStorageClassIdentity StorageClass;
    uint32_t Format = 0U;
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    uint32_t MipLevels = 1U;
    uint32_t Layers = 1U;
    uint32_t Samples = 1U;
    uint32_t Usage = 0U;

    [[nodiscard]] bool Valid() const noexcept {
        return Resource.Valid() &&
               Residency == ExtensionResourceResidency::TransientImage &&
               StorageClass.Valid() && Format != 0U && Width != 0U &&
               Height != 0U && MipLevels != 0U && Layers != 0U &&
               Samples != 0U && Usage != 0U;
    }
};

struct ExtensionTransientAllocationResource {
    ExtensionTransientImageRequirement Requirement;
    size_t FirstUse = 0U;
    size_t LastUse = 0U;
    uint32_t Slot = kNoExtensionAllocationSlot;
};

struct ExtensionTransientAllocationSlot {
    ExtensionStorageClassIdentity StorageClass;
    uint32_t Format = 0U;
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    uint32_t MipLevels = 1U;
    uint32_t Layers = 1U;
    uint32_t Samples = 1U;
    uint32_t Usage = 0U;
    size_t FirstUse = 0U;
    size_t LastUse = 0U;
    std::vector<ExtensionResourceIdentity> Resources;
};

struct ExtensionTransientAllocationSummary {
    uint32_t RequestedResourceCount = 0U;
    uint32_t PlannedResourceCount = 0U;
    uint32_t PhysicalSlotCount = 0U;
    uint32_t AliasOpportunityCount = 0U;
    uint32_t MissingLifetimeCount = 0U;
    uint32_t InvalidRequirementCount = 0U;
    uint32_t PeakLiveResourceCount = 0U;

    [[nodiscard]] bool Complete() const noexcept {
        return RequestedResourceCount == PlannedResourceCount &&
               MissingLifetimeCount == 0U &&
               InvalidRequirementCount == 0U;
    }
};

struct CompiledExtensionTransientAllocation {
    std::vector<ExtensionTransientAllocationResource> Resources;
    std::vector<ExtensionTransientAllocationSlot> Slots;
    std::vector<ExtensionResourceIdentity> MissingLifetimes;
    std::vector<ExtensionResourceIdentity> InvalidRequirements;
    ExtensionTransientAllocationSummary Summary;
};

// Plans compatible, non-overlapping image aliases from graph-derived
// lifetimes. Formats and storage classes are supplied by the title/provider
// adapter; this planner does not choose native API formats or usage policy.
[[nodiscard]] CompiledExtensionTransientAllocation
CompileExtensionTransientAllocation(
    const CompiledExtensionResourceGraph& graph,
    std::span<const ExtensionTransientImageRequirement> requirements);

} // namespace Fast::Renderer
