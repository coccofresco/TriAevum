#pragma once

#include "oot3d_gameplay_time.h"

#include <cstdint>

namespace oot3d::gameplay {

struct CameraLogicalCounterResult {
  std::uint32_t Value = 0;
  bool Mutated = false;
};

struct CameraWaterDistortionTimerResult {
  std::int16_t Value = 0;
  bool Mutated = false;
};

struct CameraModeCountdownResult {
  std::int16_t Value = 0;
  bool Mutated = false;
};

enum class CameraSpecial5TimerAction : std::uint8_t {
  Hold,
  Decrement,
  ZeroTransition,
  Terminal,
};

struct CameraSpecial5TimerResult {
  std::int16_t Value = 0;
  CameraSpecial5TimerAction Action = CameraSpecial5TimerAction::Hold;
};

// Mirrors Camera_Update's native wrapping increment. The field remains an
// integer on the original 30 Hz timeline while the surrounding solve can run
// on every simulation tick.
CameraLogicalCounterResult AdvanceCameraFloorMissCounter(
    std::uint32_t value, const TimeContext &time) noexcept;

// Mirrors the native nonzero interface-delay decrement. Callers retain the
// original branch precondition and integer wire representation.
CameraLogicalCounterResult AdvanceCameraInterfaceDelay(
    std::uint32_t value, const TimeContext &time) noexcept;

// Camera_CheckWater treats this signed field as a positive countdown and
// preserves zero/negative sentinels.
CameraWaterDistortionTimerResult AdvanceCameraWaterDistortionTimer(
    std::int16_t value, const TimeContext &time) noexcept;

// Evaluates the linear countdown between two integer legacy states without
// changing the guest field consumed by residual code and save data.
float SampleCameraWaterDistortionTimer(
    std::int16_t value, const TimeContext &time) noexcept;

// Mirrors the unconditional SUB/STRH countdown pairs shared by native camera
// mode callbacks. The owning callback retains all branch preconditions.
CameraModeCountdownResult AdvanceCameraModeFrameCountdown(
    std::int16_t value, const TimeContext &time) noexcept;

// Special5 uses zero as a one-shot transition state and negative values as
// terminal sentinels, so it cannot share the unconditional mode countdown.
CameraSpecial5TimerResult AdvanceCameraSpecial5Timer(
    std::int16_t value, const TimeContext &time) noexcept;

} // namespace oot3d::gameplay
