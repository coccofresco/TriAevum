#pragma once

#include "oot3d_top_screen_config.h"
#include "oot3d_top_screen_controls.h"
#include "oot3d_control_settings_widgets.h"
#include <imgui.h>
#include <array>

namespace Oot3dNativeGame {

// Controls owns these fields, not the HUD layout stored in the same JSON file.
inline void CopyTopScreenControlSettings(const TopScreenUiConfig& source, TopScreenUiConfig& target) {
    target.SelectAction = source.SelectAction;
    target.ExitItemsToSaveScreen = source.ExitItemsToSaveScreen;
    target.CameraZoomPercent = source.CameraZoomPercent;
    target.CameraFovPercent = source.CameraFovPercent;
    target.ChildDpad = source.ChildDpad;
    target.AdultDpad = source.AdultDpad;
    target.FreeCameraEnabled = source.FreeCameraEnabled;
    target.FreeCameraSpeedLevel = source.FreeCameraSpeedLevel;
    target.FreeCameraSmoothing = source.FreeCameraSmoothing;
    target.FreeCameraInvertX = source.FreeCameraInvertX;
    target.FreeCameraInvertY = source.FreeCameraInvertY;
    target.CStickAimSpeedLevel = source.CStickAimSpeedLevel;
    target.CStickAimInvertX = source.CStickAimInvertX;
    target.CStickAimInvertY = source.CStickAimInvertY;
    target.QuickPutAway = source.QuickPutAway;
    target.LandingRollFix = source.LandingRollFix;
    target.RollDash = source.RollDash;
    target.AimingReticle = source.AimingReticle;
    target.Rain3d = source.Rain3d;
    target.Rain3dDensityPercent = source.Rain3dDensityPercent;
}

// SingleScreen 3.0 gameplay options.
inline bool DrawTopScreenGameplay(TopScreenUiConfig& config) {
    bool changed = ImGui::Checkbox("Quick put away", &config.QuickPutAway);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Items can be put away immediately instead of after one second.");
    changed |= ImGui::Checkbox("Landing roll", &config.LandingRollFix);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Roll when holding forward after a fall, as on the N64.");
    changed |= ImGui::Checkbox("Roll dash", &config.RollDash);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold the action button after a roll to dash.");
    changed |= ImGui::Checkbox("Aiming reticle", &config.AimingReticle);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show a reticle while aiming the bow or slingshot.");
    changed |= ImGui::Checkbox("3D rain", &config.Rain3d);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rain falls in the world around the camera and stops at roofs.");
    int density = config.Rain3dDensityPercent;
    if (ControlWidgets::SliderInt("Rain density", &density, kTopScreenRainDensityMinimum,
                                  kTopScreenRainDensityMaximum, "%d%%")) {
        config.Rain3dDensityPercent =
            static_cast<uint16_t>(density / kTopScreenRainDensityStep * kTopScreenRainDensityStep);
        changed = true;
    }
    return changed;
}

inline bool DrawTopScreenCameraBehavior(TopScreenUiConfig& config) {
    bool changed = ImGui::Checkbox("Enabled##freecam", &config.FreeCameraEnabled);
    int speed = config.FreeCameraSpeedLevel;
    if (ControlWidgets::SliderInt("Speed##freecam", &speed, 0, 6)) {
        config.FreeCameraSpeedLevel = static_cast<uint8_t>(speed);
        changed = true;
    }
    changed |= ImGui::Checkbox("Invert X##freecam", &config.FreeCameraInvertX);
    changed |= ImGui::Checkbox("Invert Y##freecam", &config.FreeCameraInvertY);
    int zoom = config.CameraZoomPercent, fov = config.CameraFovPercent;
    if (ControlWidgets::SliderInt("Camera zoom", &zoom, kTopScreenCameraZoomMinimum, kTopScreenCameraZoomMaximum, "%d%%")) {
        config.CameraZoomPercent = static_cast<uint8_t>(zoom / 5 * 5);
        changed = true;
    }
    if (ControlWidgets::SliderInt("Field of view", &fov, kTopScreenCameraFovMinimum, kTopScreenCameraFovMaximum, "%d%%")) {
        config.CameraFovPercent = static_cast<uint8_t>(fov / 5 * 5);
        changed = true;
    }
    return changed;
}

inline bool DrawTopScreenStickAiming(TopScreenUiConfig& config) {
    ImGui::SeparatorText("C-stick aiming");
    bool changed = false;
    int speed = config.CStickAimSpeedLevel;
    if (ControlWidgets::SliderInt("Speed##aim", &speed, 0,
                                  static_cast<int>(kTopScreenCStickAimSpeeds.size()) - 1)) {
        config.CStickAimSpeedLevel = static_cast<uint8_t>(speed);
        changed = true;
    }
    constexpr const char* smoothingLabels[]{"Off", "Light", "Medium", "Default", "Heavy"};
    int smoothing = static_cast<int>(config.FreeCameraSmoothing);
    if (ControlWidgets::Combo("Smoothing##aim", &smoothing, smoothingLabels, 5)) {
        config.FreeCameraSmoothing = static_cast<TopScreenFreeCameraSmoothing>(smoothing);
        changed = true;
    }
    changed |= ImGui::Checkbox("Invert X##aim", &config.CStickAimInvertX);
    changed |= ImGui::Checkbox("Invert Y##aim", &config.CStickAimInvertY);
    return changed;
}

inline bool DrawTopScreenActionBindings(TopScreenUiConfig& config) {
    constexpr const char* labels[]{"None", "View", "Ocarina", "Iron Boots", "Hover Boots",
        "Item ZR", "Item ZL", "Sword toggle", "All boots", "Minimap toggle", "Boomerang",
        "Slingshot", "Tunic toggle", "Shield toggle"};
    constexpr const char* directions[]{"Up", "Down", "Left", "Right"};
    bool changed = false;
    const auto draw = [&](const char* id, auto& bindings) {
        ImGui::SeparatorText(id);
        ImGui::PushID(id);
        for (size_t i = 0; i < bindings.size(); ++i) {
            int action = static_cast<int>(bindings[i]);
            if (ControlWidgets::Combo(directions[i], &action, labels, 14)) {
                bindings[i] = static_cast<TopScreenDpadAction>(action);
                changed = true;
            }
        }
        ImGui::PopID();
    };
    draw("Child D-pad", config.ChildDpad);
    draw("Adult D-pad", config.AdultDpad);
    int select = static_cast<int>(config.SelectAction);
    constexpr const char* actions[]{"Open Save screen", "Toggle minimap"};
    if (ControlWidgets::Combo("SELECT action", &select, actions, 2)) {
        config.SelectAction = static_cast<TopScreenSelectAction>(select);
        changed = true;
    }
    changed |= ImGui::Checkbox("B exits Items to Save screen", &config.ExitItemsToSaveScreen);
    return changed;
}

} // namespace Oot3dNativeGame
