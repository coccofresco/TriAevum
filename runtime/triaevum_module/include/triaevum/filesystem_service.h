#ifndef TRIAEVUM_FILESYSTEM_SERVICE_H
#define TRIAEVUM_FILESYSTEM_SERVICE_H

#include "triaevum/service_abi.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace triaevum::module {

struct FilesystemOpenResultV1 {
  std::uint64_t handle = 0U;
  std::uint64_t size = 0U;
};

struct FilesystemReadResultV1 {
  std::uint32_t returnedSize = 0U;
  bool endOfFile = false;
};

struct FilesystemStatV1 {
  std::uint64_t size = 0U;
  std::uint64_t modifiedTimeNs = 0U;
  TriAevumFilesystemStatFlagsV1 flags = 0U;
};

class FilesystemServiceBackendV1 {
public:
  virtual ~FilesystemServiceBackendV1() = default;

  virtual TriAevumModuleStatusV1 Open(TriAevumFilesystemRootV1 root,
                                      std::string_view utf8Path,
                                      TriAevumFilesystemOpenFlagsV1 flags,
                                      FilesystemOpenResultV1 *result) = 0;
  virtual TriAevumModuleStatusV1 Read(std::uint64_t handle,
                                      std::uint64_t offset,
                                      std::span<std::uint8_t> destination,
                                      FilesystemReadResultV1 *result) = 0;
  virtual TriAevumModuleStatusV1 Write(std::uint64_t handle,
                                       std::uint64_t offset,
                                       std::span<const std::uint8_t> data,
                                       std::uint32_t *writtenSize) = 0;
  virtual TriAevumModuleStatusV1 Close(std::uint64_t handle) = 0;
  virtual TriAevumModuleStatusV1 Stat(TriAevumFilesystemRootV1 root,
                                      std::string_view utf8Path,
                                      FilesystemStatV1 *result) = 0;
  virtual TriAevumModuleStatusV1 Resize(std::uint64_t handle,
                                        std::uint64_t size) = 0;
  virtual TriAevumModuleStatusV1 RemoveFile(TriAevumFilesystemRootV1,
                                            std::string_view, bool *) {
    return TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1;
  }
};

} // namespace triaevum::module

#endif
