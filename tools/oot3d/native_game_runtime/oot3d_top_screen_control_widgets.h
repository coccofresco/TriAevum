#pragma once

#include "oot3d_top_screen_config.h"
#include <imgui.h>
#include <array>

namespace Oot3dNativeGame {

inline bool DrawTopScreenCameraBehavior(TopScreenUiConfig& config) {
    bool changed = ImGui::Checkbox("Enabled##freecam", &config.FreeCameraEnabled);
    int speed = config.FreeCameraSpeedLevel;
    if (ImGui::SliderInt("Speed##freecam", &speed, 0, 6)) {
        config.FreeCameraSpeedLevel = static_cast<uint8_t>(speed);
        changed = true;
    }
    changed |= ImGui::Checkbox("Invert X##freecam", &config.FreeCameraInvertX);
    changed |= ImGui::Checkbox("Invert Y##freecam", &config.FreeCameraInvertY);
    int zoom = config.CameraZoomPercent, fov = config.CameraFovPercent;
    if (ImGui::SliderInt("Camera zoom", &zoom, kTopScreenCameraZoomMinimum, kTopScreenCameraZoomMaximum, "%d%%")) {
        config.CameraZoomPercent = static_cast<uint8_t>(zoom / 5 * 5);
        changed = true;
    }
    if (ImGui::SliderInt("Field of view", &fov, kTopScreenCameraFovMinimum, kTopScreenCameraFovMaximum, "%d%%")) {
        config.CameraFovPercent = static_cast<uint8_t>(fov / 5 * 5);
        changed = true;
    }
    return changed;
}

inline bool DrawTopScreenStickAiming(TopScreenUiConfig& config) {
    ImGui::SeparatorText("C-stick aiming");
    bool changed = false;
    int speed = config.CStickAimSpeedLevel;
    if (ImGui::SliderInt("Speed##aim", &speed, 0, 6)) {
        config.CStickAimSpeedLevel = static_cast<uint8_t>(speed);
        changed = true;
    }
    constexpr const char* smoothingLabels[]{"Off", "Light", "Medium", "Default", "Heavy"};
    int smoothing = static_cast<int>(config.FreeCameraSmoothing);
    if (ImGui::Combo("Smoothing##aim", &smoothing, smoothingLabels, 5)) {
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
            if (ImGui::Combo(directions[i], &action, labels, 14)) {
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
    if (ImGui::Combo("SELECT action", &select, actions, 2)) {
        config.SelectAction = static_cast<TopScreenSelectAction>(select);
        changed = true;
    }
    changed |= ImGui::Checkbox("B exits Items to Save screen", &config.ExitItemsToSaveScreen);
    return changed;
}

} // namespace Oot3dNativeGame
