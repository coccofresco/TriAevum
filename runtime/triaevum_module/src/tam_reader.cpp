#include "triaevum/tam_reader.h"

#include "triaevum/sha256.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace triaevum::module {
namespace {

std::uint16_t Read16(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset]) |
         (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

std::uint32_t Read32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::uint64_t Read64(std::span<const std::uint8_t> bytes, std::size_t offset) {
  std::uint64_t result = 0;
  for (std::size_t index = 0; index < 8; ++index) {
    result |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
  }
  return result;
}

bool IsZero(std::span<const std::uint8_t> bytes) {
  return std::all_of(bytes.begin(), bytes.end(),
                     [](std::uint8_t value) { return value == 0U; });
}

std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment) {
  return (value + alignment - 1U) & ~(alignment - 1U);
}

TamParseResult Failure(TamErrorCode code, std::string message) {
  TamParseResult result;
  result.error = {code, std::move(message)};
  return result;
}

} // namespace

const TamSectionView *TamView::Find(TamSectionType type) const {
  const auto found = std::find_if(
      sections.begin(), sections.end(),
      [type](const auto &section) { return section.type == type; });
  return found == sections.end() ? nullptr : &*found;
}

TamParseResult ParseTam(std::span<const std::uint8_t> file,
                        std::uint32_t expectedRuntimeAbi) {
  if (file.size() < kTamHeaderSize) {
    return Failure(TamErrorCode::Truncated, "TAM header is truncated");
  }
  if (!std::equal(kTamMagic.begin(), kTamMagic.end(), file.begin())) {
    return Failure(TamErrorCode::InvalidMagic, "TAM magic is invalid");
  }

  const std::uint16_t formatMajor = Read16(file, 8U);
  const std::uint16_t formatMinor = Read16(file, 10U);
  const std::uint16_t headerSize = Read16(file, 12U);
  const std::uint16_t sectionEntrySize = Read16(file, 14U);
  const std::uint32_t runtimeAbi = Read32(file, 16U);
  const std::uint32_t sectionCount = Read32(file, 20U);
  const std::uint64_t declaredFileSize = Read64(file, 24U);
  const std::uint64_t sectionTableOffset = Read64(file, 32U);

  if (formatMajor != kTamFormatMajor || formatMinor > kTamFormatMinor) {
    return Failure(TamErrorCode::UnsupportedFormat,
                   "TAM format version is not supported");
  }
  if (headerSize != kTamHeaderSize ||
      sectionEntrySize != kTamSectionEntrySize ||
      sectionTableOffset != kTamHeaderSize || !IsZero(file.subspan(40U, 24U))) {
    return Failure(TamErrorCode::InvalidHeader,
                   "TAM canonical header fields are invalid");
  }
  if (runtimeAbi != expectedRuntimeAbi) {
    return Failure(TamErrorCode::IncompatibleRuntimeAbi,
                   "TAM runtime ABI does not match the host");
  }
  if (declaredFileSize != file.size()) {
    return Failure(TamErrorCode::InvalidHeader,
                   "TAM declared file size does not match the input");
  }
  if (sectionCount == 0U || sectionCount > kTamMaximumSections) {
    return Failure(TamErrorCode::InvalidSectionTable,
                   "TAM section count is outside the supported range");
  }

  const std::uint64_t tableBytes =
      static_cast<std::uint64_t>(sectionCount) * kTamSectionEntrySize;
  if (sectionTableOffset > file.size() ||
      tableBytes > file.size() - sectionTableOffset) {
    return Failure(TamErrorCode::Truncated, "TAM section table is truncated");
  }
  const std::uint64_t dataBegin =
      AlignUp(sectionTableOffset + tableBytes, kTamSectionAlignment);
  if (dataBegin > file.size()) {
    return Failure(TamErrorCode::Truncated,
                   "TAM aligned section data is truncated");
  }
  const std::size_t tableEnd =
      static_cast<std::size_t>(sectionTableOffset + tableBytes);
  if (!IsZero(file.subspan(tableEnd,
                           static_cast<std::size_t>(dataBegin) - tableEnd))) {
    return Failure(TamErrorCode::InvalidSectionTable,
                   "TAM section-table padding is not zero");
  }

  TamParseResult result;
  result.module.formatMinor = formatMinor;
  result.module.runtimeAbi = runtimeAbi;
  result.module.sections.reserve(sectionCount);
  bool hasMetadata = false;
  bool hasNativeImage = false;
  bool hasTitleAotImage = false;
  std::uint64_t previousEnd = dataBegin;
  constexpr std::uint32_t knownFlags =
      TamSectionRequired | TamSectionExecutable | TamSectionCompressed;

  for (std::uint32_t index = 0; index < sectionCount; ++index) {
    const std::size_t descriptor =
        static_cast<std::size_t>(sectionTableOffset) +
        static_cast<std::size_t>(index) * kTamSectionEntrySize;
    const std::uint32_t rawType = Read32(file, descriptor);
    const std::uint32_t flags = Read32(file, descriptor + 4U);
    const std::uint64_t offset = Read64(file, descriptor + 8U);
    const std::uint64_t size = Read64(file, descriptor + 16U);
    std::array<std::uint8_t, 32> expectedHash{};
    std::copy_n(file.begin() + static_cast<std::ptrdiff_t>(descriptor + 24U),
                expectedHash.size(), expectedHash.begin());

    if (!IsZero(file.subspan(descriptor + 56U, 8U)) ||
        (flags & ~knownFlags) != 0U || (flags & TamSectionCompressed) != 0U) {
      return Failure(TamErrorCode::InvalidSection,
                     "TAM section uses unsupported fields or flags");
    }
    if (size == 0U || offset < dataBegin ||
        (offset % kTamSectionAlignment) != 0U || offset > file.size() ||
        size > file.size() - offset || offset < previousEnd) {
      return Failure(TamErrorCode::InvalidSection,
                     "TAM section range or ordering is invalid");
    }
    if (!IsZero(file.subspan(static_cast<std::size_t>(previousEnd),
                             static_cast<std::size_t>(offset - previousEnd)))) {
      return Failure(TamErrorCode::InvalidSection,
                     "TAM inter-section padding is not zero");
    }

    const auto sectionBytes = file.subspan(static_cast<std::size_t>(offset),
                                           static_cast<std::size_t>(size));
    if (detail::Sha256(sectionBytes) != expectedHash) {
      return Failure(TamErrorCode::HashMismatch,
                     "TAM section SHA-256 does not match");
    }

    TamSectionType type{};
    if (rawType == static_cast<std::uint32_t>(TamSectionType::MetadataJson)) {
      type = TamSectionType::MetadataJson;
      if (hasMetadata) {
        return Failure(TamErrorCode::DuplicateSection,
                       "TAM has duplicate metadata sections");
      }
      if (size > kTamMaximumMetadataBytes || flags != TamSectionRequired) {
        return Failure(TamErrorCode::InvalidSection,
                       "TAM metadata section contract is invalid");
      }
      hasMetadata = true;
    } else if (rawType ==
               static_cast<std::uint32_t>(TamSectionType::NativeImage)) {
      type = TamSectionType::NativeImage;
      if (hasNativeImage) {
        return Failure(TamErrorCode::DuplicateSection,
                       "TAM has duplicate native-image sections");
      }
      if (flags != (TamSectionRequired | TamSectionExecutable)) {
        return Failure(TamErrorCode::InvalidSection,
                       "TAM native-image section contract is invalid");
      }
      hasNativeImage = true;
    } else if (rawType ==
               static_cast<std::uint32_t>(TamSectionType::TitleAotImage)) {
      type = TamSectionType::TitleAotImage;
      if (hasTitleAotImage) {
        return Failure(TamErrorCode::DuplicateSection,
                       "TAM has duplicate title-AOT sections");
      }
      if (formatMinor < 1U ||
          flags != (TamSectionRequired | TamSectionExecutable)) {
        return Failure(TamErrorCode::InvalidSection,
                       "TAM title-AOT section contract is invalid");
      }
      hasTitleAotImage = true;
    } else if ((flags & TamSectionRequired) != 0U) {
      return Failure(TamErrorCode::UnsupportedSection,
                     "TAM contains an unknown required section");
    } else {
      previousEnd = offset + size;
      continue;
    }

    result.module.sections.push_back({type, flags, sectionBytes, expectedHash});
    previousEnd = offset + size;
  }

  if (previousEnd != file.size()) {
    return Failure(TamErrorCode::InvalidSection,
                   "TAM has unclaimed trailing bytes");
  }
  if (!hasMetadata || !hasNativeImage) {
    return Failure(TamErrorCode::MissingRequiredSection,
                   "TAM is missing metadata or native image");
  }
  return result;
}

} // namespace triaevum::module
