#include "triaevum/native_module_loader.h"

#include "triaevum/sha256.h"
#include "triaevum/tam_metadata.h"
#include "triaevum/tam_reader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace triaevum::module {
namespace {

void SetError(ModuleLoadError *error, ModuleLoadErrorCode code,
              std::string message) {
  if (error != nullptr) {
    *error = {code, std::move(message)};
  }
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

std::string BundleCacheKey(const std::array<std::uint8_t, 32> &nativeHash,
                           const std::array<std::uint8_t, 32> &titleHash) {
  std::array<std::uint8_t, 64> identity{};
  std::copy(nativeHash.begin(), nativeHash.end(), identity.begin());
  std::copy(titleHash.begin(), titleHash.end(), identity.begin() + 32U);
  return HashHex(detail::Sha256(identity)).substr(0U, 32U);
}

class MappedFile {
public:
  ~MappedFile() { Close(); }
  MappedFile(const MappedFile &) = delete;
  MappedFile &operator=(const MappedFile &) = delete;

  MappedFile(MappedFile &&other) noexcept { MoveFrom(std::move(other)); }
  MappedFile &operator=(MappedFile &&other) noexcept {
    if (this != &other) {
      Close();
      MoveFrom(std::move(other));
    }
    return *this;
  }

  static std::unique_ptr<MappedFile> Open(const std::filesystem::path &path,
                                          std::string *error) {
    auto result = std::unique_ptr<MappedFile>(new MappedFile());
#if defined(_WIN32)
    result->mFile =
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (result->mFile == INVALID_HANDLE_VALUE) {
      if (error != nullptr) {
        *error =
            "CreateFileW failed with error " + std::to_string(GetLastError());
      }
      return nullptr;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(result->mFile, &size) || size.QuadPart <= 0 ||
        static_cast<unsigned long long>(size.QuadPart) >
            std::numeric_limits<std::size_t>::max()) {
      if (error != nullptr) {
        *error = "mapped file has an invalid size";
      }
      return nullptr;
    }
    result->mMapping = CreateFileMappingW(result->mFile, nullptr, PAGE_READONLY,
                                          0, 0, nullptr);
    if (result->mMapping == nullptr) {
      if (error != nullptr) {
        *error = "CreateFileMappingW failed with error " +
                 std::to_string(GetLastError());
      }
      return nullptr;
    }
    result->mData = static_cast<const std::uint8_t *>(
        MapViewOfFile(result->mMapping, FILE_MAP_READ, 0, 0, 0));
    if (result->mData == nullptr) {
      if (error != nullptr) {
        *error =
            "MapViewOfFile failed with error " + std::to_string(GetLastError());
      }
      return nullptr;
    }
    result->mSize = static_cast<std::size_t>(size.QuadPart);
#else
    result->mFile = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (result->mFile < 0) {
      if (error != nullptr) {
        *error = "open failed";
      }
      return nullptr;
    }
    struct stat status{};
    if (fstat(result->mFile, &status) != 0 || status.st_size <= 0 ||
        static_cast<std::uint64_t>(status.st_size) >
            std::numeric_limits<std::size_t>::max()) {
      if (error != nullptr) {
        *error = "mapped file has an invalid size";
      }
      return nullptr;
    }
    void *mapping = mmap(nullptr, static_cast<std::size_t>(status.st_size),
                         PROT_READ, MAP_PRIVATE, result->mFile, 0);
    if (mapping == MAP_FAILED) {
      if (error != nullptr) {
        *error = "mmap failed";
      }
      return nullptr;
    }
    result->mData = static_cast<const std::uint8_t *>(mapping);
    result->mSize = static_cast<std::size_t>(status.st_size);
#endif
    return result;
  }

  [[nodiscard]] std::span<const std::uint8_t> Bytes() const {
    return {mData, mSize};
  }

private:
  MappedFile() = default;

  void Close() {
#if defined(_WIN32)
    if (mData != nullptr) {
      UnmapViewOfFile(mData);
    }
    if (mMapping != nullptr) {
      CloseHandle(mMapping);
    }
    if (mFile != INVALID_HANDLE_VALUE) {
      CloseHandle(mFile);
    }
    mFile = INVALID_HANDLE_VALUE;
    mMapping = nullptr;
#else
    if (mData != nullptr) {
      munmap(const_cast<std::uint8_t *>(mData), mSize);
    }
    if (mFile >= 0) {
      close(mFile);
    }
    mFile = -1;
#endif
    mData = nullptr;
    mSize = 0;
  }

  void MoveFrom(MappedFile &&other) {
    mData = std::exchange(other.mData, nullptr);
    mSize = std::exchange(other.mSize, 0U);
#if defined(_WIN32)
    mFile = std::exchange(other.mFile, INVALID_HANDLE_VALUE);
    mMapping = std::exchange(other.mMapping, nullptr);
#else
    mFile = std::exchange(other.mFile, -1);
#endif
  }

  const std::uint8_t *mData = nullptr;
  std::size_t mSize = 0;
#if defined(_WIN32)
  HANDLE mFile = INVALID_HANDLE_VALUE;
  HANDLE mMapping = nullptr;
#else
  int mFile = -1;
#endif
};

bool VerifyFileHash(const std::filesystem::path &path,
                    const std::array<std::uint8_t, 32> &expected) {
  std::string ignored;
  auto mapping = MappedFile::Open(path, &ignored);
  return mapping != nullptr && detail::Sha256(mapping->Bytes()) == expected;
}

std::filesystem::path ExtractImage(const TamSectionView &image,
                                   const std::filesystem::path &output,
                                   ModuleLoadError *error) {
  std::error_code filesystemError;
  std::filesystem::create_directories(output.parent_path(), filesystemError);
  if (filesystemError) {
    SetError(error, ModuleLoadErrorCode::Extraction,
             "cannot create private module cache: " +
                 filesystemError.message());
    return {};
  }
  if (std::filesystem::exists(output)) {
    if (VerifyFileHash(output, image.sha256)) {
      return output;
    }
    SetError(error, ModuleLoadErrorCode::Extraction,
             "existing module cache file has the wrong identity");
    return {};
  }

  const std::filesystem::path temporary = output.string() + ".tmp";
  if (std::filesystem::exists(temporary)) {
    SetError(error, ModuleLoadErrorCode::Extraction,
             "stale private module extraction is present");
    return {};
  }
  {
    if (image.bytes.size() >
        static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
      SetError(error, ModuleLoadErrorCode::Extraction,
               "native module image is too large to extract");
      return {};
    }
    std::ofstream destination(temporary, std::ios::binary | std::ios::out);
    if (!destination) {
      SetError(error, ModuleLoadErrorCode::Extraction,
               "cannot create private module extraction");
      return {};
    }
    destination.write(reinterpret_cast<const char *>(image.bytes.data()),
                      static_cast<std::streamsize>(image.bytes.size()));
    destination.flush();
    if (!destination) {
      destination.close();
      std::filesystem::remove(temporary, filesystemError);
      SetError(error, ModuleLoadErrorCode::Extraction,
               "cannot write private module extraction");
      return {};
    }
  }
  if (!VerifyFileHash(temporary, image.sha256)) {
    std::filesystem::remove(temporary, filesystemError);
    SetError(error, ModuleLoadErrorCode::Extraction,
             "private module extraction failed hash verification");
    return {};
  }
  std::filesystem::rename(temporary, output, filesystemError);
  if (filesystemError) {
    std::filesystem::remove(temporary, filesystemError);
    SetError(error, ModuleLoadErrorCode::Extraction,
             "cannot publish private module extraction");
    return {};
  }
  return output;
}

bool HasNonzeroIdentity(const TriAevumModuleApiV1 &api) {
  return std::any_of(std::begin(api.module_identity_sha256),
                     std::end(api.module_identity_sha256),
                     [](std::uint8_t value) { return value != 0U; });
}

} // namespace

struct LoadedNativeModule::Impl {
  std::filesystem::path extractedImage;
  std::filesystem::path extractedTitleAot;
  TamModuleMetadataV1 metadata;
  const TriAevumModuleApiV1 *api = nullptr;
  bool initialized = false;
#if defined(_WIN32)
  HMODULE library = nullptr;
#else
  void *library = nullptr;
#endif

  ~Impl() {
#if defined(_WIN32)
    if (library != nullptr) {
      FreeLibrary(library);
    }
#else
    if (library != nullptr) {
      dlclose(library);
    }
#endif
  }
};

LoadedNativeModule::LoadedNativeModule(std::unique_ptr<Impl> impl)
    : mImpl(std::move(impl)) {}

LoadedNativeModule::~LoadedNativeModule() { Shutdown(); }

std::unique_ptr<LoadedNativeModule>
LoadedNativeModule::Load(const std::filesystem::path &tamPath,
                         const std::filesystem::path &privateCacheDirectory,
                         ModuleLoadError *error) {
  if (error != nullptr) {
    *error = {};
  }
  std::string mapError;
  auto mapped = MappedFile::Open(tamPath, &mapError);
  if (mapped == nullptr) {
    SetError(error, ModuleLoadErrorCode::Io,
             "cannot map TAM input: " + mapError);
    return nullptr;
  }
  const TamParseResult parsed =
      ParseTam(mapped->Bytes(), TRIAEVUM_RUNTIME_ABI_V1);
  if (!parsed.Ok()) {
    SetError(error, ModuleLoadErrorCode::InvalidContainer,
             parsed.error.message);
    return nullptr;
  }
  const TamSectionView *nativeImage =
      parsed.module.Find(TamSectionType::NativeImage);
  const TamSectionView *metadataSection =
      parsed.module.Find(TamSectionType::MetadataJson);
  const TamSectionView *titleAotImage =
      parsed.module.Find(TamSectionType::TitleAotImage);
  if (nativeImage == nullptr || metadataSection == nullptr) {
    SetError(error, ModuleLoadErrorCode::InvalidContainer,
             "validated TAM has no native image or metadata");
    return nullptr;
  }
  const TamMetadataParseResult metadata =
      ParseTamMetadataV1(metadataSection->bytes);
  if (!metadata.Ok()) {
    SetError(error, ModuleLoadErrorCode::InvalidMetadata,
             metadata.error.message);
    return nullptr;
  }
  if (metadata.metadata.runtimeAbi != parsed.module.runtimeAbi ||
      metadata.metadata.nativeImageBytes != nativeImage->bytes.size() ||
      metadata.metadata.nativeImageSha256 != nativeImage->sha256 ||
      metadata.metadata.hasTitleAotImage != (titleAotImage != nullptr) ||
      (titleAotImage != nullptr &&
       (metadata.metadata.titleAotAbi != 1U ||
        metadata.metadata.titleAotImageBytes != titleAotImage->bytes.size() ||
        metadata.metadata.titleAotImageSha256 != titleAotImage->sha256)) ||
      !TamTargetMatchesCurrentProcess(metadata.metadata.targetTriple)) {
    SetError(error, ModuleLoadErrorCode::InvalidMetadata,
             "TAM metadata does not match the container or current target");
    return nullptr;
  }
  std::filesystem::path extracted;
  std::filesystem::path extractedTitleAot;
#if defined(_WIN32)
  constexpr std::string_view extension = ".dll";
#elif defined(__APPLE__)
  constexpr std::string_view extension = ".dylib";
#else
  constexpr std::string_view extension = ".so";
#endif
  if (titleAotImage != nullptr) {
    const std::filesystem::path bundleDirectory =
        privateCacheDirectory /
        ("bundle-" +
         BundleCacheKey(nativeImage->sha256, titleAotImage->sha256));
    extractedTitleAot = ExtractImage(
        *titleAotImage,
        bundleDirectory / ("triaevum_title_aot" + std::string(extension)),
        error);
    if (extractedTitleAot.empty()) {
      return nullptr;
    }
    extracted = ExtractImage(*nativeImage,
                             bundleDirectory /
                                 ("oot3d_game_module" + std::string(extension)),
                             error);
  } else {
    extracted = ExtractImage(
        *nativeImage,
        privateCacheDirectory /
            ("module-" + HashHex(nativeImage->sha256) + std::string(extension)),
        error);
  }
  if (extracted.empty()) {
    return nullptr;
  }

  auto impl = std::make_unique<Impl>();
  impl->extractedImage = extracted;
  impl->extractedTitleAot = extractedTitleAot;
  impl->metadata = metadata.metadata;
#if defined(_WIN32)
  impl->library = LoadLibraryExW(extracted.c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                     LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (impl->library == nullptr) {
    SetError(error, ModuleLoadErrorCode::DynamicLibrary,
             "LoadLibraryExW failed with error " +
                 std::to_string(GetLastError()));
    return nullptr;
  }
  const auto query = reinterpret_cast<TriAevumQueryModuleApiV1>(
      GetProcAddress(impl->library, TRIAEVUM_MODULE_QUERY_SYMBOL_V1));
#else
  impl->library = dlopen(extracted.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (impl->library == nullptr) {
    SetError(error, ModuleLoadErrorCode::DynamicLibrary, dlerror());
    return nullptr;
  }
  const auto query = reinterpret_cast<TriAevumQueryModuleApiV1>(
      dlsym(impl->library, TRIAEVUM_MODULE_QUERY_SYMBOL_V1));
#endif
  if (query == nullptr) {
    SetError(error, ModuleLoadErrorCode::MissingQueryExport,
             "native module has no TriAevumQueryModuleV1 export");
    return nullptr;
  }
  impl->api = query(TRIAEVUM_MODULE_QUERY_ABI_V1, TRIAEVUM_RUNTIME_ABI_V1);
  if (impl->api == nullptr ||
      impl->api->abi_version != TRIAEVUM_RUNTIME_ABI_V1) {
    SetError(error, ModuleLoadErrorCode::IncompatibleModuleAbi,
             "native module rejected the runtime ABI");
    return nullptr;
  }
  if (impl->api->struct_size < sizeof(TriAevumModuleApiV1) ||
      impl->api->initialize == nullptr || impl->api->run_frame == nullptr ||
      impl->api->shutdown == nullptr || impl->api->state_size == nullptr ||
      impl->api->save_state == nullptr || impl->api->load_state == nullptr ||
      impl->api->map_guest_memory == nullptr ||
      impl->api->unmap_guest_memory == nullptr ||
      !HasNonzeroIdentity(*impl->api)) {
    SetError(error, ModuleLoadErrorCode::InvalidModuleApi,
             "native module API is incomplete");
    return nullptr;
  }
  if (!std::equal(std::begin(impl->api->module_identity_sha256),
                  std::end(impl->api->module_identity_sha256),
                  impl->metadata.sourceIdentitySha256.begin())) {
    SetError(error, ModuleLoadErrorCode::InvalidModuleApi,
             "native module identity does not match TAM source identity");
    return nullptr;
  }
  return std::unique_ptr<LoadedNativeModule>(
      new LoadedNativeModule(std::move(impl)));
}

const TriAevumModuleApiV1 &LoadedNativeModule::Api() const {
  return *mImpl->api;
}

const TamModuleMetadataV1 &LoadedNativeModule::Metadata() const {
  return mImpl->metadata;
}

const std::filesystem::path &LoadedNativeModule::ExtractedImagePath() const {
  return mImpl->extractedImage;
}

const std::filesystem::path &LoadedNativeModule::ExtractedTitleAotPath() const {
  return mImpl->extractedTitleAot;
}

bool LoadedNativeModule::IsInitialized() const { return mImpl->initialized; }

TriAevumModuleStatusV1
LoadedNativeModule::Initialize(const TriAevumHostApiV1 &host,
                               std::string_view privateContentIndex) {
  if (mImpl->initialized || host.struct_size < sizeof(TriAevumHostApiV1) ||
      host.abi_version != TRIAEVUM_RUNTIME_ABI_V1 || host.log == nullptr ||
      host.monotonic_time_ns == nullptr || host.invoke_service == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  const TriAevumModuleStatusV1 status = mImpl->api->initialize(
      &host, privateContentIndex.data(), privateContentIndex.size());
  mImpl->initialized = status == TRIAEVUM_MODULE_OK_V1;
  return status;
}

TriAevumModuleStatusV1
LoadedNativeModule::RunFrame(const TriAevumFrameInputV1 &input) {
  if (!mImpl->initialized || input.struct_size < sizeof(TriAevumFrameInputV1)) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  return mImpl->api->run_frame(&input);
}

TriAevumModuleStatusV1
LoadedNativeModule::SaveState(std::vector<std::uint8_t> *state) const {
  if (!mImpl->initialized || state == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  const std::size_t size = mImpl->api->state_size();
  state->assign(size, 0U);
  std::size_t written = 0;
  const TriAevumModuleStatusV1 status =
      mImpl->api->save_state({state->data(), state->size()}, &written);
  if (status != TRIAEVUM_MODULE_OK_V1 || written > state->size()) {
    state->clear();
    return status == TRIAEVUM_MODULE_OK_V1 ? TRIAEVUM_MODULE_TITLE_ERROR_V1
                                           : status;
  }
  state->resize(written);
  return status;
}

TriAevumModuleStatusV1
LoadedNativeModule::LoadState(std::span<const std::uint8_t> state) {
  if (!mImpl->initialized) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  return mImpl->api->load_state({state.data(), state.size()});
}

TriAevumModuleStatusV1 LoadedNativeModule::MapGuestMemory(
    const TriAevumGuestMemoryMapRequestV1 &request,
    TriAevumGuestMemoryViewV1 *view) const {
  if (view == nullptr ||
      request.struct_size < sizeof(TriAevumGuestMemoryMapRequestV1) ||
      request.byte_count == 0U ||
      (request.access & ~(TRIAEVUM_GUEST_MEMORY_READ_V1 |
                          TRIAEVUM_GUEST_MEMORY_WRITE_V1)) != 0U ||
      request.access == 0U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  *view = {};
  view->struct_size = sizeof(TriAevumGuestMemoryViewV1);
  const TriAevumModuleStatusV1 status =
      mImpl->api->map_guest_memory(&request, view);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    *view = {};
    return status;
  }
  if (view->struct_size < sizeof(TriAevumGuestMemoryViewV1) ||
      view->data == nullptr || view->size < request.byte_count ||
      view->token == 0U || (view->access & request.access) != request.access) {
    *view = {};
    return TRIAEVUM_MODULE_TITLE_ERROR_V1;
  }
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1
LoadedNativeModule::UnmapGuestMemory(std::uint64_t token,
                                     std::uint32_t flags) const {
  if (token == 0U || (flags & ~TRIAEVUM_GUEST_MEMORY_UNMAP_WRITTEN_V1) != 0U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  return mImpl->api->unmap_guest_memory(token, flags);
}

void LoadedNativeModule::Shutdown() {
  if (mImpl != nullptr && mImpl->initialized) {
    mImpl->api->shutdown();
    mImpl->initialized = false;
  }
}

} // namespace triaevum::module
