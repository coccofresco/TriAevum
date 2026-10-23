#include "oot3d_top_screen_link_ext.h"

#include <cstring>
#include <string_view>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kZarMagic = 0x0152415AU; // "ZAR\x01"
constexpr std::uint32_t kMaximumEntryIndex = 0x7FFEU;
constexpr std::uint32_t kMaximumTypeEntries = 0x1000U;

bool Read32(std::span<const std::uint8_t> bytes, std::uint64_t offset, std::uint32_t *value) {
  if (offset + 4U > bytes.size()) return false;
  std::memcpy(value, bytes.data() + offset, 4U);
  return true;
}

// The NUL-terminated name at `offset`, or empty when it runs past the end.
std::string_view Name(std::span<const std::uint8_t> bytes, std::uint32_t offset) {
  if (offset >= bytes.size()) return {};
  const auto *begin = reinterpret_cast<const char *>(bytes.data() + offset);
  const auto *end = static_cast<const char *>(std::memchr(begin, 0, bytes.size() - offset));
  return end == nullptr ? std::string_view{} : std::string_view(begin, end - begin);
}

void Write32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
  std::memcpy(bytes.data() + offset, &value, 4U);
}

struct Selected {
  std::uint32_t Offset = 0;
  std::uint32_t Size = 0;
  bool Present = false;
};

} // namespace

bool BuildTopScreenLinkExtImage(std::span<const std::uint8_t> archive, std::uint32_t base,
                                TopScreenLinkExtImage *image, std::string *error) {
  const auto fail = [error](const char *text) {
    if (error != nullptr) *error = text;
    return false;
  };
  std::uint32_t magic = 0, typesOffset = 0, metaOffset = 0, dataOffset = 0;
  if (!Read32(archive, 0U, &magic) || magic != kZarMagic || archive.size() < 0x18U ||
      !Read32(archive, 0x0CU, &typesOffset) || !Read32(archive, 0x10U, &metaOffset) ||
      !Read32(archive, 0x14U, &dataOffset))
    return fail("zelda_link_ext.zar is not a ZAR v1 archive");
  const std::uint16_t typeCount = static_cast<std::uint16_t>(archive[8] | (archive[9] << 8));

  // [age][animation]; the last matching entry wins, as in 3.0.
  std::array<std::array<Selected, kTopScreenLinkExtAnimations.size()>, 2> selected{};
  for (std::uint32_t type = 0; type < typeCount; ++type) {
    const std::uint64_t record = typesOffset + static_cast<std::uint64_t>(type) * 0x10U;
    std::uint32_t count = 0, list = 0, typeName = 0;
    if (!Read32(archive, record, &count) || !Read32(archive, record + 4U, &list) ||
        !Read32(archive, record + 8U, &typeName))
      break;
    if (Name(archive, typeName) != "csab") continue;
    for (std::uint32_t k = 0; k < count && k < kMaximumTypeEntries; ++k) {
      std::uint32_t index = 0, size = 0, nameOffset = 0, csab = 0;
      if (!Read32(archive, list + static_cast<std::uint64_t>(k) * 4U, &index)) break;
      if (index > kMaximumEntryIndex ||
          !Read32(archive, metaOffset + static_cast<std::uint64_t>(index) * 8U, &size) ||
          !Read32(archive, metaOffset + static_cast<std::uint64_t>(index) * 8U + 4U,
                  &nameOffset) ||
          !Read32(archive, dataOffset + static_cast<std::uint64_t>(index) * 4U, &csab))
        continue;
      std::string_view name = Name(archive, nameOffset);
      std::uint32_t age = 0;
      if (name.starts_with("adult")) age = 0U;
      else if (name.starts_with("child")) age = 1U;
      else continue;
      if (name.size() > 10U && name.ends_with(".csab")) name.remove_suffix(5U);
      if (name.size() < 6U) continue;
      name.remove_prefix(6U);
      for (std::size_t i = 0; i < kTopScreenLinkExtAnimations.size(); ++i) {
        if (name != kTopScreenLinkExtAnimations[i].Name) continue;
        const std::uint32_t rounded = (size + 3U) & ~3U;
        if (size > 3U && csab < archive.size() && (csab & 3U) == 0U &&
            rounded <= archive.size() - csab &&
            std::memcmp(archive.data() + csab, "csab", 4U) == 0)
          selected[age][i] = {csab, rounded, true};
        break;
      }
    }
  }

  TopScreenLinkExtImage built;
  std::size_t total = TopScreenLinkExtImage::kData;
  bool any = false;
  for (std::size_t i = 0; i < kTopScreenLinkExtAnimations.size(); ++i) {
    if (!selected[1][i].Present) selected[1][i] = selected[0][i];
    for (std::uint32_t age = 0; age < 2U; ++age) {
      built.Present[age][i] = selected[age][i].Present;
      any = any || selected[age][i].Present;
    }
    if (selected[0][i].Present) total += (selected[0][i].Size + 15U) & ~15U;
    if (selected[1][i].Present && selected[1][i].Offset != selected[0][i].Offset)
      total += (selected[1][i].Size + 15U) & ~15U;
  }
  if (!any) return fail("zelda_link_ext.zar holds none of the 3.0 animations");

  built.Bytes.assign((total + 0xFFFU) & ~std::size_t{0xFFFU}, 0U);
  std::uint32_t hash = 2166136261U;
  for (const auto byte : archive) hash = (hash ^ byte) * 16777619U;
  built.ContentHash = hash;
  Write32(built.Bytes, 0U, TopScreenLinkExtImage::kMagic);
  Write32(built.Bytes, 4U, hash);
  std::size_t cursor = TopScreenLinkExtImage::kData;
  for (std::uint32_t age = 0; age < 2U; ++age) {
    for (std::size_t i = 0; i < kTopScreenLinkExtAnimations.size(); ++i) {
      const auto &entry = selected[age][i];
      if (!entry.Present) continue;
      const std::size_t slot = TopScreenLinkExtImage::kSlots + (age * 16U + i) * 4U;
      if (age == 1U && entry.Offset == selected[0][i].Offset) {
        std::memcpy(built.Bytes.data() + slot,
                    built.Bytes.data() + TopScreenLinkExtImage::kSlots + i * 4U, 4U);
        continue;
      }
      std::memcpy(built.Bytes.data() + cursor, archive.data() + entry.Offset, entry.Size);
      Write32(built.Bytes, slot, base + static_cast<std::uint32_t>(cursor));
      cursor += (entry.Size + 15U) & ~std::size_t{15U};
    }
  }
  *image = std::move(built);
  return true;
}

} // namespace Oot3dNativeGame
