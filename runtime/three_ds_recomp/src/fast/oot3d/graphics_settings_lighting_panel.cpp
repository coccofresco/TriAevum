#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/settings_panel_widgets.h"

namespace Fast::Oot3d {
using SettingsUi::EnumCombo;

bool GraphicsSettingsPanel::DrawLightingSettings(GraphicsSettings& settings, const GraphicsCapabilities& capabilities) {
    bool changed = false;
    ImGui::SeparatorText("Directional shadows");
    auto& directionalShadows = settings.Effects.DirectionalShadows;
    const auto& directionalShadowCapability =
        capabilities.Get(GraphicsCapability::NriDirectionalShadows);
    const bool directionalShadowsReady =
        directionalShadowCapability.Available;
    const char* const directionalShadowModes[] = {
        "Off", "Single cascade" };
    changed |= SettingsUi::ValidatedCombo<DirectionalShadowMode>(
        "Shadow mode", settings, capabilities, directionalShadowModes,
        [](auto& value, auto mode) { value.Effects.DirectionalShadows.Mode = mode; },
        [](const auto& value) { return value.Effects.DirectionalShadows.Mode; });
    if (directionalShadows.Mode ==
        DirectionalShadowMode::SingleCascade) {
        if (!directionalShadowsReady) ImGui::BeginDisabled();
        constexpr uint32_t resolutions[] = {
            512U, 1024U, 2048U, 4096U };
        int resolutionIndex = 1;
        for (size_t index = 0; index < std::size(resolutions);
             ++index) {
            if (resolutions[index] ==
                directionalShadows.Resolution) {
                resolutionIndex = static_cast<int>(index);
                break;
            }
        }
        const char* const resolutionNames[] = {
            "512", "1024", "2048", "4096" };
        if (ImGui::Combo("Shadow resolution", &resolutionIndex,
                         resolutionNames,
                         static_cast<int>(
                             std::size(resolutionNames)))) {
            directionalShadows.Resolution =
                resolutions[resolutionIndex];
            changed = true;
        }
        changed |= ImGui::DragFloat(
            "Shadow distance",
            &directionalShadows.MaximumDistance,
            10.0F, 50.0F, 20000.0F, "%.0f");
        changed |= ImGui::SliderFloat(
            "Shadow strength", &directionalShadows.Strength,
            0.0F, 1.0F, "%.2f");
        int pcfRadius = directionalShadows.PcfRadius;
        if (ImGui::SliderInt("PCF radius", &pcfRadius, 0, 2)) {
            directionalShadows.PcfRadius =
                static_cast<uint8_t>(pcfRadius);
            changed = true;
        }
        changed |= ImGui::Checkbox(
            "Stable shadow projection",
            &directionalShadows.Stabilize);
        if (ImGui::TreeNode("Advanced directional shadows")) {
            changed |= ImGui::DragFloat(
                "Shadow depth padding",
                &directionalShadows.DepthPadding,
                5.0F, 0.0F, 5000.0F, "%.0f");
            changed |= ImGui::DragFloat(
                "Shadow constant bias",
                &directionalShadows.DepthBiasConstant,
                0.05F, 0.0F, 16.0F, "%.2f");
            changed |= ImGui::DragFloat(
                "Shadow slope bias",
                &directionalShadows.DepthBiasSlope,
                0.05F, 0.0F, 16.0F, "%.2f");
            ImGui::TreePop();
        }
        if (!directionalShadowsReady) ImGui::EndDisabled();
    }
    if (!directionalShadowsReady) {
        ImGui::TextDisabled("Unavailable: %s",
            directionalShadowCapability.Reason.c_str());
    }

    ImGui::SeparatorText("Ambient occlusion");
    const bool cacaoReady = capabilities.Has(GraphicsCapability::NriInterop) &&
                            capabilities.Has(GraphicsCapability::SampledDepth) &&
                            capabilities.Has(GraphicsCapability::ValidViewMetadata);
    const char* const aoModes[] = {"Off", "FidelityFX CACAO"};
    changed |= SettingsUi::ValidatedCombo<AmbientOcclusionMode>(
        "Ambient occlusion", settings, capabilities, aoModes,
        [](auto& value, auto mode) { value.Effects.AmbientOcclusion = mode; },
        [](const auto& value) { return value.Effects.AmbientOcclusion; });
    const bool ao = settings.Effects.AmbientOcclusion == AmbientOcclusionMode::Cacao;
    if (ao) {
        ImGui::BeginDisabled(!cacaoReady);
        int quality = settings.Effects.AoQuality;
        const char* const qualityNames[] = {"Low", "Medium"};
        if (ImGui::Combo("AO quality", &quality, qualityNames, 2)) {
            settings.Effects.AoQuality = static_cast<uint8_t>(quality);
            changed = true;
        }
        changed |= ImGui::SliderFloat("AO radius", &settings.Effects.AoRadius,
                                      0.05F, 100.0F, "%.2f",
                                      ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat("AO strength", &settings.Effects.AoStrength,
                                      0.0F, 10.0F, "%.2f");
        if (ImGui::TreeNode("Advanced CACAO")) {
            changed |= ImGui::SliderFloat("Shadow power",
                                          &settings.Effects.AoShadowPower,
                                          0.1F, 10.0F, "%.2f");
            changed |= ImGui::SliderFloat("Shadow clamp",
                                          &settings.Effects.AoShadowClamp,
                                          0.0F, 1.0F, "%.3f");
            changed |= ImGui::SliderFloat(
                "Horizon threshold",
                &settings.Effects.AoHorizonAngleThreshold,
                0.0F, 1.0F, "%.3f");
            changed |= ImGui::DragFloat("Fade start",
                                        &settings.Effects.AoFadeOutFrom,
                                        1.0F, 0.0F, 100000.0F, "%.1f");
            changed |= ImGui::DragFloat("Fade end",
                                        &settings.Effects.AoFadeOutTo,
                                        1.0F, 0.01F, 100000.0F, "%.1f");
            int blurPasses = settings.Effects.AoBlurPassCount;
            if (ImGui::SliderInt("Blur passes", &blurPasses, 0, 8)) {
                settings.Effects.AoBlurPassCount =
                    static_cast<uint8_t>(blurPasses);
                changed = true;
            }
            changed |= ImGui::SliderFloat("Sharpness",
                                          &settings.Effects.AoSharpness,
                                          0.0F, 1.0F, "%.3f");
            changed |= ImGui::SliderFloat("Detail strength",
                                          &settings.Effects.AoDetailStrength,
                                          0.0F, 10.0F, "%.2f");
            ImGui::TreePop();
        }
        ImGui::EndDisabled();
    }
    if (!cacaoReady) {
        ImGui::TextWrapped("CACAO unavailable: NRI, depth or camera metadata missing.");
    }

    return changed;
}
} // namespace Fast::Oot3d
