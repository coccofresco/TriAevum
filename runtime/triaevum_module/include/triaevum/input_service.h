#ifndef TRIAEVUM_INPUT_SERVICE_H
#define TRIAEVUM_INPUT_SERVICE_H

#include "triaevum/service_abi.h"

#include <cstdint>

namespace triaevum::module {

struct InputStateV1 {
  std::uint64_t sampleSequence = 0U;
  TriAevumInputButtonsV1 buttons = 0U;
  float leftStickX = 0.0F;
  float leftStickY = 0.0F;
  float rightStickX = 0.0F;
  float rightStickY = 0.0F;
  float touchX = 0.0F;
  float touchY = 0.0F;
  float gyroscopeX = 0.0F;
  float gyroscopeY = 0.0F;
  float gyroscopeZ = 0.0F;
  float accelerometerX = 0.0F;
  float accelerometerY = 0.0F;
  float accelerometerZ = 0.0F;
  TriAevumInputStateFlagsV1 flags = 0U;
};

[[nodiscard]] bool ValidateInputStateV1(const InputStateV1 &state) noexcept;

} // namespace triaevum::module

#endif
