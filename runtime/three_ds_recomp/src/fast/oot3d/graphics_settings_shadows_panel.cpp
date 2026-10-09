#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/settings_panel_widgets.h"

namespace Fast::Oot3d {
namespace {
bool DrawDirectionalShadows(GraphicsSettings& settings, const GraphicsCapabilities& capabilities) {
    bool changed = false;
    ImGui::SeparatorText("Directional shadows");
    auto& directionalShadows = settings.Effects.DirectionalShadows;
    const auto& directionalShadowCapability =
        capabilities.Get(GraphicsCapability::NriDirectionalShadows);
    const bool directionalShadowsReady =
        directionalShadowCapability.Available;
    const char* const directionalShadowModes[] = {
        "Off", "Virtual Shadow Maps" };
    changed |= SettingsUi::ValidatedCombo<DirectionalShadowMode>(
        "Shadow mode", settings, capabilities, directionalShadowModes,
        [](auto& value, auto mode) { value.Effects.DirectionalShadows.Mode = mode; },
        [](const auto& value) { return value.Effects.DirectionalShadows.Mode; });
    if (directionalShadows.Mode ==
        DirectionalShadowMode::VirtualShadowMaps) {
        if (!directionalShadowsReady) ImGui::BeginDisabled();
        constexpr uint32_t resolutions[] = {
            2048U, 4096U };
        int resolutionIndex = 0;
        for (size_t index = 0; index < std::size(resolutions);
             ++index) {
            if (resolutions[index] ==
                directionalShadows.Resolution) {
                resolutionIndex = static_cast<int>(index);
                break;
            }
        }
        const char* const resolutionNames[] = {
            "2048 (32 MiB)", "4096 (128 MiB)" };
        if (ImGui::Combo("Shadow atlas budget", &resolutionIndex,
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
            100.0F, 50.0F, 65536.0F, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::SliderFloat(
            "Shadow strength", &directionalShadows.Strength,
            0.0F, 4.0F, "%.2fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Above 1x amplifies partial occlusion. Visibility saturates at zero instead of producing negative light.");
        int pcfRadius = directionalShadows.PcfRadius;
        if (ImGui::SliderInt("PCF radius", &pcfRadius, 0, 2)) {
            directionalShadows.PcfRadius =
                static_cast<uint8_t>(pcfRadius);
            changed = true;
        }
        ImGui::TextWrapped("World-anchored virtual pages; unchanged pages are reused. Budget includes the static cache and composed atlas, and controls residency rather than near-field texel size.");
        {
            // Godot 4.8's screen-space contact shadows (project settings and
            // the light's contact shadow group).
            auto& contact = directionalShadows.ContactShadows;
            changed |= ImGui::Checkbox("Contact shadows", &contact.Enabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Screen-space shadows marched toward the light from a camera depth of the shadow "
                                  "casters: small-scale shadows the shadow map misses (contacts, creases, folds). "
                                  "Combined with the shadow map by its minimum.");
            ImGui::BeginDisabled(!contact.Enabled);
            int length = contact.Length;
            const char* lengths[] = { "Short", "Normal", "Long" };
            if (ImGui::Combo("Contact shadow length", &length, lengths, 3)) {
                contact.Length = static_cast<uint8_t>(length);
                changed = true;
            }
            changed |= ImGui::SliderFloat("Contact surface thickness", &contact.SurfaceThickness, 0.001F, 0.1F,
                                          "%.3f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Assumed depth thickness of surfaces, a fraction of the depth range. Larger fills "
                                  "gaps in thin geometry but can bleed shadows through surfaces.");
            changed |= ImGui::SliderFloat("Contact shadow opacity", &contact.Opacity, 0.0F, 1.0F, "%.2f");
            changed |= ImGui::SliderFloat("Contact shadow blur", &contact.Blur, 0.0F, 10.0F, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Noise on the ray start: 0 is sharp; higher softens the edges (best with "
                                  "temporal anti-aliasing).");
            changed |= ImGui::Checkbox("Contact shadows at half resolution", &contact.HalfResolution);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Camera depth and rays at half width and height: about a quarter of the GPU "
                                  "cost. Very thin occluders can be missed.");
            ImGui::EndDisabled();
        }
        if (ImGui::TreeNode("Advanced directional shadows")) {
            changed |= ImGui::DragFloat(
                "Shadow constant bias",
                &directionalShadows.DepthBiasConstant,
                0.05F, 0.0F, 16.0F, "%.2f");
            changed |= ImGui::DragFloat(
                "Shadow slope bias",
                &directionalShadows.DepthBiasSlope,
                0.05F, 0.0F, 16.0F, "%.2f");
            changed |= ImGui::SliderFloat(
                "Shadow normal bias",
                &directionalShadows.NormalBias,
                0.0F, 10.0F, "%.2f texels");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("As Godot's directional normal bias: smooth-shaded models read the shadow this many "
                                  "texels along their vertex normal, across the light. Too low: they shadow "
                                  "themselves; too high: shadows look thinner.");
            ImGui::TreePop();
        }
        if (!directionalShadowsReady) ImGui::EndDisabled();
    }
    if (!directionalShadowsReady) {
        ImGui::TextDisabled("Unavailable: %s",
            directionalShadowCapability.Reason.c_str());
    }
    if (GraphicsSettingsRuntime::Instance().NativePresentationOverrideActive())
        ImGui::TextWrapped("Inactive while the F2 native override is enabled.");
    ImGui::TextWrapped("Shadows apply to every scene and are saved automatically; the scene lights above are saved per scene.");
    return changed;
}

bool DrawPointShadows(GraphicsSettings& settings, const GraphicsCapabilities& capabilities) {
    bool changed = false;
    ImGui::SeparatorText("Point light shadows");
    auto& pointShadows = settings.Effects.PointShadows;
    const auto& capability = capabilities.Get(GraphicsCapability::NriDirectionalShadows);
    const char* const modes[] = { "Off", "Cube maps" };
    changed |= SettingsUi::ValidatedCombo<PointShadowMode>(
        "Point shadow mode", settings, capabilities, modes,
        [](auto& value, auto mode) { value.Effects.PointShadows.Mode = mode; },
        [](const auto& value) { return value.Effects.PointShadows.Mode; });
    if (pointShadows.Mode == PointShadowMode::CubeMaps) {
        if (!capability.Available) ImGui::BeginDisabled();
        constexpr uint32_t resolutions[] = { 256U, 512U, 1024U };
        int resolutionIndex = 1;
        for (size_t index = 0; index < std::size(resolutions); ++index)
            if (resolutions[index] == pointShadows.Resolution) resolutionIndex = static_cast<int>(index);
        const char* const resolutionNames[] = {
            "256 (1.5 MiB per light)", "512 (6.1 MiB per light)", "1024 (24 MiB per light)" };
        if (ImGui::Combo("Point shadow face resolution", &resolutionIndex, resolutionNames,
                         static_cast<int>(std::size(resolutionNames)))) {
            pointShadows.Resolution = resolutions[resolutionIndex];
            changed = true;
        }
        int maxLights = pointShadows.MaxLights;
        if (ImGui::SliderInt("Shadowed point lights", &maxLights, 1, 8)) {
            pointShadows.MaxLights = static_cast<uint8_t>(maxLights);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The lights nearest the camera cast shadows; the others stay unshadowed.");
        float reach = pointShadows.Range * 100.0F;
        if (ImGui::SliderFloat("Point shadow reach", &reach, 25.0F, 100.0F, "%.0f%% of light radius",
                               ImGuiSliderFlags_AlwaysClamp)) {
            pointShadows.Range = reach / 100.0F;
            changed = true;
        }
        changed |= ImGui::SliderFloat("Point shadow strength", &pointShadows.Strength, 0.0F, 1.0F, "%.2f",
                                      ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextWrapped("Navi, fairies and native local lights shadow only within their own radius. Rows of unchanged static lights are reused.");
        if (!capability.Available) ImGui::EndDisabled();
    }
    if (!capability.Available)
        ImGui::TextDisabled("Unavailable: %s", capability.Reason.c_str());
    return changed;
}
} // namespace

bool GraphicsSettingsPanel::DrawShadowSettings(GraphicsSettings& settings, const GraphicsCapabilities& capabilities) {
    // Both shadow kinds need the full scene lighting of the Modern model.
    auto shadowCapabilities = capabilities;
    if (!UsesFullSceneLighting(settings.Effects.SurfaceLighting))
        shadowCapabilities.Set(GraphicsCapability::NriDirectionalShadows, false,
                               "Choose the Modern lighting model in Lighting & GI.");
    bool changed = DrawDirectionalShadows(settings, shadowCapabilities);
    changed |= DrawPointShadows(settings, shadowCapabilities);
    return changed;
}
} // namespace Fast::Oot3d
