#ifndef TRIAEVUM_RUNTIME_SESSION_H
#define TRIAEVUM_RUNTIME_SESSION_H

#include "triaevum/native_module_loader.h"
#include "triaevum/service_registry.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace triaevum::module {

enum class RuntimeSessionErrorCode {
  None,
  AlreadyStarted,
  NotLoaded,
  ContentIndexIo,
  ModuleLoad,
  MissingHostService,
  ModuleInitialize,
};

struct RuntimeSessionError {
  RuntimeSessionErrorCode code = RuntimeSessionErrorCode::None;
  std::string message;
  std::uint32_t serviceId = 0U;
  ModuleLoadError moduleLoad;
};

class RuntimeSession final {
public:
  explicit RuntimeSession(HostRuntimeCallbacksV1 callbacks);
  ~RuntimeSession();

  RuntimeSession(const RuntimeSession &) = delete;
  RuntimeSession &operator=(const RuntimeSession &) = delete;

  ServiceRegistrationResult RegisterService(std::uint32_t serviceId,
                                            HostServiceHandlerV1 handler,
                                            void *serviceContext);

  bool Load(const std::filesystem::path &tamPath,
            const std::filesystem::path &privateCacheDirectory,
            const std::filesystem::path &privateContentIndex,
            RuntimeSessionError *error);
  bool Initialize(RuntimeSessionError *error);
  bool Start(const std::filesystem::path &tamPath,
             const std::filesystem::path &privateCacheDirectory,
             const std::filesystem::path &privateContentIndex,
             RuntimeSessionError *error);
  void Stop();

  [[nodiscard]] bool IsLoaded() const;
  [[nodiscard]] bool IsRunning() const;
  [[nodiscard]] const TamModuleMetadataV1 *Metadata() const;

  TriAevumModuleStatusV1 RunFrame(const TriAevumFrameInputV1 &input);
  TriAevumModuleStatusV1 SaveState(std::vector<std::uint8_t> *state) const;
  TriAevumModuleStatusV1 LoadState(std::span<const std::uint8_t> state);
  TriAevumModuleStatusV1 MapGuestMemory(
      const TriAevumGuestMemoryMapRequestV1 &request,
      TriAevumGuestMemoryViewV1 *view) const;
  TriAevumModuleStatusV1 UnmapGuestMemory(std::uint64_t token,
                                         std::uint32_t flags) const;

private:
  HostServiceRegistry mServices;
  TriAevumHostApiV1 mHostApi{};
  std::unique_ptr<LoadedNativeModule> mModule;
  std::vector<std::uint8_t> mPrivateContentIndex;
};

} // namespace triaevum::module

#endif
