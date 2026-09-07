#include "triaevum/rooted_filesystem_backend.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>

namespace triaevum::module {
namespace {

void SetError(std::string *error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

std::optional<std::string> NormalizeRelativePath(std::string_view encoded) {
  if (encoded.empty() || encoded.find('\0') != std::string_view::npos ||
      encoded.find('\\') != std::string_view::npos || encoded.front() == '/' ||
      encoded.find(':') != std::string_view::npos) {
    return std::nullopt;
  }
  std::filesystem::path path(encoded);
  if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
    return std::nullopt;
  }
  for (const auto &component : path) {
    if (component.empty() || component == "." || component == "..") {
      return std::nullopt;
    }
  }
  const std::string normalized = path.lexically_normal().generic_string();
  return normalized.empty() ? std::nullopt
                            : std::optional<std::string>(normalized);
}

bool PathWithin(const std::filesystem::path &root,
                const std::filesystem::path &candidate) {
  auto rootPart = root.begin();
  auto candidatePart = candidate.begin();
  while (rootPart != root.end() && candidatePart != candidate.end() &&
         *rootPart == *candidatePart) {
    ++rootPart;
    ++candidatePart;
  }
  return rootPart == root.end();
}

std::uint64_t LastWriteTimeNs(const std::filesystem::path &path) {
  std::error_code error;
  const auto value = std::filesystem::last_write_time(path, error);
  if (error) {
    return 0U;
  }
  const auto count = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         value.time_since_epoch())
                         .count();
  return count > 0 ? static_cast<std::uint64_t>(count) : 0U;
}

} // namespace

struct RootedFilesystemBackendV1::Impl {
  struct OpenFile {
    std::filesystem::path Path;
    TriAevumFilesystemOpenFlagsV1 Flags = 0U;
    std::fstream Stream;
  };

  std::optional<std::filesystem::path> Resolve(TriAevumFilesystemRootV1 root,
                                               std::string_view encoded,
                                               bool forCreation) const {
    const auto logical = NormalizeRelativePath(encoded);
    if (!logical.has_value()) {
      return std::nullopt;
    }
    if (root == TRIAEVUM_FILESYSTEM_CONTENT_V1) {
      const auto mapped = ContentFiles.find(*logical);
      return mapped == ContentFiles.end()
                 ? std::nullopt
                 : std::optional<std::filesystem::path>(mapped->second);
    }
    if (root != TRIAEVUM_FILESYSTEM_SAVE_V1) {
      return std::nullopt;
    }
    const auto candidate = (SaveRoot / *logical).lexically_normal();
    if (!PathWithin(SaveRoot, candidate)) {
      return std::nullopt;
    }
    std::error_code error;
    const auto checkPath = forCreation ? candidate.parent_path() : candidate;
    const auto resolved = std::filesystem::weakly_canonical(checkPath, error);
    if (error || !PathWithin(SaveRoot, resolved)) {
      return std::nullopt;
    }
    return candidate;
  }

  std::unordered_map<std::string, std::filesystem::path> ContentFiles;
  std::filesystem::path SaveRoot;
  std::unordered_map<std::uint64_t, OpenFile> OpenFiles;
  std::uint64_t NextHandle = 1U;
  RootedFilesystemStatsV1 Statistics;
  mutable std::mutex Mutex;
};

std::unique_ptr<RootedFilesystemBackendV1>
RootedFilesystemBackendV1::Create(RootedFilesystemConfigV1 config,
                                  std::string *error) {
  try {
    auto impl = std::make_unique<Impl>();
    if (config.contentFiles.empty() || config.saveRoot.empty()) {
      throw std::runtime_error("filesystem roots are incomplete");
    }
    for (auto &[logical, path] : config.contentFiles) {
      const auto normalized = NormalizeRelativePath(logical);
      std::error_code fileError;
      if (!normalized.has_value() ||
          !std::filesystem::is_regular_file(path, fileError) || fileError) {
        throw std::runtime_error("content mapping is invalid: " + logical);
      }
      auto absolute = std::filesystem::weakly_canonical(path, fileError);
      if (fileError ||
          !impl->ContentFiles.emplace(*normalized, absolute).second) {
        throw std::runtime_error("content mapping is ambiguous: " + logical);
      }
    }
    std::error_code directoryError;
    std::filesystem::create_directories(config.saveRoot, directoryError);
    if (directoryError) {
      throw std::runtime_error("save root could not be created");
    }
    impl->SaveRoot =
        std::filesystem::weakly_canonical(config.saveRoot, directoryError);
    if (directoryError || !std::filesystem::is_directory(impl->SaveRoot)) {
      throw std::runtime_error("save root is invalid");
    }
    return std::unique_ptr<RootedFilesystemBackendV1>(
        new RootedFilesystemBackendV1(std::move(impl)));
  } catch (const std::exception &exception) {
    SetError(error, exception.what());
    return nullptr;
  }
}

RootedFilesystemBackendV1::RootedFilesystemBackendV1(std::unique_ptr<Impl> impl)
    : mImpl(std::move(impl)) {}

RootedFilesystemBackendV1::~RootedFilesystemBackendV1() = default;

TriAevumModuleStatusV1 RootedFilesystemBackendV1::Open(
    TriAevumFilesystemRootV1 root, std::string_view utf8Path,
    TriAevumFilesystemOpenFlagsV1 flags, FilesystemOpenResultV1 *result) {
  if (result == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  const bool writable = (flags & TRIAEVUM_FILESYSTEM_OPEN_WRITE_V1) != 0U;
  const bool create = (flags & TRIAEVUM_FILESYSTEM_OPEN_CREATE_V1) != 0U;
  const bool truncate = (flags & TRIAEVUM_FILESYSTEM_OPEN_TRUNCATE_V1) != 0U;
  if (root == TRIAEVUM_FILESYSTEM_CONTENT_V1 &&
      (writable || create || truncate)) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  std::scoped_lock lock(mImpl->Mutex);
  auto path = mImpl->Resolve(root, utf8Path, create);
  if (!path.has_value()) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  std::error_code fileError;
  bool exists = std::filesystem::is_regular_file(*path, fileError);
  if (!exists && fileError == std::errc::no_such_file_or_directory) {
    fileError.clear();
  }
  if (!exists && !fileError && create && root == TRIAEVUM_FILESYSTEM_SAVE_V1) {
    std::filesystem::create_directories(path->parent_path(), fileError);
    if (!fileError) {
      std::ofstream created(*path, std::ios::binary);
      exists = created.good();
    }
  }
  if (!exists || fileError) {
    return TRIAEVUM_MODULE_TITLE_ERROR_V1;
  }
  if (truncate) {
    std::ofstream truncated(*path, std::ios::binary | std::ios::trunc);
    if (!truncated) {
      return TRIAEVUM_MODULE_HOST_ERROR_V1;
    }
  }
  std::ios::openmode mode = std::ios::binary;
  if ((flags & TRIAEVUM_FILESYSTEM_OPEN_READ_V1) != 0U || writable) {
    mode |= std::ios::in;
  }
  if (writable) {
    mode |= std::ios::out;
  }
  Impl::OpenFile opened{*path, flags, std::fstream(*path, mode)};
  if (!opened.Stream) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  const std::uint64_t size = std::filesystem::file_size(*path, fileError);
  if (fileError) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  std::uint64_t handle = mImpl->NextHandle++;
  while (handle == 0U || mImpl->OpenFiles.contains(handle)) {
    handle = mImpl->NextHandle++;
  }
  mImpl->OpenFiles.emplace(handle, std::move(opened));
  ++mImpl->Statistics.opens;
  *result = {handle, size};
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1
RootedFilesystemBackendV1::Read(std::uint64_t handle, std::uint64_t offset,
                                std::span<std::uint8_t> destination,
                                FilesystemReadResultV1 *result) {
  if (result == nullptr ||
      offset > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamoff>::max()) ||
      destination.size() > static_cast<std::size_t>(
                               std::numeric_limits<std::streamsize>::max())) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  std::scoped_lock lock(mImpl->Mutex);
  const auto opened = mImpl->OpenFiles.find(handle);
  if (opened == mImpl->OpenFiles.end() ||
      (opened->second.Flags & TRIAEVUM_FILESYSTEM_OPEN_READ_V1) == 0U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  auto &stream = opened->second.Stream;
  stream.clear();
  stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!stream) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  stream.read(reinterpret_cast<char *>(destination.data()),
              static_cast<std::streamsize>(destination.size()));
  const std::streamsize count = stream.gcount();
  if (count < 0 || (!stream && !stream.eof())) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  std::error_code sizeError;
  const std::uint64_t size =
      std::filesystem::file_size(opened->second.Path, sizeError);
  if (sizeError) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  result->returnedSize = static_cast<std::uint32_t>(count);
  result->endOfFile = offset + static_cast<std::uint64_t>(count) >= size;
  ++mImpl->Statistics.reads;
  mImpl->Statistics.readBytes += static_cast<std::uint64_t>(count);
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1
RootedFilesystemBackendV1::Write(std::uint64_t handle, std::uint64_t offset,
                                 std::span<const std::uint8_t> data,
                                 std::uint32_t *writtenSize) {
  if (writtenSize == nullptr ||
      offset > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamoff>::max()) ||
      data.size() > static_cast<std::size_t>(
                        std::numeric_limits<std::streamsize>::max())) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  std::scoped_lock lock(mImpl->Mutex);
  const auto opened = mImpl->OpenFiles.find(handle);
  if (opened == mImpl->OpenFiles.end() ||
      (opened->second.Flags & TRIAEVUM_FILESYSTEM_OPEN_WRITE_V1) == 0U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  auto &stream = opened->second.Stream;
  stream.clear();
  stream.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!stream ||
      (!data.empty() &&
       !stream.write(reinterpret_cast<const char *>(data.data()),
                     static_cast<std::streamsize>(data.size()))) ||
      !stream.flush()) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  *writtenSize = static_cast<std::uint32_t>(data.size());
  ++mImpl->Statistics.writes;
  mImpl->Statistics.writtenBytes += data.size();
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1 RootedFilesystemBackendV1::Close(std::uint64_t handle) {
  std::scoped_lock lock(mImpl->Mutex);
  if (mImpl->OpenFiles.erase(handle) != 1U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  ++mImpl->Statistics.closes;
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1
RootedFilesystemBackendV1::Stat(TriAevumFilesystemRootV1 root,
                                std::string_view utf8Path,
                                FilesystemStatV1 *result) {
  if (result == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  std::scoped_lock lock(mImpl->Mutex);
  const auto path = mImpl->Resolve(root, utf8Path, false);
  if (!path.has_value()) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  std::error_code fileError;
  const bool regular = std::filesystem::is_regular_file(*path, fileError);
  const bool directory = !fileError && std::filesystem::is_directory(*path);
  if (fileError || (!regular && !directory)) {
    return TRIAEVUM_MODULE_TITLE_ERROR_V1;
  }
  result->size = regular ? std::filesystem::file_size(*path, fileError) : 0U;
  if (fileError) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  result->modifiedTimeNs = LastWriteTimeNs(*path);
  result->flags = regular ? TRIAEVUM_FILESYSTEM_STAT_REGULAR_FILE_V1
                          : TRIAEVUM_FILESYSTEM_STAT_DIRECTORY_V1;
  ++mImpl->Statistics.stats;
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1 RootedFilesystemBackendV1::Resize(std::uint64_t handle,
                                                         std::uint64_t size) {
  std::scoped_lock lock(mImpl->Mutex);
  const auto opened = mImpl->OpenFiles.find(handle);
  if (opened == mImpl->OpenFiles.end() ||
      (opened->second.Flags & TRIAEVUM_FILESYSTEM_OPEN_WRITE_V1) == 0U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  auto &file = opened->second;
  file.Stream.flush();
  file.Stream.close();
  std::error_code resizeError;
  std::filesystem::resize_file(file.Path, size, resizeError);
  std::ios::openmode mode = std::ios::binary | std::ios::in | std::ios::out;
  file.Stream.open(file.Path, mode);
  if (resizeError || !file.Stream) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  ++mImpl->Statistics.resizes;
  return TRIAEVUM_MODULE_OK_V1;
}

RootedFilesystemStatsV1 RootedFilesystemBackendV1::Stats() const noexcept {
  std::scoped_lock lock(mImpl->Mutex);
  return mImpl->Statistics;
}

} // namespace triaevum::module
