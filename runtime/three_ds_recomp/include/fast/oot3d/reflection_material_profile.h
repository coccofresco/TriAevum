#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace Fast::Oot3d {

enum class ReflectionMaterialProfile : uint8_t {
    Water,
    Metal,
    Polished,
    Custom,
};

struct ReflectionMaterialParameters {
    float Reflectivity = 0.78F;
    float Roughness = 0.10F;
    float MaterialClass = 0.625F;
};

struct ReflectionMaterialTextureSelector {
    uint64_t ContentHash = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint8_t MapperSlotMask = 0x07;
};

struct ReflectionMaterialRule {
    uint32_t RuleId = 0;
    ReflectionMaterialTextureSelector Target;
    ReflectionMaterialProfile Profile = ReflectionMaterialProfile::Water;
    float Reflectivity = 0.78F;
    float Roughness = 0.10F;
};

struct ReflectionMaterialTextureIdentity {
    uint64_t ContentHash = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint8_t MapperSlot = 0;
};

struct ResolvedReflectionMaterialProfile {
    uint32_t RuleId = 0;
    uint64_t TextureHash = 0;
    uint8_t MapperSlot = 0;
    ReflectionMaterialProfile Profile = ReflectionMaterialProfile::Water;
    ReflectionMaterialParameters Parameters;
};

[[nodiscard]] ReflectionMaterialParameters
DefaultReflectionMaterialParameters(ReflectionMaterialProfile profile);

void ApplyReflectionMaterialProfileDefaults(
    ReflectionMaterialRule& rule,
    ReflectionMaterialProfile profile);

// Resolution is deterministic: the lowest texture mapper slot wins, then the
// first matching rule in settings order. A zero selector dimension is a
// wildcard; hashes are always exact.
[[nodiscard]] std::optional<ResolvedReflectionMaterialProfile>
ResolveReflectionMaterialProfile(
    std::span<const ReflectionMaterialRule> rules,
    std::span<const ReflectionMaterialTextureIdentity> textures);

} // namespace Fast::Oot3d
