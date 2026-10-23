#pragma once

// Where save states live and which one is "the last": the folder (a default
// next to the executable or the save data, or one the player chose),
// timestamped file names, the listing shown in F1, and the request/outcome
// mailbox between the F1 tab and the frame loop. No ImGui and no game state: the window session performs
// the saves and loads at a frame boundary.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

inline constexpr const char* kSavestateExtension = ".oot3dsav";

// The default save-state folder. The portable Windows layout keeps it next to
// the executable. The Linux packages run from a read-only image (the AppImage
// mount, the Flatpak's /app), so there it sits beside the save data instead.
[[nodiscard]] std::filesystem::path DefaultSavestateDirectory(
    bool besideExecutable, const std::filesystem::path& executableDirectory,
    const std::filesystem::path& saveDataDirectory);

enum class SavestateRequestKind : uint8_t { Save, Load };

struct SavestateRequest {
  SavestateRequestKind Kind = SavestateRequestKind::Save;
  // Save: also store the host settings. Load: also apply the stored settings.
  bool WithSettings = false;
  // Load only; empty loads the last state.
  std::filesystem::path Path;
};

struct SavestateEntry {
  std::filesystem::path Path;
  std::uintmax_t Bytes = 0;
  std::filesystem::file_time_type Modified{};
  // From the file name written by a "state + settings" save; a renamed file
  // is checked when it is loaded.
  bool WithSettings = false;
};

struct SavestateOutcome {
  bool Success = false;
  std::string Message;
  std::chrono::steady_clock::time_point When{};
};

class SavestateLibrary final {
 public:
  // preferencesPath: small JSON with the chosen folder and the last state.
  // fixedSlot: a launch-time quick-state path; when set, every save writes it
  // and the last state is always it (scripted runs keep one known file).
  SavestateLibrary(std::filesystem::path preferencesPath,
                   std::filesystem::path defaultDirectory,
                   std::filesystem::path fixedSlot = {});

  [[nodiscard]] std::filesystem::path Directory() const;
  [[nodiscard]] const std::filesystem::path& DefaultDirectory() const noexcept {
    return mDefaultDirectory;
  }
  [[nodiscard]] bool UsingDefaultDirectory() const;
  [[nodiscard]] const std::filesystem::path& FixedSlot() const noexcept {
    return mFixedSlot;
  }
  // An empty path restores the default. The folder is created on first save.
  bool SetDirectory(const std::filesystem::path& directory,
                    std::string* error = nullptr);

  // A new file in the folder (or the fixed slot), never an existing one.
  [[nodiscard]] std::filesystem::path NewStatePath(
      bool withSettings,
      std::chrono::system_clock::time_point now =
          std::chrono::system_clock::now()) const;
  [[nodiscard]] static bool NameCarriesSettings(
      const std::filesystem::path& path);

  [[nodiscard]] std::optional<std::filesystem::path> LastState() const;
  void SetLastState(const std::filesystem::path& path);

  // States in the folder, newest first.
  [[nodiscard]] std::vector<SavestateEntry> List(
      std::string* error = nullptr) const;

  void Request(SavestateRequest request);
  [[nodiscard]] std::optional<SavestateRequest> TakeRequest();

  void Report(bool success, std::string message);
  [[nodiscard]] std::optional<SavestateOutcome> LastOutcome() const;
  // Non-empty when the preferences could not be written.
  [[nodiscard]] std::string PreferencesError() const;

 private:
  void LoadPreferencesLocked();
  void SavePreferencesLocked();
  [[nodiscard]] std::filesystem::path DirectoryLocked() const;

  std::filesystem::path mPreferencesPath;
  std::filesystem::path mDefaultDirectory;
  std::filesystem::path mFixedSlot;
  mutable std::mutex mMutex;
  std::filesystem::path mChosenDirectory;
  std::filesystem::path mLastState;
  std::optional<SavestateRequest> mPendingRequest;
  std::optional<SavestateOutcome> mLastOutcome;
  std::string mPreferencesError;
};

} // namespace Oot3dNativeGame
