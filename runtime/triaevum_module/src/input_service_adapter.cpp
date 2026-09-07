#include "triaevum/input_service_adapter.h"

#include "triaevum/service_codec.h"

namespace triaevum::module {

InputHostServiceAdapterV1::InputHostServiceAdapterV1(
    InputServiceBackendV1 &backend)
    : mBackend(backend) {}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL InputHostServiceAdapterV1::Invoke(
    void *context, std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
    TriAevumMutableBytesV1 response, std::size_t *responseSize) {
  if (context == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  return static_cast<InputHostServiceAdapterV1 *>(context)->Dispatch(
      operation, request, response, responseSize);
}

TriAevumModuleStatusV1 InputHostServiceAdapterV1::Dispatch(
    std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
    TriAevumMutableBytesV1 response, std::size_t *responseSize) {
  if (responseSize == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  if (operation != TRIAEVUM_INPUT_READ_STATE_V1) {
    return TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1;
  }

  TriAevumInputReadStateRequestV1 decoded{};
  TriAevumModuleStatusV1 status = DecodeServiceRequest(request, &decoded);
  if (status != TRIAEVUM_MODULE_OK_V1 || decoded.reserved != 0U) {
    return status == TRIAEVUM_MODULE_OK_V1
               ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
               : status;
  }
  InputStateV1 state;
  status = mBackend.ReadState(decoded.player_index, &state);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  if (!ValidateInputStateV1(state)) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  const TriAevumInputReadStateResponseV1 result = {
      ResponseHeader<TriAevumInputReadStateResponseV1>(),
      state.sampleSequence,
      state.buttons,
      state.leftStickX,
      state.leftStickY,
      state.rightStickX,
      state.rightStickY,
      state.touchX,
      state.touchY,
      state.gyroscopeX,
      state.gyroscopeY,
      state.gyroscopeZ,
      state.accelerometerX,
      state.accelerometerY,
      state.accelerometerZ,
      state.flags,
      0U,
  };
  return EncodeServiceResponse(result, response, responseSize);
}

} // namespace triaevum::module
