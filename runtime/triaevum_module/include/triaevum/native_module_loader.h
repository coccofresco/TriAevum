#ifndef TRIAEVUM_NATIVE_MODULE_LOADER_H
#define TRIAEVUM_NATIVE_MODULE_LOADER_H

#include "triaevum/module_abi.h"
#include "triaevum/tam_metadata.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace triaevum::module {

enum class ModuleLoadErrorCode {
  None,
  Io,
  InvalidContainer,
  InvalidMetadata,
  Extraction,
  DynamicLibrary,
  MissingQueryExport,
  IncompatibleModuleAbi,
  InvalidModuleApi,
};

struct ModuleLoadError {
  ModuleLoadErrorCode code = ModuleLoadErrorCode::None;
  std::string message;
};

class LoadedNativeModule {
public:
  ~LoadedNativeModule();
  LoadedNativeModule(LoadedNativeModule &&) = delete;
  LoadedNativeModule &operator=(LoadedNativeModule &&) = delete;
  LoadedNativeModule(const LoadedNativeModule &) = delete;
  LoadedNativeModule &operator=(const LoadedNativeModule &) = delete;

  [[nodiscard]] static std::unique_ptr<LoadedNativeModule>
  Load(const std::filesystem::path &tamPath,
       const std::filesystem::path &privateCacheDirectory,
       ModuleLoadError *error);

  [[nodiscard]] const TriAevumModuleApiV1 &Api() const;
  [[nodiscard]] const TamModuleMetadataV1 &Metadata() const;
  [[nodiscard]] const std::filesystem::path &ExtractedImagePath() const;
  [[nodiscard]] const std::filesystem::path &ExtractedTitleAotPath() const;
  [[nodiscard]] bool IsInitialized() const;

  TriAevumModuleStatusV1 Initialize(const TriAevumHostApiV1 &host,
                                    std::string_view privateContentIndex);
  TriAevumModuleStatusV1 RunFrame(const TriAevumFrameInputV1 &input);
  TriAevumModuleStatusV1 SaveState(std::vector<std::uint8_t> *state) const;
  TriAevumModuleStatusV1 LoadState(std::span<const std::uint8_t> state);
  TriAevumModuleStatusV1
  MapGuestMemory(const TriAevumGuestMemoryMapRequestV1 &request,
                 TriAevumGuestMemoryViewV1 *view) const;
  TriAevumModuleStatusV1 UnmapGuestMemory(std::uint64_t token,
                                          std::uint32_t flags) const;
  void Shutdown();

private:
  struct Impl;
  explicit LoadedNativeModule(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> mImpl;
};

} // namespace triaevum::module

#endif
