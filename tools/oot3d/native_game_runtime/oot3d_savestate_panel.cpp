#include "oot3d_savestate_panel.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace Oot3dNativeGame {
namespace {

std::string PathText(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}

std::filesystem::path PathFromText(const char* text) {
  const std::string value(text);
  return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

std::string SizeText(std::uintmax_t bytes) {
  char text[32];
  if (bytes >= (1ULL << 30U)) {
    std::snprintf(text, sizeof(text), "%.2f GB", static_cast<double>(bytes) / (1ULL << 30U));
  } else {
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1ULL << 20U));
  }
  return text;
}

std::string TimeText(std::filesystem::file_time_type time) {
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
  const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
#else
  // libc++ has no clock_cast yet: offset the file time onto the system clock.
  const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      std::chrono::system_clock::now() +
      (time - std::filesystem::file_time_type::clock::now()));
#endif
  const std::time_t seconds = std::chrono::system_clock::to_time_t(system);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &seconds);
#else
  localtime_r(&seconds, &local);
#endif
  char text[32];
  std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
  return text;
}

// The nearest existing folder at or above `path`, so a browser opens
// somewhere real even before the default folder exists.
std::filesystem::path ExistingFolder(std::filesystem::path path) {
  std::error_code error;
  while (!path.empty() && !std::filesystem::is_directory(path, error)) {
    const auto parent = path.parent_path();
    if (parent == path) break;
    path = parent;
  }
  return path.empty() ? std::filesystem::current_path(error) : path;
}

class SavestatePanel final : public Fast::Extension::GraphicsSettingsPanelTab {
 public:
  explicit SavestatePanel(std::shared_ptr<SavestateLibrary> library, std::string settingsNotice)
      : mLibrary(std::move(library)), mSettingsNotice(std::move(settingsNotice)) {}

  const char* Label() const noexcept override { return "Save states"; }
  const char* Group() const noexcept override { return Fast::Extension::kGameSettingsGroup; }

  void Draw() override {
    const bool fixedSlot = !mLibrary->FixedSlot().empty();
    ImGui::SeparatorText("Save");
    if (ImGui::Button("Save state (F5)")) {
      mLibrary->Request({SavestateRequestKind::Save, false, {}});
    }
    ImGui::SameLine();
    if (ImGui::Button("Save state + settings (Shift+F5)")) {
      mLibrary->Request({SavestateRequestKind::Save, true, {}});
    }
    ImGui::TextWrapped(
        "A save state holds the game moment only and loads with any resolution and "
        "settings. \"+ settings\" also stores graphics, TopScreen, controls and language.");
    if (!mSettingsNotice.empty()) ImGui::TextWrapped("%s", mSettingsNotice.c_str());
    DrawFolder(fixedSlot);

    ImGui::SeparatorText("Load");
    const auto last = mLibrary->LastState();
    ImGui::Text("Last state: %s",
                last.has_value() ? PathText(last->filename()).c_str() : "none yet");
    ImGui::BeginDisabled(!last.has_value());
    if (ImGui::Button("Load last state (F8)")) {
      mLibrary->Request({SavestateRequestKind::Load, false, {}});
    }
    ImGui::SameLine();
    if (ImGui::Button("Load last state + settings (Shift+F8)")) {
      mLibrary->Request({SavestateRequestKind::Load, true, {}});
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped(
        "\"+ settings\" applies the settings stored in the state, except the display "
        "mode (window, monitor, output size, refresh rate), which stays as it is.");
    ImGui::BeginDisabled(fixedSlot);
    if (ImGui::Button("Choose a state file...")) {
      OpenBrowser(BrowseMode::File, last.has_value() ? last->parent_path()
                                                     : mLibrary->Directory());
    }
    ImGui::EndDisabled();

    DrawListing(fixedSlot, last);
    DrawStatus();
    DrawBrowser();
  }

 private:
  enum class BrowseMode : uint8_t { None, Folder, File };

  void DrawFolder(bool fixedSlot) {
    if (fixedSlot) {
      ImGui::TextWrapped("Fixed file (--quick-state): %s",
                         PathText(mLibrary->FixedSlot()).c_str());
      return;
    }
    ImGui::TextWrapped("Folder: %s%s", PathText(mLibrary->Directory()).c_str(),
                       mLibrary->UsingDefaultDirectory() ? " (default)" : "");
    if (ImGui::Button("Browse...##SavestateFolder")) {
      OpenBrowser(BrowseMode::Folder, mLibrary->Directory());
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(mLibrary->UsingDefaultDirectory());
    if (ImGui::Button("Use default folder")) {
      mLibrary->SetDirectory({}, &mFolderError);
      mListedAt = {};
    }
    ImGui::EndDisabled();
    if (!mFolderError.empty()) {
      ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.4F, 1.0F), "%s", mFolderError.c_str());
    }
  }

  void RefreshListing(bool force) {
    const auto now = std::chrono::steady_clock::now();
    const auto directory = mLibrary->Directory();
    if (!force && directory == mListedDirectory &&
        now - mListedAt < std::chrono::seconds(2)) {
      return;
    }
    mEntries = mLibrary->List(&mListError);
    mListedDirectory = directory;
    mListedAt = now;
  }

  void DrawListing(bool fixedSlot, const std::optional<std::filesystem::path>& last) {
    if (fixedSlot) return;
    ImGui::SeparatorText("States in this folder");
    RefreshListing(false);
    std::uintmax_t total = 0;
    for (const auto& entry : mEntries) total += entry.Bytes;
    ImGui::Text("%zu states, %s", mEntries.size(), SizeText(total).c_str());
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) RefreshListing(true);
    if (!mListError.empty()) {
      ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.4F, 1.0F), "%s", mListError.c_str());
    }
    if (mEntries.empty()) return;
    ImGui::TextDisabled("Select a state to make it the one F8 loads.");
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    const float height = ImGui::GetTextLineHeightWithSpacing() *
                         static_cast<float>(std::min<size_t>(mEntries.size(), 8U) + 1U);
    if (ImGui::BeginTable("##SavestateList", 3, flags, ImVec2(0.0F, height))) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 3.0F);
      ImGui::TableSetupColumn("Saved", ImGuiTableColumnFlags_WidthStretch, 2.0F);
      ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1.0F);
      ImGui::TableHeadersRow();
      for (const auto& entry : mEntries) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const auto name = PathText(entry.Path.filename());
        const bool selected = last.has_value() && *last == entry.Path;
        if (ImGui::Selectable(name.c_str(), selected,
                              ImGuiSelectableFlags_SpanAllColumns)) {
          mLibrary->SetLastState(entry.Path);
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(TimeText(entry.Modified).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(SizeText(entry.Bytes).c_str());
      }
      ImGui::EndTable();
    }
  }

  void DrawStatus() {
    const auto outcome = mLibrary->LastOutcome();
    const auto preferencesError = mLibrary->PreferencesError();
    if (!outcome.has_value() && preferencesError.empty()) return;
    ImGui::SeparatorText("Status");
    if (outcome.has_value()) {
      if (outcome->Success) {
        ImGui::TextWrapped("%s", outcome->Message.c_str());
      } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.45F, 0.4F, 1.0F));
        ImGui::TextWrapped("%s", outcome->Message.c_str());
        ImGui::PopStyleColor();
      }
    }
    if (!preferencesError.empty()) {
      ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.4F, 1.0F), "Preferences: %s",
                         preferencesError.c_str());
    }
  }

  void OpenBrowser(BrowseMode mode, const std::filesystem::path& start) {
    mBrowseMode = mode;
    Navigate(ExistingFolder(start));
  }

  void Navigate(const std::filesystem::path& directory) {
    mBrowseDirectory = directory;
    const auto text = PathText(directory);
    std::snprintf(mBrowsePathInput.data(), mBrowsePathInput.size(), "%s", text.c_str());
    mBrowseEntries.clear();
    mBrowseError.clear();
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
      std::error_code entryError;
      if (it->is_directory(entryError) ||
          (mBrowseMode == BrowseMode::File && it->is_regular_file(entryError) &&
           it->path().extension() == kSavestateExtension)) {
        mBrowseEntries.push_back(*it);
      }
    }
    if (error) mBrowseError = error.message();
    std::sort(mBrowseEntries.begin(), mBrowseEntries.end(), [](const auto& a, const auto& b) {
      std::error_code first, second;
      const bool aFolder = a.is_directory(first), bFolder = b.is_directory(second);
      return aFolder != bFolder ? aFolder : a.path().filename() < b.path().filename();
    });
  }

  void DrawBrowser() {
    if (mBrowseMode == BrowseMode::None) return;
    const char* title = mBrowseMode == BrowseMode::Folder ? "Choose the save-state folder"
                                                          : "Choose a save state";
    if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    ImGui::SetNextWindowSize(ImVec2(720.0F, 520.0F), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
      mBrowseMode = BrowseMode::None;
      return;
    }
    ImGui::SetNextItemWidth(-140.0F);
    const bool entered = ImGui::InputText("##SavestateBrowsePath", mBrowsePathInput.data(),
                                          mBrowsePathInput.size(),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Go") || entered) Navigate(PathFromText(mBrowsePathInput.data()));
    ImGui::SameLine();
    if (ImGui::Button("Up")) Navigate(mBrowseDirectory.parent_path());
    if (!mBrowseError.empty()) {
      ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.4F, 1.0F), "%s", mBrowseError.c_str());
    }
    const float footer = ImGui::GetFrameHeightWithSpacing() * 1.5F;
    std::optional<std::filesystem::path> navigateTo;
    if (ImGui::BeginChild("##SavestateBrowseEntries", ImVec2(0.0F, -footer), true)) {
      for (const auto& entry : mBrowseEntries) {
        std::error_code error;
        const bool folder = entry.is_directory(error);
        const auto label = (folder ? "[folder] " : "") + PathText(entry.path().filename());
        if (ImGui::Selectable(label.c_str())) {
          if (folder) {
            navigateTo = entry.path();
          } else {
            mLibrary->SetLastState(entry.path());
            mBrowseMode = BrowseMode::None;
            ImGui::CloseCurrentPopup();
          }
        }
      }
    }
    ImGui::EndChild();
    if (navigateTo.has_value()) Navigate(*navigateTo);
    if (mBrowseMode == BrowseMode::Folder && ImGui::Button("Use this folder")) {
      if (mLibrary->SetDirectory(mBrowseDirectory, &mFolderError)) {
        mListedAt = {};
        mBrowseMode = BrowseMode::None;
        ImGui::CloseCurrentPopup();
      }
    }
    if (mBrowseMode != BrowseMode::None) {
      if (mBrowseMode == BrowseMode::Folder) ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        mBrowseMode = BrowseMode::None;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  std::shared_ptr<SavestateLibrary> mLibrary;
  std::string mSettingsNotice;
  std::vector<SavestateEntry> mEntries;
  std::string mListError;
  std::filesystem::path mListedDirectory;
  std::chrono::steady_clock::time_point mListedAt{};
  std::string mFolderError;
  BrowseMode mBrowseMode = BrowseMode::None;
  std::filesystem::path mBrowseDirectory;
  std::array<char, 1024> mBrowsePathInput{};
  std::vector<std::filesystem::directory_entry> mBrowseEntries;
  std::string mBrowseError;
};

} // namespace

std::shared_ptr<Fast::Extension::GraphicsSettingsPanelTab>
CreateSavestatePanel(std::shared_ptr<SavestateLibrary> library, std::string settingsNotice) {
  return std::make_shared<SavestatePanel>(std::move(library), std::move(settingsNotice));
}

} // namespace Oot3dNativeGame
