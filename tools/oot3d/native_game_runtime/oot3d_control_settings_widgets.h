#pragma once

#include <imgui.h>
#include <cctype>
#include <cstring>
#include <string>

namespace Oot3dNativeGame::ControlWidgets {

// Labels above full-width fields avoid a label/slider overflow at narrow widths.
inline std::string Field(const char* label) {
    const char* end = std::strstr(label, "##");
    ImGui::TextUnformatted(label, end);
    ImGui::SetNextItemWidth(-1.0F);
    return std::string("##") + label;
}

inline bool SliderInt(const char* label, int* value, int minimum, int maximum,
                      const char* format = "%d") {
    return ImGui::SliderInt(Field(label).c_str(), value, minimum, maximum, format);
}

inline bool SliderFloat(const char* label, float* value, float minimum, float maximum,
                        const char* format, ImGuiSliderFlags flags = 0) {
    return ImGui::SliderFloat(Field(label).c_str(), value, minimum, maximum, format, flags);
}

inline bool Combo(const char* label, int* value, const char* const items[], int count) {
    return ImGui::Combo(Field(label).c_str(), value, items, count);
}

inline std::string DisplayName(const char* name) {
    if (std::strcmp(name, "none") == 0) return "Unassigned";
    if (std::strcmp(name, "keyboard_mouse") == 0) return "Keyboard + mouse";
    if (std::strcmp(name, "digital_look") == 0) return "Look direction keys";
    if (std::strcmp(name, "controller_motion") == 0) return "Controller gyro + accelerometer";
    std::string result(name);
    bool upper = true;
    for (char& c : result) {
        if (c == '_') { c = ' '; upper = true; }
        else if (upper) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); upper = false; }
    }
    return result;
}

inline bool Contains(const std::string& value, const char* filter) {
    std::string haystack(value), needle(filter);
    const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    for (char& c : haystack) c = lower(c);
    for (char& c : needle) c = lower(c);
    return haystack.find(needle) != std::string::npos;
}

} // namespace Oot3dNativeGame::ControlWidgets
