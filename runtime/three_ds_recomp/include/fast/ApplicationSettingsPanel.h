#pragma once

#include <imgui.h>
#include <algorithm>
#include <functional>
#include <span>
#include <string>

namespace Fast {

struct ApplicationSettingsPage {
    std::string Id;
    std::string Label;
    std::string Group;
    std::function<void()> Draw;
};

// Product navigation follows Dusklight's separate navigation/content panes and
// remembered page focus. Widget rendering stays on our existing NRI UI pass.
// See docs/TRIAEVUM_DUSKLIGHT_UI_INPUT_DONOR.md for source references and scope.
class ApplicationSettingsPanel {
  public:
    void Draw(std::span<const ApplicationSettingsPage> pages) {
        if (pages.empty()) return;
        auto selected = std::find_if(pages.begin(), pages.end(), [&](const auto& page) {
            return page.Id == mSelected;
        });
        if (selected == pages.end()) {
            mSelected = pages.front().Id;
            selected = pages.begin();
        }
        const bool compact = ImGui::GetContentRegionAvail().x < 640.0F;
        if (compact) {
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::BeginCombo("##SettingsPage", selected->Label.c_str())) {
                std::string group;
                for (const auto& page : pages) {
                    if (group != page.Group) {
                        group = page.Group;
                        ImGui::SeparatorText(group.c_str());
                    }
                    ImGui::PushID(page.Id.c_str());
                    if (ImGui::Selectable(page.Label.c_str(), page.Id == mSelected))
                        mSelected = page.Id;
                    if (page.Id == mSelected) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::BeginChild("##SettingsNavigation", ImVec2(190.0F, 0.0F));
            std::string group;
            for (const auto& page : pages) {
                if (group != page.Group) {
                    group = page.Group;
                    ImGui::SeparatorText(group.c_str());
                }
                ImGui::PushID(page.Id.c_str());
                if (ImGui::Selectable(page.Label.c_str(), page.Id == mSelected,
                                      ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetFrameHeight())))
                    mSelected = page.Id;
                if (page.Id == mSelected) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::SameLine();
        }
        for (const auto& page : pages) {
            if (page.Id != mSelected) continue;
            ImGui::PushID(page.Id.c_str());
            ImGui::BeginChild("##SettingsPageContent", ImVec2(0, 0));
            ImGui::SeparatorText(page.Label.c_str());
            ImGui::PushItemWidth(std::min(320.0F, ImGui::GetContentRegionAvail().x * 0.65F));
            page.Draw();
            ImGui::PopItemWidth();
            ImGui::EndChild();
            ImGui::PopID();
            break;
        }
    }

  private:
    std::string mSelected;
};

} // namespace Fast
