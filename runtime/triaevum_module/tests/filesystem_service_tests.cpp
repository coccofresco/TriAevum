#include "triaevum/filesystem_service_adapter.h"
#include "triaevum/filesystem_service_client.h"
#include "triaevum/rooted_filesystem_backend.h"
#include "triaevum/service_codec.h"
#include "triaevum/service_registry.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace triaevum::module;

bool Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

class MemoryFilesystemBackend final : public FilesystemServiceBackendV1 {
public:
  TriAevumModuleStatusV1 Open(TriAevumFilesystemRootV1 root,
                              std::string_view path,
                              TriAevumFilesystemOpenFlagsV1 flags,
                              FilesystemOpenResultV1 *result) override {
    if (result == nullptr || path.find("..") != std::string_view::npos) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    const std::string key = std::to_string(root) + ":" + std::string(path);
    auto file = Files.find(key);
    if (file == Files.end()) {
      if ((flags & TRIAEVUM_FILESYSTEM_OPEN_CREATE_V1) == 0U) {
        return TRIAEVUM_MODULE_TITLE_ERROR_V1;
      }
      file = Files.emplace(key, std::vector<std::uint8_t>{}).first;
    }
    if ((flags & TRIAEVUM_FILESYSTEM_OPEN_TRUNCATE_V1) != 0U) {
      file->second.clear();
    }
    const std::uint64_t handle = NextHandle++;
    Handles.emplace(handle, Handle{key, flags});
    *result = {handle, file->second.size()};
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 Read(std::uint64_t handle, std::uint64_t offset,
                              std::span<std::uint8_t> destination,
                              FilesystemReadResultV1 *result) override {
    const auto opened = Handles.find(handle);
    if (opened == Handles.end() || result == nullptr ||
        (opened->second.Flags & TRIAEVUM_FILESYSTEM_OPEN_READ_V1) == 0U) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    const auto &file = Files.at(opened->second.Key);
    if (offset > file.size()) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    const std::size_t count = std::min<std::size_t>(
        destination.size(), file.size() - static_cast<std::size_t>(offset));
    std::copy_n(file.data() + offset, count, destination.data());
    *result = {static_cast<std::uint32_t>(count),
               offset + count == file.size()};
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 Write(std::uint64_t handle, std::uint64_t offset,
                               std::span<const std::uint8_t> data,
                               std::uint32_t *writtenSize) override {
    const auto opened = Handles.find(handle);
    if (opened == Handles.end() || writtenSize == nullptr ||
        (opened->second.Flags & TRIAEVUM_FILESYSTEM_OPEN_WRITE_V1) == 0U ||
        offset > SIZE_MAX || data.size() > SIZE_MAX - offset) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    auto &file = Files.at(opened->second.Key);
    file.resize(
        std::max(file.size(), static_cast<std::size_t>(offset) + data.size()));
    std::copy(data.begin(), data.end(), file.begin() + offset);
    *writtenSize = static_cast<std::uint32_t>(data.size());
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 Close(std::uint64_t handle) override {
    return Handles.erase(handle) == 1U ? TRIAEVUM_MODULE_OK_V1
                                       : TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }

  TriAevumModuleStatusV1 Stat(TriAevumFilesystemRootV1 root,
                              std::string_view path,
                              FilesystemStatV1 *result) override {
    const auto file =
        Files.find(std::to_string(root) + ":" + std::string(path));
    if (file == Files.end() || result == nullptr) {
      return TRIAEVUM_MODULE_TITLE_ERROR_V1;
    }
    *result = {file->second.size(), 123U,
               TRIAEVUM_FILESYSTEM_STAT_REGULAR_FILE_V1};
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 Resize(std::uint64_t handle,
                                std::uint64_t size) override {
    const auto opened = Handles.find(handle);
    if (opened == Handles.end() || size > SIZE_MAX ||
        (opened->second.Flags & TRIAEVUM_FILESYSTEM_OPEN_WRITE_V1) == 0U) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    Files.at(opened->second.Key).resize(static_cast<std::size_t>(size));
    return TRIAEVUM_MODULE_OK_V1;
  }

  std::unordered_map<std::string, std::vector<std::uint8_t>> Files{
      {"1:inputs/code", {1U, 2U, 3U, 4U}},
  };

private:
  struct Handle {
    std::string Key;
    TriAevumFilesystemOpenFlagsV1 Flags = 0U;
  };
  std::unordered_map<std::uint64_t, Handle> Handles;
  std::uint64_t NextHandle = 1U;
};

} // namespace

int main() {
  MemoryFilesystemBackend backend;
  FilesystemHostServiceAdapterV1 adapter(backend);
  HostServiceRegistry registry({});
  bool ok = true;
  ok &= Expect(registry.Register(TRIAEVUM_SERVICE_FILESYSTEM_V1,
                                 FilesystemHostServiceAdapterV1::Invoke,
                                 &adapter) ==
                   ServiceRegistrationResult::Registered,
               "filesystem service registration failed");
  const TriAevumHostApiV1 host = registry.SealAndCreateHostApi();
  FilesystemServiceClientV1 client(&host);
  ok &= Expect(client.IsAvailable(), "filesystem client is unavailable");

  std::vector<std::uint8_t> code;
  ok &= Expect(client.ReadAll(TRIAEVUM_FILESYSTEM_CONTENT_V1, "inputs/code",
                              16U, &code) == TRIAEVUM_MODULE_OK_V1 &&
                   code == std::vector<std::uint8_t>({1U, 2U, 3U, 4U}),
               "content file did not cross the filesystem boundary");

  FilesystemOpenResultV1 opened;
  ok &= Expect(client.Open(TRIAEVUM_FILESYSTEM_SAVE_V1, "slot/save.bin",
                           TRIAEVUM_FILESYSTEM_OPEN_READ_V1 |
                               TRIAEVUM_FILESYSTEM_OPEN_WRITE_V1 |
                               TRIAEVUM_FILESYSTEM_OPEN_CREATE_V1,
                           &opened) == TRIAEVUM_MODULE_OK_V1,
               "save file could not be created");
  const std::array<std::uint8_t, 3> payload{9U, 8U, 7U};
  std::uint32_t written = 0U;
  ok &= Expect(client.Write(opened.handle, 2U, payload, &written) ==
                       TRIAEVUM_MODULE_OK_V1 &&
                   written == payload.size() &&
                   client.Resize(opened.handle, 8U) == TRIAEVUM_MODULE_OK_V1 &&
                   client.Close(opened.handle) == TRIAEVUM_MODULE_OK_V1,
               "save write/resize/close did not cross the boundary");
  FilesystemStatV1 stat;
  ok &= Expect(client.Stat(TRIAEVUM_FILESYSTEM_SAVE_V1, "slot/save.bin",
                           &stat) == TRIAEVUM_MODULE_OK_V1 &&
                   stat.size == 8U &&
                   stat.flags == TRIAEVUM_FILESYSTEM_STAT_REGULAR_FILE_V1,
               "save stat did not cross the boundary");
  ok &= Expect(client.Open(TRIAEVUM_FILESYSTEM_SAVE_V1, "../escape.bin",
                           TRIAEVUM_FILESYSTEM_OPEN_READ_V1,
                           &opened) == TRIAEVUM_MODULE_INVALID_ARGUMENT_V1,
               "backend accepted a traversal path");

  TriAevumFilesystemReadRequestV1 malformed{};
  malformed.header = RequestHeader<TriAevumFilesystemReadRequestV1>();
  malformed.handle = 1U;
  malformed.requested_size = 16U * 1024U * 1024U + 1U;
  std::array<std::uint8_t, sizeof(TriAevumFilesystemReadResponseV1)> response{};
  std::size_t responseSize = response.size();
  ok &= Expect(
      host.invoke_service(host.host_context, TRIAEVUM_SERVICE_FILESYSTEM_V1,
                          TRIAEVUM_FILESYSTEM_READ_V1,
                          {reinterpret_cast<const std::uint8_t *>(&malformed),
                           sizeof(malformed)},
                          {response.data(), response.size()}, &responseSize) ==
          TRIAEVUM_MODULE_MALFORMED_REQUEST_V1,
      "adapter accepted an oversized read");

  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto testRoot =
      std::filesystem::temp_directory_path() / ("triaevum-fs-" + unique);
  const auto contentPath = testRoot / "private-content.bin";
  const auto saveRoot = testRoot / "save";
  std::filesystem::create_directories(testRoot);
  {
    std::ofstream content(contentPath, std::ios::binary);
    content.write("content", 7);
  }
  RootedFilesystemConfigV1 rootedConfig;
  rootedConfig.contentFiles.emplace("inputs/content", contentPath);
  rootedConfig.saveRoot = saveRoot;
  std::string rootedError;
  auto rooted =
      RootedFilesystemBackendV1::Create(std::move(rootedConfig), &rootedError);
  ok &= Expect(rooted != nullptr, "rooted backend creation failed");
  if (rooted != nullptr) {
    FilesystemOpenResultV1 rootedFile;
    ok &= Expect(rooted->Open(TRIAEVUM_FILESYSTEM_CONTENT_V1, "inputs/content",
                              TRIAEVUM_FILESYSTEM_OPEN_READ_V1,
                              &rootedFile) == TRIAEVUM_MODULE_OK_V1,
                 "rooted content file did not open");
    std::array<std::uint8_t, 7> rootedBytes{};
    FilesystemReadResultV1 rootedRead;
    ok &= Expect(
        rooted->Read(rootedFile.handle, 0U, rootedBytes, &rootedRead) ==
                TRIAEVUM_MODULE_OK_V1 &&
            rootedRead.returnedSize == rootedBytes.size() &&
            std::string(rootedBytes.begin(), rootedBytes.end()) == "content" &&
            rooted->Close(rootedFile.handle) == TRIAEVUM_MODULE_OK_V1,
        "rooted content read failed");
    ok &= Expect(rooted->Open(TRIAEVUM_FILESYSTEM_SAVE_V1, "../escape",
                              TRIAEVUM_FILESYSTEM_OPEN_READ_V1, &rootedFile) ==
                     TRIAEVUM_MODULE_INVALID_ARGUMENT_V1,
                 "rooted backend accepted path traversal");
    ok &= Expect(rooted->Open(TRIAEVUM_FILESYSTEM_SAVE_V1, "slot/save.bin",
                              TRIAEVUM_FILESYSTEM_OPEN_READ_V1 |
                                  TRIAEVUM_FILESYSTEM_OPEN_WRITE_V1 |
                                  TRIAEVUM_FILESYSTEM_OPEN_CREATE_V1,
                              &rootedFile) == TRIAEVUM_MODULE_OK_V1,
                 "rooted save file did not open");
    std::uint32_t rootedWritten = 0U;
    const std::array<std::uint8_t, 2> saveBytes{5U, 6U};
    ok &= Expect(
        rooted->Write(rootedFile.handle, 1U, saveBytes, &rootedWritten) ==
                TRIAEVUM_MODULE_OK_V1 &&
            rootedWritten == saveBytes.size() &&
            rooted->Resize(rootedFile.handle, 8U) == TRIAEVUM_MODULE_OK_V1 &&
            rooted->Close(rootedFile.handle) == TRIAEVUM_MODULE_OK_V1 &&
            std::filesystem::file_size(saveRoot / "slot/save.bin") == 8U,
        "rooted save write/resize failed");
    FilesystemHostServiceAdapterV1 rootedAdapter(*rooted);
    HostServiceRegistry rootedRegistry({});
    ok &= Expect(rootedRegistry.Register(TRIAEVUM_SERVICE_FILESYSTEM_V1,
        FilesystemHostServiceAdapterV1::Invoke, &rootedAdapter) == ServiceRegistrationResult::Registered,
        "rooted removal service registration failed");
    const auto rootedHost = rootedRegistry.SealAndCreateHostApi();
    FilesystemServiceClientV1 rootedClient(&rootedHost);
    bool removed = false;
    ok &= Expect(rootedClient.RemoveFile(TRIAEVUM_FILESYSTEM_SAVE_V1, "slot/save.bin", &removed) ==
        TRIAEVUM_MODULE_OK_V1 && removed && !std::filesystem::exists(saveRoot / "slot/save.bin"),
        "save removal did not cross the service boundary");
    ok &= Expect(rootedClient.RemoveFile(TRIAEVUM_FILESYSTEM_SAVE_V1, "slot/save.bin", &removed) ==
        TRIAEVUM_MODULE_OK_V1 && !removed, "missing file removal was reported as a deletion");
    for (const auto* invalid : {"../private-content.bin", "/private-content.bin", "slot"})
      ok &= Expect(rootedClient.RemoveFile(TRIAEVUM_FILESYSTEM_SAVE_V1, invalid, &removed) !=
          TRIAEVUM_MODULE_OK_V1 && std::filesystem::exists(contentPath),
          "removal accepted a directory or escaped the save root");
    ok &= Expect(rootedClient.RemoveFile(TRIAEVUM_FILESYSTEM_CONTENT_V1, "inputs/content", &removed) !=
        TRIAEVUM_MODULE_OK_V1 && std::filesystem::exists(contentPath), "removal accepted the content root");
    const auto linked = saveRoot / "linked.bin";
    std::error_code linkError;
    std::filesystem::create_symlink(contentPath, linked, linkError);
    if (!linkError)
      ok &= Expect(rootedClient.RemoveFile(TRIAEVUM_FILESYSTEM_SAVE_V1, "linked.bin", &removed) !=
          TRIAEVUM_MODULE_OK_V1 && std::filesystem::exists(contentPath), "removal followed an external symlink");
  }
  std::error_code cleanupError;
  std::filesystem::remove_all(testRoot, cleanupError);
  return ok ? 0 : 1;
}
