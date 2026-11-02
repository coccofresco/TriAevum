#include "oot3d_game_language.h"
#include "ship/config/ConfigPersistence.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#if !defined(_WIN32)
#include <cerrno>
#include <charconv>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

#if !defined(__APPLE__) || defined(TRIAEVUM_TITLE_MM3D)
// MM3D: layouts in layout/<REGION>_<Language>/, messages in
// message/<region>/<region><letter>.gmsg. The EUR card ships Dutch layouts
// whose archives are empty, so Dutch is not offered.
struct Mm3dLanguage {
  const char *Layout;
  const char *Message;
  const char *Code;
  const char *Label;
  uint8_t SystemId;
};
constexpr std::array<Mm3dLanguage, 9> kMm3dLanguages{{
    {"EU_English", "eu/eue.gmsg", "en", "English", 1},
    {"EU_French", "eu/euf.gmsg", "fr", "Francais", 2},
    {"EU_German", "eu/eug.gmsg", "de", "Deutsch", 3},
    {"EU_Italian", "eu/eui.gmsg", "it", "Italiano", 4},
    {"EU_Spanish", "eu/eus.gmsg", "es", "Espanol", 5},
    {"EU_Dutch", "eu/eud.gmsg", "nl", "Nederlands", 8},
    {"US_English", "us/use.gmsg", "en", "English", 1},
    {"US_French", "us/usf.gmsg", "fr", "Francais", 2},
    {"US_Spanish", "us/uss.gmsg", "es", "Espanol", 5},
}};

#endif

struct RomFsFile {
  std::string Parent, Owner; // containing directory and its parent
  std::string Name;
  uint64_t Payload; // relative to the image
  uint64_t Length;
};

class RomFsImage {
public:
  RomFsImage(const std::filesystem::path &path, uint64_t offset, uint64_t size)
      : mDescriptor(InheritedDescriptor(path)), mOffset(offset), mSize(size) {
    if (mDescriptor < 0)
      mStream.open(path, std::ios::binary);
    const auto fileSize = FileSize(path);
    if (mOffset > fileSize)
      throw std::runtime_error("RomFS offset outside file");
    if (!mSize)
      mSize = fileSize - mOffset;
    if (mSize > fileSize - mOffset)
      throw std::runtime_error("RomFS size outside file");
    const auto magic = Read(0, 4);
    const uint64_t base = U32(magic, 0) == 0x43465649U ? 0x1000 : 0;
    const auto header = Read(base, 40);
    if (U32(header, 0) != 40)
      throw std::runtime_error("Invalid RomFS Level-3 header");
    const auto dirs = Read(base + U32(header, 12), U32(header, 16));
    const auto files = Read(base + U32(header, 28), U32(header, 32));
    const uint64_t data = base + U32(header, 36);
    std::map<uint32_t, std::pair<uint32_t, std::string>> directory;
    for (size_t p = 0; p < dirs.size();) {
      const auto n = U32(dirs, p + 20);
      directory.emplace(static_cast<uint32_t>(p),
                        std::make_pair(U32(dirs, p), Name(dirs, p + 24, n)));
      p += (24ULL + n + 3) & ~size_t(3);
    }
    for (size_t p = 0; p < files.size();) {
      const auto n = U32(files, p + 28);
      RomFsFile file{{}, {}, Name(files, p + 32, n), U64(files, p + 8),
                     U64(files, p + 16)};
      if (data > mSize || file.Payload > mSize - data ||
          file.Length > mSize - data - file.Payload)
        throw std::runtime_error("RomFS language payload outside image");
      file.Payload += data;
      if (const auto dir = directory.find(U32(files, p));
          dir != directory.end()) {
        file.Parent = dir->second.second;
        if (const auto owner = directory.find(dir->second.first);
            owner != directory.end())
          file.Owner = owner->second.second;
      }
      mFiles.push_back(std::move(file));
      p += (32ULL + n + 3) & ~size_t(3);
    }
  }
  Bytes Read(uint64_t at, uint64_t count) {
    if (at > mSize || count > mSize - at || count > 16 * 1024 * 1024)
      throw std::runtime_error("RomFS language metadata outside image");
    Bytes result(static_cast<size_t>(count));
#if !defined(_WIN32)
    if (mDescriptor >= 0) {
      for (size_t done = 0; done < result.size();) {
        const auto read = ::pread(mDescriptor, result.data() + done, result.size() - done,
                                  static_cast<off_t>(mOffset + at + done));
        if (read < 0 && errno == EINTR)
          continue;
        if (read <= 0)
          throw std::runtime_error("Cannot read RomFS language metadata");
        done += static_cast<size_t>(read);
      }
      return result;
    }
#endif
    mStream.seekg(static_cast<std::streamoff>(mOffset + at));
    if (!mStream.read(reinterpret_cast<char *>(result.data()), result.size()))
      throw std::runtime_error("Cannot read RomFS language metadata");
    return result;
  }
  const std::vector<RomFsFile> &Files() const { return mFiles; }

private:
  // An open document handed over as /proc/self/fd/N (the Android Forge reads
  // a ROM picked through the document picker in this process): read through
  // that descriptor. Reopening the path would ask the provider's file system
  // again, which refuses an app without storage permission.
  static int InheritedDescriptor(const std::filesystem::path &path) {
#if !defined(_WIN32)
    constexpr std::string_view prefix = "/proc/self/fd/";
    const auto text = path.string();
    if (!std::string_view(text).starts_with(prefix))
      return -1;
    int descriptor = -1;
    const auto *first = text.data() + prefix.size();
    const auto *last = text.data() + text.size();
    const auto parsed = std::from_chars(first, last, descriptor);
    if (parsed.ec != std::errc{} || parsed.ptr != last || descriptor < 0)
      return -1;
    struct stat status {};
    return ::fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode) ? descriptor : -1;
#else
    (void)path;
    return -1;
#endif
  }
  uint64_t FileSize(const std::filesystem::path &path) const {
#if !defined(_WIN32)
    if (mDescriptor >= 0) {
      struct stat status {};
      if (::fstat(mDescriptor, &status) != 0)
        throw std::runtime_error("Cannot read RomFS language metadata");
      return static_cast<uint64_t>(status.st_size);
    }
#endif
    return std::filesystem::file_size(path);
  }

  int mDescriptor = -1; // borrowed, never closed here
  std::ifstream mStream;
  uint64_t mOffset, mSize;
  std::vector<RomFsFile> mFiles;
};

std::vector<GameLanguage> DetectOot3dLanguages(RomFsImage &image) {
  std::array<bool, 9> hud{}, menu{}, messages{};
  for (const auto &file : image.Files()) {
    const auto &name = file.Name;
    const auto length = file.Length;
    if (length && file.Owner == "menu") {
      for (size_t slot = 0; slot < kLanguages.size(); ++slot) {
        if (file.Parent != kLanguages[slot].Directory)
          continue;
        hud[slot] = hud[slot] || name == "hud_all00.ctxb";
        menu[slot] = menu[slot] || name == "menu_file_select_parts00.ctxb";
      }
    }
    if (name.ends_with(".qm")) {
      if (length < 16)
        throw std::runtime_error("Truncated QM language table");
      const auto qm = image.Read(file.Payload, 16);
      const uint32_t count = U32(qm, 8);
      const uint64_t tableEnd = 16ULL + uint64_t(count) * 96;
      if (U32(qm, 0) != 0x4d51 || U32(qm, 4) != 4 || U32(qm, 12) || !count ||
          count > 100000 || tableEnd > length)
        throw std::runtime_error("Invalid QM language table");
      const auto table = image.Read(file.Payload, tableEnd);
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

#if !defined(__APPLE__) || defined(TRIAEVUM_TITLE_MM3D)
// A GAR v2 archive whose every member has content.
bool PopulatedGar(RomFsImage &image, const RomFsFile &file) {
  if (file.Length < 0x20)
    return false;
  const auto header = image.Read(file.Payload, 0x20);
  if (U32(header, 0) != 0x02524147U) // "GAR\2"
    throw std::runtime_error("Invalid MM3D layout archive " + file.Name);
  const uint32_t count = uint32_t(header[10]) | uint32_t(header[11]) << 8;
  const uint32_t entries = U32(header, 16);
  if (!count || entries > file.Length ||
      uint64_t(count) * 12 > file.Length - entries)
    throw std::runtime_error("Invalid MM3D layout archive " + file.Name);
  const auto table = image.Read(file.Payload + entries, uint64_t(count) * 12);
  for (uint32_t i = 0; i < count; ++i)
    if (!U32(table, size_t(i) * 12))
      return false;
  return true;
}

std::vector<GameLanguage> DetectMm3dLanguages(RomFsImage &image) {
  std::array<size_t, kMm3dLanguages.size()> archives{};
  std::array<bool, kMm3dLanguages.size()> emptyArchive{}, messages{};
  for (const auto &file : image.Files())
    for (size_t slot = 0; slot < kMm3dLanguages.size(); ++slot) {
      const auto &lang = kMm3dLanguages[slot];
      if (file.Owner == "layout" && file.Parent == lang.Layout &&
          file.Name.ends_with(".gar")) {
        ++archives[slot];
        emptyArchive[slot] = emptyArchive[slot] || !PopulatedGar(image, file);
      }
      const std::string_view message = lang.Message;
      if (file.Owner == "message" && file.Length >= 16 &&
          message == file.Parent + "/" + file.Name)
        messages[slot] = U32(image.Read(file.Payload, 4), 0) ==
                         0x47534d47U; // "GMSG"
    }
  std::vector<GameLanguage> result;
  std::set<std::string> seen;
  for (size_t slot = 0; slot < kMm3dLanguages.size(); ++slot)
    if (archives[slot] && !emptyArchive[slot] && messages[slot]) {
      const auto &lang = kMm3dLanguages[slot];
      if (seen.insert(lang.Code).second)
        result.push_back({lang.Code, lang.Label, lang.SystemId});
    }
  if (result.empty())
    throw std::runtime_error(
        "No complete MM3D layout/message language found in RomFS");
  return result;
}
#endif
} // namespace

std::vector<GameLanguage> DetectGameLanguages(const std::filesystem::path &path,
                                              uint64_t offset, uint64_t size,
                                              TitleAdapter title) {
  RomFsImage image(path, offset, size);
  switch (title) {
  case TitleAdapter::Oot3dEur:
    return DetectOot3dLanguages(image);
  case TitleAdapter::Mm3d:
#if !defined(__APPLE__) || defined(TRIAEVUM_TITLE_MM3D)
    return DetectMm3dLanguages(image);
#else
    break;
#endif
  case TitleAdapter::Generic:
    break;
  }
  throw std::runtime_error("No game language layout is known for this title");
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

std::vector<std::string> UiTexturePackLanguageFolders(uint8_t systemLanguage) {
  switch (systemLanguage) {
  case 0: return {"00_JP_JAPANESE", "JAPANESE"};
  case 1: return {"02_EU_ENGLISH", "01_US_ENGLISH"};
  case 2: return {"04_EU_FRENCH", "05_US_FRENCH"};
  case 3: return {"03_EU_GERMAN", "GERMAN"};
  case 4: return {"08_EU_ITALIAN", "ITALIAN"};
  case 5: return {"06_EU_SPANISH", "07_US_SPANISH", "SPANISH"};
  case 8: return {"09_EU_DUTCH"};
  default: return {};
  }
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
