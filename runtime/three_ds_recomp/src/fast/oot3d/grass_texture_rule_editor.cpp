#include "fast/oot3d/grass_texture_rule_editor.h"

#include "fast/oot3d/grass_surface_extractor.h"
#include "fast/oot3d/grass_texture_source_cache.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {
namespace {

template <typename T, size_t N>
bool EnumCombo(const char* label, T& value,
               const char* const (&labels)[N]) {
    int selected = static_cast<int>(value);
    if (!ImGui::Combo(label, &selected, labels,
                      static_cast<int>(N))) {
        return false;
    }
    value = static_cast<T>(selected);
    return true;
}

// The mask as extraction sees it (active rectangles included). Left-drag on
// it adds an active rectangle; right-click removes the one under the cursor.
bool DrawMaskPreview(const GrassScalarMask& mask, GrassPlacementRule& rule, float strength) {
    if (mask.Samples.empty() || mask.Width == 0U || mask.Height == 0U) {
        ImGui::TextDisabled("Mask preview unavailable");
        return false;
    }

    constexpr uint32_t kMaximumPreviewCells = 64U;
    const uint32_t columns =
        std::min<uint32_t>(mask.Width, kMaximumPreviewCells);
    const uint32_t rows =
        std::min<uint32_t>(mask.Height, kMaximumPreviewCells);
    const float availableWidth =
        std::max(ImGui::GetContentRegionAvail().x, 1.0F);
    const float previewWidth =
        std::min(availableWidth, 320.0F);
    const float aspect =
        static_cast<float>(mask.Height) /
        static_cast<float>(mask.Width);
    const ImVec2 previewSize{
        previewWidth,
        std::clamp(previewWidth * aspect, 24.0F, 180.0F)};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##GrassMaskPreview", previewSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const auto toMask = [&](ImVec2 point) {
        return ImVec2(std::clamp((point.x - origin.x) / previewSize.x, 0.0F, 1.0F),
                      std::clamp((point.y - origin.y) / previewSize.y, 0.0F, 1.0F));
    };
    bool changed = false;
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0F) &&
        rule.ActiveRects.size() < kMaximumGrassMaskRects) {
        const ImVec2 start = toMask({io.MouseClickedPos[0].x, io.MouseClickedPos[0].y});
        const ImVec2 end = toMask(io.MousePos);
        const ImVec2 a{origin.x + std::min(start.x, end.x) * previewSize.x,
                       origin.y + std::min(start.y, end.y) * previewSize.y};
        const ImVec2 b{origin.x + std::max(start.x, end.x) * previewSize.x,
                       origin.y + std::max(start.y, end.y) * previewSize.y};
        ImGui::GetWindowDrawList()->AddRect(a, b, IM_COL32(255, 200, 40, 255), 0.0F, 0, 2.0F);
    }
    if (ImGui::IsItemDeactivated() && rule.ActiveRects.size() < kMaximumGrassMaskRects &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const ImVec2 start = toMask({io.MouseClickedPos[0].x, io.MouseClickedPos[0].y});
        const ImVec2 end = toMask(io.MousePos);
        if (std::abs(end.x - start.x) > 0.01F && std::abs(end.y - start.y) > 0.01F) {
            rule.ActiveRects.push_back({std::min(start.x, end.x), std::min(start.y, end.y),
                                        std::max(start.x, end.x), std::max(start.y, end.y)});
            changed = true;
        }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const ImVec2 at = toMask(io.MousePos);
        for (size_t i = rule.ActiveRects.size(); i-- > 0;) {
            const auto& rect = rule.ActiveRects[i];
            if (at.x >= rect.MinU && at.x <= rect.MaxU && at.y >= rect.MinV && at.y <= rect.MaxV) {
                rule.ActiveRects.erase(rule.ActiveRects.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    double total = 0.0;
    uint32_t nonZero = 0U;
    uint32_t full = 0U;
    for (uint32_t row = 0U; row < rows; ++row) {
        const uint32_t sourceY = std::min<uint32_t>(
            static_cast<uint32_t>(
                (static_cast<uint64_t>(row) * mask.Height) /
                rows),
            mask.Height - 1U);
        for (uint32_t column = 0U; column < columns;
             ++column) {
            const uint32_t sourceX = std::min<uint32_t>(
                static_cast<uint32_t>(
                    (static_cast<uint64_t>(column) *
                     mask.Width) /
                    columns),
                mask.Width - 1U);
            const size_t sourceIndex =
                static_cast<size_t>(sourceY) * mask.Width +
                sourceX;
            const float value =
                EvaluateGrassMaskLevel(static_cast<float>(mask.Samples[sourceIndex]) / 255.0F, rule,
                                       (static_cast<float>(sourceX) + 0.5F) / static_cast<float>(mask.Width),
                                       (static_cast<float>(sourceY) + 0.5F) / static_cast<float>(mask.Height)) *
                strength;
            total += value;
            nonZero += value > (0.5F / 255.0F) ? 1U : 0U;
            full += value >= (254.5F / 255.0F) ? 1U : 0U;
            const ImU32 color = ImGui::GetColorU32(
                ImVec4(value, value, value, 1.0F));
            const ImVec2 minimum{
                origin.x + previewSize.x *
                    static_cast<float>(column) /
                    static_cast<float>(columns),
                origin.y + previewSize.y *
                    static_cast<float>(row) /
                    static_cast<float>(rows)};
            const ImVec2 maximum{
                origin.x + previewSize.x *
                    static_cast<float>(column + 1U) /
                    static_cast<float>(columns),
                origin.y + previewSize.y *
                    static_cast<float>(row + 1U) /
                    static_cast<float>(rows)};
            drawList->AddRectFilled(minimum, maximum, color);
        }
    }
    drawList->AddRect(
        origin,
        {origin.x + previewSize.x, origin.y + previewSize.y},
        ImGui::GetColorU32(ImGuiCol_Border));
    for (const auto& rect : rule.ActiveRects)
        drawList->AddRect({origin.x + rect.MinU * previewSize.x, origin.y + rect.MinV * previewSize.y},
                          {origin.x + rect.MaxU * previewSize.x, origin.y + rect.MaxV * previewSize.y},
                          IM_COL32(255, 200, 40, 255), 0.0F, 0, 1.5F);
    const float sampleCount =
        static_cast<float>(columns * rows);
    ImGui::TextDisabled(
        "Mean %.3f | present %.1f%% | full %.1f%%",
        static_cast<float>(total / sampleCount),
        100.0F * static_cast<float>(nonZero) / sampleCount,
        100.0F * static_cast<float>(full) / sampleCount);
    return changed;
}

} // namespace

bool DrawGrassTextureRuleControls(GrassPlacementRule& rule, bool surfaceControls, float previewStrength) {
    bool changed = false;
    const auto scalarMask =
        GrassTextureSourceCache::Instance().AcquireMask(
            rule.Target.Rgba8Hash, rule.Channel);
    const auto averageColor =
        GrassTextureSourceCache::Instance().AcquireAverageColor(
            rule.Target.Rgba8Hash);
    ImGui::Text("Grass mask: %s (%ux%u)",
                scalarMask.Samples.empty() ? "waiting" : "ready",
                scalarMask.Width, scalarMask.Height);
    if (averageColor.has_value()) {
        ImGui::ColorButton(
            "Texture average",
            ImVec4((*averageColor)[0], (*averageColor)[1],
                   (*averageColor)[2], 1.0F),
            ImGuiColorEditFlags_NoTooltip,
            ImVec2(20.0F, 20.0F));
        ImGui::SameLine();
        ImGui::TextDisabled(
            "alpha-weighted average RGB %.3f, %.3f, %.3f",
            (*averageColor)[0], (*averageColor)[1],
            (*averageColor)[2]);
    }
    const char* const channels[] = {
        "Red", "Green", "Blue", "Alpha", "Luminance"};
    const char* const wraps[] = {
        "Material", "Clamp", "Repeat", "Mirror"};
    ImGui::SeparatorText("Texture mask");
    changed |=
        EnumCombo("Sample channel", rule.Channel, channels);
    changed |=
        EnumCombo("UV wrap", rule.Wrap, wraps);
    changed |=
        ImGui::Checkbox("Invert mask", &rule.Invert);
    changed |= ImGui::SliderFloat(
        "Input black", &rule.InputBlack,
        0.0F, rule.InputWhite - 0.001F, "%.3f");
    changed |= ImGui::SliderFloat(
        "Input white", &rule.InputWhite,
        rule.InputBlack + 0.001F, 1.0F, "%.3f");
    changed |= ImGui::SliderFloat(
        "Response exponent", &rule.ResponseExponent,
        0.05F, 8.0F,
        "%.2f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat(
        "Output black", &rule.OutputBlack,
        0.0F, 1.0F, "%.3f");
    changed |= ImGui::SliderFloat("Output white", &rule.OutputWhite, 0.0F, 1.0F, "%.3f");
    changed |= DrawMaskPreview(GrassTextureSourceCache::Instance().AcquireMask(rule.Target.Rgba8Hash, rule.Channel),
                               rule, previewStrength);

    ImGui::SeparatorText("Active rectangles");
    if (rule.ActiveRects.empty())
        ImGui::TextDisabled("None: the whole mask applies. Drag on the preview to add one.");
    else
        ImGui::TextDisabled("Only inside them the mask applies; elsewhere it is 0 (right-click removes).");
    for (size_t i = 0; i < rule.ActiveRects.size(); ++i) {
        auto& rect = rule.ActiveRects[i];
        ImGui::PushID(static_cast<int>(i));
        changed |= ImGui::DragFloatRange2("U", &rect.MinU, &rect.MaxU, 0.002F, 0.0F, 1.0F, "%.3f", "%.3f",
                                          ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloatRange2("V", &rect.MinV, &rect.MaxV, 0.002F, 0.0F, 1.0F, "%.3f", "%.3f",
                                          ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::SmallButton("Remove rectangle")) {
            rule.ActiveRects.erase(rule.ActiveRects.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (rule.ActiveRects.size() < kMaximumGrassMaskRects && ImGui::Button("Add rectangle")) {
        rule.ActiveRects.push_back({0.25F, 0.25F, 0.75F, 0.75F});
        changed = true;
    }
    if (!rule.ActiveRects.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Clear rectangles")) {
            rule.ActiveRects.clear();
            changed = true;
        }
    }

    if (!surfaceControls)
        return changed;
    ImGui::SeparatorText("Surface filtering");
    changed |= ImGui::SliderFloat(
        "Maximum surface slope",
        &rule.MaximumSlopeDegrees,
        0.0F, 90.0F, "%.0f deg");
    changed |= ImGui::SliderFloat(
        "Surface offset", &rule.NormalOffset,
        -10.0F, 10.0F, "%.2f");
    return changed;
}

} // namespace Fast::Oot3d
