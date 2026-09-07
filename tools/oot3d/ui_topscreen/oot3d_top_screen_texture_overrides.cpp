#include "oot3d_top_screen_texture_overrides.h"

#include "oot3d_native_a32_memory.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kPackVersionV1 = 1U;
constexpr std::uint32_t kPackVersionV2 = 2U;
constexpr std::size_t kPackV1HeaderSize = 12U;
constexpr std::size_t kPackV2HeaderSize = 16U;
constexpr std::size_t kEntryHeaderSize = 20U;
constexpr std::size_t kProfileTextureHeaderSize = 24U;

std::uint32_t ReadU32(std::span<const std::uint8_t> bytes,
                      std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::uint64_t ReadU64(std::span<const std::uint8_t> bytes,
                      std::size_t offset) noexcept {
  return static_cast<std::uint64_t>(ReadU32(bytes, offset)) |
         (static_cast<std::uint64_t>(ReadU32(bytes, offset + 4U)) << 32U);
}

std::uint64_t Fnv1a64(std::span<const std::uint8_t> bytes) noexcept {
  std::uint64_t value = 0xCBF29CE484222325ULL;
  for (const auto byte : bytes) {
    value ^= byte;
    value *= 0x100000001B3ULL;
  }
  return value;
}

void SetError(std::string *error, const char *message) {
  if (error != nullptr) *error = message;
}

} // namespace

bool TopScreenTextureOverridePack::Load(const std::filesystem::path &path,
                                        std::string *error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    SetError(error, "cannot open TopScreen texture override pack");
    return false;
  }
  const std::vector<std::uint8_t> bytes{
      std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  return LoadBytes(bytes, error);
}

bool TopScreenTextureOverridePack::LoadBytes(
    std::span<const std::uint8_t> bytes, std::string *error) {
  if (bytes.size() < kPackV1HeaderSize ||
      !std::equal(bytes.begin(), bytes.begin() + 4U, "O3TU")) {
    SetError(error, "invalid TopScreen texture override pack header");
    return false;
  }
  const auto version = ReadU32(bytes, 4U);
  if (version != kPackVersionV1 && version != kPackVersionV2) {
    SetError(error, "unsupported TopScreen texture override pack version");
    return false;
  }
  if (version == kPackVersionV2 && bytes.size() < kPackV2HeaderSize) {
    SetError(error, "truncated TopScreen texture override pack header");
    return false;
  }
  const auto count = ReadU32(bytes, 8U);
  const auto profileTextureCount =
      version == kPackVersionV2 ? ReadU32(bytes, 12U) : 0U;
  std::vector<Entry> entries;
  entries.reserve(count);
  std::size_t cursor = version == kPackVersionV2 ? kPackV2HeaderSize
                                                  : kPackV1HeaderSize;
  for (std::uint32_t index = 0; index < count; ++index) {
    if (cursor > bytes.size() || bytes.size() - cursor < kEntryHeaderSize) {
      SetError(error, "truncated TopScreen texture override entry");
      return false;
    }
    Entry entry;
    entry.OriginalHash = ReadU64(bytes, cursor);
    entry.ReplacementHash = ReadU64(bytes, cursor + 8U);
    const auto size = ReadU32(bytes, cursor + 16U);
    cursor += kEntryHeaderSize;
    if (cursor > bytes.size() || size > bytes.size() - cursor) {
      SetError(error, "truncated TopScreen texture override payload");
      return false;
    }
    entry.Replacement.assign(bytes.begin() + cursor,
                             bytes.begin() + cursor + size);
    if (Fnv1a64(entry.Replacement) != entry.ReplacementHash) {
      SetError(error, "TopScreen texture override payload hash mismatch");
      return false;
    }
    cursor += size;
    entries.push_back(std::move(entry));
  }
  std::vector<TopScreenProfileTextureAsset> profileTextures;
  profileTextures.reserve(profileTextureCount);
  for (std::uint32_t index = 0U; index < profileTextureCount; ++index) {
    if (cursor > bytes.size() ||
        bytes.size() - cursor < kProfileTextureHeaderSize) {
      SetError(error, "truncated TopScreen profile texture entry");
      return false;
    }
    const auto nameSize = ReadU32(bytes, cursor);
    TopScreenProfileTextureAsset texture;
    texture.Width = static_cast<std::uint16_t>(
        bytes[cursor + 4U] | (bytes[cursor + 5U] << 8U));
    texture.Height = static_cast<std::uint16_t>(
        bytes[cursor + 6U] | (bytes[cursor + 7U] << 8U));
    texture.NativePicaFormat = bytes[cursor + 8U];
    const auto payloadSize = ReadU32(bytes, cursor + 12U);
    const auto payloadHash = ReadU64(bytes, cursor + 16U);
    cursor += kProfileTextureHeaderSize;
    if (nameSize == 0U || cursor > bytes.size() ||
        nameSize > bytes.size() - cursor) {
      SetError(error, "invalid TopScreen profile texture semantic");
      return false;
    }
    texture.SemanticName.assign(
        reinterpret_cast<const char *>(bytes.data() + cursor), nameSize);
    cursor += nameSize;
    if (texture.SemanticName.find('\0') != std::string::npos ||
        texture.Width == 0U || texture.Height == 0U ||
        cursor > bytes.size() || payloadSize > bytes.size() - cursor) {
      SetError(error, "invalid TopScreen profile texture payload");
      return false;
    }
    texture.EncodedPayload.assign(bytes.begin() + cursor,
                                  bytes.begin() + cursor + payloadSize);
    if (Fnv1a64(texture.EncodedPayload) != payloadHash) {
      SetError(error, "TopScreen profile texture payload hash mismatch");
      return false;
    }
    const auto duplicate = std::find_if(
        profileTextures.begin(), profileTextures.end(),
        [&](const TopScreenProfileTextureAsset &candidate) {
          return candidate.SemanticName == texture.SemanticName;
        });
    if (duplicate != profileTextures.end()) {
      SetError(error, "duplicate TopScreen profile texture semantic");
      return false;
    }
    cursor += payloadSize;
    profileTextures.push_back(std::move(texture));
  }
  if (cursor != bytes.size()) {
    SetError(error, "TopScreen texture override pack has trailing data");
    return false;
  }
  mEntries = std::move(entries);
  mProfileTextures = std::move(profileTextures);
  return true;
}

TopScreenTextureOverrideResult TopScreenTextureOverridePack::Apply(
    std::span<std::uint8_t> payload) const noexcept {
  const auto hash = Fnv1a64(payload);
  for (const auto &entry : mEntries) {
    if (entry.Replacement.size() != payload.size()) continue;
    if (hash == entry.ReplacementHash) {
      return TopScreenTextureOverrideResult::AlreadyApplied;
    }
    if (hash == entry.OriginalHash) {
      std::copy(entry.Replacement.begin(), entry.Replacement.end(),
                payload.begin());
      return TopScreenTextureOverrideResult::Applied;
    }
  }
  return TopScreenTextureOverrideResult::NoMatch;
}

TopScreenTextureOverrideResult TopScreenTextureOverridePack::Apply(
    NativeA32Memory &memory, std::uint32_t payloadAddress,
    std::uint32_t payloadSize, std::string *error) const {
  std::vector<std::uint8_t> payload(payloadSize);
  if (!memory.IsWritable(payloadAddress, payload.size()) ||
      !memory.ReadBytes(payloadAddress, payload)) {
    SetError(error, "TopScreen native texture payload is unavailable");
    return TopScreenTextureOverrideResult::NoMatch;
  }
  const auto result = Apply(payload);
  if (result == TopScreenTextureOverrideResult::Applied &&
      !memory.WriteBytes(payloadAddress, payload)) {
    SetError(error, "cannot write TopScreen native texture payload");
    return TopScreenTextureOverrideResult::NoMatch;
  }
  return result;
}

bool TopScreenTextureOverridePack::Empty() const noexcept {
  return mEntries.empty() && mProfileTextures.empty();
}

const TopScreenProfileTextureAsset *
TopScreenTextureOverridePack::FindProfileTexture(
    std::string_view semanticName) const noexcept {
  const auto found = std::find_if(
      mProfileTextures.begin(), mProfileTextures.end(),
      [semanticName](const TopScreenProfileTextureAsset &texture) {
        return texture.SemanticName == semanticName;
      });
  return found != mProfileTextures.end() ? &*found : nullptr;
}

} // namespace Oot3dNativeGame
