#include "triaevum/input_service_adapter.h"
#include "triaevum/input_service_client.h"
#include "triaevum/service_codec.h"
#include "triaevum/service_registry.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using namespace triaevum::module;

bool Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

class RecordingInputBackend final : public InputServiceBackendV1 {
public:
  TriAevumModuleStatusV1 ReadState(std::uint32_t playerIndex,
                                   InputStateV1 *state) override {
    if (state == nullptr) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    if (playerIndex != 0U) {
      return TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1;
    }
    LastPlayer = playerIndex;
    ++Reads;
    *state = State;
    return TRIAEVUM_MODULE_OK_V1;
  }

  InputStateV1 State;
  std::uint32_t LastPlayer = 99U;
  std::uint32_t Reads = 0U;
};

} // namespace

int main() {
  static_assert(TRIAEVUM_INPUT_BUTTON_A_V1 == 1U << 0U);
  static_assert(TRIAEVUM_INPUT_BUTTON_ZR_V1 == 1U << 15U);

  RecordingInputBackend backend;
  backend.State = {
      71U,
      TRIAEVUM_INPUT_BUTTON_A_V1 | TRIAEVUM_INPUT_BUTTON_START_V1 |
          TRIAEVUM_INPUT_BUTTON_ZR_V1,
      -0.75F,
      0.5F,
      0.25F,
      -0.125F,
      0.4F,
      0.6F,
      1.0F,
      -2.0F,
      3.0F,
      0.1F,
      -0.9F,
      0.2F,
      TRIAEVUM_INPUT_TOUCH_VALID_V1 | TRIAEVUM_INPUT_TOUCH_PRESSED_V1 |
          TRIAEVUM_INPUT_GYROSCOPE_VALID_V1 |
          TRIAEVUM_INPUT_ACCELEROMETER_VALID_V1,
  };
  InputHostServiceAdapterV1 adapter(backend);
  HostServiceRegistry registry({});
  bool ok = true;
  ok &= Expect(registry.Register(TRIAEVUM_SERVICE_INPUT_V1,
                                 InputHostServiceAdapterV1::Invoke, &adapter) ==
                   ServiceRegistrationResult::Registered,
               "input service registration failed");
  const TriAevumHostApiV1 host = registry.SealAndCreateHostApi();
  InputServiceClientV1 client(&host);
  ok &= Expect(client.IsAvailable(), "input client is unavailable");

  InputStateV1 observed;
  ok &= Expect(client.ReadState(0U, &observed) == TRIAEVUM_MODULE_OK_V1 &&
                   observed.sampleSequence == backend.State.sampleSequence &&
                   observed.buttons == backend.State.buttons &&
                   observed.leftStickX == backend.State.leftStickX &&
                   observed.rightStickY == backend.State.rightStickY &&
                   observed.touchX == backend.State.touchX &&
                   observed.gyroscopeZ == backend.State.gyroscopeZ &&
                   observed.accelerometerY == backend.State.accelerometerY &&
                   observed.flags == backend.State.flags && backend.Reads == 1U,
               "input state did not cross the service boundary");
  ok &= Expect(client.ReadState(1U, &observed) ==
                   TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1,
               "input service accepted an unsupported player");

  backend.State.flags = TRIAEVUM_INPUT_TOUCH_PRESSED_V1;
  ok &= Expect(client.ReadState(0U, &observed) == TRIAEVUM_MODULE_HOST_ERROR_V1,
               "input service accepted pressed touch without coordinates");
  backend.State.flags = 0U;
  backend.State.leftStickX = std::numeric_limits<float>::quiet_NaN();
  ok &= Expect(client.ReadState(0U, &observed) == TRIAEVUM_MODULE_HOST_ERROR_V1,
               "input service accepted non-finite analog state");

  TriAevumInputReadStateRequestV1 malformed = {
      RequestHeader<TriAevumInputReadStateRequestV1>(), 0U, 1U};
  TriAevumInputReadStateResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  ok &= Expect(
      host.invoke_service(
          host.host_context, TRIAEVUM_SERVICE_INPUT_V1,
          TRIAEVUM_INPUT_READ_STATE_V1,
          {reinterpret_cast<const std::uint8_t *>(&malformed),
           sizeof(malformed)},
          {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
          &responseSize) == TRIAEVUM_MODULE_MALFORMED_REQUEST_V1,
      "input adapter accepted a nonzero reserved field");
  return ok ? 0 : 1;
}
