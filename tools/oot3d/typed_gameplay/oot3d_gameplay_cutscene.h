#pragma once

#include "oot3d_gameplay_time.h"
#include "oot3d_gameplay_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace oot3d::gameplay {

struct CutsceneCommandStream;

struct CutsceneActorCueWire {
  std::uint16_t Action = 0;
  std::uint16_t StartFrame = 0;
  std::uint16_t EndFrame = 0;
  std::array<std::byte, 0x06> Reserved06{};
  std::int32_t StartX = 0;
  std::int32_t StartY = 0;
  std::int32_t StartZ = 0;
  std::int32_t EndX = 0;
  std::int32_t EndY = 0;
  std::int32_t EndZ = 0;
};

// Sparse wire layouts for the fields owned by Cutscene_UpdateFrameAndCommands.
// Unknown ranges remain opaque and are not copied into source-facing state.
struct CutsceneContextWire {
  std::array<std::byte, 0x20> Reserved00{};
  std::uint16_t Frame = 0;
  std::array<std::byte, 0x258> Reserved22{};
  std::uint8_t CommandState27A = 0;
};

struct CutscenePlayStateWire {
  std::array<std::byte, 0x229C> Reserved0000{};
  GuestPtr<CutsceneCommandStream> ActiveCutsceneData;
  std::array<std::byte, 0x18> Reserved22A0{};
  std::uint16_t Frame = 0;
  std::array<std::byte, 0x22> Reserved22BA{};
  std::array<GuestPtr<CutsceneActorCueWire>, 9> ActorCueSlots{};
};

// Sparse owner-side timing fields used to distinguish normal advancement from
// the native backend-clock/catch-up path.
struct CutsceneFrameOwnerPlayStateWire {
  std::array<std::byte, 0x101> Reserved0000{};
  std::uint8_t SceneStateMode = 0;
  std::array<std::byte, 0x5F26> Reserved0102{};
  std::uint8_t BackendClockActive = 0;
  std::uint8_t BackendClockReady = 0;
  std::array<std::byte, 0x1C36> Reserved602A{};
  std::int32_t BackendClockTicks = -1;
};

static_assert(offsetof(CutsceneActorCueWire, StartFrame) == 0x02);
static_assert(offsetof(CutsceneActorCueWire, EndFrame) == 0x04);
static_assert(offsetof(CutsceneActorCueWire, StartX) == 0x0C);
static_assert(offsetof(CutsceneActorCueWire, EndX) == 0x18);
static_assert(sizeof(CutsceneActorCueWire) == 0x24);
static_assert(offsetof(CutsceneContextWire, Frame) == 0x20);
static_assert(offsetof(CutsceneContextWire, CommandState27A) == 0x27A);
static_assert(sizeof(CutsceneContextWire) == 0x27C);
static_assert(offsetof(CutscenePlayStateWire, ActiveCutsceneData) == 0x229C);
static_assert(offsetof(CutscenePlayStateWire, Frame) == 0x22B8);
static_assert(offsetof(CutscenePlayStateWire, ActorCueSlots) == 0x22DC);
static_assert(sizeof(CutscenePlayStateWire) == 0x2300);
static_assert(offsetof(CutsceneFrameOwnerPlayStateWire, SceneStateMode) ==
              0x101);
static_assert(
    offsetof(CutsceneFrameOwnerPlayStateWire, BackendClockActive) == 0x6028);
static_assert(
    offsetof(CutsceneFrameOwnerPlayStateWire, BackendClockReady) == 0x6029);
static_assert(
    offsetof(CutsceneFrameOwnerPlayStateWire, BackendClockTicks) == 0x7C60);
static_assert(sizeof(CutsceneFrameOwnerPlayStateWire) == 0x7C64);
static_assert(std::is_standard_layout_v<CutsceneActorCueWire>);
static_assert(std::is_trivially_copyable_v<CutsceneActorCueWire>);
static_assert(std::is_standard_layout_v<CutsceneFrameOwnerPlayStateWire>);
static_assert(std::is_trivially_copyable_v<CutsceneFrameOwnerPlayStateWire>);
static_assert(std::is_standard_layout_v<CutsceneContextWire>);
static_assert(std::is_trivially_copyable_v<CutsceneContextWire>);
static_assert(std::is_standard_layout_v<CutscenePlayStateWire>);
static_assert(std::is_trivially_copyable_v<CutscenePlayStateWire>);

// Continuous consumers sample this cursor every simulation tick. The legacy
// field remains an integer so residual A32 code and save data retain their ABI.
struct CutsceneFrameCursor {
  double PreviousFrame = 0.0;
  double CurrentFrame = 0.0;
  std::uint16_t LegacyVisibleFrame = 0;
  bool CrossedLegacyFrame = false;
};

struct CutsceneNormalFrameAdvance {
  CutsceneFrameCursor Cursor;
  bool DispatchDiscreteCommands = false;
};

// Implements only the normal frame-owner path. The separate OOT3D
// wall-clock/catch-up path already computes a 30 Hz target and remains native.
CutsceneNormalFrameAdvance AdvanceCutsceneNormalFrame(
    std::uint16_t &legacyFrame, const TimeContext &time) noexcept;

// Samples continuous cue consumers without changing the integer frame exposed
// to command dispatch, exact-frame triggers, residual A32, or save data.
double ResolveCutsceneContinuousFrame(std::uint16_t legacyFrame,
                                      const TimeContext &time) noexcept;
float InterpolateCutsceneCueWeight(std::uint16_t startFrame,
                                   std::uint16_t endFrame,
                                   double currentFrame) noexcept;

} // namespace oot3d::gameplay
