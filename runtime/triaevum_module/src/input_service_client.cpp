#include "triaevum/input_service_client.h"

#include "triaevum/service_codec.h"

#include <cstddef>
#include <cstring>
#include <span>

namespace triaevum::module {
namespace {

template <typename T> std::span<const std::uint8_t> AsBytes(const T &value) {
  return {reinterpret_cast<const std::uint8_t *>(&value), sizeof(T)};
}

} // namespace

InputServiceClientV1::InputServiceClientV1(const TriAevumHostApiV1 *host) {
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

bool InputServiceClientV1::IsAvailable() const noexcept { return mAvailable; }

TriAevumModuleStatusV1 InputServiceClientV1::Invoke(
    std::uint32_t operation, std::span<const std::uint8_t> request,
    std::span<std::uint8_t> response, std::size_t *responseSize) const {
  if (!mAvailable || responseSize == nullptr) {
    return mAvailable ? TRIAEVUM_MODULE_INVALID_ARGUMENT_V1
                      : TRIAEVUM_MODULE_INCOMPATIBLE_ABI_V1;
  }
  return mHost.invoke_service(mHost.host_context, TRIAEVUM_SERVICE_INPUT_V1,
                              operation, {request.data(), request.size()},
                              {response.data(), response.size()}, responseSize);
}

TriAevumModuleStatusV1
InputServiceClientV1::ReadState(std::uint32_t playerIndex,
                                InputStateV1 *state) const {
  if (state == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  const TriAevumInputReadStateRequestV1 request = {
      RequestHeader<TriAevumInputReadStateRequestV1>(), playerIndex, 0U};
  TriAevumInputReadStateResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  const TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_INPUT_READ_STATE_V1, AsBytes(request),
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  if (responseSize < sizeof(response) ||
      response.header.schema_version != TRIAEVUM_SERVICE_SCHEMA_V1 ||
      response.header.struct_size < sizeof(response) ||
      response.header.struct_size > responseSize || response.reserved != 0U) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  const InputStateV1 decoded = {
      response.sample_sequence, response.buttons,
      response.left_stick_x,    response.left_stick_y,
      response.right_stick_x,   response.right_stick_y,
      response.touch_x,         response.touch_y,
      response.gyroscope_x,     response.gyroscope_y,
      response.gyroscope_z,     response.accelerometer_x,
      response.accelerometer_y, response.accelerometer_z,
      response.flags,
  };
  if (!ValidateInputStateV1(decoded)) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  *state = decoded;
  return TRIAEVUM_MODULE_OK_V1;
}

} // namespace triaevum::module
