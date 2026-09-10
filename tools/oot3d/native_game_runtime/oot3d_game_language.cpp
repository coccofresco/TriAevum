#include "oot3d_game_language.h"
#include "ship/config/ConfigPersistence.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {
using Bytes = std::vector<uint8_t>;
uint32_t U32(const Bytes &b, size_t p) {
  if (p > b.size() || b.size() - p < 4)
    throw std::runtime_error("Truncated language metadata");
  return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 |
         uint32_t(b[p + 3]) << 24;
}
uint64_t U64(const Bytes &b, size_t p) {
  return U32(b, p) | uint64_t(U32(b, p + 4)) << 32;
}
std::string Name(const Bytes &b, size_t p, size_t n) {
  if (n % 2 || p > b.size() || n > b.size() - p)
    throw std::runtime_error("Invalid RomFS name extent");
  std::string result;
  for (size_t i = 0; i < n; i += 2) {
    if (b[p + i + 1] || b[p + i] > 127)
      return {}; // Localized resource keys are ASCII.
    result += char(b[p + i]);
  }
  return result;
}
struct NativeLanguage {
  const char *Directory;
  const char *Code;
  const char *Label;
  uint8_t SystemId;
};
// Native resource/QM slot order; CFG language IDs are a different enumeration.
constexpr std::array<NativeLanguage, 9> kLanguages{{
    {"00_JP_JAPANESE", "ja", "Japanese", 0},
    {"01_US_ENGLISH", "en", "English", 1},
    {"02_EU_ENGLISH", "en", "English", 1},
    {"03_EU_GERMAN", "de", "Deutsch", 3},
    {"04_EU_FRENCH", "fr", "Francais", 2},
    {"05_US_FRENCH", "fr", "Francais", 2},
    {"06_EU_SPANISH", "es", "Espanol", 5},
    {"07_US_SPANISH", "es", "Espanol", 5},
    {"08_EU_ITALIAN", "it", "Italiano", 4},
}};
} // namespace

std::vector<GameLanguage> DetectGameLanguages(const std::filesystem::path &path,
                                              uint64_t offset, uint64_t size) {
  const auto fileSize = std::filesystem::file_size(path);
  if (offset > fileSize)
    throw std::runtime_error("RomFS offset outside file");
  if (!size)
    size = fileSize - offset;
  if (size > fileSize - offset)
    throw std::runtime_error("RomFS size outside file");
  std::ifstream stream(path, std::ios::binary);
  auto read = [&](uint64_t at, uint64_t count) {
    if (at > size || count > size - at || count > 16 * 1024 * 1024)
      throw std::runtime_error("RomFS language metadata outside image");
    Bytes result(static_cast<size_t>(count));
    stream.seekg(static_cast<std::streamoff>(offset + at));
    if (!stream.read(reinterpret_cast<char *>(result.data()), result.size()))
      throw std::runtime_error("Cannot read RomFS language metadata");
    return result;
  };
  const auto magic = read(0, 4);
  const uint64_t base = U32(magic, 0) == 0x43465649U ? 0x1000 : 0;
  const auto header = read(base, 40);
  if (U32(header, 0) != 40)
    throw std::runtime_error("Invalid RomFS Level-3 header");
  const auto dirs = read(base + U32(header, 12), U32(header, 16));
  const auto files = read(base + U32(header, 28), U32(header, 32));
  const uint64_t data = base + U32(header, 36);
  std::map<uint32_t, std::pair<uint32_t, std::string>> directory;
  for (size_t p = 0; p < dirs.size();) {
    const auto n = U32(dirs, p + 20);
    directory.emplace(static_cast<uint32_t>(p),
                      std::make_pair(U32(dirs, p), Name(dirs, p + 24, n)));
    p += (24ULL + n + 3) & ~size_t(3);
  }
  std::array<bool, 9> hud{}, menu{}, messages{};
  for (size_t p = 0; p < files.size();) {
    const auto n = U32(files, p + 28);
    const auto name = Name(files, p + 32, n);
    const auto parent = U32(files, p);
    const auto payload = U64(files, p + 8), length = U64(files, p + 16);
    if (data > size || payload > size - data || length > size - data - payload)
      throw std::runtime_error("RomFS language payload outside image");
    const auto dir = directory.find(parent);
    if (dir != directory.end() && length) {
      const auto owner = directory.find(dir->second.first);
      if (owner != directory.end() && owner->second.second == "menu") {
        for (size_t slot = 0; slot < kLanguages.size(); ++slot) {
          if (dir->second.second != kLanguages[slot].Directory)
            continue;
          hud[slot] = hud[slot] || name == "hud_all00.ctxb";
          menu[slot] = menu[slot] || name == "menu_file_select_parts00.ctxb";
        }
      }
    }
    if (name.ends_with(".qm")) {
      if (length < 16)
        throw std::runtime_error("Truncated QM language table");
      const auto qm = read(data + payload, 16);
      const uint32_t count = U32(qm, 8);
      const uint64_t tableEnd = 16ULL + uint64_t(count) * 96;
      if (U32(qm, 0) != 0x4d51 || U32(qm, 4) != 4 || U32(qm, 12) || !count ||
          count > 100000 || tableEnd > length)
        throw std::runtime_error("Invalid QM language table");
      const auto table = read(data + payload, tableEnd);
      for (uint32_t i = 0; i < count; ++i)
        for (size_t slot = 0; slot < 9; ++slot) {
          const size_t field = 16 + size_t(i) * 96 + 16 + slot * 8;
          const auto start = U32(table, field), bytes = U32(table, field + 4);
          if (bool(start) != bool(bytes) ||
              (start && (start < tableEnd || uint64_t(start) + bytes > length)))
            throw std::runtime_error("QM language payload outside file");
          messages[slot] = messages[slot] || bytes != 0;
        }
    }
    p += (32ULL + n + 3) & ~size_t(3);
  }
  std::vector<GameLanguage> result;
  std::set<std::string> seen;
  for (size_t slot = 0; slot < 9; ++slot)
    if (hud[slot] && menu[slot] && messages[slot]) {
      const auto &lang = kLanguages[slot];
      if (seen.insert(lang.Code).second)
        result.push_back({lang.Code, lang.Label, lang.SystemId});
    }
  if (result.empty())
    throw std::runtime_error(
        "No complete OOT3D menu/message language found in RomFS");
  return result;
}

nlohmann::json GameLanguageDocument(const std::vector<GameLanguage> &languages,
                                    const std::string &selected) {
  if (languages.empty())
    throw std::runtime_error("Empty game language list");
  auto entries = nlohmann::json::array();
  for (const auto &lang : languages)
    entries.push_back({{"code", lang.Code},
                       {"label", lang.Label},
                       {"system_id", lang.SystemId}});
  const bool supported =
      std::any_of(languages.begin(), languages.end(),
                  [&](const auto &v) { return v.Code == selected; });
  return {{"format", "triaevum_game_language_v1"},
          {"available", entries},
          {"selected", supported ? selected : languages.front().Code}};
}
GameLanguageSettings::GameLanguageSettings(std::filesystem::path path,
                                           std::vector<GameLanguage> available)
    : mPath(std::move(path)), mAvailable(std::move(available)) {
  if (mAvailable.empty())
    throw std::runtime_error("Empty game language list");
  mSelected = mAvailable.front().Code;
  try {
    if (std::filesystem::exists(mPath)) {
      std::ifstream file(mPath);
      const auto doc = nlohmann::json::parse(file);
      if (doc.value("format", "") != "triaevum_game_language_v1")
        throw std::runtime_error("Unsupported game language configuration");
      const auto candidate = doc.at("selected").get<std::string>();
      for (const auto &lang : mAvailable)
        if (lang.Code == candidate)
          mSelected = candidate;
    }
  } catch (const std::exception &e) {
    mError = e.what();
  }
  mBootLanguage = mSelected;
  // Preserve a malformed file for diagnosis; F1 can explicitly replace it.
  if (mError.empty())
    Select(mSelected);
}
bool GameLanguageSettings::Select(const std::string &code) {
  if (std::none_of(mAvailable.begin(), mAvailable.end(),
                   [&](const auto &v) { return v.Code == code; }))
    return false;
  std::error_code error;
  if (!mPath.parent_path().empty())
    std::filesystem::create_directories(mPath.parent_path(), error);
  if (error) {
    mError = error.message();
    return false;
  }
  const auto saved = Ship::WriteConfigFileAtomically(
      mPath, GameLanguageDocument(mAvailable, code).dump(2));
  mError = saved.Error;
  if (saved.Success)
    mSelected = code;
  return saved.Success;
}
uint8_t GameLanguageSettings::SystemId() const {
  for (const auto &lang : mAvailable)
    if (lang.Code == mBootLanguage)
      return lang.SystemId;
  return 1;
}
} // namespace Oot3dNativeGame
