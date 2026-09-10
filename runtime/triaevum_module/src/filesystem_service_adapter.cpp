#include "triaevum/filesystem_service_adapter.h"

#include "triaevum/service_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

namespace triaevum::module {
namespace {

constexpr std::uint32_t kMaximumPathBytes = 4096U;
constexpr std::uint32_t kMaximumIoBytes = 16U * 1024U * 1024U;
constexpr TriAevumFilesystemOpenFlagsV1 kKnownOpenFlags =
    TRIAEVUM_FILESYSTEM_OPEN_READ_V1 | TRIAEVUM_FILESYSTEM_OPEN_WRITE_V1 |
    TRIAEVUM_FILESYSTEM_OPEN_CREATE_V1 | TRIAEVUM_FILESYSTEM_OPEN_TRUNCATE_V1;

bool ValidRoot(TriAevumFilesystemRootV1 root) {
  return root == TRIAEVUM_FILESYSTEM_CONTENT_V1 ||
         root == TRIAEVUM_FILESYSTEM_SAVE_V1;
}

TriAevumModuleStatusV1 DecodePath(TriAevumReadOnlyBytesV1 request,
                                  std::uint32_t minimumOffset,
                                  TriAevumPayloadRangeV1 range,
                                  std::string_view *path) {
  if (path == nullptr || range.size == 0U || range.size > kMaximumPathBytes) {
    return TRIAEVUM_MODULE_MALFORMED_REQUEST_V1;
  }
  std::span<const std::uint8_t> encoded;
  const TriAevumModuleStatusV1 status =
      ResolveServicePayload(request, minimumOffset, range, &encoded);
  if (status != TRIAEVUM_MODULE_OK_V1 ||
      std::find(encoded.begin(), encoded.end(), 0U) != encoded.end()) {
    return status == TRIAEVUM_MODULE_OK_V1
               ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
               : status;
  }
  *path = {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
  return TRIAEVUM_MODULE_OK_V1;
}

template <typename T>
TriAevumModuleStatusV1 EmptyResponse(TriAevumMutableBytesV1 destination,
                                     std::size_t *responseSize) {
  const T response = {ResponseHeader<T>()};
  return EncodeServiceResponse(response, destination, responseSize);
}

} // namespace

FilesystemHostServiceAdapterV1::FilesystemHostServiceAdapterV1(
    FilesystemServiceBackendV1 &backend)
    : mBackend(backend) {}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL FilesystemHostServiceAdapterV1::Invoke(
    void *context, std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
    TriAevumMutableBytesV1 response, std::size_t *responseSize) {
  if (context == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  return static_cast<FilesystemHostServiceAdapterV1 *>(context)->Dispatch(
      operation, request, response, responseSize);
}

TriAevumModuleStatusV1 FilesystemHostServiceAdapterV1::Dispatch(
    std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
    TriAevumMutableBytesV1 response, std::size_t *responseSize) {
  if (responseSize == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  if (operation == TRIAEVUM_FILESYSTEM_OPEN_V1) {
    TriAevumFilesystemOpenRequestV1 decoded{};
    TriAevumModuleStatusV1 status = DecodeServiceRequest(request, &decoded);
    std::string_view path;
    if (status == TRIAEVUM_MODULE_OK_V1) {
      status = DecodePath(request, decoded.header.struct_size,
                          decoded.utf8_path, &path);
    }
    if (status != TRIAEVUM_MODULE_OK_V1 || !ValidRoot(decoded.root) ||
        decoded.flags == 0U || (decoded.flags & ~kKnownOpenFlags) != 0U ||
        ((decoded.flags & (TRIAEVUM_FILESYSTEM_OPEN_CREATE_V1 |
                           TRIAEVUM_FILESYSTEM_OPEN_TRUNCATE_V1)) != 0U &&
         (decoded.flags & TRIAEVUM_FILESYSTEM_OPEN_WRITE_V1) == 0U)) {
      return status == TRIAEVUM_MODULE_OK_V1
                 ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
                 : status;
    }
    FilesystemOpenResultV1 opened;
    status = mBackend.Open(decoded.root, path, decoded.flags, &opened);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    const TriAevumFilesystemOpenResponseV1 result = {
        ResponseHeader<TriAevumFilesystemOpenResponseV1>(), opened.handle,
        opened.size};
    return EncodeServiceResponse(result, response, responseSize);
  }

  if (operation == TRIAEVUM_FILESYSTEM_REMOVE_FILE_V1) {
    TriAevumFilesystemRemoveFileRequestV1 decoded{};
    auto status = DecodeServiceRequest(request, &decoded);
    std::string_view path;
    if (status == TRIAEVUM_MODULE_OK_V1)
      status = DecodePath(request, decoded.header.struct_size, decoded.utf8_path, &path);
    if (status != TRIAEVUM_MODULE_OK_V1) return status;
    if (decoded.root != TRIAEVUM_FILESYSTEM_SAVE_V1 || decoded.reserved != 0U)
      return TRIAEVUM_MODULE_MALFORMED_REQUEST_V1;
    *responseSize = sizeof(TriAevumFilesystemRemoveFileResponseV1);
    if (response.data == nullptr || response.size < *responseSize)
      return TRIAEVUM_MODULE_RESPONSE_TOO_SMALL_V1;
    bool removed = false;
    status = mBackend.RemoveFile(decoded.root, path, &removed);
    if (status != TRIAEVUM_MODULE_OK_V1) return status;
    const TriAevumFilesystemRemoveFileResponseV1 result = {
        ResponseHeader<TriAevumFilesystemRemoveFileResponseV1>(), removed ? 1U : 0U, 0U};
    return EncodeServiceResponse(result, response, responseSize);
  }

  if (operation == TRIAEVUM_FILESYSTEM_READ_V1) {
    TriAevumFilesystemReadRequestV1 decoded{};
    const TriAevumModuleStatusV1 decodeStatus =
        DecodeServiceRequest(request, &decoded);
    if (decodeStatus != TRIAEVUM_MODULE_OK_V1 || decoded.handle == 0U ||
        decoded.reserved != 0U || decoded.requested_size > kMaximumIoBytes) {
      return decodeStatus == TRIAEVUM_MODULE_OK_V1
                 ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
                 : decodeStatus;
    }
    const std::size_t required =
        sizeof(TriAevumFilesystemReadResponseV1) + decoded.requested_size;
    *responseSize = required;
    if (response.data == nullptr || response.size < required) {
      return TRIAEVUM_MODULE_RESPONSE_TOO_SMALL_V1;
    }
    FilesystemReadResultV1 read;
    const TriAevumModuleStatusV1 status =
        mBackend.Read(decoded.handle, decoded.offset,
                      {response.data + sizeof(TriAevumFilesystemReadResponseV1),
                       decoded.requested_size},
                      &read);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    if (read.returnedSize > decoded.requested_size) {
      return TRIAEVUM_MODULE_HOST_ERROR_V1;
    }
    const TriAevumFilesystemReadResponseV1 result = {
        ResponseHeader<TriAevumFilesystemReadResponseV1>(),
        read.returnedSize,
        read.endOfFile ? 1U : 0U,
        {static_cast<std::uint32_t>(sizeof(TriAevumFilesystemReadResponseV1)),
         read.returnedSize}};
    std::memcpy(response.data, &result, sizeof(result));
    *responseSize = sizeof(result) + read.returnedSize;
    return TRIAEVUM_MODULE_OK_V1;
  }

  if (operation == TRIAEVUM_FILESYSTEM_WRITE_V1) {
    TriAevumFilesystemWriteRequestV1 decoded{};
    TriAevumModuleStatusV1 status = DecodeServiceRequest(request, &decoded);
    if (status != TRIAEVUM_MODULE_OK_V1 || decoded.handle == 0U ||
        decoded.data.size > kMaximumIoBytes) {
      return status == TRIAEVUM_MODULE_OK_V1
                 ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
                 : status;
    }
    std::span<const std::uint8_t> data;
    status = ResolveServicePayload(request, decoded.header.struct_size,
                                   decoded.data, &data);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    std::uint32_t written = 0U;
    status = mBackend.Write(decoded.handle, decoded.offset, data, &written);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    if (written > data.size()) {
      return TRIAEVUM_MODULE_HOST_ERROR_V1;
    }
    const TriAevumFilesystemWriteResponseV1 result = {
        ResponseHeader<TriAevumFilesystemWriteResponseV1>(), written, 0U};
    return EncodeServiceResponse(result, response, responseSize);
  }

  if (operation == TRIAEVUM_FILESYSTEM_CLOSE_V1) {
    TriAevumFilesystemCloseRequestV1 decoded{};
    const TriAevumModuleStatusV1 status =
        DecodeServiceRequest(request, &decoded);
    if (status != TRIAEVUM_MODULE_OK_V1 || decoded.handle == 0U) {
      return status == TRIAEVUM_MODULE_OK_V1
                 ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
                 : status;
    }
    const TriAevumModuleStatusV1 backendStatus = mBackend.Close(decoded.handle);
    return backendStatus == TRIAEVUM_MODULE_OK_V1
               ? EmptyResponse<TriAevumFilesystemCloseResponseV1>(response,
                                                                  responseSize)
               : backendStatus;
  }

  if (operation == TRIAEVUM_FILESYSTEM_STAT_V1) {
    TriAevumFilesystemStatRequestV1 decoded{};
    TriAevumModuleStatusV1 status = DecodeServiceRequest(request, &decoded);
    std::string_view path;
    if (status == TRIAEVUM_MODULE_OK_V1) {
      status = DecodePath(request, decoded.header.struct_size,
                          decoded.utf8_path, &path);
    }
    if (status != TRIAEVUM_MODULE_OK_V1 || !ValidRoot(decoded.root) ||
        decoded.reserved != 0U) {
      return status == TRIAEVUM_MODULE_OK_V1
                 ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
                 : status;
    }
    FilesystemStatV1 stat;
    status = mBackend.Stat(decoded.root, path, &stat);
    constexpr TriAevumFilesystemStatFlagsV1 knownFlags =
        TRIAEVUM_FILESYSTEM_STAT_REGULAR_FILE_V1 |
        TRIAEVUM_FILESYSTEM_STAT_DIRECTORY_V1;
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    if (stat.flags == 0U || (stat.flags & ~knownFlags) != 0U) {
      return TRIAEVUM_MODULE_HOST_ERROR_V1;
    }
    const TriAevumFilesystemStatResponseV1 result = {
        ResponseHeader<TriAevumFilesystemStatResponseV1>(), stat.size,
        stat.modifiedTimeNs, stat.flags, 0U};
    return EncodeServiceResponse(result, response, responseSize);
  }

  if (operation == TRIAEVUM_FILESYSTEM_RESIZE_V1) {
    TriAevumFilesystemResizeRequestV1 decoded{};
    const TriAevumModuleStatusV1 status =
        DecodeServiceRequest(request, &decoded);
    if (status != TRIAEVUM_MODULE_OK_V1 || decoded.handle == 0U) {
      return status == TRIAEVUM_MODULE_OK_V1
                 ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
                 : status;
    }
    const TriAevumModuleStatusV1 backendStatus =
        mBackend.Resize(decoded.handle, decoded.size);
    return backendStatus == TRIAEVUM_MODULE_OK_V1
               ? EmptyResponse<TriAevumFilesystemResizeResponseV1>(response,
                                                                   responseSize)
               : backendStatus;
  }

  return TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1;
}

} // namespace triaevum::module
