#ifndef TRIAEVUM_ROOTED_FILESYSTEM_BACKEND_H
#define TRIAEVUM_ROOTED_FILESYSTEM_BACKEND_H

#include "triaevum/filesystem_service.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace triaevum::module {

struct RootedFilesystemConfigV1 {
  std::unordered_map<std::string, std::filesystem::path> contentFiles;
  std::filesystem::path saveRoot;
};

struct RootedFilesystemStatsV1 {
  std::uint64_t opens = 0U;
  std::uint64_t reads = 0U;
  std::uint64_t readBytes = 0U;
  std::uint64_t writes = 0U;
  std::uint64_t writtenBytes = 0U;
  std::uint64_t closes = 0U;
  std::uint64_t stats = 0U;
  std::uint64_t resizes = 0U;
};

class RootedFilesystemBackendV1 final : public FilesystemServiceBackendV1 {
public:
  static std::unique_ptr<RootedFilesystemBackendV1>
  Create(RootedFilesystemConfigV1 config, std::string *error = nullptr);
  ~RootedFilesystemBackendV1() override;

  RootedFilesystemBackendV1(const RootedFilesystemBackendV1 &) = delete;
  RootedFilesystemBackendV1 &
  operator=(const RootedFilesystemBackendV1 &) = delete;

  TriAevumModuleStatusV1 Open(TriAevumFilesystemRootV1 root,
                              std::string_view utf8Path,
                              TriAevumFilesystemOpenFlagsV1 flags,
                              FilesystemOpenResultV1 *result) override;
  TriAevumModuleStatusV1 Read(std::uint64_t handle, std::uint64_t offset,
                              std::span<std::uint8_t> destination,
                              FilesystemReadResultV1 *result) override;
  TriAevumModuleStatusV1 Write(std::uint64_t handle, std::uint64_t offset,
                               std::span<const std::uint8_t> data,
                               std::uint32_t *writtenSize) override;
  TriAevumModuleStatusV1 Close(std::uint64_t handle) override;
  TriAevumModuleStatusV1 Stat(TriAevumFilesystemRootV1 root,
                              std::string_view utf8Path,
                              FilesystemStatV1 *result) override;
  TriAevumModuleStatusV1 Resize(std::uint64_t handle,
                                std::uint64_t size) override;
  TriAevumModuleStatusV1 RemoveFile(TriAevumFilesystemRootV1 root,
                                    std::string_view utf8Path, bool *removed) override;

  [[nodiscard]] RootedFilesystemStatsV1 Stats() const noexcept;

private:
  struct Impl;
  explicit RootedFilesystemBackendV1(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> mImpl;
};

} // namespace triaevum::module

#endif
