#include "oot3d_savestate_library.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

constexpr const char* kSettingsSuffix = "+settings";

std::string PathToUtf8(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}

std::filesystem::path PathFromUtf8(const std::string& text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string TimestampName(std::chrono::system_clock::time_point now) {
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &seconds);
#else
  localtime_r(&seconds, &local);
#endif
  std::ostringstream name;
  name << "state-" << std::put_time(&local, "%Y-%m-%d_%H-%M-%S");
  return name.str();
}

} // namespace

std::filesystem::path DefaultSavestateDirectory(
    bool besideExecutable, const std::filesystem::path& executableDirectory,
    const std::filesystem::path& saveDataDirectory) {
  if (besideExecutable || saveDataDirectory.empty())
    return executableDirectory / "savestates";
  // ".../data/savedata" -> ".../data/savestates"
  auto saveData = saveDataDirectory.lexically_normal();
  if (!saveData.has_filename()) saveData = saveData.parent_path();
  return saveData.parent_path() / "savestates";
}

SavestateLibrary::SavestateLibrary(std::filesystem::path preferencesPath,
                                   std::filesystem::path defaultDirectory,
                                   std::filesystem::path fixedSlot)
    : mPreferencesPath(std::move(preferencesPath)),
      mDefaultDirectory(std::move(defaultDirectory)),
      mFixedSlot(std::move(fixedSlot)) {
  std::scoped_lock lock(mMutex);
  LoadPreferencesLocked();
}

void SavestateLibrary::LoadPreferencesLocked() {
  if (mPreferencesPath.empty()) return;
  std::ifstream input(mPreferencesPath, std::ios::binary);
  if (!input) return;  // First run: defaults.
  const auto document = nlohmann::json::parse(input, nullptr, false);
  if (!document.is_object()) {
    mPreferencesError = "the save-state preferences are unreadable; using defaults";
    return;
  }
  mChosenDirectory = PathFromUtf8(document.value("Directory", std::string{}));
  mLastState = PathFromUtf8(document.value("LastState", std::string{}));
}

void SavestateLibrary::SavePreferencesLocked() {
  if (mPreferencesPath.empty()) return;
  const nlohmann::json document{
      {"Directory", PathToUtf8(mChosenDirectory)},
      {"LastState", PathToUtf8(mLastState)},
  };
  std::error_code error;
  std::filesystem::create_directories(mPreferencesPath.parent_path(), error);
  const auto temporary = std::filesystem::path(mPreferencesPath).concat(".tmp");
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << document.dump(4) << '\n';
    if (!output) {
      mPreferencesError = "could not write " + PathToUtf8(temporary);
      return;
    }
  }
  std::filesystem::rename(temporary, mPreferencesPath, error);
  mPreferencesError = error ? "could not write " + PathToUtf8(mPreferencesPath) +
                                  ": " + error.message()
                            : std::string{};
}

std::filesystem::path SavestateLibrary::DirectoryLocked() const {
  if (!mFixedSlot.empty()) return mFixedSlot.parent_path();
  return mChosenDirectory.empty() ? mDefaultDirectory : mChosenDirectory;
}

std::filesystem::path SavestateLibrary::Directory() const {
  std::scoped_lock lock(mMutex);
  return DirectoryLocked();
}

bool SavestateLibrary::UsingDefaultDirectory() const {
  std::scoped_lock lock(mMutex);
  return mFixedSlot.empty() && mChosenDirectory.empty();
}

bool SavestateLibrary::SetDirectory(const std::filesystem::path& directory,
                                    std::string* error) {
  std::scoped_lock lock(mMutex);
  if (!mFixedSlot.empty()) {
    if (error) *error = "the save-state file is fixed by --quick-state";
    return false;
  }
  std::error_code status;
  if (!directory.empty() && std::filesystem::exists(directory, status) &&
      !std::filesystem::is_directory(directory, status)) {
    if (error) *error = PathToUtf8(directory) + " is not a folder";
    return false;
  }
  mChosenDirectory = directory == mDefaultDirectory ? std::filesystem::path{}
                                                    : directory;
  SavePreferencesLocked();
  if (error) error->clear();
  return true;
}

std::filesystem::path SavestateLibrary::NewStatePath(
    bool withSettings, std::chrono::system_clock::time_point now) const {
  std::scoped_lock lock(mMutex);
  if (!mFixedSlot.empty()) return mFixedSlot;
  const auto directory = DirectoryLocked();
  const std::string base =
      TimestampName(now) + (withSettings ? kSettingsSuffix : "");
  auto candidate = directory / (base + kSavestateExtension);
  std::error_code error;
  for (int index = 2; std::filesystem::exists(candidate, error); ++index) {
    candidate = directory / (base + "-" + std::to_string(index) + kSavestateExtension);
  }
  return candidate;
}

bool SavestateLibrary::NameCarriesSettings(const std::filesystem::path& path) {
  return PathToUtf8(path.stem()).find(kSettingsSuffix) != std::string::npos;
}

std::optional<std::filesystem::path> SavestateLibrary::LastState() const {
  std::scoped_lock lock(mMutex);
  if (!mFixedSlot.empty()) return mFixedSlot;
  if (mLastState.empty()) return std::nullopt;
  return mLastState;
}

void SavestateLibrary::SetLastState(const std::filesystem::path& path) {
  std::scoped_lock lock(mMutex);
  if (!mFixedSlot.empty() || path == mLastState) return;
  mLastState = path;
  SavePreferencesLocked();
}

std::vector<SavestateEntry> SavestateLibrary::List(std::string* error) const {
  const auto directory = Directory();
  std::vector<SavestateEntry> entries;
  std::error_code status;
  if (!std::filesystem::exists(directory, status)) {
    if (error) error->clear();
    return entries;
  }
  for (std::filesystem::directory_iterator it(directory, status), end;
       !status && it != end; it.increment(status)) {
    std::error_code entryStatus;
    if (!it->is_regular_file(entryStatus) ||
        it->path().extension() != kSavestateExtension) {
      continue;
    }
    SavestateEntry entry;
    entry.Path = it->path();
    entry.Bytes = it->file_size(entryStatus);
    entry.Modified = it->last_write_time(entryStatus);
    entry.WithSettings = NameCarriesSettings(entry.Path);
    entries.push_back(std::move(entry));
  }
  if (error) *error = status ? status.message() : std::string{};
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    return a.Modified != b.Modified ? a.Modified > b.Modified : a.Path > b.Path;
  });
  return entries;
}

void SavestateLibrary::Request(SavestateRequest request) {
  std::scoped_lock lock(mMutex);
  mPendingRequest = std::move(request);
}

std::optional<SavestateRequest> SavestateLibrary::TakeRequest() {
  std::scoped_lock lock(mMutex);
  auto request = std::move(mPendingRequest);
  mPendingRequest.reset();
  return request;
}

void SavestateLibrary::Report(bool success, std::string message) {
  std::scoped_lock lock(mMutex);
  mLastOutcome = SavestateOutcome{success, std::move(message),
                                  std::chrono::steady_clock::now()};
}

std::optional<SavestateOutcome> SavestateLibrary::LastOutcome() const {
  std::scoped_lock lock(mMutex);
  return mLastOutcome;
}

std::string SavestateLibrary::PreferencesError() const {
  std::scoped_lock lock(mMutex);
  return mPreferencesError;
}

} // namespace Oot3dNativeGame
