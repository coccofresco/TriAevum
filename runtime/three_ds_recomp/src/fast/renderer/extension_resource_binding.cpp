#include "fast/renderer/extension_resource_binding.h"

#include <algorithm>

namespace Fast::Renderer {
namespace {

[[nodiscard]] constexpr bool ValidResidency(
    ExtensionResourceResidency residency) noexcept {
    switch (residency) {
        case ExtensionResourceResidency::Semantic:
        case ExtensionResourceResidency::ExternalImage:
        case ExtensionResourceResidency::NativeAttachment:
        case ExtensionResourceResidency::TransientImage:
        case ExtensionResourceResidency::TemporalHistory:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr ExtensionResourceBindingKind ExpectedBindingKind(
    ExtensionResourceResidency residency) noexcept {
    return residency == ExtensionResourceResidency::Semantic
        ? ExtensionResourceBindingKind::Semantic
        : ExtensionResourceBindingKind::Image;
}

[[nodiscard]] bool CompatibleAliasSlot(
    const ExtensionPhysicalAliasSlot& slot,
    const ExtensionPhysicalResource& resource) noexcept {
    return slot.StorageClass == resource.StorageClass &&
           slot.Format == resource.Binding.Format &&
           slot.Width == resource.Binding.Width &&
           slot.Height == resource.Binding.Height &&
           slot.LastUse < resource.FirstUse;
}

[[nodiscard]] const ExtensionPhysicalResourcePolicy* FindPolicy(
    std::span<const ExtensionPhysicalResourcePolicy> policies,
    const ExtensionResourceIdentity& resource) noexcept {
    const ExtensionPhysicalResourcePolicy* found = nullptr;
    for (const auto& policy : policies) {
        if (policy.Resource != resource) {
            continue;
        }
        if (found != nullptr) {
            return nullptr;
        }
        found = &policy;
    }
    return found;
}

} // namespace

bool ExtensionResourceBinding::Bound() const noexcept {
    if (!Resource.Valid() || !Object.Valid()) {
        return false;
    }
    switch (Kind) {
        case ExtensionResourceBindingKind::Image:
            return Format != 0U && Width != 0U && Height != 0U;
        case ExtensionResourceBindingKind::Semantic:
            return Format == 0U && Width == 0U && Height == 0U &&
                   !Sampleable;
        case ExtensionResourceBindingKind::None:
            return false;
    }
    return false;
}

ExtensionResourceBindingTableView::ExtensionResourceBindingTableView(
    std::span<const ExtensionResourceBinding> bindings) noexcept
    : mBindings(bindings) {
}

bool ExtensionResourceBindingTableView::Valid() const noexcept {
    for (size_t index = 0U; index < mBindings.size(); ++index) {
        const auto& binding = mBindings[index];
        if (binding.Kind == ExtensionResourceBindingKind::None) {
            continue;
        }
        if (!binding.Bound()) {
            return false;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (mBindings[previous].Bound() &&
                mBindings[previous].Resource == binding.Resource) {
                return false;
            }
        }
    }
    return true;
}

const ExtensionResourceBinding* ExtensionResourceBindingTableView::Find(
    const ExtensionResourceIdentity& resource) const noexcept {
    if (!resource.Valid()) {
        return nullptr;
    }
    const auto found = std::find_if(
        mBindings.begin(), mBindings.end(),
        [&resource](const ExtensionResourceBinding& binding) {
            return binding.Bound() && binding.Resource == resource;
        });
    return found == mBindings.end() ? nullptr : &*found;
}

size_t ExtensionResourceBindingTableView::BoundCount() const noexcept {
    return static_cast<size_t>(std::count_if(
        mBindings.begin(), mBindings.end(),
        [](const ExtensionResourceBinding& binding) {
            return binding.Bound();
        }));
}

ExtensionResourceReadValidation
ExtensionResourceBindingTableView::ValidateReads(
    std::span<const ExtensionResourceUse> uses) const noexcept {
    ExtensionResourceReadValidation result;
    for (const auto& use : uses) {
        if (!ReadsExtensionResource(use.Access)) {
            continue;
        }
        ++result.DeclaredReadCount;
        if (Find(use.Resource) != nullptr) {
            ++result.ResolvedReadCount;
        } else {
            ++result.MissingReadCount;
        }
    }
    return result;
}

bool ExtensionPhysicalResourcePolicy::Valid() const noexcept {
    return Resource.Valid() && ValidResidency(Residency) &&
           (Residency != ExtensionResourceResidency::TransientImage ||
            StorageClass.Valid());
}

CompiledExtensionPhysicalPlan CompileExtensionPhysicalPlan(
    const CompiledExtensionResourceGraph& graph,
    std::span<const ExtensionPhysicalResourcePolicy> policies,
    ExtensionResourceBindingTableView bindings,
    std::span<ExtensionPhysicalResource> resourceStorage,
    std::span<ExtensionPhysicalAliasSlot> aliasSlotStorage) noexcept {
    CompiledExtensionPhysicalPlan result;
    if (!graph.Valid()) {
        result.Status = ExtensionPhysicalPlanStatus::InvalidGraph;
        return result;
    }
    if (!bindings.Valid()) {
        result.Status = ExtensionPhysicalPlanStatus::InvalidBindings;
        return result;
    }
    for (size_t index = 0U; index < policies.size(); ++index) {
        if (!policies[index].Valid()) {
            result.Status = ExtensionPhysicalPlanStatus::InvalidPolicy;
            return result;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (policies[previous].Resource == policies[index].Resource) {
                result.Status = ExtensionPhysicalPlanStatus::InvalidPolicy;
                return result;
            }
        }
    }
    if (resourceStorage.size() < graph.Lifetimes.size() ||
        aliasSlotStorage.size() < graph.Lifetimes.size()) {
        result.Status =
            ExtensionPhysicalPlanStatus::InsufficientOutputCapacity;
        return result;
    }

    std::fill(resourceStorage.begin(), resourceStorage.end(),
              ExtensionPhysicalResource{});
    std::fill(aliasSlotStorage.begin(), aliasSlotStorage.end(),
              ExtensionPhysicalAliasSlot{});
    result.Status = ExtensionPhysicalPlanStatus::Ready;

    for (const auto& lifetime : graph.Lifetimes) {
        const auto* policy = FindPolicy(policies, lifetime.Resource);
        if (policy == nullptr) {
            result.Status = ExtensionPhysicalPlanStatus::InvalidPolicy;
            result.ResourceCount = 0U;
            result.AliasSlotCount = 0U;
            result.Summary = {};
            return result;
        }
        auto& destination = resourceStorage[result.ResourceCount++];
        destination.Resource = lifetime.Resource;
        destination.Residency = policy->Residency;
        destination.StorageClass = policy->StorageClass;
        destination.FirstUse = lifetime.FirstUse;
        destination.LastUse = lifetime.LastUse;
        destination.ReadCount = lifetime.ReadCount;
        destination.WriteCount = lifetime.WriteCount;

        ++result.Summary.DeclaredResourceCount;
        switch (destination.Residency) {
            case ExtensionResourceResidency::Semantic:
                ++result.Summary.SemanticResourceCount;
                break;
            case ExtensionResourceResidency::ExternalImage:
                ++result.Summary.ExternalImageResourceCount;
                break;
            case ExtensionResourceResidency::NativeAttachment:
                ++result.Summary.NativeAttachmentResourceCount;
                break;
            case ExtensionResourceResidency::TransientImage:
                ++result.Summary.TransientImageResourceCount;
                break;
            case ExtensionResourceResidency::TemporalHistory:
                ++result.Summary.TemporalHistoryResourceCount;
                break;
        }

        const auto* binding = bindings.Find(lifetime.Resource);
        if (binding == nullptr ||
            binding->Kind != ExpectedBindingKind(destination.Residency)) {
            ++result.Summary.MissingResourceCount;
            continue;
        }
        destination.Binding = *binding;
        destination.Resolved = true;
        ++result.Summary.ResolvedResourceCount;
        if (binding->Kind != ExtensionResourceBindingKind::Image) {
            continue;
        }
        bool duplicateImage = false;
        for (size_t previous = 0U;
             previous + 1U < result.ResourceCount; ++previous) {
            const auto& previousResource = resourceStorage[previous];
            if (previousResource.Resolved &&
                previousResource.Binding.Kind ==
                    ExtensionResourceBindingKind::Image &&
                previousResource.Binding.Object == binding->Object) {
                duplicateImage = true;
                break;
            }
        }
        if (!duplicateImage) {
            ++result.Summary.PhysicalImageCount;
        }
    }

    for (size_t iteration = 0U; iteration < result.ResourceCount;
         ++iteration) {
        size_t selected = result.ResourceCount;
        for (size_t index = 0U; index < result.ResourceCount; ++index) {
            const auto& candidate = resourceStorage[index];
            if (!candidate.AliasEligible() ||
                candidate.AliasSlot != kNoExtensionAllocationSlot) {
                continue;
            }
            if (selected == result.ResourceCount ||
                candidate.FirstUse < resourceStorage[selected].FirstUse ||
                (candidate.FirstUse == resourceStorage[selected].FirstUse &&
                 candidate.Resource.Resource <
                     resourceStorage[selected].Resource.Resource)) {
                selected = index;
            }
        }
        if (selected == result.ResourceCount) {
            break;
        }
        auto& resource = resourceStorage[selected];
        ++result.Summary.AliasEligibleResourceCount;

        size_t slotIndex = result.AliasSlotCount;
        for (size_t index = 0U; index < result.AliasSlotCount; ++index) {
            if (CompatibleAliasSlot(aliasSlotStorage[index], resource)) {
                slotIndex = index;
                break;
            }
        }
        if (slotIndex == result.AliasSlotCount) {
            auto& slot = aliasSlotStorage[result.AliasSlotCount++];
            slot.StorageClass = resource.StorageClass;
            slot.Format = resource.Binding.Format;
            slot.Width = resource.Binding.Width;
            slot.Height = resource.Binding.Height;
            slot.FirstUse = resource.FirstUse;
            slot.LastUse = resource.LastUse;
        } else {
            aliasSlotStorage[slotIndex].LastUse = resource.LastUse;
        }
        ++aliasSlotStorage[slotIndex].ResourceCount;
        resource.AliasSlot = static_cast<uint32_t>(slotIndex);
    }

    result.Summary.AliasSlotCount =
        static_cast<uint32_t>(result.AliasSlotCount);
    result.Summary.AliasOpportunityCount =
        result.Summary.AliasEligibleResourceCount -
        result.Summary.AliasSlotCount;
    for (size_t passIndex = 0U; passIndex < graph.PassCount; ++passIndex) {
        uint32_t live = 0U;
        for (size_t index = 0U; index < result.ResourceCount; ++index) {
            const auto& resource = resourceStorage[index];
            if (resource.Residency ==
                    ExtensionResourceResidency::TransientImage &&
                resource.FirstUse <= passIndex &&
                passIndex <= resource.LastUse) {
                ++live;
            }
        }
        result.Summary.PeakLiveTransientCount = std::max(
            result.Summary.PeakLiveTransientCount, live);
    }
    return result;
}

} // namespace Fast::Renderer
