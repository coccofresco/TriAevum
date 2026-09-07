#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/settings_panel_widgets.h"
#include "fast/oot3d/reflection_material_editor.h"
#include "fast/oot3d/texture_assignment_model.h"
#include "fast/oot3d/texture_catalog_viewer.h"
#include <cstdio>

namespace Fast::Oot3d {
using SettingsUi::EnumCombo;

bool GraphicsSettingsPanel::DrawReflectionSettings(GraphicsSettings& settings, const GraphicsCapabilities& capabilities) {
    bool changed = false;
    ImGui::SeparatorText("Screen-space reflections");
    const bool reflectionReady =
        capabilities.Has(GraphicsCapability::NriInterop) &&
        capabilities.Has(GraphicsCapability::SampledSceneColor) &&
        capabilities.Has(GraphicsCapability::SampledDepth) &&
        capabilities.Has(GraphicsCapability::NormalGuide) &&
        capabilities.Has(GraphicsCapability::MaterialGuide) &&
        capabilities.Has(GraphicsCapability::ValidViewMetadata);
    const char* const reflectionModes[] = {
        "Off", "Hi-Z SSR", "FidelityFX SSSR" };
    changed |= SettingsUi::ValidatedCombo<ReflectionMode>(
        "Reflection provider", settings, capabilities, reflectionModes,
        [](auto& value, auto mode) { value.Effects.Reflections = mode; },
        [](const auto& value) { return value.Effects.Reflections; });
    if (settings.Effects.Reflections != ReflectionMode::Off) {
        ImGui::BeginDisabled(!reflectionReady);
        changed |= ImGui::SliderFloat("Reflection strength",
            &settings.Effects.ReflectionStrength, 0.0F, 2.0F, "%.2f");
        const bool sssr = settings.Effects.Reflections == ReflectionMode::FidelityFxSssr;
        changed |= ImGui::SliderFloat("Reflection thickness",
            &settings.Effects.ReflectionThickness, sssr ? 1.0F : 0.1F,
            sssr ? 30.0F : 100.0F, "%.2f",
            ImGuiSliderFlags_Logarithmic);
        if (!sssr || ImGui::TreeNode("Hi-Z fallback parameters")) {
            changed |= ImGui::SliderFloat("Reflection distance",
                &settings.Effects.ReflectionMaxDistance, 10.0F, 10000.0F,
                "%.0f", ImGuiSliderFlags_Logarithmic);
            changed |= ImGui::SliderFloat("Reflection edge fade",
                &settings.Effects.ReflectionEdgeFade, 0.001F, 0.5F, "%.3f");
            if (sssr) ImGui::TreePop();
        }
        int reflectionSteps = settings.Effects.ReflectionMaxSteps;
        if (ImGui::SliderInt("Reflection steps", &reflectionSteps, 8, 64)) {
            settings.Effects.ReflectionMaxSteps =
                static_cast<uint8_t>(reflectionSteps);
            changed = true;
        }
        changed |= ImGui::SliderFloat("Roughness bias",
            &settings.Effects.ReflectionRoughnessBias, -0.5F, 0.5F, "%.2f");
        const char* const reflectionDebugViews[] = {
            "Off", "Material reflectivity/class", "Roughness proxy",
            "Filtered hit color", "Hit confidence"};
        int reflectionDebug = settings.Effects.ReflectionDebugView;
        if (ImGui::Combo("Reflection debug", &reflectionDebug,
                         reflectionDebugViews,
                         static_cast<int>(std::size(reflectionDebugViews)))) {
            settings.Effects.ReflectionDebugView =
                static_cast<uint8_t>(reflectionDebug);
            changed = true;
        }
        ImGui::EndDisabled();
    }
    if (!reflectionReady) {
        ImGui::TextWrapped("SSR unavailable: depth, normal, material or camera metadata missing.");
    }

    if (ImGui::CollapsingHeader("Reflection material assignments")) {
        std::vector<TextureCatalogTag> textureTags;
        textureTags.reserve(
            settings.Effects.ReflectionMaterials.size());
        for (const auto& rule :
             settings.Effects.ReflectionMaterials) {
            textureTags.push_back(
                {rule.Target.ContentHash, rule.Target.Width,
                 rule.Target.Height,
                 std::string(ReflectionMaterialProfileName(
                     rule.Profile))});
        }
        const auto catalog = DrawTextureCatalogViewer(
            mReflectionTextureSelection, textureTags);
        if (catalog.SelectionChanged &&
            catalog.Selected.has_value()) {
            char status[96]{};
            std::snprintf(
                status, sizeof(status),
                "Selected %016llX (%ux%u, format %u)",
                static_cast<unsigned long long>(
                    catalog.Selected->ContentHash),
                catalog.Selected->Width,
                catalog.Selected->Height,
                catalog.Selected->NativeFormat);
            mReflectionAssignmentStatus = status;
        }
        if (catalog.Selected.has_value()) {
            const auto& texture = *catalog.Selected;
            auto assignments = DescribeTextureAssignments(
                settings, texture);
            if (ImGui::Button("Assign selected as water")) {
                changed |= AssignTextureReflectionProfile(
                    settings.Effects, texture,
                    ReflectionMaterialProfile::Water);
            }
            if (ImGui::Button("Assign selected as metal")) {
                changed |= AssignTextureReflectionProfile(
                    settings.Effects, texture,
                    ReflectionMaterialProfile::Metal);
            }
            if (ImGui::Button("Assign selected as polished")) {
                changed |= AssignTextureReflectionProfile(
                    settings.Effects, texture,
                    ReflectionMaterialProfile::Polished);
            }
            assignments = DescribeTextureAssignments(
                settings, texture);
            if (assignments.Reflection.has_value() &&
                ImGui::Button(
                    "Remove selected reflection profile")) {
                changed |= RemoveTextureReflectionProfile(
                    settings.Effects, texture);
                assignments = DescribeTextureAssignments(
                    settings, texture);
            }

            if (assignments.Reflection.has_value() &&
                ImGui::TreeNode(
                    "Selected reflection material parameters")) {
                changed |= DrawReflectionMaterialControls(
                    texture, settings.Effects);
                ImGui::TreePop();
            }
        }
        ImGui::Text(
            "Active reflection assignments: %zu",
            settings.Effects.ReflectionMaterials.size());
        if (!mReflectionAssignmentStatus.empty()) {
            ImGui::TextDisabled(
                "%s", mReflectionAssignmentStatus.c_str());
        }
    }

    return changed;
}
} // namespace Fast::Oot3d
