#include "oot3d_game_module_runtime.h"

#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <string_view>

namespace {

std::unique_ptr<Oot3dNativeGame::Oot3dGameModuleRuntime> gRuntime;
TriAevumHostApiV1 gHost{};

void Log(TriAevumLogLevelV1 level, std::string_view message) noexcept {
  try {
    if (gHost.log != nullptr) {
      gHost.log(gHost.host_context, level, message.data(), message.size());
    }
  } catch (...) {
  }
}

template <typename Callback>
TriAevumModuleStatusV1 GuardStatus(Callback &&callback) noexcept {
  try {
    return callback();
  } catch (const std::exception &exception) {
    Log(TRIAEVUM_LOG_ERROR_V1, exception.what());
  } catch (...) {
    Log(TRIAEVUM_LOG_ERROR_V1, "unhandled non-standard module exception");
  }
  return TRIAEVUM_MODULE_TITLE_ERROR_V1;
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
Initialize(const TriAevumHostApiV1 *host, const char *privateContentIndex,
           size_t privateContentIndexSize) noexcept {
  return GuardStatus([&]() -> TriAevumModuleStatusV1 {
    if (gRuntime != nullptr || host == nullptr ||
        host->struct_size < sizeof(TriAevumHostApiV1) ||
        host->abi_version != TRIAEVUM_RUNTIME_ABI_V1 ||
        privateContentIndex == nullptr || privateContentIndexSize == 0U) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    gHost = *host;
    std::string error;
    auto runtime = Oot3dNativeGame::Oot3dGameModuleRuntime::Create(
        gHost, std::string_view(privateContentIndex, privateContentIndexSize),
        &error);
    if (runtime == nullptr) {
      Log(TRIAEVUM_LOG_ERROR_V1, error);
      gHost = {};
      return TRIAEVUM_MODULE_TITLE_ERROR_V1;
    }
    gRuntime = std::move(runtime);
    const TriAevumModuleStatusV1 status = gRuntime->Start(&error);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      Log(TRIAEVUM_LOG_ERROR_V1, error);
      gRuntime.reset();
      gHost = {};
    }
    return status;
  });
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
RunFrame(const TriAevumFrameInputV1 *input) noexcept {
  return GuardStatus([&]() -> TriAevumModuleStatusV1 {
    if (gRuntime == nullptr || input == nullptr) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    std::string error;
    const TriAevumModuleStatusV1 status = gRuntime->RunFrame(*input, &error);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      Log(TRIAEVUM_LOG_ERROR_V1, error);
    }
    return status;
  });
}

void TRIAEVUM_ABI_CALL Shutdown() noexcept {
  try {
    gRuntime.reset();
    gHost = {};
  } catch (...) {
    gRuntime.release();
    gHost = {};
  }
}

size_t TRIAEVUM_ABI_CALL StateSize() noexcept {
  try {
    if (gRuntime == nullptr) {
      return 0U;
    }
    std::string error;
    const size_t size = gRuntime->StateSize(&error);
    if (size == 0U && !error.empty()) {
      Log(TRIAEVUM_LOG_ERROR_V1, error);
    }
    return size;
  } catch (const std::exception &exception) {
    Log(TRIAEVUM_LOG_ERROR_V1, exception.what());
  } catch (...) {
    Log(TRIAEVUM_LOG_ERROR_V1, "unhandled state-size exception");
  }
  return 0U;
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
SaveState(TriAevumMutableBytesV1 destination, size_t *writtenSize) noexcept {
  return GuardStatus([&]() -> TriAevumModuleStatusV1 {
    if (gRuntime == nullptr) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    std::string error;
    const TriAevumModuleStatusV1 status =
        gRuntime->SaveState(destination, writtenSize, &error);
    if (status != TRIAEVUM_MODULE_OK_V1 && !error.empty()) {
      Log(TRIAEVUM_LOG_ERROR_V1, error);
    }
    return status;
  });
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
LoadState(TriAevumReadOnlyBytesV1 source) noexcept {
  return GuardStatus([&]() -> TriAevumModuleStatusV1 {
    if (gRuntime == nullptr) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    std::string error;
    const TriAevumModuleStatusV1 status = gRuntime->LoadState(source, &error);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      Log(TRIAEVUM_LOG_ERROR_V1, error);
    }
    return status;
  });
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
MapGuestMemory(const TriAevumGuestMemoryMapRequestV1 *request,
               TriAevumGuestMemoryViewV1 *view) noexcept {
  return GuardStatus([&]() -> TriAevumModuleStatusV1 {
    return gRuntime == nullptr || request == nullptr
               ? TRIAEVUM_MODULE_INVALID_ARGUMENT_V1
               : gRuntime->MapGuestMemory(*request, view);
  });
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
UnmapGuestMemory(uint64_t token, uint32_t flags) noexcept {
  return GuardStatus([&]() -> TriAevumModuleStatusV1 {
    return gRuntime == nullptr ? TRIAEVUM_MODULE_INVALID_ARGUMENT_V1
                               : gRuntime->UnmapGuestMemory(token, flags);
  });
}

const TriAevumModuleApiV1 kModuleApi = [] {
  TriAevumModuleApiV1 api{};
  api.struct_size = sizeof(api);
  api.abi_version = TRIAEVUM_RUNTIME_ABI_V1;
  std::memcpy(api.module_identity_sha256,
              Oot3dNativeGame::kOot3dGameModuleIdentity.data(),
              Oot3dNativeGame::kOot3dGameModuleIdentity.size());
  api.initialize = Initialize;
  api.run_frame = RunFrame;
  api.shutdown = Shutdown;
  api.state_size = StateSize;
  api.save_state = SaveState;
  api.load_state = LoadState;
  api.map_guest_memory = MapGuestMemory;
  api.unmap_guest_memory = UnmapGuestMemory;
  return api;
}();

} // namespace

extern "C" TRIAEVUM_MODULE_EXPORT const TriAevumModuleApiV1 *TRIAEVUM_ABI_CALL
TriAevumQueryModuleV1(uint32_t queryAbi, uint32_t runtimeAbi) noexcept {
  if (queryAbi != TRIAEVUM_MODULE_QUERY_ABI_V1 ||
      runtimeAbi != TRIAEVUM_RUNTIME_ABI_V1) {
    return nullptr;
  }
  return &kModuleApi;
}
