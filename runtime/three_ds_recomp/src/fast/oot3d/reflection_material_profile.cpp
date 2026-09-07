#include "fast/oot3d/reflection_material_profile.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {
namespace {

float MaterialClass(ReflectionMaterialProfile profile) {
    switch (profile) {
        case ReflectionMaterialProfile::Water:
            return 0.625F;
        case ReflectionMaterialProfile::Metal:
            return 0.750F;
        case ReflectionMaterialProfile::Polished:
            return 0.875F;
        case ReflectionMaterialProfile::Custom:
            return 1.000F;
    }
    return 1.000F;
}

float FiniteClamp(float value, float minimum, float maximum,
                  float fallback) {
    return std::isfinite(value)
               ? std::clamp(value, minimum, maximum)
               : fallback;
}

bool Matches(const ReflectionMaterialRule& rule,
             const ReflectionMaterialTextureIdentity& texture) {
    if (rule.Target.ContentHash == 0 ||
        rule.Target.ContentHash != texture.ContentHash ||
        texture.MapperSlot >= 8U ||
        (rule.Target.MapperSlotMask & (1U << texture.MapperSlot)) == 0U) {
        return false;
    }
    if (rule.Target.Width != 0U &&
        rule.Target.Width != texture.Width) {
        return false;
    }
    if (rule.Target.Height != 0U &&
        rule.Target.Height != texture.Height) {
        return false;
    }
    return true;
}

} // namespace

ReflectionMaterialParameters
DefaultReflectionMaterialParameters(ReflectionMaterialProfile profile) {
    switch (profile) {
        case ReflectionMaterialProfile::Water:
            return {0.78F, 0.10F, MaterialClass(profile)};
        case ReflectionMaterialProfile::Metal:
            return {0.88F, 0.22F, MaterialClass(profile)};
        case ReflectionMaterialProfile::Polished:
            return {0.58F, 0.32F, MaterialClass(profile)};
        case ReflectionMaterialProfile::Custom:
            return {0.65F, 0.35F, MaterialClass(profile)};
    }
    return {0.65F, 0.35F, 1.0F};
}

void ApplyReflectionMaterialProfileDefaults(
    ReflectionMaterialRule& rule,
    ReflectionMaterialProfile profile) {
    const auto parameters =
        DefaultReflectionMaterialParameters(profile);
    rule.Profile = profile;
    rule.Reflectivity = parameters.Reflectivity;
    rule.Roughness = parameters.Roughness;
}

std::optional<ResolvedReflectionMaterialProfile>
ResolveReflectionMaterialProfile(
    std::span<const ReflectionMaterialRule> rules,
    std::span<const ReflectionMaterialTextureIdentity> textures) {
    const ReflectionMaterialTextureIdentity* selectedTexture = nullptr;
    const ReflectionMaterialRule* selectedRule = nullptr;
    for (const auto& texture : textures) {
        for (const auto& rule : rules) {
            if (!Matches(rule, texture)) continue;
            if (selectedTexture == nullptr ||
                texture.MapperSlot < selectedTexture->MapperSlot) {
                selectedTexture = &texture;
                selectedRule = &rule;
            }
            break;
        }
    }
    if (selectedTexture == nullptr || selectedRule == nullptr) {
        return std::nullopt;
    }

    ResolvedReflectionMaterialProfile result;
    result.RuleId = selectedRule->RuleId;
    result.TextureHash = selectedTexture->ContentHash;
    result.MapperSlot = selectedTexture->MapperSlot;
    result.Profile = selectedRule->Profile;
    const auto defaults =
        DefaultReflectionMaterialParameters(
            selectedRule->Profile);
    result.Parameters = {
        FiniteClamp(selectedRule->Reflectivity, 0.0F, 1.0F,
                    defaults.Reflectivity),
        FiniteClamp(selectedRule->Roughness, 0.02F, 1.0F,
                    defaults.Roughness),
        MaterialClass(selectedRule->Profile),
    };
    return result;
}

} // namespace Fast::Oot3d
