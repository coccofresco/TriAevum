#pragma once

#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/texture_catalog_runtime.h"

#include <optional>
#include <string_view>

namespace Fast::Oot3d {

struct TextureAssignmentSummary {
    bool Grass = false;
    std::optional<ReflectionMaterialProfile> Reflection;

    [[nodiscard]] bool Assigned() const {
        return Grass || Reflection.has_value();
    }
};

[[nodiscard]] GrassPlacementRule* FindGrassTextureRule(
    InteractiveGrassSettings& grass,
    const TextureCatalogEntry& texture);
[[nodiscard]] const GrassPlacementRule* FindGrassTextureRule(
    const InteractiveGrassSettings& grass,
    const TextureCatalogEntry& texture);
[[nodiscard]] ReflectionMaterialRule* FindReflectionTextureRule(
    EffectsSettings& effects,
    const TextureCatalogEntry& texture);
[[nodiscard]] const ReflectionMaterialRule*
FindReflectionTextureRule(
    const EffectsSettings& effects,
    const TextureCatalogEntry& texture);

[[nodiscard]] TextureAssignmentSummary DescribeTextureAssignments(
    const GraphicsSettings& settings,
    const TextureCatalogEntry& texture);
[[nodiscard]] std::string_view ReflectionMaterialProfileName(
    ReflectionMaterialProfile profile);

bool AssignTextureToGrass(
    InteractiveGrassSettings& grass,
    const TextureCatalogEntry& texture);
bool RemoveTextureFromGrass(
    InteractiveGrassSettings& grass,
    const TextureCatalogEntry& texture);
bool AssignTextureReflectionProfile(
    EffectsSettings& effects,
    const TextureCatalogEntry& texture,
    ReflectionMaterialProfile profile);
bool RemoveTextureReflectionProfile(
    EffectsSettings& effects,
    const TextureCatalogEntry& texture);

} // namespace Fast::Oot3d
