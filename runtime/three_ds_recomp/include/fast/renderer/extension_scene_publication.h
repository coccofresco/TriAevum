#pragma once

#include "fast/renderer/extension_resource_binding.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Fast::Renderer {

struct ExtensionSceneCapabilityIdentity {
    uint64_t Namespace = 0U;
    uint64_t Capability = 0U;

    [[nodiscard]] bool Valid() const noexcept {
        return Namespace != 0U && Capability != 0U;
    }

    bool operator==(const ExtensionSceneCapabilityIdentity&) const = default;
};

struct ExtensionSceneCapabilityPublication {
    ExtensionSceneCapabilityIdentity Capability;
    ExtensionObjectIdentity Object;
    const void* Payload = nullptr;
    uint32_t PayloadSchemaVersion = 0U;
    uint64_t Generation = 0U;
    uint64_t FrameId = 0U;

    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] bool Published() const noexcept;
};

struct ExtensionSceneCapabilityRequirement {
    ExtensionSceneCapabilityIdentity Capability;
    uint32_t MinimumSchemaVersion = 0U;
    uint32_t MaximumSchemaVersion = 0U;

    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] bool Accepts(uint32_t schemaVersion) const noexcept;
};

// One immutable, non-owning publication snapshot. Payload ownership and typed
// interpretation remain with the 3DS/title adapter that published them.
class ExtensionScenePublicationTableView final {
  public:
    explicit ExtensionScenePublicationTableView(
        uint64_t frameId = 0U,
        std::span<const ExtensionSceneCapabilityPublication> publications = {}) noexcept;

    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] uint64_t FrameId() const noexcept;
    [[nodiscard]] size_t PublishedCount() const noexcept;
    [[nodiscard]] const ExtensionSceneCapabilityPublication* Find(
        const ExtensionSceneCapabilityIdentity& capability) const noexcept;

  private:
    uint64_t mFrameId = 0U;
    std::span<const ExtensionSceneCapabilityPublication> mPublications;
};

enum class ExtensionSceneCapabilityResolutionStatus : uint8_t {
    Ready,
    InvalidPublications,
    InvalidRequirements,
    MissingCapability,
    IncompatibleSchema,
};

struct ExtensionSceneCapabilityResolution {
    ExtensionSceneCapabilityResolutionStatus Status =
        ExtensionSceneCapabilityResolutionStatus::InvalidPublications;
    ExtensionSceneCapabilityIdentity FailedCapability;
    size_t RequiredCapabilityCount = 0U;
    size_t ResolvedCapabilityCount = 0U;
    uint32_t PublishedSchemaVersion = 0U;

    [[nodiscard]] bool Complete() const noexcept {
        return Status == ExtensionSceneCapabilityResolutionStatus::Ready &&
               RequiredCapabilityCount == ResolvedCapabilityCount;
    }
};

// Resolves provider requirements without copying payloads or allocating frame
// memory. Requirements are exact/ranged contracts selected by the 3DS/title
// adapter, never inferred from backend state.
[[nodiscard]] ExtensionSceneCapabilityResolution ResolveExtensionSceneCapabilities(
    ExtensionScenePublicationTableView publications,
    std::span<const ExtensionSceneCapabilityRequirement> requirements) noexcept;

} // namespace Fast::Renderer
