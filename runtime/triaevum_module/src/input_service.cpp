#include "triaevum/input_service.h"

#include <cmath>

namespace triaevum::module {
namespace {

constexpr TriAevumInputStateFlagsV1 kKnownInputFlags =
    TRIAEVUM_INPUT_TOUCH_VALID_V1 | TRIAEVUM_INPUT_TOUCH_PRESSED_V1 |
    TRIAEVUM_INPUT_GYROSCOPE_VALID_V1 | TRIAEVUM_INPUT_ACCELEROMETER_VALID_V1;

bool FiniteUnit(float value) noexcept {
  return std::isfinite(value) && value >= -1.0F && value <= 1.0F;
}

} // namespace

bool ValidateInputStateV1(const InputStateV1 &state) noexcept {
  const bool touchValid = (state.flags & TRIAEVUM_INPUT_TOUCH_VALID_V1) != 0U;
  const bool touchPressed =
      (state.flags & TRIAEVUM_INPUT_TOUCH_PRESSED_V1) != 0U;
  if ((state.buttons & ~static_cast<TriAevumInputButtonsV1>(
                           TRIAEVUM_INPUT_BUTTON_MASK_V1)) != 0U ||
      (state.flags & ~kKnownInputFlags) != 0U ||
      (touchPressed && !touchValid) || !FiniteUnit(state.leftStickX) ||
      !FiniteUnit(state.leftStickY) || !FiniteUnit(state.rightStickX) ||
      !FiniteUnit(state.rightStickY)) {
    return false;
  }
  if (touchValid &&
      (!std::isfinite(state.touchX) || !std::isfinite(state.touchY) ||
       state.touchX < 0.0F || state.touchX > 1.0F || state.touchY < 0.0F ||
       state.touchY > 1.0F)) {
    return false;
  }
  return std::isfinite(state.gyroscopeX) && std::isfinite(state.gyroscopeY) &&
         std::isfinite(state.gyroscopeZ) &&
         std::isfinite(state.accelerometerX) &&
         std::isfinite(state.accelerometerY) &&
         std::isfinite(state.accelerometerZ);
}

} // namespace triaevum::module
