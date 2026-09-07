#include "fast/oot3d/reflection_material_editor.h"

#include "fast/oot3d/texture_assignment_model.h"

#include <imgui.h>

#include <array>
#include <cstdio>

namespace Fast::Oot3d {

bool DrawReflectionMaterialControls(
    const TextureCatalogEntry& texture,
    EffectsSettings& effects) {
    auto* rule =
        FindReflectionTextureRule(effects, texture);
    if (rule == nullptr) {
        return false;
    }
    bool changed = false;
    const char* const profiles[] = {
        "Water", "Metal", "Polished", "Custom"};
    int profile = static_cast<int>(rule->Profile);
    if (ImGui::Combo("Reflection material profile", &profile,
                     profiles,
                     static_cast<int>(std::size(profiles)))) {
        ApplyReflectionMaterialProfileDefaults(
            *rule,
            static_cast<ReflectionMaterialProfile>(profile));
        changed = true;
    }
    changed |= ImGui::SliderFloat(
        "Material reflectivity", &rule->Reflectivity,
        0.0F, 1.0F, "%.3f");
    changed |= ImGui::SliderFloat(
        "Material roughness", &rule->Roughness,
        0.02F, 1.0F, "%.3f");

    if (ImGui::TreeNode("Reflection texture mapper slots")) {
        for (uint8_t slot = 0U; slot < 3U; ++slot) {
            bool enabled =
                (rule->Target.MapperSlotMask &
                 (1U << slot)) != 0U;
            char label[32]{};
            std::snprintf(label, sizeof(label),
                          "Reflection mapper %u", slot);
            if (ImGui::Checkbox(label, &enabled)) {
                if (enabled) {
                    rule->Target.MapperSlotMask |=
                        static_cast<uint8_t>(1U << slot);
                } else {
                    rule->Target.MapperSlotMask &=
                        static_cast<uint8_t>(
                            ~(1U << slot));
                }
                if (rule->Target.MapperSlotMask == 0U) {
                    rule->Target.MapperSlotMask =
                        static_cast<uint8_t>(1U << slot);
                }
                changed = true;
            }
        }
        ImGui::TextDisabled(
            "The reflection rule matches only enabled PICA texture units.");
        ImGui::TreePop();
    }
    return changed;
}

} // namespace Fast::Oot3d
