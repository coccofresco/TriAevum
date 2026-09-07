#include "fast/renderer/extension_scene_publication.h"

#include <algorithm>

namespace Fast::Renderer {

bool ExtensionSceneCapabilityPublication::Empty() const noexcept {
    return Capability == ExtensionSceneCapabilityIdentity{} &&
           Object == ExtensionObjectIdentity{} && Payload == nullptr &&
           PayloadSchemaVersion == 0U && Generation == 0U && FrameId == 0U;
}

bool ExtensionSceneCapabilityPublication::Published() const noexcept {
    return Capability.Valid() && Object.Valid() && Payload != nullptr &&
           PayloadSchemaVersion != 0U && Generation != 0U && FrameId != 0U;
}

bool ExtensionSceneCapabilityRequirement::Valid() const noexcept {
    return Capability.Valid() && MinimumSchemaVersion != 0U &&
           MaximumSchemaVersion >= MinimumSchemaVersion;
}

bool ExtensionSceneCapabilityRequirement::Accepts(
    uint32_t schemaVersion) const noexcept {
    return Valid() && schemaVersion >= MinimumSchemaVersion &&
           schemaVersion <= MaximumSchemaVersion;
}

ExtensionScenePublicationTableView::ExtensionScenePublicationTableView(
    uint64_t frameId,
    std::span<const ExtensionSceneCapabilityPublication> publications) noexcept
    : mFrameId(frameId), mPublications(publications) {
}

bool ExtensionScenePublicationTableView::Valid() const noexcept {
    bool hasPublication = false;
    for (size_t index = 0U; index < mPublications.size(); ++index) {
        const auto& publication = mPublications[index];
        if (publication.Empty()) {
            continue;
        }
        hasPublication = true;
        if (mFrameId == 0U || !publication.Published() ||
            publication.FrameId != mFrameId) {
            return false;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (mPublications[previous].Published() &&
                mPublications[previous].Capability == publication.Capability) {
                return false;
            }
        }
    }
    return !hasPublication || mFrameId != 0U;
}

uint64_t ExtensionScenePublicationTableView::FrameId() const noexcept {
    return mFrameId;
}

size_t ExtensionScenePublicationTableView::PublishedCount() const noexcept {
    if (!Valid()) {
        return 0U;
    }
    return static_cast<size_t>(std::count_if(
        mPublications.begin(), mPublications.end(),
        [](const ExtensionSceneCapabilityPublication& publication) {
            return publication.Published();
        }));
}

const ExtensionSceneCapabilityPublication*
ExtensionScenePublicationTableView::Find(
    const ExtensionSceneCapabilityIdentity& capability) const noexcept {
    if (!capability.Valid() || !Valid()) {
        return nullptr;
    }
    const auto found = std::find_if(
        mPublications.begin(), mPublications.end(),
        [&capability](const ExtensionSceneCapabilityPublication& publication) {
            return publication.Published() &&
                   publication.Capability == capability;
        });
    return found == mPublications.end() ? nullptr : &*found;
}

ExtensionSceneCapabilityResolution ResolveExtensionSceneCapabilities(
    ExtensionScenePublicationTableView publications,
    std::span<const ExtensionSceneCapabilityRequirement> requirements) noexcept {
    ExtensionSceneCapabilityResolution result;
    if (!publications.Valid()) {
        return result;
    }
    result.Status = ExtensionSceneCapabilityResolutionStatus::Ready;
    result.RequiredCapabilityCount = requirements.size();
    for (size_t index = 0U; index < requirements.size(); ++index) {
        const auto& requirement = requirements[index];
        if (!requirement.Valid()) {
            result.Status =
                ExtensionSceneCapabilityResolutionStatus::InvalidRequirements;
            result.FailedCapability = requirement.Capability;
            return result;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (requirements[previous].Capability == requirement.Capability) {
                result.Status =
                    ExtensionSceneCapabilityResolutionStatus::InvalidRequirements;
                result.FailedCapability = requirement.Capability;
                return result;
            }
        }
        const auto* publication = publications.Find(requirement.Capability);
        if (publication == nullptr) {
            result.Status =
                ExtensionSceneCapabilityResolutionStatus::MissingCapability;
            result.FailedCapability = requirement.Capability;
            return result;
        }
        if (!requirement.Accepts(publication->PayloadSchemaVersion)) {
            result.Status =
                ExtensionSceneCapabilityResolutionStatus::IncompatibleSchema;
            result.FailedCapability = requirement.Capability;
            result.PublishedSchemaVersion = publication->PayloadSchemaVersion;
            return result;
        }
        ++result.ResolvedCapabilityCount;
    }
    return result;
}

} // namespace Fast::Renderer
