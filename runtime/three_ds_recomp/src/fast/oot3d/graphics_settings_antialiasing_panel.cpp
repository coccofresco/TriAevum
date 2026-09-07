#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/settings_panel_widgets.h"

namespace Fast::Oot3d {
bool GraphicsSettingsPanel::DrawAntialiasingSettings(
    GraphicsSettings& settings, const GraphicsCapabilities& capabilities) {
    bool changed = false;
    const char* const modes[] = {"Off", "FXAA", "SMAA 1x (High)", "MSAA", "Temporal AA", "NRI Upscaler"};
    changed |= SettingsUi::ValidatedCombo<AntiAliasingMode>(
        "Mode", settings, capabilities, modes,
        [&](auto& value, auto mode) {
            value.AntiAliasing = mode;
            if (mode == AntiAliasingMode::Msaa && value.MsaaSamples < 2U)
                value.MsaaSamples = 2U;
            if (mode == AntiAliasingMode::Upscaler) {
                auto probe = value;
                probe.Preset = GraphicsPreset::Custom;
                if (GraphicsSettingsService::Validate(probe, capabilities).Value.AntiAliasing != mode) {
                    for (auto provider : {UpscalerProvider::Nis, UpscalerProvider::Fsr,
                                          UpscalerProvider::Xess, UpscalerProvider::Dlss}) {
                        probe.Upscaler = provider;
                        if (GraphicsSettingsService::Validate(probe, capabilities).Value.AntiAliasing == mode) {
                            value.Upscaler = provider;
                            break;
                        }
                    }
                }
            }
        }, [](const auto& value) { return value.AntiAliasing; });

    if (settings.AntiAliasing == AntiAliasingMode::Msaa) {
        const char* const samples[] = {"2x", "4x", "8x"};
        changed |= SettingsUi::ValidatedCombo<int>(
            "Samples", settings, capabilities, samples,
            [](auto& value, int index) { value.MsaaSamples = static_cast<uint8_t>(2U << index); },
            [](const auto& value) {
                return value.AntiAliasing != AntiAliasingMode::Msaa ? -1
                     : value.MsaaSamples == 8 ? 2 : value.MsaaSamples == 4 ? 1 : 0;
            });
    }
    if (settings.AntiAliasing == AntiAliasingMode::Taa) {
        changed |= ImGui::SliderFloat("History weight", &settings.TaaHistoryWeight, 0.0F, 0.98F, "%.2f");
        changed |= ImGui::SliderFloat("Clamp expansion", &settings.TaaClampExpansion, 0.0F, 0.5F, "%.3f");
        changed |= ImGui::SliderFloat("Sharpness", &settings.TaaSharpness, 0.0F, 1.0F, "%.2f");
    }
    if (settings.AntiAliasing == AntiAliasingMode::Upscaler) {
        const char* const providers[] = {"NVIDIA Image Scaling (NIS)", "AMD FSR", "Intel XeSS", "NVIDIA DLSS"};
        changed |= SettingsUi::ValidatedCombo<UpscalerProvider>(
            "Provider", settings, capabilities, providers,
            [](auto& value, auto provider) {
                value.Upscaler = provider;
                value.AntiAliasing = AntiAliasingMode::Upscaler;
            }, [](const auto& value) {
                return value.AntiAliasing == AntiAliasingMode::Upscaler ? value.Upscaler
                     : static_cast<UpscalerProvider>(255);
            });
        const char* const qualities[] = {"Native", "Ultra quality", "Quality", "Balanced", "Performance", "Ultra performance"};
        changed |= SettingsUi::EnumCombo("Quality", settings.UpscalerMode, qualities);
        // Only NIS and FSR consume this setting. DLSS and XeSS do not.
        if (settings.Upscaler == UpscalerProvider::Nis || settings.Upscaler == UpscalerProvider::Fsr)
            changed |= ImGui::SliderFloat("Sharpness", &settings.UpscalerSharpness, 0.0F, 1.0F, "%.2f");
    }
    return changed;
}
} // namespace Fast::Oot3d
