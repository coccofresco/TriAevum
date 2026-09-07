#include "triaevum/module_abi.h"
#include "triaevum/tam_format.h"
#include "triaevum/tam_reader.h"

#include "triaevum/sha256.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace triaevum::module;

void Write16(std::vector<std::uint8_t> &bytes, std::size_t offset,
             std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void Write32(std::vector<std::uint8_t> &bytes, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

void Write64(std::vector<std::uint8_t> &bytes, std::size_t offset,
             std::uint64_t value) {
  for (std::size_t index = 0; index < 8; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

std::size_t Align(std::size_t value) {
  return (value + kTamSectionAlignment - 1U) & ~(kTamSectionAlignment - 1U);
}

std::vector<std::uint8_t> BuildModule(bool withTitleAot = false) {
  const std::vector<std::uint8_t> metadata = {
      '{', '"', 'f', 'o', 'r', 'm', 'a', 't', '"',
      ':', '"', 't', 'e', 's', 't', '"', '}',
  };
  const std::vector<std::uint8_t> nativeImage = {'M', 'Z', 1U, 2U, 3U};
  const std::vector<std::uint8_t> titleAotImage = {'M', 'Z', 4U, 5U, 6U};
  const std::size_t sectionCount = withTitleAot ? 3U : 2U;
  const std::size_t tableEnd =
      kTamHeaderSize + sectionCount * kTamSectionEntrySize;
  const std::size_t metadataOffset = Align(tableEnd);
  const std::size_t nativeOffset = Align(metadataOffset + metadata.size());
  const std::size_t titleAotOffset = Align(nativeOffset + nativeImage.size());
  std::vector<std::uint8_t> bytes(withTitleAot
                                      ? titleAotOffset + titleAotImage.size()
                                      : nativeOffset + nativeImage.size(),
                                  0U);
  std::copy(kTamMagic.begin(), kTamMagic.end(), bytes.begin());
  Write16(bytes, 8U, kTamFormatMajor);
  Write16(bytes, 10U, kTamFormatMinor);
  Write16(bytes, 12U, kTamHeaderSize);
  Write16(bytes, 14U, kTamSectionEntrySize);
  Write32(bytes, 16U, TRIAEVUM_RUNTIME_ABI_V1);
  Write32(bytes, 20U, sectionCount);
  Write64(bytes, 24U, bytes.size());
  Write64(bytes, 32U, kTamHeaderSize);

  const auto writeSection = [&](std::size_t descriptor, TamSectionType type,
                                std::uint32_t flags, std::size_t offset,
                                const std::vector<std::uint8_t> &contents) {
    Write32(bytes, descriptor, static_cast<std::uint32_t>(type));
    Write32(bytes, descriptor + 4U, flags);
    Write64(bytes, descriptor + 8U, offset);
    Write64(bytes, descriptor + 16U, contents.size());
    const auto hash = detail::Sha256(contents);
    std::copy(hash.begin(), hash.end(), bytes.begin() + descriptor + 24U);
    std::copy(contents.begin(), contents.end(), bytes.begin() + offset);
  };
  writeSection(kTamHeaderSize, TamSectionType::MetadataJson, TamSectionRequired,
               metadataOffset, metadata);
  writeSection(
      kTamHeaderSize + kTamSectionEntrySize, TamSectionType::NativeImage,
      TamSectionRequired | TamSectionExecutable, nativeOffset, nativeImage);
  if (withTitleAot) {
    writeSection(kTamHeaderSize + 2U * kTamSectionEntrySize,
                 TamSectionType::TitleAotImage,
                 TamSectionRequired | TamSectionExecutable, titleAotOffset,
                 titleAotImage);
  }
  return bytes;
}

std::string Hex(std::span<const std::uint8_t> bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2U);
  for (const std::uint8_t value : bytes) {
    result.push_back(digits[value >> 4U]);
    result.push_back(digits[value & 0x0FU]);
  }
  return result;
}

bool Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main(int argc, char **argv) {
  bool ok = true;
  const std::array<std::uint8_t, 3> abc = {'a', 'b', 'c'};
  ok &= Expect(
      Hex(triaevum::module::detail::Sha256(abc)) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      "SHA-256 implementation failed its known vector");

  auto bytes = BuildModule();
  const auto parsed = ParseTam(bytes, TRIAEVUM_RUNTIME_ABI_V1);
  ok &= Expect(parsed.Ok(), "valid TAM did not parse");
  ok &= Expect(parsed.module.sections.size() == 2U,
               "valid TAM did not expose both required sections");
  ok &= Expect(parsed.module.Find(TamSectionType::MetadataJson) != nullptr,
               "valid TAM has no metadata view");
  ok &= Expect(parsed.module.Find(TamSectionType::NativeImage) != nullptr,
               "valid TAM has no native-image view");

  const auto titleAot = ParseTam(BuildModule(true), TRIAEVUM_RUNTIME_ABI_V1);
  ok &= Expect(titleAot.Ok() && titleAot.module.sections.size() == 3U,
               "valid title-AOT TAM did not expose all sections");
  ok &= Expect(titleAot.module.Find(TamSectionType::TitleAotImage) != nullptr,
               "valid title-AOT TAM has no title image view");

  const auto incompatible = ParseTam(bytes, TRIAEVUM_RUNTIME_ABI_V1 + 1U);
  ok &= Expect(incompatible.error.code == TamErrorCode::IncompatibleRuntimeAbi,
               "TAM runtime ABI mismatch was accepted");

  bytes.back() ^= 0x80U;
  const auto corrupt = ParseTam(bytes, TRIAEVUM_RUNTIME_ABI_V1);
  ok &= Expect(corrupt.error.code == TamErrorCode::HashMismatch,
               "corrupt TAM payload was accepted");

  bytes = BuildModule();
  bytes.push_back(0U);
  Write64(bytes, 24U, bytes.size());
  const auto trailing = ParseTam(bytes, TRIAEVUM_RUNTIME_ABI_V1);
  ok &= Expect(trailing.error.code == TamErrorCode::InvalidSection,
               "unclaimed TAM trailer was accepted");

  if (argc == 2) {
    std::ifstream source(argv[1], std::ios::binary | std::ios::ate);
    ok &= Expect(source.good(), "external TAM could not be opened");
    if (source.good()) {
      const std::streamsize byteCount = source.tellg();
      ok &= Expect(byteCount > 0, "external TAM is empty");
      if (byteCount > 0) {
        source.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> external(static_cast<std::size_t>(byteCount));
        source.read(reinterpret_cast<char *>(external.data()), byteCount);
        ok &= Expect(source.good(), "external TAM could not be read");
        const auto externalResult = ParseTam(external, TRIAEVUM_RUNTIME_ABI_V1);
        ok &= Expect(externalResult.Ok(),
                     "external TAM did not pass the C++ reader");
      }
    }
  } else if (argc != 1) {
    ok &= Expect(false, "expected zero or one external TAM path");
  }
  return ok ? 0 : 1;
}
