#pragma once

#include "oot3d_gameplay_time.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace oot3d::gameplay {

enum class CameraQuakeCallback : std::uint8_t {
  None = 0,
  SineRandom = 1,
  Random = 2,
  SineFade = 3,
  RandomFade = 4,
  Sine = 5,
  PerpetualSineRandom = 6,
};

#pragma pack(push, 1)
struct CameraQuakeRequestWire {
  std::int16_t RequestId = 0;
  std::int16_t InitialCountdown = 0;
  std::uint32_t CameraAddress = 0;
  std::uint8_t CallbackIndex = 0;
  std::uint8_t CallbackPadding = 0;
  std::int16_t VerticalAmplitude = 0;
  std::int16_t HorizontalAmplitude = 0;
  std::int16_t FovAmplitude = 0;
  std::int16_t RollAmplitude = 0;
  std::int16_t PitchOffset = 0;
  std::int16_t YawOffset = 0;
  std::int16_t Reserved16 = 0;
  std::int16_t Speed = 0;
  std::int16_t RelativeToCamera = 0;
  std::int16_t Countdown = 0;
  std::int16_t CameraSlot = 0;
  std::array<std::uint8_t, 4> Reserved20{};
};
#pragma pack(pop)

static_assert(sizeof(CameraQuakeRequestWire) == 0x24);
static_assert(offsetof(CameraQuakeRequestWire, InitialCountdown) == 0x02);
static_assert(offsetof(CameraQuakeRequestWire, CallbackIndex) == 0x08);
static_assert(offsetof(CameraQuakeRequestWire, Speed) == 0x18);
static_assert(offsetof(CameraQuakeRequestWire, Countdown) == 0x1C);

struct CameraQuakeSignalPlan {
  CameraQuakeCallback Callback = CameraQuakeCallback::None;
  std::int16_t CountdownBefore = 0;
  std::int16_t CountdownAfter = 0;
  std::int16_t SineAngle = 0;
  std::int32_t ReturnValue = 0;
  float Envelope = 1.0F;
  std::uint8_t RandomSampleCount = 0;
  bool Supported = false;
  bool Evaluate = false;
  bool NeedsSine = false;
  bool CountdownMutated = false;
};

struct CameraQuakeFactors {
  float Primary = 0.0F;
  float Secondary = 0.0F;
  bool Supported = false;
};

struct CameraQuakeRandomStep {
  std::uint32_t Seed = 0;
  std::uint32_t ScratchBits = 0;
  float Value = 0.0F;
};

CameraQuakeSignalPlan ResolveCameraQuakeSignal(
    const CameraQuakeRequestWire &request, const TimeContext &time) noexcept;

CameraQuakeFactors ResolveCameraQuakeFactors(
    const CameraQuakeSignalPlan &plan, float sine,
    std::span<const float> randomSamples) noexcept;

CameraQuakeRandomStep AdvanceCameraQuakeRandom(
    std::uint32_t seed) noexcept;

} // namespace oot3d::gameplay
