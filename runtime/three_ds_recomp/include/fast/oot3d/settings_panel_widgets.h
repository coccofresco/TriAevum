#pragma once

#include "fast/oot3d/graphics_settings.h"
#include <imgui.h>
#include <algorithm>
#include <string>

namespace Fast::Oot3d::SettingsUi {

template <typename T, size_t N>
bool EnumCombo(const char* label, T& value, const char* const (&labels)[N]) {
    int index = static_cast<int>(value);
    if (!ImGui::Combo(label, &index, labels, static_cast<int>(N))) return false;
    value = static_cast<T>(index);
    return true;
}

// Probe the same validator used by Apply, rather than maintaining a second
// capability policy in the menu. The current value (and Off) remains visible.
template <typename T, size_t N, typename Set, typename Get>
bool ValidatedCombo(const char* label, GraphicsSettings& settings,
                    const GraphicsCapabilities& capabilities,
                    const char* const (&labels)[N], Set set, Get get) {
    const auto current = get(settings);
    const size_t index = std::min(static_cast<size_t>(current), N - 1U);
    if (!ImGui::BeginCombo(label, labels[index])) return false;
    bool changed = false;
    for (size_t item = 0; item < N; ++item) {
        const auto option = static_cast<T>(item);
        auto candidate = settings;
        candidate.Preset = GraphicsPreset::Custom;
        set(candidate, option);
        const auto validation = GraphicsSettingsService::Validate(candidate, capabilities);
        const bool supported = validation.Accepted() && get(validation.Value) == option;
        ImGui::BeginDisabled(!supported);
        if (ImGui::Selectable(labels[item], current == option)) {
            set(settings, option);
            changed = true;
        }
        ImGui::EndDisabled();
        if (!supported && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0F);
            for (const auto& issue : validation.Issues)
                ImGui::TextWrapped("%s: %s", issue.Field.c_str(), issue.Message.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        if (current == option) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
}

inline std::string DescribeApply(const GraphicsSettingsValidation& result) {
    std::string message = result.Accepted() ? "Applied" : "Rejected";
    for (const auto& issue : result.Issues)
        message += "\n" + issue.Field + ": " + issue.Message;
    return message;
}

} // namespace Fast::Oot3d::SettingsUi
