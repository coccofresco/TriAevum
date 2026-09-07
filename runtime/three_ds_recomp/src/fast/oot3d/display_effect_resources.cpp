#include "fast/oot3d/display_effect_resources.h"

namespace Fast::Oot3d {
namespace {

[[nodiscard]] constexpr size_t ResourceIndex(
    EffectResource resource) noexcept {
    return static_cast<size_t>(resource);
}

[[nodiscard]] constexpr uint32_t ResourceBit(
    EffectResource resource) noexcept {
    return 1U << static_cast<uint32_t>(resource);
}

inline constexpr uint64_t kOot3dImageObjectNamespace =
    0x4F4F543344494D47ULL; // OOT3DIMG
inline constexpr uint64_t kOot3dSemanticObjectNamespace =
    0x4F4F54334453454DULL; // OOT3DSEM
inline constexpr uint32_t kOot3dObjectIdentitySchemaVersion = 1U;

[[nodiscard]] constexpr ::Fast::Renderer::ExtensionObjectIdentity
BuildImageObjectIdentity(uintptr_t nativeImage) noexcept {
    return {
        kOot3dImageObjectNamespace,
        static_cast<uint64_t>(nativeImage),
        kOot3dObjectIdentitySchemaVersion,
    };
}

[[nodiscard]] constexpr ::Fast::Renderer::ExtensionObjectIdentity
BuildSemanticObjectIdentity(uint64_t identity) noexcept {
    return {
        kOot3dSemanticObjectNamespace,
        identity,
        kOot3dObjectIdentitySchemaVersion,
    };
}

} // namespace

bool EffectResourceReadSet::Declares(
    EffectResource resource) const noexcept {
    const size_t index = ResourceIndex(resource);
    return index < Bindings.size() &&
           (Validation.DeclaredReadMask & ResourceBit(resource)) != 0U;
}

const EffectResourceBinding* EffectResourceReadSet::Find(
    EffectResource resource) const noexcept {
    const size_t index = ResourceIndex(resource);
    return Declares(resource) ? Bindings[index] : nullptr;
}

const EffectResourceBinding* EffectResourceReadSet::FindFirstColorRead(
    std::span<const EffectResourceUse> uses) const noexcept {
    for (const auto& use : uses) {
        if (ReadsEffectResource(use.Access) &&
            EffectResourceCarriesColor(use.Resource)) {
            return Find(use.Resource);
        }
    }
    return nullptr;
}

bool EffectResourceBinding::Bound() const noexcept {
    switch (Kind) {
        case EffectResourceBindingKind::Image:
            return NativeImage != 0U && NativeView != 0U &&
                   Format != 0U && Width != 0U && Height != 0U;
        case EffectResourceBindingKind::Semantic:
            return Identity != 0U;
        case EffectResourceBindingKind::None:
            return false;
    }
    return false;
}

bool DisplayEffectResourceTable::BindImage(
    EffectResource resource, uintptr_t nativeImage,
    uintptr_t nativeView, uint32_t format, uint32_t width,
    uint32_t height, uint64_t generation,
    SceneColorEncoding colorEncoding) noexcept {
    const size_t index = ResourceIndex(resource);
    if (index >= mBindings.size() || nativeImage == 0U ||
        nativeView == 0U || format == 0U || width == 0U ||
        height == 0U ||
        (EffectResourceCarriesColor(resource) !=
         IsKnownSceneColorEncoding(colorEncoding))) {
        return false;
    }
    mBindings[index] = {
        .Resource = resource,
        .Kind = EffectResourceBindingKind::Image,
        .Identity = static_cast<uint64_t>(nativeImage),
        .Generation = generation,
        .NativeImage = nativeImage,
        .NativeView = nativeView,
        .Format = format,
        .Width = width,
        .Height = height,
        .Sampleable = true,
        .ColorEncoding = colorEncoding,
    };
    mPortableBindings[index] = {
        BuildOot3dEffectResourceIdentity(resource),
        EffectResourceBindingKind::Image,
        BuildImageObjectIdentity(nativeImage),
        generation,
        format,
        width,
        height,
        true,
    };
    return true;
}

bool DisplayEffectResourceTable::BindSemantic(
    EffectResource resource, uint64_t identity,
    uint64_t generation) noexcept {
    const size_t index = ResourceIndex(resource);
    if (index >= mBindings.size() || identity == 0U) {
        return false;
    }
    mBindings[index] = {
        .Resource = resource,
        .Kind = EffectResourceBindingKind::Semantic,
        .Identity = identity,
        .Generation = generation,
    };
    mPortableBindings[index] = {
        BuildOot3dEffectResourceIdentity(resource),
        EffectResourceBindingKind::Semantic,
        BuildSemanticObjectIdentity(identity),
        generation,
    };
    return true;
}

bool DisplayEffectResourceTable::BindSemanticObject(
    EffectResource resource, const void* object,
    uint32_t schemaVersion, uint64_t generation) noexcept {
    if (object == nullptr || schemaVersion == 0U) {
        return false;
    }
    const auto identity = reinterpret_cast<uintptr_t>(object);
    const size_t index = ResourceIndex(resource);
    if (index >= mBindings.size() || identity == 0U) {
        return false;
    }
    mBindings[index] = {
        .Resource = resource,
        .Kind = EffectResourceBindingKind::Semantic,
        .Identity = static_cast<uint64_t>(identity),
        .Generation = generation,
        .SemanticObject = identity,
        .SemanticSchemaVersion = schemaVersion,
    };
    mPortableBindings[index] = {
        BuildOot3dEffectResourceIdentity(resource),
        EffectResourceBindingKind::Semantic,
        BuildSemanticObjectIdentity(static_cast<uint64_t>(identity)),
        generation,
    };
    return true;
}

void DisplayEffectResourceTable::Unbind(
    EffectResource resource) noexcept {
    const size_t index = ResourceIndex(resource);
    if (index < mBindings.size()) {
        mBindings[index] = {};
        mPortableBindings[index] = {};
    }
}

void DisplayEffectResourceTable::Clear() noexcept {
    mBindings.fill({});
    mPortableBindings.fill({});
}

const EffectResourceBinding* DisplayEffectResourceTable::Find(
    EffectResource resource) const noexcept {
    const size_t index = ResourceIndex(resource);
    if (index >= mBindings.size() || !mBindings[index].Bound()) {
        return nullptr;
    }
    return &mBindings[index];
}

EffectResourceBindingValidation
DisplayEffectResourceTable::ValidateReads(
    std::span<const EffectResourceUse> uses) const noexcept {
    EffectResourceBindingValidation result;
    std::array<::Fast::Renderer::ExtensionResourceUse,
               kResourceCount> portableUses{};
    size_t portableUseCount = 0U;
    ::Fast::Renderer::ExtensionResourceReadValidation
        portableValidation{};
    const auto portableBindings = PortableBindings();
    const auto flushPortableUses = [&]() {
        const auto batch = portableBindings.ValidateReads(
            std::span<const ::Fast::Renderer::ExtensionResourceUse>(
                portableUses.data(), portableUseCount));
        portableValidation.DeclaredReadCount += batch.DeclaredReadCount;
        portableValidation.ResolvedReadCount += batch.ResolvedReadCount;
        portableValidation.MissingReadCount += batch.MissingReadCount;
        portableUseCount = 0U;
    };
    for (const auto& use : uses) {
        if (!ReadsEffectResource(use.Access) ||
            use.Resource == EffectResource::Count) {
            continue;
        }
        portableUses[portableUseCount++] = {
            BuildOot3dEffectResourceIdentity(use.Resource),
            use.Access,
        };
        if (portableUseCount == portableUses.size()) {
            flushPortableUses();
        }
        const uint32_t bit = ResourceBit(use.Resource);
        result.DeclaredReadMask |= bit;
        if (portableBindings.Find(
                BuildOot3dEffectResourceIdentity(use.Resource)) !=
            nullptr) {
            result.ResolvedReadMask |= bit;
        } else {
            result.MissingReadMask |= bit;
        }
    }
    flushPortableUses();
    result.DeclaredReadCount = portableValidation.DeclaredReadCount;
    result.ResolvedReadCount = portableValidation.ResolvedReadCount;
    result.MissingReadCount = portableValidation.MissingReadCount;
    return result;
}

EffectResourceReadSet DisplayEffectResourceTable::ResolveReads(
    std::span<const EffectResourceUse> uses) const noexcept {
    EffectResourceReadSet result;
    result.Validation = ValidateReads(uses);
    for (const auto& use : uses) {
        if (!ReadsEffectResource(use.Access) ||
            use.Resource == EffectResource::Count) {
            continue;
        }
        const size_t index = ResourceIndex(use.Resource);
        result.Bindings[index] = Find(use.Resource);
    }
    return result;
}

size_t DisplayEffectResourceTable::BoundCount() const noexcept {
    return PortableBindings().BoundCount();
}

::Fast::Renderer::ExtensionResourceBindingTableView
DisplayEffectResourceTable::PortableBindings() const noexcept {
    return ::Fast::Renderer::ExtensionResourceBindingTableView(
        mPortableBindings);
}

} // namespace Fast::Oot3d
