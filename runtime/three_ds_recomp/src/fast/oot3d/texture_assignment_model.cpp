#include "fast/oot3d/texture_assignment_model.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace Fast::Oot3d {
namespace {

template <typename Rules>
uint32_t NextRuleId(const Rules& rules) {
    std::vector<uint32_t> used;
    used.reserve(rules.size());
    for (const auto& rule : rules) {
        if (rule.RuleId != 0U) {
            used.push_back(rule.RuleId);
        }
    }
    uint32_t candidate = 1U;
    while (std::find(used.begin(), used.end(), candidate) !=
           used.end()) {
        if (candidate ==
            std::numeric_limits<uint32_t>::max()) {
            candidate = 1U;
        } else {
            ++candidate;
        }
    }
    return candidate;
}

bool MatchesGrassTexture(
    const GrassPlacementRule& rule,
    const TextureCatalogEntry& texture) {
    return rule.Target.Rgba8Hash == texture.ContentHash &&
        (rule.Target.Width == 0U ||
         rule.Target.Width == texture.Width) &&
        (rule.Target.Height == 0U ||
         rule.Target.Height == texture.Height);
}

bool MatchesReflectionTexture(
    const ReflectionMaterialRule& rule,
    const TextureCatalogEntry& texture) {
    return rule.Target.ContentHash == texture.ContentHash &&
        (rule.Target.Width == 0U ||
         rule.Target.Width == texture.Width) &&
        (rule.Target.Height == 0U ||
         rule.Target.Height == texture.Height);
}

template <typename Rules, typename Matches>
auto FindBestTextureRule(
    Rules& rules, const TextureCatalogEntry& texture,
    Matches&& matches) {
    const auto exact = std::find_if(
        rules.begin(), rules.end(),
        [&](const auto& rule) {
            return matches(rule, texture) &&
                rule.Target.Width == texture.Width &&
                rule.Target.Height == texture.Height;
        });
    return exact != rules.end()
        ? exact
        : std::find_if(
              rules.begin(), rules.end(),
              [&](const auto& rule) {
                  return matches(rule, texture);
              });
}

} // namespace

GrassPlacementRule* FindGrassTextureRule(
    InteractiveGrassSettings& grass,
    const TextureCatalogEntry& texture) {
    const auto found = FindBestTextureRule(
        grass.Rules, texture, MatchesGrassTexture);
    return found == grass.Rules.end() ? nullptr : &*found;
}

const GrassPlacementRule* FindGrassTextureRule(
    const InteractiveGrassSettings& grass,
    const TextureCatalogEntry& texture) {
    const auto found = FindBestTextureRule(
        grass.Rules, texture, MatchesGrassTexture);
    return found == grass.Rules.end() ? nullptr : &*found;
}

ReflectionMaterialRule* FindReflectionTextureRule(
    EffectsSettings& effects,
    const TextureCatalogEntry& texture) {
    const auto found = FindBestTextureRule(
        effects.ReflectionMaterials, texture,
        MatchesReflectionTexture);
    return found == effects.ReflectionMaterials.end()
        ? nullptr
        : &*found;
}

const ReflectionMaterialRule* FindReflectionTextureRule(
    const EffectsSettings& effects,
    const TextureCatalogEntry& texture) {
    const auto found = FindBestTextureRule(
        effects.ReflectionMaterials, texture,
        MatchesReflectionTexture);
    return found == effects.ReflectionMaterials.end()
        ? nullptr
        : &*found;
}

TextureAssignmentSummary DescribeTextureAssignments(
    const GraphicsSettings& settings,
    const TextureCatalogEntry& texture) {
    TextureAssignmentSummary summary;
    summary.Grass =
        FindGrassTextureRule(settings.Grass, texture) !=
        nullptr;
    if (const auto* reflection = FindReflectionTextureRule(
            settings.Effects, texture)) {
        summary.Reflection = reflection->Profile;
    }
    return summary;
}

std::string_view ReflectionMaterialProfileName(
    ReflectionMaterialProfile profile) {
    switch (profile) {
        case ReflectionMaterialProfile::Water:
            return "WATER";
        case ReflectionMaterialProfile::Metal:
            return "METAL";
        case ReflectionMaterialProfile::Polished:
            return "POLISHED";
        case ReflectionMaterialProfile::Custom:
            return "CUSTOM";
    }
    return "CUSTOM";
}

bool AssignTextureToGrass(
    InteractiveGrassSettings& grass,
    const TextureCatalogEntry& texture) {
    if (auto* existing =
            FindGrassTextureRule(grass, texture)) {
        const bool changed =
            existing->Target.Width != texture.Width ||
            existing->Target.Height != texture.Height ||
            existing->Target.MapperSlotMask == 0U;
        existing->Target.Width = texture.Width;
        existing->Target.Height = texture.Height;
        if (existing->Target.MapperSlotMask == 0U) {
            existing->Target.MapperSlotMask = 0x07U;
        }
        return changed;
    }
    GrassPlacementRule rule;
    rule.RuleId = NextRuleId(grass.Rules);
    rule.Target.Rgba8Hash = texture.ContentHash;
    rule.Target.Width = texture.Width;
    rule.Target.Height = texture.Height;
    rule.Target.MapperSlotMask = 0x07U;
    grass.Rules.push_back(std::move(rule));
    if (grass.Quality == GrassQuality::Off) {
        ApplyGrassQualityPreset(
            grass, GrassQuality::Medium);
        grass.WindDirectionDegrees = 35.0F;
        grass.WindStrength = 0.35F;
        grass.WindSpeed = 1.2F;
    }
    return true;
}

bool RemoveTextureFromGrass(
    InteractiveGrassSettings& grass,
    const TextureCatalogEntry& texture) {
    const size_t previousSize = grass.Rules.size();
    std::erase_if(
        grass.Rules, [&](const auto& rule) {
            return MatchesGrassTexture(rule, texture);
        });
    return grass.Rules.size() != previousSize;
}

bool AssignTextureReflectionProfile(
    EffectsSettings& effects,
    const TextureCatalogEntry& texture,
    ReflectionMaterialProfile profile) {
    if (auto* existing = FindReflectionTextureRule(
            effects, texture)) {
        existing->Target.Width = texture.Width;
        existing->Target.Height = texture.Height;
        if (existing->Target.MapperSlotMask == 0U) {
            existing->Target.MapperSlotMask = 0x07U;
        }
        ApplyReflectionMaterialProfileDefaults(
            *existing, profile);
        return true;
    }
    ReflectionMaterialRule rule;
    rule.RuleId =
        NextRuleId(effects.ReflectionMaterials);
    rule.Target.ContentHash = texture.ContentHash;
    rule.Target.Width = texture.Width;
    rule.Target.Height = texture.Height;
    rule.Target.MapperSlotMask = 0x07U;
    ApplyReflectionMaterialProfileDefaults(rule, profile);
    effects.ReflectionMaterials.push_back(std::move(rule));
    return true;
}

bool RemoveTextureReflectionProfile(
    EffectsSettings& effects,
    const TextureCatalogEntry& texture) {
    const size_t previousSize =
        effects.ReflectionMaterials.size();
    std::erase_if(
        effects.ReflectionMaterials,
        [&](const auto& rule) {
            return MatchesReflectionTexture(rule, texture);
        });
    return effects.ReflectionMaterials.size() != previousSize;
}

} // namespace Fast::Oot3d
