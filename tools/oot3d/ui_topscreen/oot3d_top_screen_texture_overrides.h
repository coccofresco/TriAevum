#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {

class NativeA32Memory;

enum class TopScreenTextureOverrideResult : std::uint8_t {
  NoMatch,
  AlreadyApplied,
  Applied,
};

struct TopScreenProfileTextureAsset {
  std::string SemanticName;
  std::uint16_t Width = 0U;
  std::uint16_t Height = 0U;
  std::uint8_t NativePicaFormat = 0U;
  std::vector<std::uint8_t> EncodedPayload;
};

class TopScreenTextureOverridePack {
 public:
  bool Load(const std::filesystem::path &path, std::string *error = nullptr);
  bool LoadBytes(std::span<const std::uint8_t> bytes,
                 std::string *error = nullptr);
  TopScreenTextureOverrideResult Apply(
      std::span<std::uint8_t> payload) const noexcept;
  TopScreenTextureOverrideResult Apply(
      NativeA32Memory &memory, std::uint32_t payloadAddress,
      std::uint32_t payloadSize, std::string *error = nullptr) const;
  const TopScreenProfileTextureAsset *FindProfileTexture(
      std::string_view semanticName) const noexcept;
  bool Empty() const noexcept;

 private:
  struct Entry {
    std::uint64_t OriginalHash = 0;
    std::uint64_t ReplacementHash = 0;
    std::vector<std::uint8_t> Replacement;
  };
  std::vector<Entry> mEntries;
  std::vector<TopScreenProfileTextureAsset> mProfileTextures;
};

} // namespace Oot3dNativeGame
