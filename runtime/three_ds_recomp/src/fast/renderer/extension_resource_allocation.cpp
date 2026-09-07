#include "fast/renderer/extension_resource_allocation.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Fast::Renderer {
namespace {

[[nodiscard]] bool ResourceLess(
    const ExtensionResourceIdentity& left,
    const ExtensionResourceIdentity& right) noexcept {
    if (left.Namespace != right.Namespace) {
        return left.Namespace < right.Namespace;
    }
    if (left.Resource != right.Resource) {
        return left.Resource < right.Resource;
    }
    return left.SchemaVersion < right.SchemaVersion;
}

[[nodiscard]] bool CompatibleSlot(
    const ExtensionTransientAllocationSlot& slot,
    const ExtensionTransientAllocationResource& resource) noexcept {
    const auto& requirement = resource.Requirement;
    return slot.StorageClass == requirement.StorageClass &&
           slot.Format == requirement.Format &&
           slot.Width == requirement.Width &&
           slot.Height == requirement.Height &&
           slot.MipLevels == requirement.MipLevels &&
           slot.Layers == requirement.Layers &&
           slot.Samples == requirement.Samples &&
           slot.LastUse < resource.FirstUse;
}

} // namespace

CompiledExtensionTransientAllocation
CompileExtensionTransientAllocation(
    const CompiledExtensionResourceGraph& graph,
    std::span<const ExtensionTransientImageRequirement> requirements) {
    CompiledExtensionTransientAllocation result;
    result.Summary.RequestedResourceCount = static_cast<uint32_t>(
        std::min(requirements.size(),
                 static_cast<size_t>(
                     std::numeric_limits<uint32_t>::max())));
    if (!graph.Valid()) {
        result.Summary.InvalidRequirementCount =
            result.Summary.RequestedResourceCount;
        return result;
    }

    std::unordered_map<ExtensionResourceIdentity,
                       const ExtensionResourceLifetime*,
                       ExtensionResourceIdentityHash> lifetimes;
    lifetimes.reserve(graph.Lifetimes.size());
    for (const auto& lifetime : graph.Lifetimes) {
        lifetimes.emplace(lifetime.Resource, &lifetime);
    }

    std::unordered_set<ExtensionResourceIdentity,
                       ExtensionResourceIdentityHash> seen;
    seen.reserve(requirements.size());
    result.Resources.reserve(requirements.size());
    for (const auto& requirement : requirements) {
        const bool validIdentity = requirement.Resource.Valid();
        if (!requirement.Valid() ||
            !seen.insert(requirement.Resource).second) {
            ++result.Summary.InvalidRequirementCount;
            if (validIdentity) {
                result.InvalidRequirements.push_back(
                    requirement.Resource);
            }
            continue;
        }
        const auto lifetime = lifetimes.find(requirement.Resource);
        if (lifetime == lifetimes.end() ||
            lifetime->second->WriteCount == 0U) {
            ++result.Summary.MissingLifetimeCount;
            result.MissingLifetimes.push_back(requirement.Resource);
            continue;
        }
        result.Resources.push_back({
            requirement,
            lifetime->second->FirstUse,
            lifetime->second->LastUse,
            kNoExtensionAllocationSlot,
        });
        ++result.Summary.PlannedResourceCount;
    }

    std::vector<bool> assigned(result.Resources.size(), false);
    result.Slots.reserve(result.Resources.size());
    for (size_t iteration = 0U; iteration < result.Resources.size();
         ++iteration) {
        size_t selected = result.Resources.size();
        for (size_t index = 0U; index < result.Resources.size(); ++index) {
            const auto& candidate = result.Resources[index];
            if (assigned[index]) {
                continue;
            }
            if (selected == result.Resources.size() ||
                candidate.FirstUse < result.Resources[selected].FirstUse ||
                (candidate.FirstUse == result.Resources[selected].FirstUse &&
                 ResourceLess(candidate.Requirement.Resource,
                              result.Resources[selected]
                                  .Requirement.Resource))) {
                selected = index;
            }
        }
        if (selected == result.Resources.size()) {
            break;
        }
        assigned[selected] = true;
        auto& resource = result.Resources[selected];

        size_t slotIndex = result.Slots.size();
        for (size_t index = 0U; index < result.Slots.size(); ++index) {
            if (CompatibleSlot(result.Slots[index], resource)) {
                slotIndex = index;
                break;
            }
        }
        if (slotIndex == result.Slots.size()) {
            result.Slots.push_back({
                resource.Requirement.StorageClass,
                resource.Requirement.Format,
                resource.Requirement.Width,
                resource.Requirement.Height,
                resource.Requirement.MipLevels,
                resource.Requirement.Layers,
                resource.Requirement.Samples,
                resource.Requirement.Usage,
                resource.FirstUse,
                resource.LastUse,
                {},
            });
        } else {
            auto& slot = result.Slots[slotIndex];
            slot.LastUse = resource.LastUse;
            slot.Usage |= resource.Requirement.Usage;
        }
        result.Slots[slotIndex].Resources.push_back(
            resource.Requirement.Resource);
        resource.Slot = static_cast<uint32_t>(slotIndex);
    }

    result.Summary.PhysicalSlotCount =
        static_cast<uint32_t>(result.Slots.size());
    result.Summary.AliasOpportunityCount =
        result.Summary.PlannedResourceCount -
        result.Summary.PhysicalSlotCount;
    for (size_t passIndex = 0U; passIndex < graph.PassCount; ++passIndex) {
        uint32_t live = 0U;
        for (const auto& resource : result.Resources) {
            if (resource.FirstUse <= passIndex &&
                passIndex <= resource.LastUse) {
                ++live;
            }
        }
        result.Summary.PeakLiveResourceCount = std::max(
            result.Summary.PeakLiveResourceCount, live);
    }
    return result;
}

} // namespace Fast::Renderer
