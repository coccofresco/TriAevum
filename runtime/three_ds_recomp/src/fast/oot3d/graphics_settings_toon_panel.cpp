#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/settings_panel_widgets.h"
#include "fast/oot3d/pica_toon_telemetry.h"

namespace Fast::Oot3d {
using SettingsUi::EnumCombo;

bool GraphicsSettingsPanel::DrawToonSettings(GraphicsSettings& settings) {
    bool changed = false;
    ImGui::SeparatorText("Advanced PICA toon");
    const char* const toonModes[] = {
        "Off", "Albedo-preserving preview", "PICA material toon" };
    int toonMode = static_cast<int>(settings.Effects.Toon);
    if (ImGui::Combo("Toon mode", &toonMode, toonModes,
                     static_cast<int>(std::size(toonModes)))) {
        settings.Effects.Toon = static_cast<ToonMode>(toonMode);
        changed = true;
    }
    if (settings.Effects.Toon != ToonMode::Off) {
        auto& toon = settings.Effects.ToonStyle;
        int lightBands = toon.LightBands;
        if (ImGui::SliderInt("Light bands", &lightBands, 2,
                             kMaximumToonLightBands)) {
            ResizeToonLightBandProfile(toon, static_cast<uint8_t>(lightBands));
            changed = true;
        }
        bool customLightBands = toon.CustomLightBands;
        if (ImGui::Checkbox("Custom light band profile",
                            &customLightBands)) {
            toon.CustomLightBands = customLightBands;
            changed = true;
        }
        if (toon.CustomLightBands) {
            const size_t bandCount = toon.LightBands;
            const size_t thresholdCount = bandCount - 1U;
            if (ImGui::Button("Reset light bands")) {
                ResetToonLightBandProfile(toon);
                changed = true;
            }
            for (size_t index = 0; index < bandCount; ++index) {
                const std::string label =
                    "Band " + std::to_string(index + 1U) + " level";
                changed |= ImGui::SliderFloat(
                    label.c_str(), &toon.LightBandLevels[index],
                    0.0F, 1.0F, "%.3f");
            }
            for (size_t index = 0; index < thresholdCount; ++index) {
                const float minimum = index == 0U
                    ? 0.001F
                    : toon.LightBandThresholds[index - 1U] + 0.001F;
                const float maximum = index + 1U == thresholdCount
                    ? 0.999F
                    : toon.LightBandThresholds[index + 1U] - 0.001F;
                const std::string label =
                    "Threshold " + std::to_string(index + 1U) + " to " +
                    std::to_string(index + 2U);
                changed |= ImGui::SliderFloat(
                    label.c_str(), &toon.LightBandThresholds[index],
                    minimum, std::max(minimum, maximum), "%.3f");
            }
        }
        changed |= ImGui::SliderFloat("Band softness", &toon.BandSoftness,
                                      0.0F, 0.5F, "%.3f");
        changed |= ImGui::SliderFloat("Toon saturation", &toon.Saturation,
                                      0.0F, 2.0F, "%.2f");
        changed |= ImGui::ColorEdit3("Shadow tint", toon.ShadowTint.data());
        changed |= ImGui::SliderFloat("Shadow tint strength",
                                      &toon.ShadowStrength, 0.0F, 1.0F, "%.2f");
        changed |= ImGui::ColorEdit3("Rim tint", toon.RimTint.data());
        changed |= ImGui::SliderFloat("Rim strength", &toon.RimStrength,
                                      0.0F, 2.0F, "%.2f");
        changed |= ImGui::SliderFloat("Rim falloff", &toon.RimWidth,
                                      0.25F, 12.0F, "%.2f");
        if (ImGui::TreeNode("Outline")) {
            changed |= ImGui::Checkbox("Enable depth outline",
                                       &toon.OutlineEnabled);
            if (toon.OutlineEnabled) {
                changed |= ImGui::SliderFloat("Outline width (1080p)",
                    &toon.OutlineWidth, 0.5F, 12.0F, "%.2f px");
                changed |= ImGui::SliderFloat("Outline softness",
                    &toon.OutlineSoftness, 0.0F, 1.0F, "%.2f");
                changed |= ImGui::ColorEdit3("Outline tint",
                                              toon.OutlineTint.data());
                changed |= ImGui::SliderFloat("Outline opacity",
                    &toon.OutlineOpacity, 0.0F, 1.0F, "%.2f");
                if (ImGui::TreeNode("Edge detection")) {
                    changed |= ImGui::SliderFloat("Depth sensitivity",
                        &toon.OutlineDepthSensitivity, 0.0F, 8.0F, "%.2f");
                    changed |= ImGui::SliderFloat("Normal sensitivity",
                        &toon.OutlineNormalSensitivity, 0.0F, 8.0F, "%.2f");
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Toon status")) {
            const auto telemetry = PicaToonTelemetry::Instance().Snapshot();
            ImGui::Text("Eligible draws: %llu / %llu",
                static_cast<unsigned long long>(telemetry.Count(PicaToonEligibility::Eligible)),
                static_cast<unsigned long long>(telemetry.Total()));
            ImGui::TreePop();
        }
    }

    return changed;
}
} // namespace Fast::Oot3d
