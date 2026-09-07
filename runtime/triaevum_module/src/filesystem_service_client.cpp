#include "triaevum/filesystem_service_client.h"

#include "triaevum/service_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace triaevum::module {
namespace {

constexpr std::size_t kReadChunkBytes = 4U * 1024U * 1024U;

template <typename T>
TriAevumModuleStatusV1
DecodeFixedResponse(std::span<const std::uint8_t> encoded,
                    std::size_t responseSize, T *decoded) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (decoded == nullptr || responseSize < sizeof(T) ||
      responseSize > encoded.size()) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  std::memcpy(decoded, encoded.data(), sizeof(T));
  if (decoded->header.schema_version != TRIAEVUM_SERVICE_SCHEMA_V1 ||
      decoded->header.struct_size < sizeof(T) ||
      decoded->header.struct_size > responseSize) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  return TRIAEVUM_MODULE_OK_V1;
}

template <typename T> std::span<const std::uint8_t> AsBytes(const T &value) {
  return {reinterpret_cast<const std::uint8_t *>(&value), sizeof(T)};
}

template <typename T>
std::vector<std::uint8_t> EncodePathRequest(T request, std::string_view path) {
  request.utf8_path = {static_cast<std::uint32_t>(sizeof(T)),
                       static_cast<std::uint32_t>(path.size())};
  std::vector<std::uint8_t> encoded(sizeof(T) + path.size());
  std::memcpy(encoded.data(), &request, sizeof(request));
  std::memcpy(encoded.data() + sizeof(request), path.data(), path.size());
  return encoded;
}

} // namespace

FilesystemServiceClientV1::FilesystemServiceClientV1(
    const TriAevumHostApiV1 *host) {
  constexpr std::size_t requiredSize =
      offsetof(TriAevumHostApiV1, invoke_service) +
      sizeof(TriAevumInvokeServiceFnV1);
  if (host == nullptr || host->struct_size < requiredSize ||
      host->abi_version != TRIAEVUM_RUNTIME_ABI_V1 ||
      host->invoke_service == nullptr) {
    return;
  }
  mHost = *host;
  mAvailable = true;
}

bool FilesystemServiceClientV1::IsAvailable() const noexcept {
  return mAvailable;
}

TriAevumModuleStatusV1 FilesystemServiceClientV1::Invoke(
    std::uint32_t operation, std::span<const std::uint8_t> request,
    std::span<std::uint8_t> response, std::size_t *responseSize) const {
  if (!mAvailable || responseSize == nullptr) {
    return mAvailable ? TRIAEVUM_MODULE_INVALID_ARGUMENT_V1
                      : TRIAEVUM_MODULE_INCOMPATIBLE_ABI_V1;
  }
  return mHost.invoke_service(mHost.host_context,
                              TRIAEVUM_SERVICE_FILESYSTEM_V1, operation,
                              {request.data(), request.size()},
                              {response.data(), response.size()}, responseSize);
}

TriAevumModuleStatusV1 FilesystemServiceClientV1::Open(
    TriAevumFilesystemRootV1 root, std::string_view utf8Path,
    TriAevumFilesystemOpenFlagsV1 flags, FilesystemOpenResultV1 *result) {
  if (result == nullptr || utf8Path.empty() ||
      utf8Path.size() > std::numeric_limits<std::uint32_t>::max()) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  TriAevumFilesystemOpenRequestV1 request{};
  request.header = RequestHeader<TriAevumFilesystemOpenRequestV1>();
  request.root = root;
  request.flags = flags;
  const auto encoded = EncodePathRequest(request, utf8Path);
  TriAevumFilesystemOpenResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_FILESYSTEM_OPEN_V1, encoded,
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  status = DecodeFixedResponse(
      {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)},
      responseSize, &response);
  if (status != TRIAEVUM_MODULE_OK_V1 || response.handle == 0U) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  result->handle = response.handle;
  result->size = response.size;
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1
FilesystemServiceClientV1::Read(std::uint64_t handle, std::uint64_t offset,
                                std::span<std::uint8_t> destination,
                                FilesystemReadResultV1 *result) {
  if (handle == 0U || result == nullptr ||
      destination.size() > std::numeric_limits<std::uint32_t>::max()) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  const TriAevumFilesystemReadRequestV1 request = {
      RequestHeader<TriAevumFilesystemReadRequestV1>(), handle, offset,
      static_cast<std::uint32_t>(destination.size()), 0U};
  std::vector<std::uint8_t> response(sizeof(TriAevumFilesystemReadResponseV1) +
                                     destination.size());
  std::size_t responseSize = response.size();
  TriAevumModuleStatusV1 status = Invoke(
      TRIAEVUM_FILESYSTEM_READ_V1, AsBytes(request), response, &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  TriAevumFilesystemReadResponseV1 decoded{};
  status = DecodeFixedResponse(response, responseSize, &decoded);
  if (status != TRIAEVUM_MODULE_OK_V1 || decoded.end_of_file > 1U ||
      decoded.returned_size > destination.size()) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  std::span<const std::uint8_t> data;
  status =
      ResolveServicePayload({response.data(), responseSize},
                            decoded.header.struct_size, decoded.data, &data);
  if (status != TRIAEVUM_MODULE_OK_V1 || data.size() != decoded.returned_size) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  std::copy(data.begin(), data.end(), destination.begin());
  result->returnedSize = decoded.returned_size;
  result->endOfFile = decoded.end_of_file != 0U;
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1
FilesystemServiceClientV1::Write(std::uint64_t handle, std::uint64_t offset,
                                 std::span<const std::uint8_t> data,
                                 std::uint32_t *writtenSize) {
  if (handle == 0U || writtenSize == nullptr ||
      data.size() > std::numeric_limits<std::uint32_t>::max()) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  TriAevumFilesystemWriteRequestV1 request{};
  request.header = RequestHeader<TriAevumFilesystemWriteRequestV1>();
  request.handle = handle;
  request.offset = offset;
  request.data = {static_cast<std::uint32_t>(sizeof(request)),
                  static_cast<std::uint32_t>(data.size())};
  std::vector<std::uint8_t> encoded(sizeof(request) + data.size());
  std::memcpy(encoded.data(), &request, sizeof(request));
  if (!data.empty()) {
    std::memcpy(encoded.data() + sizeof(request), data.data(), data.size());
  }
  TriAevumFilesystemWriteResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_FILESYSTEM_WRITE_V1, encoded,
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  status = DecodeFixedResponse(
      {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)},
      responseSize, &response);
  if (status != TRIAEVUM_MODULE_OK_V1 || response.written_size > data.size()) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  *writtenSize = response.written_size;
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1 FilesystemServiceClientV1::Close(std::uint64_t handle) {
  if (handle == 0U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  const TriAevumFilesystemCloseRequestV1 request = {
      RequestHeader<TriAevumFilesystemCloseRequestV1>(), handle};
  TriAevumFilesystemCloseResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_FILESYSTEM_CLOSE_V1, AsBytes(request),
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  return DecodeFixedResponse(
      {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)},
      responseSize, &response);
}

TriAevumModuleStatusV1
FilesystemServiceClientV1::Stat(TriAevumFilesystemRootV1 root,
                                std::string_view utf8Path,
                                FilesystemStatV1 *result) {
  if (result == nullptr || utf8Path.empty() ||
      utf8Path.size() > std::numeric_limits<std::uint32_t>::max()) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  TriAevumFilesystemStatRequestV1 request{};
  request.header = RequestHeader<TriAevumFilesystemStatRequestV1>();
  request.root = root;
  const auto encoded = EncodePathRequest(request, utf8Path);
  TriAevumFilesystemStatResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_FILESYSTEM_STAT_V1, encoded,
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  status = DecodeFixedResponse(
      {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)},
      responseSize, &response);
  if (status != TRIAEVUM_MODULE_OK_V1 || response.reserved != 0U) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  result->size = response.size;
  result->modifiedTimeNs = response.modified_time_ns;
  result->flags = response.flags;
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1 FilesystemServiceClientV1::Resize(std::uint64_t handle,
                                                         std::uint64_t size) {
  if (handle == 0U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  const TriAevumFilesystemResizeRequestV1 request = {
      RequestHeader<TriAevumFilesystemResizeRequestV1>(), handle, size};
  TriAevumFilesystemResizeResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_FILESYSTEM_RESIZE_V1, AsBytes(request),
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  return DecodeFixedResponse(
      {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)},
      responseSize, &response);
}

TriAevumModuleStatusV1 FilesystemServiceClientV1::ReadAll(
    TriAevumFilesystemRootV1 root, std::string_view utf8Path,
    std::uint64_t maximumSize, std::vector<std::uint8_t> *bytes) {
  if (bytes == nullptr ||
      maximumSize > std::numeric_limits<std::size_t>::max()) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  FilesystemOpenResultV1 opened;
  TriAevumModuleStatusV1 status =
      Open(root, utf8Path, TRIAEVUM_FILESYSTEM_OPEN_READ_V1, &opened);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  const auto close = [&]() { return Close(opened.handle); };
  if (opened.size > maximumSize ||
      opened.size > std::numeric_limits<std::size_t>::max()) {
    close();
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  bytes->assign(static_cast<std::size_t>(opened.size), 0U);
  std::size_t consumed = 0U;
  while (consumed < bytes->size()) {
    const std::size_t chunk =
        std::min(kReadChunkBytes, bytes->size() - consumed);
    FilesystemReadResultV1 read;
    status =
        Read(opened.handle, consumed, {bytes->data() + consumed, chunk}, &read);
    if (status != TRIAEVUM_MODULE_OK_V1 || read.returnedSize != chunk) {
      close();
      bytes->clear();
      return status == TRIAEVUM_MODULE_OK_V1 ? TRIAEVUM_MODULE_HOST_ERROR_V1
                                             : status;
    }
    consumed += read.returnedSize;
  }
  status = close();
  if (status != TRIAEVUM_MODULE_OK_V1) {
    bytes->clear();
  }
  return status;
}

} // namespace triaevum::module
