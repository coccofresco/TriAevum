#include "triaevum/runtime_session.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace triaevum::module {
namespace {

constexpr std::uint64_t kMaximumPrivateContentIndexBytes = 4U * 1024U * 1024U;

void ClearError(RuntimeSessionError *error) {
  if (error != nullptr) {
    *error = {};
  }
}

void SetError(RuntimeSessionError *error, RuntimeSessionErrorCode code,
              std::string message, std::uint32_t serviceId = 0U) {
  if (error != nullptr) {
    error->code = code;
    error->message = std::move(message);
    error->serviceId = serviceId;
  }
}

bool ReadPrivateContentIndex(const std::filesystem::path &path,
                             std::vector<std::uint8_t> *destination,
                             std::string *error) {
  std::error_code filesystemError;
  if (!std::filesystem::is_regular_file(path, filesystemError) ||
      filesystemError) {
    *error = "private content index is not a regular file";
    return false;
  }
  const std::uintmax_t size = std::filesystem::file_size(path, filesystemError);
  if (filesystemError || size == 0U ||
      size > kMaximumPrivateContentIndexBytes ||
      size > std::numeric_limits<std::size_t>::max()) {
    *error = "private content index size is invalid";
    return false;
  }
  std::ifstream source(path, std::ios::binary | std::ios::in);
  if (!source) {
    *error = "private content index could not be opened";
    return false;
  }
  destination->assign(static_cast<std::size_t>(size), 0U);
  source.read(reinterpret_cast<char *>(destination->data()),
              static_cast<std::streamsize>(destination->size()));
  if (!source) {
    destination->clear();
    *error = "private content index could not be read completely";
    return false;
  }
  return true;
}

} // namespace

RuntimeSession::RuntimeSession(HostRuntimeCallbacksV1 callbacks)
    : mServices(callbacks) {}

RuntimeSession::~RuntimeSession() { Stop(); }

ServiceRegistrationResult
RuntimeSession::RegisterService(std::uint32_t serviceId,
                                HostServiceHandlerV1 handler,
                                void *serviceContext) {
  return mServices.Register(serviceId, handler, serviceContext);
}

bool RuntimeSession::Load(const std::filesystem::path &tamPath,
                          const std::filesystem::path &privateCacheDirectory,
                          const std::filesystem::path &privateContentIndex,
                          RuntimeSessionError *error) {
  ClearError(error);
  if (mModule != nullptr) {
    SetError(error, RuntimeSessionErrorCode::AlreadyStarted,
             "runtime session already has a loaded module");
    return false;
  }

  std::string contentError;
  if (!ReadPrivateContentIndex(privateContentIndex, &mPrivateContentIndex,
                               &contentError)) {
    SetError(error, RuntimeSessionErrorCode::ContentIndexIo,
             std::move(contentError));
    return false;
  }

  ModuleLoadError moduleError;
  auto candidate =
      LoadedNativeModule::Load(tamPath, privateCacheDirectory, &moduleError);
  if (candidate == nullptr) {
    SetError(error, RuntimeSessionErrorCode::ModuleLoad, moduleError.message);
    if (error != nullptr) {
      error->moduleLoad = std::move(moduleError);
    }
    mPrivateContentIndex.clear();
    return false;
  }
  mModule = std::move(candidate);
  return true;
}

bool RuntimeSession::Initialize(RuntimeSessionError *error) {
  ClearError(error);
  if (mModule == nullptr) {
    SetError(error, RuntimeSessionErrorCode::NotLoaded,
             "runtime session has no loaded module");
    return false;
  }
  if (mModule->IsInitialized()) {
    SetError(error, RuntimeSessionErrorCode::AlreadyStarted,
             "runtime session is already initialized");
    return false;
  }
  for (const auto &required : mModule->Metadata().requiredServices) {
    if (!mServices.Has(required.id)) {
      SetError(error, RuntimeSessionErrorCode::MissingHostService,
               "runtime does not provide a service required by the module",
               required.id);
      return false;
    }
  }

  mHostApi = mServices.SealAndCreateHostApi();
  const std::string_view content(
      reinterpret_cast<const char *>(mPrivateContentIndex.data()),
      mPrivateContentIndex.size());
  const TriAevumModuleStatusV1 status = mModule->Initialize(mHostApi, content);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    SetError(error, RuntimeSessionErrorCode::ModuleInitialize,
             "private game module initialization failed");
    mPrivateContentIndex.clear();
    mModule.reset();
    return false;
  }
  return true;
}

bool RuntimeSession::Start(const std::filesystem::path &tamPath,
                           const std::filesystem::path &privateCacheDirectory,
                           const std::filesystem::path &privateContentIndex,
                           RuntimeSessionError *error) {
  if (!Load(tamPath, privateCacheDirectory, privateContentIndex, error)) {
    return false;
  }
  if (!Initialize(error)) {
    Stop();
    return false;
  }
  return true;
}

void RuntimeSession::Stop() {
  if (mModule != nullptr) {
    mModule->Shutdown();
    mModule.reset();
  }
  mPrivateContentIndex.clear();
}

bool RuntimeSession::IsLoaded() const { return mModule != nullptr; }

bool RuntimeSession::IsRunning() const {
  return mModule != nullptr && mModule->IsInitialized();
}

const TamModuleMetadataV1 *RuntimeSession::Metadata() const {
  return mModule == nullptr ? nullptr : &mModule->Metadata();
}

TriAevumModuleStatusV1
RuntimeSession::RunFrame(const TriAevumFrameInputV1 &input) {
  return mModule == nullptr ? TRIAEVUM_MODULE_INVALID_ARGUMENT_V1
                            : mModule->RunFrame(input);
}

TriAevumModuleStatusV1
RuntimeSession::SaveState(std::vector<std::uint8_t> *state) const {
  return mModule == nullptr ? TRIAEVUM_MODULE_INVALID_ARGUMENT_V1
                            : mModule->SaveState(state);
}

TriAevumModuleStatusV1
RuntimeSession::LoadState(std::span<const std::uint8_t> state) {
  return mModule == nullptr ? TRIAEVUM_MODULE_INVALID_ARGUMENT_V1
                            : mModule->LoadState(state);
}

TriAevumModuleStatusV1 RuntimeSession::MapGuestMemory(
    const TriAevumGuestMemoryMapRequestV1 &request,
    TriAevumGuestMemoryViewV1 *view) const {
  return mModule == nullptr ? TRIAEVUM_MODULE_INVALID_ARGUMENT_V1
                            : mModule->MapGuestMemory(request, view);
}

TriAevumModuleStatusV1
RuntimeSession::UnmapGuestMemory(std::uint64_t token,
                                 std::uint32_t flags) const {
  return mModule == nullptr ? TRIAEVUM_MODULE_INVALID_ARGUMENT_V1
                            : mModule->UnmapGuestMemory(token, flags);
}

} // namespace triaevum::module
