// The MXAO controls of the F1 Lighting tab and MXAO's credits. The labels,
// option names, ranges and descriptions are the iMMERSE module's
// (TriaevumMxaoUi, from the original); without the module MXAO cannot be
// selected and nothing is drawn.
#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/immerse_module_loader.h"
#include "fast/oot3d/settings_panel_widgets.h"
#include <type_traits>

namespace Fast::Oot3d {
bool GraphicsSettingsPanel::DrawMxaoSettings(EffectsSettings& effects) {
    const auto* module = ImmerseModule();
    if (module == nullptr || module->MxaoUi == nullptr)
        return false;
    const auto& ui = *module->MxaoUi;
    bool changed = false;
    const auto combo = [&](const TriaevumImmerseChoice& choice, auto& field) {
        int index = static_cast<int>(field);
        const int count = static_cast<int>(choice.Count);
        // Every entry visible: a scrolled list hides the first ones.
        if (ImGui::Combo(choice.Label, &index, choice.Names, count, count)) {
            field = static_cast<std::remove_reference_t<decltype(field)>>(index);
            changed = true;
        }
        if (choice.Tooltip != nullptr && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", choice.Tooltip);
    };
    const auto tooltip = [](const char* text) {
        if (text != nullptr && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text);
    };
    combo(ui.Quality, effects.MxaoQuality);
    combo(ui.ShadingRate, effects.MxaoShadingRate);
    combo(ui.Method, effects.MxaoMethod);
    combo(ui.Normals, effects.MxaoNormals);
    changed |= ImGui::Checkbox(ui.RadiusGrowsWithDistance, &effects.MxaoRadiusGrowsWithDistance);
    if (effects.MxaoRadiusGrowsWithDistance)
        changed |= ImGui::SliderFloat(ui.ScreenRadius.Label, &effects.MxaoScreenRadius, ui.ScreenRadius.Minimum,
                                      ui.ScreenRadius.Maximum, ui.ScreenRadius.Format);
    else
        changed |= ImGui::SliderFloat(ui.Radius.Label, &effects.AoRadius, ui.Radius.Minimum, ui.Radius.Maximum,
                                      ui.Radius.Format, ImGuiSliderFlags_Logarithmic);
    tooltip(effects.MxaoRadiusGrowsWithDistance ? ui.ScreenRadius.Tooltip : ui.Radius.Tooltip);
    changed |= ImGui::SliderFloat(ui.Amount.Label, &effects.AoStrength, ui.Amount.Minimum, ui.Amount.Maximum,
                                  ui.Amount.Format);
    tooltip(ui.Amount.Tooltip);
    int filter = effects.MxaoFilter;
    if (ImGui::SliderInt(ui.FilterLabel, &filter, 0, static_cast<int>(ui.FilterMaximum))) {
        effects.MxaoFilter = static_cast<uint8_t>(filter);
        changed = true;
    }
    changed |= ImGui::Checkbox(ui.TemporalLabel, &effects.MxaoTemporal);
    if (effects.MxaoTemporal)
        ImGui::TextWrapped("%s", ui.TemporalNote);
    ImGui::TextWrapped("%s", ui.ReliefNote);
    if (ImGui::TreeNode(ui.AdvancedLabel)) {
        changed |= ImGui::Checkbox(ui.OriginalFadeLabel, &effects.MxaoOriginalFade);
        tooltip(ui.OriginalFadeTooltip);
        if (effects.MxaoOriginalFade) {
            changed |= ImGui::SliderFloat(ui.FadeDistance.Label, &effects.MxaoFadeDistance, ui.FadeDistance.Minimum,
                                          ui.FadeDistance.Maximum, ui.FadeDistance.Format);
        } else {
            changed |= ImGui::DragFloat(ui.FadeStart.Label, &effects.AoFadeOutFrom, 1.0F, ui.FadeStart.Minimum,
                                        ui.FadeStart.Maximum, ui.FadeStart.Format);
            changed |= ImGui::DragFloat(ui.FadeEnd.Label, &effects.AoFadeOutTo, 1.0F, ui.FadeEnd.Minimum,
                                        ui.FadeEnd.Maximum, ui.FadeEnd.Format);
        }
        ImGui::TreePop();
    }
    combo(ui.DebugView, effects.MxaoDebug);
    return changed;
}

void GraphicsSettingsPanel::DrawMxaoCredits() {
    const auto* module = ImmerseModule();
    if (module == nullptr || module->MxaoUi == nullptr)
        return;
    ImGui::SeparatorText(module->MxaoUi->CreditsTitle);
    for (const char* line : module->MxaoUi->Credits)
        ImGui::TextWrapped("%s", line);
}
} // namespace Fast::Oot3d
