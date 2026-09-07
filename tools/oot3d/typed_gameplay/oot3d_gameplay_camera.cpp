#include "oot3d_gameplay_camera.h"

#include <bit>
#include <cmath>

namespace oot3d::gameplay {

CameraLogicalCounterResult AdvanceCameraFloorMissCounter(
    std::uint32_t value, const TimeContext &time) noexcept {
  if (!time.CrossedLogicalFrame) {
    return {value, false};
  }
  return {value + 1U, true};
}

CameraLogicalCounterResult AdvanceCameraInterfaceDelay(
    std::uint32_t value, const TimeContext &time) noexcept {
  if (!time.CrossedLogicalFrame || value == 0U) {
    return {value, false};
  }
  return {value - 1U, true};
}

CameraWaterDistortionTimerResult AdvanceCameraWaterDistortionTimer(
    std::int16_t value, const TimeContext &time) noexcept {
  if (!time.CrossedLogicalFrame || value <= 0) {
    return {value, false};
  }
  return {static_cast<std::int16_t>(value - 1), true};
}

float SampleCameraWaterDistortionTimer(
    std::int16_t value, const TimeContext &time) noexcept {
  if (value <= 0 || !std::isfinite(time.CurrentLogicalFrame)) {
    return static_cast<float>(value);
  }
  const double subframe =
      time.CurrentLogicalFrame - std::floor(time.CurrentLogicalFrame);
  return static_cast<float>(static_cast<double>(value) - subframe);
}

CameraModeCountdownResult AdvanceCameraModeFrameCountdown(
    std::int16_t value, const TimeContext &time) noexcept {
  if (!time.CrossedLogicalFrame) {
    return {value, false};
  }
  const std::uint16_t valueBits = std::bit_cast<std::uint16_t>(value);
  const auto decrementedBits =
      static_cast<std::uint16_t>(valueBits - std::uint16_t{1});
  return {std::bit_cast<std::int16_t>(decrementedBits), true};
}

CameraSpecial5TimerResult AdvanceCameraSpecial5Timer(
    std::int16_t value, const TimeContext &time) noexcept {
  if (value < 0) {
    return {value, CameraSpecial5TimerAction::Terminal};
  }
  if (!time.CrossedLogicalFrame) {
    return {value, CameraSpecial5TimerAction::Hold};
  }
  if (value > 0) {
    return {static_cast<std::int16_t>(value - 1),
            CameraSpecial5TimerAction::Decrement};
  }
  return {value, CameraSpecial5TimerAction::ZeroTransition};
}

} // namespace oot3d::gameplay
