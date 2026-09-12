#include "triaevum/tam_metadata.h"

#include <cstdint>
#include <iostream>
#include <span>
#include <string>

namespace {

using namespace triaevum::module;

bool Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

TamMetadataParseResult Parse(const std::string &json) {
  return ParseTamMetadataV1(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t *>(json.data()), json.size()));
}

std::string ValidMetadata() {
  return R"({"format":"triaevum_module_metadata_v1","private_local_artifact":true,"redistributable":false,"runtime_abi":1,"query_symbol":"TriAevumQueryModuleV1","recipe":"fixture","target_triple":"x86_64-pc-windows-msvc","source_identity_sha256":"4242424242424242424242424242424242424242424242424242424242424242","translator_identity_sha256":"1111111111111111111111111111111111111111111111111111111111111111","native_image":{"bytes":42,"sha256":"2222222222222222222222222222222222222222222222222222222222222222"},"forge_tool_version":"test","required_services":[{"id":1094928720,"schema_version":1}],"physical_memory_regions":[{"physical_base":402653184,"guest_base":520093696,"bytes":6291456},{"physical_base":536870912,"guest_base":335544320,"bytes":134217728}]})";
}

} // namespace

int main() {
  bool ok = true;
#if defined(__ANDROID__) && defined(__aarch64__)
  ok &= Expect(TamTargetMatchesCurrentProcess("aarch64-linux-android"),
               "Android ARM64 target rejected");
  ok &= Expect(!TamTargetMatchesCurrentProcess("aarch64-unknown-linux-gnu") &&
                   !TamTargetMatchesCurrentProcess("x86_64-unknown-linux-gnu"),
               "Android accepted a desktop Linux ABI");
#else
  ok &= Expect(!TamTargetMatchesCurrentProcess("aarch64-linux-android"),
               "desktop host accepted an Android module");
#endif
  auto parsed = Parse(ValidMetadata());
  ok &= Expect(parsed.Ok() && parsed.metadata.runtimeAbi == 1U &&
                   parsed.metadata.nativeImageBytes == 42U &&
                   parsed.metadata.sourceIdentitySha256.front() == 0x42U &&
                   parsed.metadata.requiredServices.size() == 1U &&
                   parsed.metadata.physicalMemoryRegions.size() == 2U &&
                   parsed.metadata.physicalMemoryRegions.front().physicalBase ==
                       0x18000000U,
               "valid TAM metadata was not decoded");

  std::string duplicate = ValidMetadata();
  duplicate.insert(1U, R"("format":"duplicate",)");
  parsed = Parse(duplicate);
  ok &= Expect(!parsed.Ok() &&
                   parsed.error.code == TamMetadataErrorCode::InvalidJson,
               "duplicate metadata key was accepted");

  std::string publicArtifact = ValidMetadata();
  const std::string expected = R"("redistributable":false)";
  publicArtifact.replace(publicArtifact.find(expected), expected.size(),
                         R"("redistributable":true)");
  parsed = Parse(publicArtifact);
  ok &= Expect(!parsed.Ok() &&
                   parsed.error.code == TamMetadataErrorCode::InvalidField,
               "redistributable game module metadata was accepted");

  std::string badHash = ValidMetadata();
  const std::string hash = std::string(64U, '1');
  badHash.replace(badHash.find(hash), hash.size(), std::string(64U, 'G'));
  parsed = Parse(badHash);
  ok &= Expect(!parsed.Ok(), "invalid metadata hash was accepted");

  std::string invalidUtf8 = ValidMetadata();
  invalidUtf8.insert(invalidUtf8.find("fixture"), 1U,
                     static_cast<char>(0xFF));
  parsed = Parse(invalidUtf8);
  ok &= Expect(!parsed.Ok(), "invalid UTF-8 metadata was accepted");

  std::string missingRegions = ValidMetadata();
  const std::string regions =
      R"(,"physical_memory_regions":[{"physical_base":402653184,"guest_base":520093696,"bytes":6291456},{"physical_base":536870912,"guest_base":335544320,"bytes":134217728}])";
  missingRegions.erase(missingRegions.find(regions), regions.size());
  parsed = Parse(missingRegions);
  ok &= Expect(!parsed.Ok() &&
                   parsed.error.code == TamMetadataErrorCode::MissingField,
               "PICA metadata without physical mappings was accepted");

  std::string overlapping = ValidMetadata();
  const std::string secondGuest = R"("guest_base":335544320)";
  overlapping.replace(overlapping.find(secondGuest), secondGuest.size(),
                      R"("guest_base":520093696)");
  parsed = Parse(overlapping);
  ok &= Expect(!parsed.Ok() &&
                   parsed.error.code == TamMetadataErrorCode::InvalidField,
               "overlapping guest memory regions were accepted");
  return ok ? 0 : 1;
}
