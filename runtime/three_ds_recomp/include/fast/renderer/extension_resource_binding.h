#pragma once

#include "fast/renderer/extension_resource_allocation.h"
#include "fast/renderer/extension_resource_graph.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Fast::Renderer {

enum class ExtensionResourceBindingKind : uint8_t {
    None,
    Image,
    Semantic,
};

struct ExtensionObjectIdentity {
    uint64_t Namespace = 0U;
    uint64_t Object = 0U;
    uint32_t SchemaVersion = 0U;

    [[nodiscard]] bool Valid() const noexcept {
        return Namespace != 0U && Object != 0U && SchemaVersion != 0U;
    }

    bool operator==(const ExtensionObjectIdentity&) const = default;
};

struct ExtensionResourceBinding {
    ExtensionResourceIdentity Resource;
    ExtensionResourceBindingKind Kind =
        ExtensionResourceBindingKind::None;
    ExtensionObjectIdentity Object;
    uint64_t Generation = 0U;
    uint32_t Format = 0U;
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    bool Sampleable = false;

    [[nodiscard]] bool Bound() const noexcept;
};

struct ExtensionResourceReadValidation {
    uint32_t DeclaredReadCount = 0U;
    uint32_t ResolvedReadCount = 0U;
    uint32_t MissingReadCount = 0U;

    [[nodiscard]] bool Complete() const noexcept {
        return MissingReadCount == 0U;
    }
};

// Non-owning, allocation-free publication surface. A title or backend keeps
// its native handles and payloads, then publishes only portable metadata.
class ExtensionResourceBindingTableView final {
  public:
    explicit ExtensionResourceBindingTableView(
        std::span<const ExtensionResourceBinding> bindings = {}) noexcept;

    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] const ExtensionResourceBinding* Find(
        const ExtensionResourceIdentity& resource) const noexcept;
    [[nodiscard]] size_t BoundCount() const noexcept;
    [[nodiscard]] ExtensionResourceReadValidation ValidateReads(
        std::span<const ExtensionResourceUse> uses) const noexcept;

  private:
    std::span<const ExtensionResourceBinding> mBindings;
};

struct ExtensionPhysicalResourcePolicy {
    ExtensionResourceIdentity Resource;
    ExtensionResourceResidency Residency =
        ExtensionResourceResidency::Semantic;
    ExtensionStorageClassIdentity StorageClass;

    [[nodiscard]] bool Valid() const noexcept;
};

struct ExtensionPhysicalResource {
    ExtensionResourceIdentity Resource;
    ExtensionResourceResidency Residency =
        ExtensionResourceResidency::Semantic;
    ExtensionStorageClassIdentity StorageClass;
    size_t FirstUse = 0U;
    size_t LastUse = 0U;
    uint32_t ReadCount = 0U;
    uint32_t WriteCount = 0U;
    ExtensionResourceBinding Binding;
    uint32_t AliasSlot = kNoExtensionAllocationSlot;
    bool Resolved = false;

    [[nodiscard]] bool AliasEligible() const noexcept {
        return Resolved &&
               Residency == ExtensionResourceResidency::TransientImage &&
               StorageClass.Valid();
    }
};

struct ExtensionPhysicalAliasSlot {
    ExtensionStorageClassIdentity StorageClass;
    uint32_t Format = 0U;
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    size_t FirstUse = 0U;
    size_t LastUse = 0U;
    uint32_t ResourceCount = 0U;
};

struct ExtensionPhysicalPlanSummary {
    uint32_t DeclaredResourceCount = 0U;
    uint32_t ResolvedResourceCount = 0U;
    uint32_t MissingResourceCount = 0U;
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

enum class ExtensionPhysicalPlanStatus : uint8_t {
    Ready,
    InvalidGraph,
    InvalidPolicy,
    InvalidBindings,
    InsufficientOutputCapacity,
};

struct CompiledExtensionPhysicalPlan {
    ExtensionPhysicalPlanStatus Status =
        ExtensionPhysicalPlanStatus::InvalidGraph;
    size_t ResourceCount = 0U;
    size_t AliasSlotCount = 0U;
    ExtensionPhysicalPlanSummary Summary;

    [[nodiscard]] bool Valid() const noexcept {
        return Status == ExtensionPhysicalPlanStatus::Ready;
    }
    [[nodiscard]] bool Complete() const noexcept {
        return Valid() && Summary.Complete();
    }
};

// Resolves graph resources against portable bindings and computes physical
// image accounting and alias candidates. Output storage is caller-owned so a
// backend can use fixed-capacity frame memory without heap allocation.
[[nodiscard]] CompiledExtensionPhysicalPlan CompileExtensionPhysicalPlan(
    const CompiledExtensionResourceGraph& graph,
    std::span<const ExtensionPhysicalResourcePolicy> policies,
    ExtensionResourceBindingTableView bindings,
    std::span<ExtensionPhysicalResource> resourceStorage,
    std::span<ExtensionPhysicalAliasSlot> aliasSlotStorage) noexcept;

} // namespace Fast::Renderer
