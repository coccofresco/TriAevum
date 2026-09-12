#include "triaevum/module_abi.h"
#include "triaevum/native_module_loader.h"
#include "triaevum/runtime_session.h"
#include "triaevum/tam_format.h"

#include "triaevum/sha256.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
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

std::vector<std::uint8_t> ReadFile(const std::filesystem::path &path) {
  std::ifstream source(path, std::ios::binary | std::ios::ate);
  if (!source) {
    return {};
  }
  const std::streamsize size = source.tellg();
  if (size <= 0) {
    return {};
  }
  source.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
  source.read(reinterpret_cast<char *>(result.data()), size);
  return source ? result : std::vector<std::uint8_t>{};
}

std::string HashHex(const std::array<std::uint8_t, 32> &hash) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(hash.size() * 2U);
  for (const std::uint8_t value : hash) {
    result.push_back(digits[value >> 4U]);
    result.push_back(digits[value & 0x0FU]);
  }
  return result;
}

std::vector<std::uint8_t>
BuildTam(const std::vector<std::uint8_t> &nativeImage,
         bool mismatchedNativeSize = false) {
  std::string sourceIdentity(64U, '0');
  sourceIdentity[0] = '4';
  sourceIdentity[1] = '2';
  const std::string nativeHash = HashHex(detail::Sha256(nativeImage));
#if defined(__ANDROID__)
  constexpr std::string_view target = "aarch64-linux-android";
#elif defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
  constexpr std::string_view target = "aarch64-pc-windows-msvc";
#elif defined(_WIN32)
  constexpr std::string_view target = "x86_64-pc-windows-msvc";
#elif defined(__APPLE__) && defined(__aarch64__)
  constexpr std::string_view target = "aarch64-apple-darwin";
#elif defined(__APPLE__)
  constexpr std::string_view target = "x86_64-apple-darwin";
#elif defined(__aarch64__)
  constexpr std::string_view target = "aarch64-unknown-linux-gnu";
#else
  constexpr std::string_view target = "x86_64-unknown-linux-gnu";
#endif
  const std::string metadata =
      std::string("{\"forge_tool_version\":\"test\",") +
      "\"format\":\"triaevum_module_metadata_v1\"," +
      "\"native_image\":{\"bytes\":" +
      std::to_string(nativeImage.size() + (mismatchedNativeSize ? 1U : 0U)) +
      ",\"sha256\":\"" + nativeHash + "\"}," +
      "\"private_local_artifact\":true," +
      "\"query_symbol\":\"TriAevumQueryModuleV1\"," +
      "\"recipe\":\"fixture\",\"redistributable\":false," +
      "\"required_services\":[{\"id\":1413829460," +
      "\"schema_version\":1}]," +
      "\"runtime_abi\":1,\"source_identity_sha256\":\"" +
      sourceIdentity + "\",\"target_triple\":\"" + std::string(target) +
      "\",\"translator_identity_sha256\":\"" + std::string(64U, '1') +
      "\"}\n";
  const std::vector<std::uint8_t> metadataBytes(metadata.begin(),
                                                metadata.end());
  const std::size_t metadataOffset =
      Align(kTamHeaderSize + 2U * kTamSectionEntrySize);
  const std::size_t nativeOffset = Align(metadataOffset + metadataBytes.size());
  std::vector<std::uint8_t> bytes(nativeOffset + nativeImage.size(), 0U);
  std::copy(kTamMagic.begin(), kTamMagic.end(), bytes.begin());
  Write16(bytes, 8U, kTamFormatMajor);
  Write16(bytes, 10U, kTamFormatMinor);
  Write16(bytes, 12U, kTamHeaderSize);
  Write16(bytes, 14U, kTamSectionEntrySize);
  Write32(bytes, 16U, TRIAEVUM_RUNTIME_ABI_V1);
  Write32(bytes, 20U, 2U);
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
               metadataOffset, metadataBytes);
  writeSection(
      kTamHeaderSize + kTamSectionEntrySize, TamSectionType::NativeImage,
      TamSectionRequired | TamSectionExecutable, nativeOffset, nativeImage);
  return bytes;
}

bool WriteFile(const std::filesystem::path &path,
               std::span<const std::uint8_t> bytes) {
  std::ofstream destination(path, std::ios::binary | std::ios::out);
  destination.write(reinterpret_cast<const char *>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(destination);
}

bool Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

struct HostState {
  bool serviceInvoked = false;
};

void TRIAEVUM_ABI_CALL Log(void *, TriAevumLogLevelV1, const char *,
                           std::size_t) {}

std::uint64_t TRIAEVUM_ABI_CALL MonotonicTime(void *) { return 1234U; }

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
InvokeService(void *context, std::uint32_t service, std::uint32_t operation,
              TriAevumReadOnlyBytesV1, TriAevumMutableBytesV1,
              std::size_t *responseSize) {
  auto *state = static_cast<HostState *>(context);
  if (state == nullptr || service != 0x54455354U || operation != 1U ||
      responseSize == nullptr) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  state->serviceInvoked = true;
  *responseSize = 0;
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
TestService(void *context, std::uint32_t operation,
            TriAevumReadOnlyBytesV1 request, TriAevumMutableBytesV1 response,
            std::size_t *responseSize) {
  return InvokeService(context, 0x54455354U, operation, request, response,
                       responseSize);
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "expected the mock native-module path\n";
    return 1;
  }
  bool ok = true;
  const auto nativeImage = ReadFile(argv[1]);
  ok &= Expect(!nativeImage.empty(), "mock native module could not be read");
  const auto unique =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("triaevum-module-test-" + std::to_string(unique));
  const auto cache = root / "cache";
  const auto tamPath = root / "fixture.tam";
  const auto contentIndexPath = root / "content.tap";
  std::error_code filesystemError;
  std::filesystem::create_directories(root, filesystemError);
  ok &= Expect(!filesystemError, "test directory could not be created");
  auto tam = BuildTam(nativeImage);
  ok &= Expect(WriteFile(tamPath, tam), "test TAM could not be written");
  const std::string contentIndex =
      R"({"format":"triaevum_content_index_v1"})";
  ok &= Expect(
      WriteFile(contentIndexPath,
                std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t *>(contentIndex.data()),
                    contentIndex.size())),
      "private content index could not be written");

  ModuleLoadError loadError;
  auto module = LoadedNativeModule::Load(tamPath, cache, &loadError);
  ok &= Expect(module != nullptr,
               "valid native module did not load: " + loadError.message);
  HostState hostState;
  if (module != nullptr) {
    const TriAevumHostApiV1 host = {
        sizeof(TriAevumHostApiV1),
        TRIAEVUM_RUNTIME_ABI_V1,
        &hostState,
        Log,
        MonotonicTime,
        InvokeService,
    };
    ok &=
        Expect(module->Initialize(host, "content.tap") == TRIAEVUM_MODULE_OK_V1,
               "mock module initialization failed");
    ok &= Expect(hostState.serviceInvoked,
                 "mock module did not cross the host service ABI");
    const TriAevumGuestMemoryMapRequestV1 memoryRequest = {
        sizeof(TriAevumGuestMemoryMapRequestV1),
        TRIAEVUM_GUEST_MEMORY_READ_V1 | TRIAEVUM_GUEST_MEMORY_WRITE_V1,
        0x1010U,
        16U,
    };
    TriAevumGuestMemoryViewV1 memoryView{};
    ok &= Expect(module->MapGuestMemory(memoryRequest, &memoryView) ==
                         TRIAEVUM_MODULE_OK_V1 &&
                     memoryView.size >= memoryRequest.byte_count &&
                     memoryView.content_version != 0U,
                 "mock module did not expose checked guest memory");
    if (memoryView.data != nullptr) {
      memoryView.data[0] = 0xA5U;
    }
    ok &= Expect(module->UnmapGuestMemory(
                         memoryView.token,
                         TRIAEVUM_GUEST_MEMORY_UNMAP_WRITTEN_V1) ==
                     TRIAEVUM_MODULE_OK_V1,
                 "mock module guest memory did not unmap");
    const TriAevumFrameInputV1 input = {
        sizeof(TriAevumFrameInputV1), 0U, 100U, 16U, {nullptr, 0U},
    };
    ok &= Expect(module->RunFrame(input) == TRIAEVUM_MODULE_OK_V1,
                 "mock module frame failed");
    std::vector<std::uint8_t> state;
    ok &= Expect(module->SaveState(&state) == TRIAEVUM_MODULE_OK_V1 &&
                     state.size() == sizeof(std::uint32_t),
                 "mock module state save failed");
    const std::uint32_t restored = 7U;
    std::memcpy(state.data(), &restored, sizeof(restored));
    ok &= Expect(module->LoadState(state) == TRIAEVUM_MODULE_OK_V1,
                 "mock module state load failed");
    ok &= Expect(module->Metadata().recipe == "fixture",
                 "validated module metadata was not retained");
    module->Shutdown();
    ok &= Expect(!module->IsInitialized(),
                 "mock module did not shut down cleanly");
    module.reset();
  }

  RuntimeSessionError sessionError;
  RuntimeSession missingService({&hostState, Log, MonotonicTime});
  ok &= Expect(!missingService.Start(tamPath, cache, contentIndexPath,
                                     &sessionError) &&
                   sessionError.code ==
                       RuntimeSessionErrorCode::MissingHostService &&
                   sessionError.serviceId == 0x54455354U,
               "runtime session accepted a module with a missing service");

  RuntimeSession session({&hostState, Log, MonotonicTime});
  ok &= Expect(session.Load(tamPath, cache, contentIndexPath, &sessionError) &&
                   session.IsLoaded() && !session.IsRunning() &&
                   session.Metadata() != nullptr &&
                   session.Metadata()->recipe == "fixture",
               "runtime session did not expose a verified loaded module");
  ok &= Expect(session.RegisterService(0x54455354U, TestService, &hostState) ==
                   ServiceRegistrationResult::Registered,
               "runtime session service could not be registered after load");
  ok &= Expect(session.Initialize(&sessionError),
               "runtime session did not initialize: " + sessionError.message);
  const TriAevumFrameInputV1 sessionFrame = {
      sizeof(TriAevumFrameInputV1), 0U, 200U, 16U, {nullptr, 0U},
  };
  ok &= Expect(session.RunFrame(sessionFrame) == TRIAEVUM_MODULE_OK_V1,
               "runtime session did not execute a module frame");
  const TriAevumGuestMemoryMapRequestV1 sessionMemoryRequest = {
      sizeof(TriAevumGuestMemoryMapRequestV1),
      TRIAEVUM_GUEST_MEMORY_READ_V1,
      0x1000U,
      4U,
  };
  TriAevumGuestMemoryViewV1 sessionMemoryView{};
  ok &= Expect(session.MapGuestMemory(sessionMemoryRequest,
                                      &sessionMemoryView) ==
                         TRIAEVUM_MODULE_OK_V1 &&
                     session.UnmapGuestMemory(sessionMemoryView.token, 0U) ==
                         TRIAEVUM_MODULE_OK_V1,
                 "runtime session did not bridge checked guest memory");
  ok &= Expect(session.IsRunning() && session.IsLoaded(),
               "runtime session did not expose its running module identity");
  session.Stop();
  ok &= Expect(!session.IsRunning() && !session.IsLoaded(),
               "runtime session did not stop");

  tam.back() ^= 0x80U;
  const auto corruptPath = root / "corrupt.tam";
  ok &= Expect(WriteFile(corruptPath, tam), "corrupt TAM could not be written");
  auto corrupt = LoadedNativeModule::Load(corruptPath, cache, &loadError);
  ok &= Expect(corrupt == nullptr &&
                   loadError.code == ModuleLoadErrorCode::InvalidContainer,
               "corrupt TAM reached dynamic loading");

  const auto mismatchedPath = root / "mismatched-metadata.tam";
  tam = BuildTam(nativeImage, true);
  ok &= Expect(WriteFile(mismatchedPath, tam),
               "mismatched metadata TAM could not be written");
  auto mismatched =
      LoadedNativeModule::Load(mismatchedPath, cache, &loadError);
  ok &= Expect(mismatched == nullptr &&
                   loadError.code == ModuleLoadErrorCode::InvalidMetadata,
               "metadata/native-image mismatch reached dynamic loading");

  std::filesystem::remove_all(root, filesystemError);
  ok &= Expect(!filesystemError, "test directory cleanup failed");
  return ok ? 0 : 1;
}
