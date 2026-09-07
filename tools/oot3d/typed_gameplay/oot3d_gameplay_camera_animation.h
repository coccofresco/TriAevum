#pragma once

#include "oot3d_gameplay_time.h"
#include "oot3d_gameplay_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace oot3d::gameplay {

struct CameraCurveHeaderWire {
  std::uint8_t Type = 0;
  std::array<std::byte, 3> Reserved01{};
  std::int32_t PointCount = 0;
  std::array<std::byte, 4> Reserved08{};
  std::int32_t LoopEndFrame = 0;
};

struct CameraLinearKeyframeWire {
  std::int32_t Frame = 0;
  float Value = 0.0F;
};

struct CameraHermiteKeyframeWire {
  std::int32_t Frame = 0;
  float Value = 0.0F;
  float TangentIn = 0.0F;
  float TangentOut = 0.0F;
};

struct CameraAnimationDefaultsWire {
  std::array<std::byte, 0x0C> Reserved00{};
  std::int32_t SegmentEndFrame = 0;
  std::array<std::byte, 0x08> Reserved10{};
  float Field8C = 0.0F;
  float Field90 = 0.0F;
  float Field94 = 0.0F;
  float Field80 = 0.0F;
  float Field84 = 0.0F;
  float Field88 = 0.0F;
  float Field1A2Source = 0.0F;
  std::array<std::byte, 0x08> Reserved34{};
  float Field144Source = 0.0F;
  std::array<std::byte, 0x04> Reserved40{};
  float FieldD0 = 0.0F;
};

struct CameraAnimationResourcePairWire {
  GuestPtr<CameraAnimationDefaultsWire> Defaults;
  GuestPtr<void> CmadContainer;
};

struct CameraAnimationCmadContainerHeaderWire {
  std::array<std::byte, 0x04> Reserved00{};
  std::int32_t RecordCount = 0;
};

struct CameraAnimationCmadRecordWire {
  std::array<std::byte, 0x04> Reserved00{};
  std::uint8_t Type = 0;
  std::array<std::byte, 0x03> Reserved05{};
  std::array<std::int16_t, 3> CurveOffsets{};
};

struct CameraAnimationStateWire {
  std::array<std::byte, 0x80> Reserved000{};
  float Field80 = 0.0F;
  float Field84 = 0.0F;
  float Field88 = 0.0F;
  float Field8C = 0.0F;
  float Field90 = 0.0F;
  float Field94 = 0.0F;
  std::array<std::byte, 0x38> Reserved098{};
  float FieldD0 = 0.0F;
  std::array<std::byte, 0x70> Reserved0D4{};
  float Field144 = 0.0F;
  std::array<std::byte, 0x5A> Reserved148{};
  std::int16_t Field1A2 = 0;
};

struct CameraDemo1Wire {
  std::array<std::byte, 0x16C> Reserved000{};
  GuestPtr<CameraAnimationStateWire> AttachedAnimationState;
  std::uint32_t AttachedActor = 0;
  std::uint16_t AnimationFlags = 0;
};

struct ActorCutsceneCameraOwnerWire {
  std::array<std::byte, 0xF18> Reserved000{};
  std::int32_t NativeFrameCursor = 0;
  std::int32_t AnimationIndex = -1;
  std::array<std::byte, 0x04> ReservedF20{};
  CameraAnimationResourcePairWire Resources;
};

struct CameraPlayStateWire {
  std::array<std::byte, 0xA54> Reserved000{};
  std::array<GuestPtr<CameraDemo1Wire>, 4> Cameras;
  std::int16_t ActiveCameraIndex = -1;
};

static_assert(sizeof(CameraCurveHeaderWire) == 0x10);
static_assert(sizeof(CameraLinearKeyframeWire) == 0x08);
static_assert(sizeof(CameraHermiteKeyframeWire) == 0x10);
static_assert(offsetof(CameraAnimationDefaultsWire, SegmentEndFrame) == 0x0C);
static_assert(offsetof(CameraAnimationDefaultsWire, Field8C) == 0x18);
static_assert(offsetof(CameraAnimationDefaultsWire, Field1A2Source) == 0x30);
static_assert(offsetof(CameraAnimationDefaultsWire, Field144Source) == 0x3C);
static_assert(offsetof(CameraAnimationDefaultsWire, FieldD0) == 0x44);
static_assert(sizeof(CameraAnimationDefaultsWire) == 0x48);
static_assert(sizeof(CameraAnimationResourcePairWire) == 0x08);
static_assert(offsetof(CameraAnimationCmadContainerHeaderWire, RecordCount) ==
              0x04);
static_assert(sizeof(CameraAnimationCmadContainerHeaderWire) == 0x08);
static_assert(offsetof(CameraAnimationCmadRecordWire, Type) == 0x04);
static_assert(offsetof(CameraAnimationCmadRecordWire, CurveOffsets) == 0x08);
static_assert(sizeof(CameraAnimationCmadRecordWire) == 0x0E);
static_assert(offsetof(CameraAnimationStateWire, Field80) == 0x80);
static_assert(offsetof(CameraAnimationStateWire, Field8C) == 0x8C);
static_assert(offsetof(CameraAnimationStateWire, FieldD0) == 0xD0);
static_assert(offsetof(CameraAnimationStateWire, Field144) == 0x144);
static_assert(offsetof(CameraAnimationStateWire, Field1A2) == 0x1A2);
static_assert(sizeof(CameraAnimationStateWire) == 0x1A4);
static_assert(offsetof(CameraDemo1Wire, AttachedAnimationState) == 0x16C);
static_assert(offsetof(CameraDemo1Wire, AnimationFlags) == 0x174);
static_assert(offsetof(ActorCutsceneCameraOwnerWire, NativeFrameCursor) ==
              0xF18);
static_assert(offsetof(ActorCutsceneCameraOwnerWire, AnimationIndex) == 0xF1C);
static_assert(offsetof(ActorCutsceneCameraOwnerWire, Resources) == 0xF24);
static_assert(sizeof(ActorCutsceneCameraOwnerWire) == 0xF2C);
static_assert(offsetof(CameraPlayStateWire, Cameras) == 0xA54);
static_assert(offsetof(CameraPlayStateWire, ActiveCameraIndex) == 0xA64);
static_assert(std::is_trivially_copyable_v<CameraCurveHeaderWire>);
static_assert(std::is_trivially_copyable_v<CameraAnimationDefaultsWire>);
static_assert(std::is_trivially_copyable_v<CameraAnimationStateWire>);
static_assert(std::is_trivially_copyable_v<ActorCutsceneCameraOwnerWire>);
static_assert(std::is_trivially_copyable_v<CameraPlayStateWire>);

enum class CameraCurveType : std::uint8_t {
  Linear = 1,
  Hermite = 2,
  Step = 3,
};

enum class CameraCurveSampleStatus : std::uint8_t {
  Ok,
  UnsupportedType,
  EmptyCurve,
  TruncatedPoints,
  ZeroFrameSpan,
  NullOutput,
};

struct CameraCurveView {
  CameraCurveType Type = CameraCurveType::Linear;
  std::int32_t PointCount = 0;
  std::int32_t LoopEndFrame = 0;
  std::span<const std::byte> PointBytes;
};

struct CameraAnimationSample {
  float Field80 = 0.0F;
  float Field84 = 0.0F;
  float Field88 = 0.0F;
  float Field8C = 0.0F;
  float Field90 = 0.0F;
  float Field94 = 0.0F;
  float FieldD0 = 0.0F;
  float Field144 = 0.0F;
  float Field1A2Scaled = 0.0F;
};

struct CameraAnimationChannelSample {
  float Value = 0.0F;
  bool Present = false;
};

using CameraAnimationChannelSamples =
    std::array<CameraAnimationChannelSample, 3>;

CameraCurveSampleStatus SampleCameraCurve(const CameraCurveView &curve,
                                          float frame, bool loop,
                                          float *output) noexcept;

void InitializeCameraAnimationSample(
    const CameraAnimationDefaultsWire &defaults,
    CameraAnimationSample *sample) noexcept;

std::size_t CameraAnimationRecordChannelCount(std::uint8_t recordType) noexcept;

void ApplyCameraAnimationRecord(std::uint8_t recordType,
                                const CameraAnimationChannelSamples &channels,
                                CameraAnimationSample *sample) noexcept;

float ResolveCameraAnimationContinuousFrame(std::int32_t nativeFrame,
                                            const TimeContext &time) noexcept;

} // namespace oot3d::gameplay
