#ifndef TRIAEVUM_TAM_METADATA_H
#define TRIAEVUM_TAM_METADATA_H

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace triaevum::module {

enum class TamMetadataErrorCode {
  None,
  InvalidJson,
  MissingField,
  InvalidField,
};

struct TamMetadataError {
  TamMetadataErrorCode code = TamMetadataErrorCode::None;
  std::string message;
};

struct TamModuleMetadataV1 {
  struct RequiredService {
    std::uint32_t id = 0;
    std::uint32_t schemaVersion = 0;
  };

  struct PhysicalMemoryRegion {
    std::uint32_t physicalBase = 0;
    std::uint32_t guestBase = 0;
    std::uint32_t byteCount = 0;
  };

  std::uint32_t runtimeAbi = 0;
  std::string querySymbol;
  std::string recipe;
  std::string targetTriple;
  std::array<std::uint8_t, 32> sourceIdentitySha256{};
  std::array<std::uint8_t, 32> translatorIdentitySha256{};
  std::uint64_t nativeImageBytes = 0;
  std::array<std::uint8_t, 32> nativeImageSha256{};
  bool hasTitleAotImage = false;
  std::uint32_t titleAotAbi = 0;
  std::uint64_t titleAotImageBytes = 0;
  std::array<std::uint8_t, 32> titleAotImageSha256{};
  std::string forgeToolVersion;
  std::vector<RequiredService> requiredServices;
  std::vector<PhysicalMemoryRegion> physicalMemoryRegions;
};

struct TamMetadataParseResult {
  TamModuleMetadataV1 metadata;
  TamMetadataError error;

  [[nodiscard]] bool Ok() const {
    return error.code == TamMetadataErrorCode::None;
  }
};

[[nodiscard]] TamMetadataParseResult
ParseTamMetadataV1(std::span<const std::uint8_t> jsonBytes);

[[nodiscard]] bool TamTargetMatchesCurrentProcess(std::string_view target);

} // namespace triaevum::module

#endif
