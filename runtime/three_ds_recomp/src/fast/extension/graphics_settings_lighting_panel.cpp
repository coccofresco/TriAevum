#include "fast/extension/graphics_settings_window.h"
#include "fast/extension/graphics_settings_runtime.h"
#include "fast/extension/settings_panel_widgets.h"

namespace Fast::Extension {
using SettingsUi::EnumCombo;

bool GraphicsSettingsPanel::DrawLightingSettings(GraphicsSettings& settings, const GraphicsCapabilities& capabilities) {
    bool changed = false;
    ImGui::SeparatorText("Surface lighting");
    // Native: native shading, PBR on supported pack materials (plain native
    // shading when the renderer cannot relight). Modern: per-pixel + PBR.
    const char* const surfaceLightingModes[] = {"Native", "Modern"};
    changed |= SettingsUi::ValidatedCombo<int>(
        "Lighting model", settings, capabilities, surfaceLightingModes,
        [&capabilities](auto& value, int choice) {
            value.Effects.SurfaceLighting =
                choice == 1 ? SurfaceLightingMode::PerPixelPbr
                : capabilities.Has(GraphicsCapability::NativeSurfaceLighting) ? SurfaceLightingMode::NativePbr
                                                                                : SurfaceLightingMode::Native;
        },
        [](const auto& value) { return UsesFullSceneLighting(value.Effects.SurfaceLighting) ? 1 : 0; });
    const bool modern = UsesFullSceneLighting(settings.Effects.SurfaceLighting);
    ImGui::TextWrapped(modern
        ? "Per-pixel lighting from native scene lights and selected local lights, with PBR materials. Unsupported native materials keep their original shading."
        : "Original native shading. Supported PBR materials use the native scene lights; other surfaces and projected shadows stay native. Lights & Shadows is available with Modern.");
    if (UsesPbrMaterials(settings.Effects.SurfaceLighting)) {
        ImGui::TextWrapped(modern
            ? "PBR uses Azahar material packs configured in Materials > Texture packs. Materials without a pack use native per-pixel lighting."
            : "PBR uses Azahar material packs configured in Materials > Texture packs. Surfaces without a supported material keep original native shading.");
        ImGui::TextWrapped("Edit individual materials, maps and PBR response in Materials > Material editor.");
    }

    ImGui::SeparatorText("Global illumination");
    const char* const giModes[] = {"Off", "Brixelizer GI", "Screen-space GI"};
    changed |= SettingsUi::ValidatedCombo<GlobalIlluminationMode>(
        "Global illumination", settings, capabilities, giModes,
        [](auto& value, auto mode) { value.Effects.GlobalIllumination.Mode = mode; },
        [](const auto& value) { return value.Effects.GlobalIllumination.Mode; });
    auto& globalIllumination = settings.Effects.GlobalIllumination;
    const bool brixelizer = globalIllumination.Mode == GlobalIlluminationMode::BrixelizerGi;
    const bool screenSpace = globalIllumination.Mode == GlobalIlluminationMode::ScreenSpace;
    if (globalIllumination.Mode != GlobalIlluminationMode::Off) {
        const char* const resolutionNames[] = {"Half", "Quarter"};
        // Each mode's own resolution: Brixelizer's internal resolution, or the
        // screen-space GI's work scale.
        auto& resolutionSetting = screenSpace ? globalIllumination.ScreenSpace.WorkScale : globalIllumination.Resolution;
        int resolution = static_cast<int>(resolutionSetting);
        if (ImGui::Combo(screenSpace ? "SSGI work scale" : "GI resolution", &resolution, resolutionNames, 2)) {
            resolutionSetting = static_cast<GlobalIlluminationResolution>(resolution);
            changed = true;
        }
        changed |= ImGui::SliderFloat("GI diffuse intensity", &globalIllumination.DiffuseIntensity,
                                      0.0F, 8.0F, "%.2f");
        changed |= ImGui::SliderFloat("GI native scene light", &globalIllumination.SceneLight, 0.0F, 4.0F, "%.2f");
        changed |= ImGui::SliderFloat("GI key light", &globalIllumination.KeyLight, 0.0F, 4.0F, "%.2f");
        changed |= ImGui::SliderFloat("GI dynamic lights", &globalIllumination.DynamicLights, 0.0F, 4.0F, "%.2f");
        if (brixelizer) {
            changed |= ImGui::SliderFloat("GI sky light", &globalIllumination.SkyLight, 0.0F, 4.0F, "%.2f");
            ImGui::TextWrapped("GI bounces the direct light of the native scene directionals, the added key light "
                               "and dynamic lights (native local lights, Navi, fairies), and adds the sky light that "
                               "open surfaces receive (the native sky colour). Ambient and fill light are baked into "
                               "OoT's colours and are never bounced.");
            ImGui::TextWrapped("Intensity, the strongest source and switching a source off apply at once; other "
                               "changes of the source mix settle over a few seconds.");
            changed |= ImGui::Checkbox("GI specular reflections", &globalIllumination.Specular);
            if (globalIllumination.Specular) {
                changed |= ImGui::SliderFloat("GI specular intensity", &globalIllumination.SpecularIntensity,
                                              0.0F, 2.0F, "%.2f");
                if (settings.Effects.Reflections != ReflectionMode::Off)
                    ImGui::TextWrapped("Screen-space reflections are enabled and provide the reflections; "
                                       "GI specular applies when they are off.");
            }
        }
        if (screenSpace) {
            auto& screen = globalIllumination.ScreenSpace;
            ImGui::TextWrapped("Screen-space GI bounces the direct light of the native scene directionals, the "
                               "added key light and dynamic lights between surfaces visible on screen. Ambient and "
                               "fill light are baked into OoT's colours and are never bounced.");
            int sections = screen.Sections;
            if (ImGui::SliderInt("SSGI sections", &sections, 1, 4)) {
                screen.Sections = static_cast<uint8_t>(sections);
                changed = true;
            }
            const auto choice = [&changed](const char* label, uint16_t& value, const auto& options,
                                           const auto& names) {
                const int count = static_cast<int>(sizeof(options) / sizeof(options[0]));
                int index = count - 1;
                for (int i = 0; i < count; ++i)
                    if (value <= options[i]) {
                        index = i;
                        break;
                    }
                if (ImGui::Combo(label, &index, names, count)) {
                    value = options[index];
                    changed = true;
                }
            };
            const uint16_t candidateOptions[] = {8U, 16U, 20U, 24U, 32U};
            const char* const candidateNames[] = {"8", "16", "20", "24", "32"};
            choice("SSGI radial candidates", screen.RadialCandidates, candidateOptions, candidateNames);
            const uint16_t sectorOptions[] = {32U, 64U, 128U};
            const char* const sectorNames[] = {"32", "64", "128"};
            choice("SSGI sectors", screen.Sectors, sectorOptions, sectorNames);
            changed |= ImGui::SliderFloat("SSGI radius", &screen.Radius, 10.0F, 1000.0F, "%.0f units");
            changed |= ImGui::SliderFloat("SSGI thickness", &screen.Thickness, 1.0F, 200.0F, "%.1f units");
            changed |= ImGui::Checkbox("SSGI temporal accumulation", &screen.Temporal);
            int filterLevels = screen.FilterLevels;
            if (ImGui::SliderInt("SSGI filter levels", &filterLevels, 0, 4)) {
                screen.FilterLevels = static_cast<uint8_t>(filterLevels);
                changed = true;
            }
            changed |= ImGui::SliderFloat("SSGI highlight clamp", &screen.HighlightClamp, 0.0F, 16.0F,
                                          screen.HighlightClamp > 0.0F ? "%.2f" : "Off");
            changed |= ImGui::Checkbox("SSGI validation", &screen.Validation);
            ImGui::TextWrapped("Radius and thickness are in OoT scene units (child Link is about 45-50 units tall). "
                               "Fewer radial candidates darken the bounce. Validation composes the bounce without "
                               "the intensity gain and the highlight clamp.");
        }
        if (!UsesFullSceneLighting(settings.Effects.SurfaceLighting))
            ImGui::TextWrapped("Active only with the Modern lighting model.");
        const auto status = GraphicsSettingsRuntime::Instance().GlobalIlluminationStatus();
        if (status.Mode == globalIllumination.Mode && !status.Ready && !status.Reason.empty())
            ImGui::TextWrapped("GI not available yet: %s", status.Reason.c_str());
    }
    if (!capabilities.Has(GraphicsCapability::BrixelizerGlobalIllumination))
        ImGui::TextWrapped("Brixelizer GI unavailable: %s",
                           capabilities.Get(GraphicsCapability::BrixelizerGlobalIllumination).Reason.c_str());
    if (!capabilities.Has(GraphicsCapability::ScreenSpaceGlobalIllumination))
        ImGui::TextWrapped("Screen-space GI unavailable: %s",
                           capabilities.Get(GraphicsCapability::ScreenSpaceGlobalIllumination).Reason.c_str());
    return changed;
}

bool GraphicsSettingsPanel::DrawAmbientOcclusionSettings(GraphicsSettings& settings,
                                                         const GraphicsCapabilities& capabilities) {
    bool changed = false;
    ImGui::SeparatorText("Ambient occlusion");
    const bool aoReady = capabilities.Has(GraphicsCapability::NriInterop) &&
                         capabilities.Has(GraphicsCapability::SampledDepth) &&
                         capabilities.Has(GraphicsCapability::NormalGuide) &&
                         capabilities.Has(GraphicsCapability::ValidViewMetadata);
    const char* const aoModes[] = {"Off", "Multiscale GTAO", "FidelityFX CACAO", "iMMERSE MXAO"};
    changed |= SettingsUi::ValidatedCombo<AmbientOcclusionMode>(
        "Ambient occlusion", settings, capabilities, aoModes,
        [](auto& value, auto mode) { value.Effects.AmbientOcclusion = mode; },
        [](const auto& value) { return value.Effects.AmbientOcclusion; });
    const bool cacao = settings.Effects.AmbientOcclusion == AmbientOcclusionMode::Cacao;
    const bool mxao = settings.Effects.AmbientOcclusion == AmbientOcclusionMode::Mxao;
    const bool ao = settings.Effects.AmbientOcclusion != AmbientOcclusionMode::Off;
    if (mxao) {
        ImGui::BeginDisabled(!aoReady || !capabilities.Has(GraphicsCapability::ImmerseMxao));
        changed |= DrawMxaoSettings(settings.Effects);
        ImGui::EndDisabled();
    } else if (ao) {
        ImGui::BeginDisabled(!aoReady || (cacao && !capabilities.Has(GraphicsCapability::FidelityFxCacao)));
        int quality = settings.Effects.AoQuality;
        const char* const qualityNames[] = {"Low", "Medium"};
        if (ImGui::Combo("AO quality", &quality, qualityNames, 2)) {
            settings.Effects.AoQuality = static_cast<uint8_t>(quality);
            changed = true;
        }
        changed |= ImGui::SliderFloat(cacao ? "AO radius" : "Broad radius", &settings.Effects.AoRadius,
                                      0.05F, 100.0F, "%.2f", ImGuiSliderFlags_Logarithmic);
        if (!cacao)
            changed |= ImGui::SliderFloat("Fine radius", &settings.Effects.AoDetailRadius,
                                          0.01F, settings.Effects.AoRadius, "%.2f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat(cacao ? "Detail strength" : "Fine strength", &settings.Effects.AoDetailStrength,
                                      0.0F, 10.0F, "%.2f");
        changed |= ImGui::SliderFloat(cacao ? "AO strength" : "Broad strength", &settings.Effects.AoStrength,
                                      0.0F, 10.0F, "%.2f");
        if (!cacao)
            ImGui::TextWrapped("Separate gains for fine contacts and additional broad occlusion. Radii use native scene units.");
        if (ImGui::TreeNode("Advanced AO")) {
            changed |= ImGui::SliderFloat("Shadow power",
                                          &settings.Effects.AoShadowPower,
                                          0.1F, 10.0F, "%.2f");
            changed |= ImGui::SliderFloat("Shadow clamp",
                                          &settings.Effects.AoShadowClamp,
                                          0.0F, 1.0F, "%.3f");
            if (cacao) {
                changed |= ImGui::SliderFloat("Horizon angle threshold", &settings.Effects.AoHorizonAngleThreshold,
                                              0.0F, 0.2F, "%.3f");
                int blurPasses = static_cast<int>(settings.Effects.AoBlurPassCount);
                if (ImGui::SliderInt("Blur passes", &blurPasses, 0, 8)) {
                    settings.Effects.AoBlurPassCount = static_cast<uint32_t>(blurPasses);
                    changed = true;
                }
            }
            changed |= ImGui::DragFloat("Fade start",
                                        &settings.Effects.AoFadeOutFrom,
                                        1.0F, 0.0F, 100000.0F, "%.1f");
            changed |= ImGui::DragFloat("Fade end",
                                        &settings.Effects.AoFadeOutTo,
                                        1.0F, 0.01F, 100000.0F, "%.1f");
            changed |= ImGui::SliderFloat("Sharpness",
                                          &settings.Effects.AoSharpness,
                                          0.0F, 1.0F, "%.3f");
            ImGui::TreePop();
        }
        ImGui::EndDisabled();
    }
    if (ao) {
        ImGui::BeginDisabled(!aoReady);
        int relief = static_cast<int>(settings.Effects.AoRelief);
        const char* const reliefNames[] = {"Off", "Subtle", "Medium", "Strong"};
        if (ImGui::Combo("AO texture relief", &relief, reliefNames, 4)) {
            settings.Effects.AoRelief = static_cast<AoTextureRelief>(relief);
            changed = true;
        }
        ImGui::TextWrapped("Relief from each world material's colour texture, added to the scene normals the AO "
                           "reads (reflections and GI read the same normals).");
        ImGui::EndDisabled();
    }
    if (!aoReady) {
        ImGui::TextWrapped("AO unavailable: NRI, depth, normals or camera metadata missing.");
    }
    if (!capabilities.Has(GraphicsCapability::FidelityFxCacao))
        ImGui::TextWrapped("CACAO unavailable: %s", capabilities.Get(GraphicsCapability::FidelityFxCacao).Reason.c_str());
    if (!capabilities.Has(GraphicsCapability::ImmerseMxao))
        ImGui::TextWrapped("MXAO unavailable: %s", capabilities.Get(GraphicsCapability::ImmerseMxao).Reason.c_str());
    DrawMxaoCredits();
    return changed;
}
} // namespace Fast::Extension
