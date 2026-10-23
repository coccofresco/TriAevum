#include "fast/oot3d/texture_catalog_viewer.h"

#include "fast/oot3d/grass_texture_source_cache.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace Fast::Oot3d {
namespace {

std::string TagsForHash(
    std::span<const TextureCatalogTag> tags,
    const TextureCatalogEntry& entry) {
    std::string result;
    std::vector<std::string_view> appended;
    for (const auto& tag : tags) {
        if (tag.ContentHash != entry.ContentHash ||
            (tag.Width != 0U && tag.Width != entry.Width) ||
            (tag.Height != 0U && tag.Height != entry.Height) ||
            tag.Label.empty()) {
            continue;
        }
        if (std::find(appended.begin(), appended.end(),
                      tag.Label) != appended.end()) {
            continue;
        }
        if (!result.empty()) {
            result += '+';
        }
        result += tag.Label;
        appended.push_back(tag.Label);
    }
    return result.empty() ? "UNASSIGNED" : result;
}

void DrawPreview(uint64_t hash) {
    const auto preview = GrassTextureSourceCache::Instance().AcquirePreview(hash);
    ImGui::PushID(static_cast<int>(hash >> 32U));
    ImGui::PushID(static_cast<int>(hash & 0xFFFFFFFFU));
    DrawTexturePixelPreview(preview.Rgba8, preview.Width, preview.Height);
    ImGui::PopID();
    ImGui::PopID();
}

} // namespace

void DrawTexturePixelPreview(std::span<const uint8_t> rgba8, uint32_t width, uint32_t height, float maximumWidth,
                             float maximumHeight) {
    if (!width || !height || uint64_t(width) * height * 4U > rgba8.size()) {
        ImGui::TextDisabled("Texture preview is waiting for decoded RGBA pixels.");
        return;
    }
    const float scale = std::min(maximumWidth / width, maximumHeight / height);
    const ImVec2 size{ std::max(1.0F, width * scale), std::max(1.0F, height * scale) };
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##shared_texture_catalog_preview", size);
    auto* draw = ImGui::GetWindowDrawList();
    const uint32_t cellsX = std::min<uint32_t>(width, 56U);
    const uint32_t cellsY = std::min<uint32_t>(height, 56U);
    for (uint32_t y = 0; y < cellsY; ++y) {
        for (uint32_t x = 0; x < cellsX; ++x) {
            const size_t offset = (size_t(y * height / cellsY) * width + x * width / cellsX) * 4U;
            const auto color = IM_COL32(rgba8[offset], rgba8[offset + 1], rgba8[offset + 2], rgba8[offset + 3]);
            draw->AddRectFilled({origin.x + size.x * x / cellsX, origin.y + size.y * y / cellsY},
                {origin.x + size.x * (x + 1U) / cellsX, origin.y + size.y * (y + 1U) / cellsY}, color);
        }
    }
    draw->AddRect(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(255, 255, 255, 160));
}

TextureCatalogViewerResult DrawTextureCatalogViewer(
    TextureCatalogSelectionState& state,
    std::span<const TextureCatalogTag> tags,
    size_t maximumVisibleRows) {
    TextureCatalogViewerResult result;
    std::vector<uint64_t> assignedHashes;
    assignedHashes.reserve(tags.size());
    for (const auto& tag : tags) {
        if (std::find(assignedHashes.begin(),
                      assignedHashes.end(),
                      tag.ContentHash) ==
            assignedHashes.end()) {
            assignedHashes.push_back(tag.ContentHash);
        }
    }
    auto textures = SortTextureCatalogEntries(
        TextureCatalogRuntime::Instance().Snapshot(),
        state.SortMode, assignedHashes);
    result.Selected = ResolveTextureCatalogSelection(
        state, textures, assignedHashes);

    ImGui::Text("Observed textures: %zu | assigned: %zu",
                textures.size(), assignedHashes.size());
    ImGui::TextDisabled(
        "Select a texture, then configure it in the current feature.");
    const char* const sortModes[] = {
        "Most observed", "Largest first", "Hash",
        "Assigned first"};
    int sortMode = static_cast<int>(state.SortMode);
    if (ImGui::Combo(
            "Order texture catalog by", &sortMode,
            sortModes,
            static_cast<int>(std::size(sortModes)))) {
        state.SortMode =
            static_cast<TextureCatalogSortMode>(sortMode);
    }

    char selectedLabel[224] = "No observed texture";
    if (result.Selected.has_value()) {
        std::snprintf(
            selectedLabel, sizeof(selectedLabel),
            "%016llX | %ux%u | fmt %u",
            static_cast<unsigned long long>(
                result.Selected->ContentHash),
            result.Selected->Width,
            result.Selected->Height,
            result.Selected->NativeFormat);
    }
    const float popupHeight = ImGui::GetTextLineHeightWithSpacing() *
        static_cast<float>(std::clamp<size_t>(maximumVisibleRows, 1U, 32U)) +
        ImGui::GetStyle().WindowPadding.y * 2.0F;
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0.0F, 0.0F), ImVec2(FLT_MAX, popupHeight));
    if (ImGui::BeginCombo(
            "Texture catalog selection", selectedLabel)) {
        if (ImGui::IsWindowAppearing()) {
            state.PopupEntries = textures;
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(state.PopupEntries.size()));
        for (size_t index = 0; index < state.PopupEntries.size(); ++index) {
            if (IsTextureCatalogEntrySelected(state, state.PopupEntries[index])) {
                clipper.IncludeItemByIndex(static_cast<int>(index));
                break;
            }
        }
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                const auto& texture = state.PopupEntries[index];
                const std::string tagsForTexture =
                    TagsForHash(tags, texture);
                char label[224]{};
                std::snprintf(
                    label, sizeof(label),
                    "[%-20s] %016llX | %ux%u | fmt %u | seen %llu",
                    tagsForTexture.c_str(),
                    static_cast<unsigned long long>(
                        texture.ContentHash),
                    texture.Width, texture.Height,
                    texture.NativeFormat,
                    static_cast<unsigned long long>(
                        texture.Observations));
                ImGui::PushID(static_cast<int>(texture.ContentHash >> 32U));
                ImGui::PushID(static_cast<int>(texture.ContentHash & 0xFFFFFFFFU));
                ImGui::PushID(static_cast<int>(texture.Width));
                ImGui::PushID(static_cast<int>(texture.Height));
                ImGui::PushID(static_cast<int>(texture.NativeFormat));
                const bool selected =
                    IsTextureCatalogEntrySelected(state, texture);
                const std::string itemLabel = std::string(label) + "###catalog_entry";
                if (ImGui::Selectable(itemLabel.c_str(), selected)) {
                    SelectTextureCatalogEntry(state, texture);
                    result.Selected = texture;
                    result.SelectionChanged = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
                ImGui::PopID();
                ImGui::PopID();
                ImGui::PopID();
                ImGui::PopID();
            }
        }
        ImGui::EndCombo();
    } else {
        state.PopupEntries.clear();
    }
    if (!textures.empty()) {
        if (ImGui::Button("Previous texture")) {
            result.Selected = MoveTextureCatalogSelection(
                state, textures, -1);
            result.SelectionChanged =
                result.Selected.has_value();
        }
        ImGui::SameLine();
        if (ImGui::Button("Next texture")) {
            result.Selected = MoveTextureCatalogSelection(
                state, textures, 1);
            result.SelectionChanged =
                result.Selected.has_value();
        }
    }

    if (!result.Selected.has_value()) {
        ImGui::TextDisabled(
            "No texture has been observed in the current scene.");
        return result;
    }
    ImGui::Text(
        "Selected %016llX | %ux%u | %s",
        static_cast<unsigned long long>(
            result.Selected->ContentHash),
        result.Selected->Width, result.Selected->Height,
        TagsForHash(tags, *result.Selected).c_str());
    DrawPreview(result.Selected->ContentHash);
    return result;
}

} // namespace Fast::Oot3d
