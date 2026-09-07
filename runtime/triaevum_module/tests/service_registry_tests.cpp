#include "triaevum/service_abi.h"
#include "triaevum/service_codec.h"
#include "triaevum/service_registry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace triaevum::module;

bool Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

struct RuntimeState {
  bool logged = false;
  std::uint32_t submittedRegisters = 0U;
};

void TRIAEVUM_ABI_CALL Log(void *context, TriAevumLogLevelV1,
                           const char *, std::size_t) {
  static_cast<RuntimeState *>(context)->logged = true;
}

std::uint64_t TRIAEVUM_ABI_CALL Time(void *) { return 0x12345678U; }

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
PicaService(void *context, std::uint32_t operation,
            TriAevumReadOnlyBytesV1 request, TriAevumMutableBytesV1 response,
            std::size_t *responseSize) {
  if (operation != TRIAEVUM_PICA_WRITE_REGISTERS_V1) {
    return TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1;
  }
  TriAevumPicaWriteRegistersRequestV1 decoded{};
  TriAevumModuleStatusV1 status = DecodeServiceRequest(request, &decoded);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  std::span<const std::uint8_t> values;
  status = ResolveServicePayload(request, decoded.header.struct_size,
                                 decoded.values, &values);
  if (status != TRIAEVUM_MODULE_OK_V1 ||
      decoded.register_count > UINT32_MAX / sizeof(std::uint32_t) ||
      values.size() != static_cast<std::size_t>(decoded.register_count) *
              sizeof(std::uint32_t)) {
    return TRIAEVUM_MODULE_MALFORMED_REQUEST_V1;
  }
  auto *state = static_cast<RuntimeState *>(context);
  state->submittedRegisters += decoded.register_count;
  const TriAevumPicaWriteRegistersResponseV1 result = {
      ResponseHeader<TriAevumPicaWriteRegistersResponseV1>()};
  return EncodeServiceResponse(result, response, responseSize);
}

std::vector<std::uint8_t> MakePicaRequest() {
  const std::array<std::uint32_t, 2> words = {0x11223344U, 0x55667788U};
  TriAevumPicaWriteRegistersRequestV1 request{};
  request.header = RequestHeader<TriAevumPicaWriteRegistersRequestV1>();
  request.base_register = 0x100U;
  request.register_count = static_cast<std::uint32_t>(words.size());
  request.values = {
      sizeof(TriAevumPicaWriteRegistersRequestV1),
      static_cast<std::uint32_t>(sizeof(words)),
  };
  std::vector<std::uint8_t> bytes(sizeof(request) + sizeof(words));
  std::memcpy(bytes.data(), &request, sizeof(request));
  std::memcpy(bytes.data() + sizeof(request), words.data(), sizeof(words));
  return bytes;
}

} // namespace

int main() {
  bool ok = true;
  RuntimeState runtime;
  HostServiceRegistry registry({&runtime, Log, Time});
  ok &= Expect(registry.Register(TRIAEVUM_SERVICE_PICA_V1, PicaService,
                                 &runtime) ==
                   ServiceRegistrationResult::Registered,
               "PICA service was not registered");
  ok &= Expect(registry.Register(TRIAEVUM_SERVICE_PICA_V1, PicaService,
                                 &runtime) ==
                   ServiceRegistrationResult::DuplicateService,
               "duplicate service registration was accepted");
  ok &= Expect(registry.Has(TRIAEVUM_SERVICE_PICA_V1),
               "registered service was not discoverable");

  TriAevumHostApiV1 host = registry.SealAndCreateHostApi();
  ok &= Expect(registry.IsSealed(), "host service registry was not sealed");
  ok &= Expect(registry.Register(TRIAEVUM_SERVICE_AUDIO_V1, PicaService,
                                 &runtime) ==
                   ServiceRegistrationResult::RegistrySealed,
               "sealed registry accepted a new service");
  host.log(host.host_context, TRIAEVUM_LOG_INFO_V1, "test", 4U);
  ok &= Expect(runtime.logged, "runtime log callback was not forwarded");
  ok &= Expect(host.monotonic_time_ns(host.host_context) == 0x12345678U,
               "runtime clock callback was not forwarded");

  auto request = MakePicaRequest();
  TriAevumPicaWriteRegistersResponseV1 response{};
  std::size_t responseSize = 0U;
  TriAevumModuleStatusV1 status = host.invoke_service(
      host.host_context, TRIAEVUM_SERVICE_PICA_V1,
      TRIAEVUM_PICA_WRITE_REGISTERS_V1,
      {request.data(), request.size()},
      {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
      &responseSize);
  ok &= Expect(status == TRIAEVUM_MODULE_OK_V1 &&
                   responseSize == sizeof(response) &&
                   runtime.submittedRegisters == 2U,
               "typed PICA service request did not cross the registry");

  responseSize = 99U;
  status = host.invoke_service(host.host_context, TRIAEVUM_SERVICE_AUDIO_V1,
                               TRIAEVUM_AUDIO_SUBMIT_PCM_V1,
                               {nullptr, 0U}, {nullptr, 0U}, &responseSize);
  ok &= Expect(status == TRIAEVUM_MODULE_SERVICE_NOT_FOUND_V1 &&
                   responseSize == 0U,
               "unknown service was not rejected deterministically");

  request[0] = 1U;
  status = host.invoke_service(
      host.host_context, TRIAEVUM_SERVICE_PICA_V1,
      TRIAEVUM_PICA_WRITE_REGISTERS_V1,
      {request.data(), request.size()},
      {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
      &responseSize);
  ok &= Expect(status == TRIAEVUM_MODULE_MALFORMED_REQUEST_V1,
               "malformed typed request was accepted");

  request = MakePicaRequest();
  std::array<std::uint8_t, 1> shortResponse{};
  status = host.invoke_service(
      host.host_context, TRIAEVUM_SERVICE_PICA_V1,
      TRIAEVUM_PICA_WRITE_REGISTERS_V1,
      {request.data(), request.size()},
      {shortResponse.data(), shortResponse.size()}, &responseSize);
  ok &= Expect(status == TRIAEVUM_MODULE_RESPONSE_TOO_SMALL_V1 &&
                   responseSize == sizeof(response),
               "response sizing contract was not preserved");

  return ok ? 0 : 1;
}
