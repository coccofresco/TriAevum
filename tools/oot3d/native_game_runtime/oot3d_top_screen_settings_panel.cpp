#include "oot3d_top_screen_settings_panel.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr std::array<const char *, 13> kHudScaleLabels{
    "0.60x", "0.65x", "0.70x", "0.75x", "0.80x", "0.85x", "0.90x",
    "0.95x", "1.00x", "1.05x", "1.10x", "1.15x", "1.20x",
};

int HudScaleIndex(float scale) {
  return std::clamp(
      static_cast<int>(std::lround(
          (scale - kTopScreenHudScaleMinimum) / kTopScreenHudScaleStep)),
      0, static_cast<int>(kHudScaleLabels.size()) - 1);
}

class TopScreenSettingsPanel final
    : public Fast::Oot3d::GraphicsSettingsPanelTab {
public:
  explicit TopScreenSettingsPanel(
      std::shared_ptr<TopScreenUiConfigRuntime> runtime)
      : mRuntime(std::move(runtime)) {}

  [[nodiscard]] const char *Label() const noexcept override {
    return "TopScreen 2.1.1";
  }

  void Draw() override {
    const auto snapshot = mRuntime->Snapshot();
    if (!mInitialized ||
        snapshot.Revision != mObservedRevision) {
      mDraft = snapshot.Config;
      mObservedRevision = snapshot.Revision;
      mInitialized = true;
    }
    const TopScreenUiConfig frameStartDraft = mDraft;

    if (ImGui::BeginTabBar("##TopScreenSections")) {
      const auto section = [&](const char* label, auto draw) {
        if (ImGui::BeginTabItem(label)) {
          ImGui::BeginChild(label, ImVec2(0.0F, std::max(
              80.0F, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 4.0F)));
          ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45F);
          draw();
          ImGui::PopItemWidth();
          ImGui::EndChild();
          ImGui::EndTabItem();
        }
      };
      section("HUD", [&] { DrawHud(); });
      ImGui::EndTabBar();
    }
    if (mDraft != frameStartDraft) {
      mRuntime->Preview(mDraft);
      const auto preview = mRuntime->Snapshot();
      mDraft = preview.Config;
      mObservedRevision = preview.Revision;
      mDirty = true;
      mStatus = "Live preview";
    }

    if (!mRuntime->Persistent()) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Save TopScreen")) {
      std::string error;
      if (mRuntime->Apply(mDraft, &error)) {
        mObservedRevision = mRuntime->Snapshot().Revision;
        mDirty = false;
        mStatus = "Saved";
      } else {
        mStatus = std::move(error);
      }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", mRuntime->Path().string().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Reload TopScreen")) {
      std::string error;
      if (mRuntime->Reload(&error)) {
        const auto reloaded = mRuntime->Snapshot();
        mDraft = reloaded.Config;
        mObservedRevision = reloaded.Revision;
        mDirty = false;
        mStatus = "Reloaded";
      } else {
        mStatus = std::move(error);
      }
    }
    if (!mRuntime->Persistent()) {
      ImGui::EndDisabled();
      ImGui::TextDisabled(
          "Live preview only; no configuration path is active");
    }

    if (mDirty) {
      ImGui::TextDisabled("Unsaved changes");
    }
    if (!mStatus.empty()) {
      ImGui::TextWrapped("%s", mStatus.c_str());
    }
  }

private:
  void DrawHud() {
    ImGui::SeparatorText("HUD");
    mDirty |= ImGui::Checkbox("Render HUD", &mDraft.RenderHud);
    mDirty |=
        ImGui::Checkbox("Render D-pad icons", &mDraft.RenderDpadIcons);
    mDirty |=
        ImGui::Checkbox("Render Items hint", &mDraft.RenderItemsHint);
    bool normal = mDraft.HudLayout == TopScreenHudLayout::Normal;
    if (ImGui::RadioButton("Normal HUD", normal)) {
      mDraft.HudLayout = TopScreenHudLayout::Normal;
      mDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Restoration HUD", !normal)) {
      mDraft.HudLayout = TopScreenHudLayout::Restoration;
      mDirty = true;
    }

    int scaleIndex = HudScaleIndex(mDraft.HudScale);
    if (ImGui::Combo("HUD scale", &scaleIndex, kHudScaleLabels.data(),
                     static_cast<int>(kHudScaleLabels.size()))) {
      mDraft.HudScale =
          kTopScreenHudScaleMinimum +
          static_cast<float>(scaleIndex) * kTopScreenHudScaleStep;
      mDirty = true;
    }
    if (ImGui::Checkbox("Show minimap", &mDraft.MinimapVisible)) {
      mDirty = true;
    }

    int marginX = mDraft.HudMarginX;
    int marginY = mDraft.HudMarginY;
    int magicBarY = mDraft.MagicBarY;
    if (ImGui::SliderInt("Horizontal margin", &marginX,
                         kTopScreenHudMarginMinimum,
                         kTopScreenHudMarginMaximum)) {
      mDraft.HudMarginX = static_cast<std::int8_t>(marginX);
      mDirty = true;
    }
    if (ImGui::SliderInt("Vertical margin", &marginY,
                         kTopScreenHudMarginMinimum,
                         kTopScreenHudMarginMaximum)) {
      mDraft.HudMarginY = static_cast<std::int8_t>(marginY);
      mDirty = true;
    }
    if (ImGui::SliderInt("Magic bar Y", &magicBarY, 0, 10)) {
      mDraft.MagicBarY = static_cast<std::uint8_t>(magicBarY);
      mDirty = true;
    }

  }
  std::shared_ptr<TopScreenUiConfigRuntime> mRuntime;
  TopScreenUiConfig mDraft;
  std::uint64_t mObservedRevision = 0U;
  bool mInitialized = false;
  bool mDirty = false;
  std::string mStatus;
};

} // namespace

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>
CreateTopScreenSettingsPanel(
    std::shared_ptr<TopScreenUiConfigRuntime> runtime) {
  return std::make_shared<TopScreenSettingsPanel>(std::move(runtime));
}

} // namespace Oot3dNativeGame
