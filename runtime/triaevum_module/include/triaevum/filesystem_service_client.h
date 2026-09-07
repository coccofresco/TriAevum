#ifndef TRIAEVUM_FILESYSTEM_SERVICE_CLIENT_H
#define TRIAEVUM_FILESYSTEM_SERVICE_CLIENT_H

#include "triaevum/filesystem_service.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace triaevum::module {

class FilesystemServiceClientV1 final {
public:
  explicit FilesystemServiceClientV1(const TriAevumHostApiV1 *host);

  [[nodiscard]] bool IsAvailable() const noexcept;

  TriAevumModuleStatusV1 Open(TriAevumFilesystemRootV1 root,
                              std::string_view utf8Path,
                              TriAevumFilesystemOpenFlagsV1 flags,
                              FilesystemOpenResultV1 *result);
  TriAevumModuleStatusV1 Read(std::uint64_t handle, std::uint64_t offset,
                              std::span<std::uint8_t> destination,
                              FilesystemReadResultV1 *result);
  TriAevumModuleStatusV1 Write(std::uint64_t handle, std::uint64_t offset,
                               std::span<const std::uint8_t> data,
                               std::uint32_t *writtenSize);
  TriAevumModuleStatusV1 Close(std::uint64_t handle);
  TriAevumModuleStatusV1 Stat(TriAevumFilesystemRootV1 root,
                              std::string_view utf8Path,
                              FilesystemStatV1 *result);
  TriAevumModuleStatusV1 Resize(std::uint64_t handle, std::uint64_t size);
  TriAevumModuleStatusV1 ReadAll(TriAevumFilesystemRootV1 root,
                                 std::string_view utf8Path,
                                 std::uint64_t maximumSize,
                                 std::vector<std::uint8_t> *bytes);

private:
  TriAevumModuleStatusV1 Invoke(std::uint32_t operation,
                                std::span<const std::uint8_t> request,
                                std::span<std::uint8_t> response,
                                std::size_t *responseSize) const;

  TriAevumHostApiV1 mHost{};
  bool mAvailable = false;
};

} // namespace triaevum::module

#endif
