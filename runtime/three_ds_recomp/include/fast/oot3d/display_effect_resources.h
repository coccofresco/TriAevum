#pragma once

#include "fast/oot3d/effect_graph.h"
#include "fast/oot3d/linear_scene_color.h"
#include "fast/renderer/extension_resource_binding.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fast::Oot3d {

using EffectResourceBindingKind =
    ::Fast::Renderer::ExtensionResourceBindingKind;

[[nodiscard]] constexpr bool EffectResourceCarriesColor(
    EffectResource resource) noexcept {
    switch (resource) {
        case EffectResource::SceneColor:
        case EffectResource::LinearWorkingColor:
        case EffectResource::ReflectionColor:
        case EffectResource::CompositeColor:
        case EffectResource::TemporalColor:
        case EffectResource::UpscaledColor:
        case EffectResource::AntiAliasedColor:
            return true;
        default:
            return false;
    }
}

struct EffectResourceBinding {
    EffectResource Resource = EffectResource::SceneColor;
    EffectResourceBindingKind Kind = EffectResourceBindingKind::None;
    uint64_t Identity = 0;
    uint64_t Generation = 0;
    uintptr_t SemanticObject = 0;
    uint32_t SemanticSchemaVersion = 0;
    uintptr_t NativeImage = 0;
    uintptr_t NativeView = 0;
    uint32_t Format = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool Sampleable = false;
    SceneColorEncoding ColorEncoding = SceneColorEncoding::Unknown;

    [[nodiscard]] bool Bound() const noexcept;
    [[nodiscard]] bool LinearColor() const noexcept {
        return SceneColorIsLinear(ColorEncoding);
    }
    [[nodiscard]] bool SrgbColor() const noexcept {
        return SceneColorIsSrgb(ColorEncoding);
    }
};

struct EffectResourceBindingValidation {
    uint32_t DeclaredReadMask = 0;
    uint32_t ResolvedReadMask = 0;
    uint32_t MissingReadMask = 0;
    uint32_t DeclaredReadCount = 0;
    uint32_t ResolvedReadCount = 0;
    uint32_t MissingReadCount = 0;

    [[nodiscard]] bool Complete() const noexcept {
        return MissingReadCount == 0U;
    }
};

// Immutable view of the resources that a compiled pass explicitly declared
// as reads. Consumers cannot use this view to reach an undeclared table entry.
struct EffectResourceReadSet {
    EffectResourceBindingValidation Validation;
    std::array<const EffectResourceBinding*,
               static_cast<size_t>(EffectResource::Count)> Bindings{};

    [[nodiscard]] bool Complete() const noexcept {
        return Validation.Complete();
    }
    [[nodiscard]] bool Declares(EffectResource resource) const noexcept;
    [[nodiscard]] const EffectResourceBinding* Find(
        EffectResource resource) const noexcept;
    [[nodiscard]] const EffectResourceBinding* FindFirstColorRead(
        std::span<const EffectResourceUse> uses) const noexcept;
};

class DisplayEffectResourceTable final {
  public:
    [[nodiscard]] bool BindImage(
        EffectResource resource, uintptr_t nativeImage,
        uintptr_t nativeView, uint32_t format, uint32_t width,
        uint32_t height, uint64_t generation,
        SceneColorEncoding colorEncoding =
            SceneColorEncoding::Unknown) noexcept;
    [[nodiscard]] bool BindSemantic(
        EffectResource resource, uint64_t identity,
        uint64_t generation) noexcept;
    [[nodiscard]] bool BindSemanticObject(
        EffectResource resource, const void* object,
        uint32_t schemaVersion, uint64_t generation) noexcept;
    void Unbind(EffectResource resource) noexcept;
    void Clear() noexcept;

    [[nodiscard]] const EffectResourceBinding* Find(
        EffectResource resource) const noexcept;
    template <typename T>
    [[nodiscard]] const T* FindSemanticObject(
        EffectResource resource,
        uint32_t expectedSchemaVersion) const noexcept {
        const auto* binding = Find(resource);
        if (binding == nullptr ||
            binding->Kind != EffectResourceBindingKind::Semantic ||
            binding->SemanticObject == 0U ||
            binding->SemanticSchemaVersion != expectedSchemaVersion) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(binding->SemanticObject);
    }
    [[nodiscard]] EffectResourceBindingValidation ValidateReads(
        std::span<const EffectResourceUse> uses) const noexcept;
    [[nodiscard]] EffectResourceReadSet ResolveReads(
        std::span<const EffectResourceUse> uses) const noexcept;
    [[nodiscard]] size_t BoundCount() const noexcept;
    [[nodiscard]] ::Fast::Renderer::ExtensionResourceBindingTableView
    PortableBindings() const noexcept;

  private:
    static constexpr size_t kResourceCount =
        static_cast<size_t>(EffectResource::Count);
    static_assert(kResourceCount <= 32U);

    std::array<EffectResourceBinding, kResourceCount> mBindings{};
    std::array<::Fast::Renderer::ExtensionResourceBinding,
               kResourceCount> mPortableBindings{};
};

} // namespace Fast::Oot3d
