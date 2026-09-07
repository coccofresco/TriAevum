#include "fast/oot3d/effect_graph_physical_plan.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

namespace Fast::Oot3d {
namespace {

[[nodiscard]] constexpr uint32_t ResourceBit(
    EffectResource resource) noexcept {
    return 1U << static_cast<uint32_t>(resource);
}

inline constexpr uint64_t kOot3dTransientStorageNamespace =
    0x4F4F543344535443ULL; // OOT3DSTC
inline constexpr uint32_t kOot3dTransientStorageSchemaVersion = 1U;

[[nodiscard]] constexpr
::Fast::Renderer::ExtensionStorageClassIdentity
BuildTransientStorageIdentity(
    EffectTransientStorageClass storageClass) noexcept {
    if (storageClass == EffectTransientStorageClass::None) {
        return {};
    }
    return {
        kOot3dTransientStorageNamespace,
        static_cast<uint64_t>(storageClass),
        kOot3dTransientStorageSchemaVersion,
    };
}

[[nodiscard]] constexpr std::optional<EffectTransientStorageClass>
ResolveTransientStorageIdentity(
    const ::Fast::Renderer::ExtensionStorageClassIdentity& identity)
    noexcept {
    if (identity.Namespace != kOot3dTransientStorageNamespace ||
        identity.SchemaVersion !=
            kOot3dTransientStorageSchemaVersion ||
        identity.Class == 0U ||
        identity.Class > static_cast<uint64_t>(
                             EffectTransientStorageClass::ShadowDepth)) {
        return std::nullopt;
    }
    return static_cast<EffectTransientStorageClass>(identity.Class);
}

} // namespace

EffectPhysicalResidency ClassifyEffectResourceResidency(
    EffectResource resource) noexcept {
    switch (resource) {
        case EffectResource::PicaSceneFrame:
        case EffectResource::NativeSceneView:
        case EffectResource::ExtensionGeometry:
            return EffectPhysicalResidency::Semantic;
        case EffectResource::SceneColor:
        case EffectResource::NativeDepth:
        case EffectResource::NativeShadow2D:
        case EffectResource::PresentationOutput:
            return EffectPhysicalResidency::ExternalImage;
        case EffectResource::NormalGuide:
        case EffectResource::MaterialGuide:
        case EffectResource::RigidMotionGuide:
        case EffectResource::AmbientGuide:
        case EffectResource::FogGuide:
        case EffectResource::OutlineGeometryGuide:
            return EffectPhysicalResidency::NativeAttachment;
        case EffectResource::TemporalColor:
        case EffectResource::DirectionalShadowHistory:
        case EffectResource::DirectionalShadowMap:
            return EffectPhysicalResidency::TemporalHistory;
        case EffectResource::HierarchicalDepth:
        case EffectResource::AmbientOcclusion:
        case EffectResource::ReflectionColor:
        case EffectResource::OutlineColor:
        case EffectResource::MotionVectors:
        case EffectResource::ReactiveMask:
        case EffectResource::LinearWorkingColor:
        case EffectResource::CompositeColor:
        case EffectResource::UpscaledColor:
        case EffectResource::AntiAliasedColor:
            return EffectPhysicalResidency::TransientImage;
        case EffectResource::Count:
            break;
    }
    return EffectPhysicalResidency::Semantic;
}

EffectTransientStorageClass ClassifyEffectTransientStorage(
    EffectResource resource) noexcept {
    switch (resource) {
        case EffectResource::DirectionalShadowMap:
        case EffectResource::DirectionalShadowHistory:
            return EffectTransientStorageClass::ShadowDepth;
        case EffectResource::HierarchicalDepth:
            return EffectTransientStorageClass::DepthPyramid;
        case EffectResource::AmbientOcclusion:
        case EffectResource::ReflectionColor:
        case EffectResource::OutlineColor:
        case EffectResource::MotionVectors:
        case EffectResource::ReactiveMask:
        case EffectResource::LinearWorkingColor:
        case EffectResource::CompositeColor:
        case EffectResource::UpscaledColor:
        case EffectResource::AntiAliasedColor:
            return EffectTransientStorageClass::SinglePlaneImage;
        default:
            return EffectTransientStorageClass::None;
    }
}

const EffectPhysicalResource* EffectGraphPhysicalPlan::Find(
    EffectResource resource) const noexcept {
    const auto found = std::find_if(
        mResources.begin(), mResources.begin() + mResourceCount,
        [resource](const EffectPhysicalResource& candidate) {
            return candidate.Resource == resource;
        });
    return found == mResources.begin() + mResourceCount
               ? nullptr
               : &*found;
}

const EffectPhysicalResource& EffectGraphPhysicalPlan::ResourceAt(
    size_t index) const noexcept {
    return mResources[index];
}

const EffectTransientAliasSlot& EffectGraphPhysicalPlan::AliasSlotAt(
    size_t index) const noexcept {
    return mAliasSlots[index];
}

size_t EffectGraphPhysicalPlan::ResourceCount() const noexcept {
    return mResourceCount;
}

size_t EffectGraphPhysicalPlan::AliasSlotCount() const noexcept {
    return mAliasSlotCount;
}

const EffectPhysicalPlanSummary& EffectGraphPhysicalPlan::Summary()
    const noexcept {
    return mSummary;
}

bool EffectGraphPhysicalPlan::Complete() const noexcept {
    return mSummary.Complete();
}

EffectGraphPhysicalPlan BuildEffectGraphPhysicalPlan(
    const CompiledEffectGraph& graph,
    const DisplayEffectResourceTable& resources) noexcept {
    EffectGraphPhysicalPlan plan;
    if (!graph.Valid()) {
        return plan;
    }

    std::array<::Fast::Renderer::ExtensionPhysicalResourcePolicy,
               EffectGraphPhysicalPlan::kResourceCapacity> policies{};
    size_t policyCount = 0U;
    for (const auto& lifetime : graph.ResourceLifetimes) {
        if (lifetime.Resource == EffectResource::Count ||
            policyCount >= policies.size()) {
            continue;
        }
        const auto storageClass =
            ClassifyEffectTransientStorage(lifetime.Resource);
        policies[policyCount++] = {
            BuildOot3dEffectResourceIdentity(lifetime.Resource),
            ClassifyEffectResourceResidency(lifetime.Resource),
            BuildTransientStorageIdentity(storageClass),
        };
    }

    std::array<::Fast::Renderer::ExtensionPhysicalResource,
               EffectGraphPhysicalPlan::kResourceCapacity>
        portableResources{};
    std::array<::Fast::Renderer::ExtensionPhysicalAliasSlot,
               EffectGraphPhysicalPlan::kResourceCapacity>
        portableSlots{};
    const auto portable =
        ::Fast::Renderer::CompileExtensionPhysicalPlan(
            graph.PortableResources,
            std::span<const ::Fast::Renderer::
                          ExtensionPhysicalResourcePolicy>(
                policies.data(), policyCount),
            resources.PortableBindings(), portableResources,
            portableSlots);
    if (!portable.Valid()) {
        return plan;
    }

    plan.mSummary.DeclaredResourceCount =
        portable.Summary.DeclaredResourceCount;
    plan.mSummary.ResolvedResourceCount =
        portable.Summary.ResolvedResourceCount;
    plan.mSummary.MissingResourceCount =
        portable.Summary.MissingResourceCount;
    plan.mSummary.SemanticResourceCount =
        portable.Summary.SemanticResourceCount;
    plan.mSummary.ExternalImageResourceCount =
        portable.Summary.ExternalImageResourceCount;
    plan.mSummary.NativeAttachmentResourceCount =
        portable.Summary.NativeAttachmentResourceCount;
    plan.mSummary.TransientImageResourceCount =
        portable.Summary.TransientImageResourceCount;
    plan.mSummary.TemporalHistoryResourceCount =
        portable.Summary.TemporalHistoryResourceCount;
    plan.mSummary.PhysicalImageCount =
        portable.Summary.PhysicalImageCount;
    plan.mSummary.AliasEligibleResourceCount =
        portable.Summary.AliasEligibleResourceCount;
    plan.mSummary.AliasSlotCount = portable.Summary.AliasSlotCount;
    plan.mSummary.AliasOpportunityCount =
        portable.Summary.AliasOpportunityCount;
    plan.mSummary.PeakLiveTransientCount =
        portable.Summary.PeakLiveTransientCount;

    for (size_t index = 0U; index < portable.ResourceCount; ++index) {
        const auto& portableResource = portableResources[index];
        const auto resource = ResolveOot3dEffectResourceIdentity(
            portableResource.Resource);
        if (!resource.has_value() ||
            plan.mResourceCount >= plan.mResources.size()) {
            continue;
        }
        auto& destination = plan.mResources[plan.mResourceCount++];
        destination.Resource = *resource;
        destination.Residency = portableResource.Residency;
        const auto storageClass = ResolveTransientStorageIdentity(
            portableResource.StorageClass);
        destination.StorageClass = storageClass.value_or(
            EffectTransientStorageClass::None);
        destination.FirstUse = portableResource.FirstUse;
        destination.LastUse = portableResource.LastUse;
        destination.ReadCount = portableResource.ReadCount;
        destination.WriteCount = portableResource.WriteCount;
        destination.Resolved = portableResource.Resolved;
        if (portableResource.Resolved) {
            const auto* binding = resources.Find(*resource);
            if (binding != nullptr) {
                destination.Binding = *binding;
            } else {
                destination.Resolved = false;
            }
        }
        if (!destination.Resolved) {
            plan.mSummary.MissingResourceMask |= ResourceBit(*resource);
        }
        if (portableResource.AliasSlot < kNoEffectAliasSlot) {
            destination.AliasSlot = static_cast<uint8_t>(
                portableResource.AliasSlot);
        }
    }

    for (size_t slotIndex = 0U;
         slotIndex < portable.AliasSlotCount; ++slotIndex) {
        const auto& portableSlot = portableSlots[slotIndex];
        if (plan.mAliasSlotCount >= plan.mAliasSlots.size()) {
            break;
        }
        const auto storageClass = ResolveTransientStorageIdentity(
            portableSlot.StorageClass);
        if (!storageClass.has_value()) {
            continue;
        }
        auto& destination = plan.mAliasSlots[plan.mAliasSlotCount++];
        destination.StorageClass = *storageClass;
        destination.Format = portableSlot.Format;
        destination.Width = portableSlot.Width;
        destination.Height = portableSlot.Height;
        destination.FirstUse = portableSlot.FirstUse;
        destination.LastUse = portableSlot.LastUse;
        destination.ResourceCount = portableSlot.ResourceCount;
        for (size_t resourceIndex = 0U;
             resourceIndex < portable.ResourceCount; ++resourceIndex) {
            const auto& portableResource = portableResources[resourceIndex];
            if (portableResource.AliasSlot != slotIndex) {
                continue;
            }
            const auto resource = ResolveOot3dEffectResourceIdentity(
                portableResource.Resource);
            if (resource.has_value()) {
                destination.ResourceMask |= ResourceBit(*resource);
            }
        }
    }
    return plan;
}

const EffectTransientAllocationResource*
EffectTransientAllocationPlan::Find(
    EffectResource resource) const noexcept {
    const auto found = std::find_if(
        mResources.begin(), mResources.begin() + mResourceCount,
        [resource](const EffectTransientAllocationResource& candidate) {
            return candidate.Requirement.Resource == resource;
        });
    return found == mResources.begin() + mResourceCount
               ? nullptr
               : &*found;
}

const EffectTransientAllocationResource&
EffectTransientAllocationPlan::ResourceAt(size_t index) const noexcept {
    return mResources[index];
}

const EffectTransientAllocationSlot&
EffectTransientAllocationPlan::SlotAt(size_t index) const noexcept {
    return mSlots[index];
}

size_t EffectTransientAllocationPlan::ResourceCount() const noexcept {
    return mResourceCount;
}

size_t EffectTransientAllocationPlan::SlotCount() const noexcept {
    return mSlotCount;
}

const EffectTransientAllocationSummary&
EffectTransientAllocationPlan::Summary() const noexcept {
    return mSummary;
}

bool EffectTransientAllocationPlan::Complete() const noexcept {
    return mSummary.Complete();
}

EffectTransientAllocationPlan BuildEffectTransientAllocationPlan(
    const CompiledEffectGraph& graph,
    std::span<const EffectTransientImageRequirement> requirements) noexcept {
    EffectTransientAllocationPlan plan;
    if (!graph.Valid()) {
        plan.mSummary.RequestedResourceCount = static_cast<uint32_t>(
            std::min(requirements.size(),
                     static_cast<size_t>(
                         std::numeric_limits<uint32_t>::max())));
        plan.mSummary.InvalidRequirementCount =
            plan.mSummary.RequestedResourceCount;
        return plan;
    }

    std::vector<::Fast::Renderer::ExtensionTransientImageRequirement>
        portableRequirements;
    portableRequirements.reserve(requirements.size());
    for (const auto& requirement : requirements) {
        const size_t resourceIndex =
            static_cast<size_t>(requirement.Resource);
        const bool resourceInRange =
            resourceIndex < EffectTransientAllocationPlan::kCapacity;
        const auto residency = resourceInRange
            ? ClassifyEffectResourceResidency(requirement.Resource)
            : EffectPhysicalResidency::Semantic;
        const auto storageClass = resourceInRange
            ? ClassifyEffectTransientStorage(requirement.Resource)
            : EffectTransientStorageClass::None;
        portableRequirements.push_back({
            BuildOot3dEffectResourceIdentity(requirement.Resource),
            residency,
            BuildTransientStorageIdentity(storageClass),
            requirement.Format,
            requirement.Width,
            requirement.Height,
            requirement.MipLevels,
            requirement.Layers,
            requirement.Samples,
            requirement.Usage,
        });
    }

    const auto portable =
        ::Fast::Renderer::CompileExtensionTransientAllocation(
            graph.PortableResources, portableRequirements);
    const auto& portableSummary = portable.Summary;
    plan.mSummary.RequestedResourceCount =
        portableSummary.RequestedResourceCount;
    plan.mSummary.PlannedResourceCount =
        portableSummary.PlannedResourceCount;
    plan.mSummary.PhysicalSlotCount =
        portableSummary.PhysicalSlotCount;
    plan.mSummary.AliasOpportunityCount =
        portableSummary.AliasOpportunityCount;
    plan.mSummary.MissingLifetimeCount =
        portableSummary.MissingLifetimeCount;
    plan.mSummary.InvalidRequirementCount =
        portableSummary.InvalidRequirementCount;
    plan.mSummary.PeakLiveResourceCount =
        portableSummary.PeakLiveResourceCount;

    for (const auto& identity : portable.MissingLifetimes) {
        const auto resource =
            ResolveOot3dEffectResourceIdentity(identity);
        if (resource.has_value()) {
            plan.mSummary.MissingLifetimeMask |= ResourceBit(*resource);
        }
    }
    for (const auto& identity : portable.InvalidRequirements) {
        const auto resource =
            ResolveOot3dEffectResourceIdentity(identity);
        if (resource.has_value()) {
            plan.mSummary.InvalidRequirementMask |= ResourceBit(*resource);
        }
    }

    for (const auto& portableResource : portable.Resources) {
        const auto resource = ResolveOot3dEffectResourceIdentity(
            portableResource.Requirement.Resource);
        const auto storageClass = ResolveTransientStorageIdentity(
            portableResource.Requirement.StorageClass);
        if (!resource.has_value() || !storageClass.has_value() ||
            portableResource.Slot >= kNoEffectAliasSlot ||
            plan.mResourceCount >= plan.mResources.size()) {
            ++plan.mSummary.InvalidRequirementCount;
            if (resource.has_value()) {
                plan.mSummary.InvalidRequirementMask |=
                    ResourceBit(*resource);
            }
            if (plan.mSummary.PlannedResourceCount != 0U) {
                --plan.mSummary.PlannedResourceCount;
            }
            continue;
        }
        auto& destination = plan.mResources[plan.mResourceCount++];
        destination.Requirement = {
            *resource,
            portableResource.Requirement.Format,
            portableResource.Requirement.Width,
            portableResource.Requirement.Height,
            portableResource.Requirement.MipLevels,
            portableResource.Requirement.Layers,
            portableResource.Requirement.Samples,
            portableResource.Requirement.Usage,
        };
        destination.StorageClass = *storageClass;
        destination.FirstUse = portableResource.FirstUse;
        destination.LastUse = portableResource.LastUse;
        destination.Slot = static_cast<uint8_t>(portableResource.Slot);
    }

    for (const auto& portableSlot : portable.Slots) {
        const auto storageClass = ResolveTransientStorageIdentity(
            portableSlot.StorageClass);
        if (!storageClass.has_value() ||
            plan.mSlotCount >= plan.mSlots.size()) {
            ++plan.mSummary.InvalidRequirementCount;
            continue;
        }
        auto& destination = plan.mSlots[plan.mSlotCount++];
        destination.StorageClass = *storageClass;
        destination.Format = portableSlot.Format;
        destination.Width = portableSlot.Width;
        destination.Height = portableSlot.Height;
        destination.MipLevels = portableSlot.MipLevels;
        destination.Layers = portableSlot.Layers;
        destination.Samples = portableSlot.Samples;
        destination.Usage = portableSlot.Usage;
        destination.FirstUse = portableSlot.FirstUse;
        destination.LastUse = portableSlot.LastUse;
        for (const auto& identity : portableSlot.Resources) {
            const auto resource =
                ResolveOot3dEffectResourceIdentity(identity);
            if (!resource.has_value()) {
                ++plan.mSummary.InvalidRequirementCount;
                continue;
            }
            destination.ResourceMask |= ResourceBit(*resource);
            ++destination.ResourceCount;
        }
    }
    return plan;
}

} // namespace Fast::Oot3d
