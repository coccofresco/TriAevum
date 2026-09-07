#ifndef TRIAEVUM_TAM_READER_H
#define TRIAEVUM_TAM_READER_H

#include "triaevum/tam_format.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace triaevum::module {

enum class TamErrorCode {
  None,
  Truncated,
  InvalidMagic,
  UnsupportedFormat,
  IncompatibleRuntimeAbi,
  InvalidHeader,
  InvalidSectionTable,
  InvalidSection,
  UnsupportedSection,
  MissingRequiredSection,
  DuplicateSection,
  HashMismatch,
};

struct TamError {
  TamErrorCode code = TamErrorCode::None;
  std::string message;
};

struct TamSectionView {
  TamSectionType type = TamSectionType::MetadataJson;
  std::uint32_t flags = 0;
  std::span<const std::uint8_t> bytes;
  std::array<std::uint8_t, 32> sha256{};
};

struct TamView {
  std::uint16_t formatMinor = 0;
  std::uint32_t runtimeAbi = 0;
  std::vector<TamSectionView> sections;

  [[nodiscard]] const TamSectionView *Find(TamSectionType type) const;
};

struct TamParseResult {
  TamView module;
  TamError error;

  [[nodiscard]] bool Ok() const { return error.code == TamErrorCode::None; }
};

[[nodiscard]] TamParseResult ParseTam(std::span<const std::uint8_t> file,
                                      std::uint32_t expectedRuntimeAbi);

} // namespace triaevum::module

#endif
