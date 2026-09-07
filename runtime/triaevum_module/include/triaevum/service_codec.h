#ifndef TRIAEVUM_SERVICE_CODEC_H
#define TRIAEVUM_SERVICE_CODEC_H

#include "triaevum/service_abi.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace triaevum::module {

template <typename T>
TriAevumModuleStatusV1 DecodeServiceRequest(TriAevumReadOnlyBytesV1 request,
                                            T *decoded) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (decoded == nullptr || request.data == nullptr ||
      request.size < sizeof(T)) {
    return TRIAEVUM_MODULE_MALFORMED_REQUEST_V1;
  }
  TriAevumServiceRequestHeaderV1 header{};
  std::memcpy(&header, request.data, sizeof(header));
  if (header.schema_version != TRIAEVUM_SERVICE_SCHEMA_V1 ||
      header.struct_size < sizeof(T) || header.struct_size > request.size) {
    return TRIAEVUM_MODULE_MALFORMED_REQUEST_V1;
  }
  std::memcpy(decoded, request.data, sizeof(T));
  return TRIAEVUM_MODULE_OK_V1;
}

inline TriAevumModuleStatusV1
ResolveServicePayload(TriAevumReadOnlyBytesV1 request,
                      std::uint32_t minimumOffset,
                      TriAevumPayloadRangeV1 range,
                      std::span<const std::uint8_t> *payload) {
  if (payload == nullptr || request.data == nullptr ||
      range.offset < minimumOffset || range.offset > request.size ||
      range.size > request.size - range.offset) {
    return TRIAEVUM_MODULE_MALFORMED_REQUEST_V1;
  }
  *payload = {request.data + range.offset, range.size};
  return TRIAEVUM_MODULE_OK_V1;
}

template <typename T>
TriAevumModuleStatusV1 EncodeServiceResponse(
    const T &response, TriAevumMutableBytesV1 destination,
    std::size_t *responseSize) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (responseSize == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  *responseSize = sizeof(T);
  if (destination.data == nullptr || destination.size < sizeof(T)) {
    return TRIAEVUM_MODULE_RESPONSE_TOO_SMALL_V1;
  }
  std::memcpy(destination.data, &response, sizeof(T));
  return TRIAEVUM_MODULE_OK_V1;
}

template <typename T> constexpr TriAevumServiceRequestHeaderV1 RequestHeader() {
  static_assert(sizeof(T) <= UINT32_MAX);
  return {static_cast<std::uint32_t>(sizeof(T)),
          TRIAEVUM_SERVICE_SCHEMA_V1};
}

template <typename T>
constexpr TriAevumServiceResponseHeaderV1 ResponseHeader() {
  static_assert(sizeof(T) <= UINT32_MAX);
  return {static_cast<std::uint32_t>(sizeof(T)),
          TRIAEVUM_SERVICE_SCHEMA_V1};
}

} // namespace triaevum::module

#endif
