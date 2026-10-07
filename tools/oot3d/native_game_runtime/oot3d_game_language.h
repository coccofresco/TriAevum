#pragma once

#include "triaevum_title_adapter.h"

#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Oot3dNativeGame {
struct GameLanguage {
  std::string Code;
  std::string Label;
  uint8_t SystemId;
};

// Reads the title's native menu/layout resources and populated message
// tables, not ROM filenames: OoT3D's menu archives and QM slots, MM3D's
// layout archives and GMSG files.
std::vector<GameLanguage>
DetectGameLanguages(const std::filesystem::path &romfs, uint64_t offset = 0,
                    uint64_t size = 0,
                    TitleAdapter title = TitleAdapter::Oot3dEur);
nlohmann::json GameLanguageDocument(const std::vector<GameLanguage> &languages,
                                    const std::string &selected = "en");

class GameLanguageSettings {
public:
  GameLanguageSettings(std::filesystem::path path,
                       std::vector<GameLanguage> available);
  bool Select(const std::string &code);
  uint8_t SystemId() const;
  const std::vector<GameLanguage> &Available() const { return mAvailable; }
  const std::string &Selected() const { return mSelected; }
  const std::string &BootLanguage() const { return mBootLanguage; }
  const std::string &Error() const { return mError; }

private:
  std::filesystem::path mPath;
  std::vector<GameLanguage> mAvailable;
  std::string mSelected, mBootLanguage, mError;
};
} // namespace Oot3dNativeGame
