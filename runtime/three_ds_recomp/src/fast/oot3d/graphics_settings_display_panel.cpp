#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/settings_panel_widgets.h"

namespace Fast::Oot3d {
using SettingsUi::EnumCombo;

bool GraphicsSettingsPanel::DrawDisplaySettings(GraphicsSettings& settings, const GraphicsCapabilities& capabilities) {
    bool changed = false;
    ImGui::SeparatorText("Display");
    const auto observed = GraphicsSettingsRuntime::Instance().DisplayMetrics();
    if (observed.OutputWidth != 0 && observed.OutputHeight != 0) {
        ImGui::Text("Output framebuffer: %u x %u (%.3f:1)", observed.OutputWidth,
                    observed.OutputHeight, float(observed.OutputWidth) / observed.OutputHeight);
        if (observed.SceneWidth != 0)
            ImGui::Text("Scene image: %u x %u (%.2fx)", observed.SceneWidth,
                        observed.SceneHeight, observed.InternalScale);
    }
    const char* const windowModes[] = { "Windowed", "Borderless", "Exclusive fullscreen" };
    changed |= SettingsUi::ValidatedCombo<WindowMode>(
        "Window mode", settings, capabilities, windowModes,
        [](auto& value, auto mode) { value.Window = mode; },
        [](const auto& value) { return value.Window; });
    struct ResolutionPreset {
        const char* Label;
        uint32_t Width;
        uint32_t Height;
    };
    constexpr std::array<ResolutionPreset, 8> resolutionPresets{{
        {"800 x 600 (4:3)", 800, 600},
        {"1280 x 960 (4:3)", 1280, 960},
        {"1600 x 1200 (4:3)", 1600, 1200},
        {"1280 x 720 (720p)", 1280, 720},
        {"1600 x 900 (900p)", 1600, 900},
        {"1920 x 1080 (1080p)", 1920, 1080},
        {"2560 x 1440 (1440p)", 2560, 1440},
        {"3840 x 2160 (4K)", 3840, 2160},
    }};
    const ResolutionPreset* selectedResolution = nullptr;
    for (const auto& preset : resolutionPresets) {
        if (preset.Width == settings.OutputWidth &&
            preset.Height == settings.OutputHeight) {
            selectedResolution = &preset;
            break;
        }
    }
    const std::string customResolution =
        std::to_string(settings.OutputWidth) + " x " +
        std::to_string(settings.OutputHeight) + " (custom)";
    ImGui::BeginDisabled(settings.Window == WindowMode::Borderless);
    if (ImGui::BeginCombo("Requested resolution",
                          selectedResolution != nullptr
                              ? selectedResolution->Label
                              : customResolution.c_str())) {
        for (const auto& preset : resolutionPresets) {
            const bool selected = selectedResolution == &preset;
            if (ImGui::Selectable(preset.Label, selected)) {
                settings.OutputWidth = preset.Width;
                settings.OutputHeight = preset.Height;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!mOutputResolutionEditing) {
        mPendingOutputResolution = {
            static_cast<int>(settings.OutputWidth),
            static_cast<int>(settings.OutputHeight)};
    }
    ImGui::InputInt2("Output resolution", mPendingOutputResolution.data());
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settings.OutputWidth = static_cast<uint32_t>(
            std::max(320, mPendingOutputResolution[0]));
        settings.OutputHeight = static_cast<uint32_t>(
            std::max(240, mPendingOutputResolution[1]));
        changed = true;
    }
    mOutputResolutionEditing = ImGui::IsItemActive();
    ImGui::EndDisabled();
    if (settings.Window == WindowMode::Borderless)
        ImGui::TextDisabled("Borderless uses the desktop resolution.");
    if (!mRenderScaleEditing) {
        mPendingRenderScale = settings.InternalResolutionScale;
    }
    const bool upscalerOwnsScale = settings.AntiAliasing == AntiAliasingMode::Upscaler;
    ImGui::BeginDisabled(upscalerOwnsScale);
    if (ImGui::SliderFloat("Internal render scale", &mPendingRenderScale,
                           0.5F, 2.0F, "%.2fx")) {
        mRenderScaleEditing = true;
    }
    if (mRenderScaleEditing && ImGui::IsItemDeactivatedAfterEdit()) {
        settings.InternalResolutionScale = mPendingRenderScale;
        mRenderScaleEditing = false;
        changed = true;
    }
    ImGui::EndDisabled();
    if (upscalerOwnsScale) ImGui::TextDisabled("Render scale is set by upscaler quality.");
    changed |= ImGui::Checkbox("VSync", &settings.VSync);

    ImGui::SeparatorText("Motion and camera");
    const char* const frameRates[] = {
        "Original 30 FPS", "Interpolated 2x (60 FPS)",
        "Interpolated 3x (90 FPS)", "Uncapped"};
    changed |= EnumCombo("Frame rate", settings.FrameRate, frameRates);
    changed |= ImGui::SliderFloat("Global scene FOV", &settings.FovMultiplier,
                                  1.0F, 1.5F, "%.2fx");

    return changed;
}
} // namespace Fast::Oot3d
