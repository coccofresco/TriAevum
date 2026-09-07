#ifndef TRIAEVUM_TAM_FORMAT_H
#define TRIAEVUM_TAM_FORMAT_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace triaevum::module {

inline constexpr std::array<std::uint8_t, 8> kTamMagic = {'T', 'R', 'I', 'A',
                                                          'E', 'V', 'M', '1'};
inline constexpr std::uint16_t kTamFormatMajor = 1;
inline constexpr std::uint16_t kTamFormatMinor = 1;
inline constexpr std::uint16_t kTamHeaderSize = 64;
inline constexpr std::uint16_t kTamSectionEntrySize = 64;
inline constexpr std::size_t kTamSectionAlignment = 16;
inline constexpr std::uint32_t kTamMaximumSections = 32;
inline constexpr std::uint64_t kTamMaximumMetadataBytes = 1024 * 1024;

enum class TamSectionType : std::uint32_t {
  MetadataJson = 1,
  NativeImage = 2,
  TitleAotImage = 3,
};

enum TamSectionFlags : std::uint32_t {
  TamSectionRequired = 1U << 0U,
  TamSectionExecutable = 1U << 1U,
  TamSectionCompressed = 1U << 2U,
};

} // namespace triaevum::module

#endif
