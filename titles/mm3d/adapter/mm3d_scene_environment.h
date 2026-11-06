#pragma once

#include "mm3d_title_contracts.h"
#include "native_scene_environment_state.h"
#include "oot3d_environment_group.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace Oot3dNativeGame {
namespace Mm3dSceneEnvironmentLayout {
// Restoration donor ABI plus MM3D code SHA 6eec27b1... live proof:
// 1233 positive South Clock Town samples validate the scene/player/owner
// reads; unobstructed walk deltas validate signed heading axes. Other scenes
// and owner loss/restore still need integration coverage. Night: a live
// gameplay night sample (South Clock Town, day 1, 20:06) reads the night byte
// as 1 with the same owner checks (mm3d_scene_environment_tests, recorded
// night receipt); the double-time transition itself is not exercised.
inline constexpr uint32_t ContextSize = 0x11030U;
inline constexpr uint32_t Frame = 0x138U, StateType = 0x13DU, Scene = 0x148U;
inline constexpr uint8_t PlayStateType = 4U;
inline constexpr uint32_t MainCamera = 0x408U, CameraContext = 0xA0U;
inline constexpr uint32_t ActorLists = 0x20C0U, ActorListSize = 0xCU, First = 4U;
inline constexpr uint32_t PlayerHead = ActorLists + 2U * ActorListSize + First;
static_assert(PlayerHead == 0x20DCU);
inline constexpr uint32_t ActorSize = 0x1F8U;
inline constexpr uint32_t ActorId = 0U, ActorCategory = 2U, ActorPosition = 0x24U;
inline constexpr uint32_t ActorYaw = 0xC2U, ActorCalc = 0x140U, ActorDraw = 0x144U;
// engine.h documents this CommonData address. Donor is_night is a bool:
// live captures read 0 by day and 1 at night (the proof trace retains the
// raw byte); any other value is not a night.
inline constexpr uint32_t Common = 0x7751D8U;
inline constexpr uint32_t Night = 0xCU, Day = 0x14U, Clock = 0x20U;
} // namespace Mm3dSceneEnvironmentLayout

struct Mm3dSceneRead {
  uint32_t Address = 0, Value = 0;
  uint8_t Width = 0;
  bool Available = false;
};
struct Mm3dSceneEnvironmentCapture {
  std::optional<NativeSceneEnvironmentState> State;
  const char* Status = "no-view";
  uint32_t View = 0, Play = 0, Player = 0;
  // Bounded proof receipt; no additional reads just for logging.
  std::array<Mm3dSceneRead, 24> Reads{};
  size_t ReadCount = 0;
};

// Reader requires Read8/16/32 only. It cannot call a guest writer, and the
// template permits a sparse/denying memory fixture without an emulator.
template<class Memory>
Mm3dSceneEnvironmentCapture CaptureMm3dSceneEnvironment(Memory& memory) {
  using namespace Mm3dSceneEnvironmentLayout;
  Mm3dSceneEnvironmentCapture out;
  const auto read = [&]<class T>(uint32_t address, T& value) {
    bool ok;
    if constexpr (sizeof(T) == 1) ok = memory.Read8(address, &value);
    else if constexpr (sizeof(T) == 2) ok = memory.Read16(address, &value);
    else ok = memory.Read32(address, &value);
    if (out.ReadCount < out.Reads.size())
      out.Reads[out.ReadCount++] = {address, ok ? uint32_t(value) : 0U, uint8_t(sizeof(T)), ok};
    return ok;
  };
  const auto object = [](uint32_t address, uint32_t size) {
    return address != 0U && (address & 3U) == 0U &&
           address <= std::numeric_limits<uint32_t>::max() - size;
  };
  if (!read(kMm3dCurrentViewPointer, out.View) || out.View == kMm3dDefaultViewAddress ||
      out.View <= kMm3dPlayViewOffset || !object(out.View, 0x200U))
    return out;
  out.Play = out.View - kMm3dPlayViewOffset;
  if (!object(out.Play, ContextSize)) { out.Status = "bad-context-range"; return out; }
  uint8_t stateType = 0;
  uint32_t cameraContext = 0;
  // A plausible scene number is insufficient: require live Play state and
  // the main camera's own GlobalContext backpointer to agree with the View.
  if (!read(out.Play + StateType, stateType) || stateType != PlayStateType ||
      !read(out.Play + MainCamera + CameraContext, cameraContext) || cameraContext != out.Play) {
    out.Status = "not-play"; return out;
  }
  NativeSceneEnvironmentState state;
  state.ContextIdentity = out.Play;
  if (!read(out.Play + Scene, state.SceneId) || state.SceneId >= 0x8000U ||
      !read(out.Play + Frame, state.GuestFrame)) {
    out.Status = "bad-scene"; return out;
  }
  uint8_t night = 0;
  uint32_t day = 0;
  uint16_t clock = 0;
  if (read(Common + Night, night) && night <= 1U) state.Night = night != 0;
  if (read(Common + Day, day) && day <= 3U) state.Day = static_cast<int32_t>(day);
  if (read(Common + Clock, clock)) state.Clock = clock;

  uint16_t id = 0, yaw = 0;
  uint8_t category = 0;
  uint32_t calc = 0, draw = 0;
  if (read(out.Play + PlayerHead, out.Player) && object(out.Player, ActorSize) &&
      read(out.Player + ActorId, id) && id == 0U &&
      read(out.Player + ActorCategory, category) && category == 2U &&
      read(out.Player + ActorCalc, calc) && calc != 0U) {
    // Draw callback is evidence, not a placement requirement (e.g. first person).
    (void)read(out.Player + ActorDraw, draw);
    ScenePlayerPlacement player;
    bool valid = true;
    for (uint32_t axis = 0; axis < 3U; ++axis) {
      uint32_t bits = 0;
      const bool available = read(out.Player + ActorPosition + axis * 4U, bits);
      player.Position[axis] = std::bit_cast<float>(bits);
      valid = valid && available && std::isfinite(player.Position[axis]);
    }
    if (read(out.Player + ActorYaw, yaw) && valid) {
      player.Yaw = float(std::bit_cast<int16_t>(yaw)) * (180.0F / 32768.0F);
      state.Player = player;
    }
  }
  out.State = state;
  out.Status = "play";
  return out;
}

// The environment group of a play visit, from the fields the authored scene
// and room header commands write into the play state's EnvironmentContext
// (play+0x86B0), read where the opaque pass begins. Writers, EUR/USA v1.1
// code.bin SHA256 6eec27b1..., located by meaning through the scene command
// table 0x00692758 (zeldaret-mm z_scene.c Scene_Command*):
//   cmd 0x10 TimeSettings 0x0040C6B4: cmd[6] -> u16 env+0x02 sceneTimeSpeed
//     (0xFF -> 0; 5 in the first cycle without the ocarina), 0x0040C73C..C750;
//     hour/minute set the clock only, nothing in env keeps them.
//   cmd 0x11 SkyboxSettings 0x004ECCB8: cmd[4]&3 -> play+0xC528 skyboxId
//     (literal 0x004ECD5C), cmd[6] -> env+0x54 lightMode, 0x004ECCE8..CCEC.
//   cmd 0x12 SkyboxDisables 0x004E65A8: cmd[4] -> env+0x16 skyboxDisabled,
//     cmd[5] -> env+0x17 sunDisabled.
// Room commands run after the scene's, so the fields hold the current room.
// Classification mirrors OoT3D's NativeRoomIsInterior/ClassifyNativeEnvironmentGroup
// on the same command bytes: sunDisabled, or fixed light settings (lightMode 1)
// with no skybox, is Indoor; lightMode > 1 is not understood (Unknown). MM keeps
// no record of a header-set hour, so outdoor groups are named by what the
// lights do: lightMode 1 (fixed light settings with a sky) is FixedTime,
// lightMode 0 with the clock stopped (speed 0) TimeAtEntry, with the clock
// running TimeFlows. Offline decode (W/analysis/parity/P1/env_table.txt):
// South/North/East Clock Town and Termina Field speed 3, lightMode 0, sky 1,
// sun on = TimeFlows; Stock Pot Inn and Woodfall Temple sunDisabled 1,
// lightMode 1, sky 0 = Indoor.
namespace Mm3dEnvironmentGroupLayout {
inline constexpr uint32_t Environment = 0x86B0U;
inline constexpr uint32_t TimeSpeed = Environment + 0x02U;      // u16
inline constexpr uint32_t SkyboxDisabled = Environment + 0x16U; // u8
inline constexpr uint32_t SunDisabled = Environment + 0x17U;    // u8
inline constexpr uint32_t LightMode = Environment + 0x54U;      // u8
inline constexpr uint32_t SkyboxId = 0xC528U;                   // u8, play
struct Guard { uint32_t Address, Word; };
// The three writers' stores and the table entries that dispatch them. A
// different image leaves the group Unknown; nothing else depends on it.
inline constexpr std::array<Guard, 13> Guards{{
    {0x00692798U, 0x0040C6B4U}, {0x0069279CU, 0x004ECCB8U}, {0x006927A0U, 0x004E65A8U},
    {0x0040C73CU, 0xE5D20006U}, {0x0040C740U, 0xE2884C86U}, {0x0040C748U, 0x11C40BB2U},
    {0x0040C750U, 0x01C46BB2U},
    {0x004ECCD0U, 0xE59F2084U}, {0x004ECD5CU, 0x0000C528U}, {0x004ECCECU, 0xE5C01704U},
    {0x004E65B0U, 0xE5C036C6U}, {0x004E65B8U, 0xE5C016C7U}, {0x004E65ACU, 0xE2810902U}}};
} // namespace Mm3dEnvironmentGroupLayout

struct Mm3dEnvironmentGroupInputs {
  uint16_t TimeSpeed = 0;
  uint8_t SkyboxDisabled = 0, SunDisabled = 0, LightMode = 0, SkyboxId = 0;
  bool operator==(const Mm3dEnvironmentGroupInputs&) const = default;
};

[[nodiscard]] inline std::optional<SceneEnvironmentGroup> ClassifyMm3dEnvironmentGroup(
    const Mm3dEnvironmentGroupInputs& in) noexcept {
  // Booleans written from header bytes; anything else is not a header we know.
  if (in.SunDisabled > 1U || in.SkyboxDisabled > 1U || in.LightMode > 1U || in.SkyboxId > 3U) return {};
  if (in.SunDisabled != 0U || (in.LightMode == 1U && in.SkyboxId == 0U)) return SceneEnvironmentGroup::Indoor;
  if (in.LightMode == 1U) return SceneEnvironmentGroup::FixedTime;
  return in.TimeSpeed == 0U ? SceneEnvironmentGroup::TimeAtEntry : SceneEnvironmentGroup::TimeFlows;
}

template<class Memory>
[[nodiscard]] bool Mm3dEnvironmentGroupGuardsHold(Memory& memory) {
  for (const auto& guard : Mm3dEnvironmentGroupLayout::Guards) {
    uint32_t word = 0;
    if (!memory.Read32(guard.Address, &word) || word != guard.Word) return false;
  }
  return true;
}

template<class Memory>
[[nodiscard]] std::optional<Mm3dEnvironmentGroupInputs> ReadMm3dEnvironmentGroupInputs(
    Memory& memory, uint32_t play) {
  using namespace Mm3dEnvironmentGroupLayout;
  if (play == 0U || (play & 3U) != 0U ||
      play > std::numeric_limits<uint32_t>::max() - Mm3dSceneEnvironmentLayout::ContextSize)
    return {};
  Mm3dEnvironmentGroupInputs in;
  if (!memory.Read16(play + TimeSpeed, &in.TimeSpeed) || !memory.Read8(play + SkyboxDisabled, &in.SkyboxDisabled) ||
      !memory.Read8(play + SunDisabled, &in.SunDisabled) || !memory.Read8(play + LightMode, &in.LightMode) ||
      !memory.Read8(play + SkyboxId, &in.SkyboxId))
    return {};
  return in;
}

// One group per visit, the first readable class, as OoT3D latches its
// authored class: a room change inside the visit never flips it. A visit is
// the play state, its scene and its frame counter running forward: a new
// visit to the same scene in a play state reallocated at the same address
// restarts the counter (the same rule as NativeSceneEnvironmentContinuity,
// whose epoch makes the environment runtime select again). Unknown (guards
// failed, unreadable or unknown values) is retried every frame and never
// latched.
class Mm3dEnvironmentGroupLatch {
public:
  void SetGuardsHold(bool hold) noexcept { mGuardsHold = hold; mGroup.reset(); mPlay = 0; }
  template<class Memory>
  std::optional<SceneEnvironmentGroup> Observe(Memory& memory, uint32_t play, uint16_t scene, uint32_t frame) {
    if (play != mPlay || scene != mScene || frame < mFrame) { mPlay = play; mScene = scene; mGroup.reset(); }
    mFrame = frame;
    if (!mGroup && mGuardsHold && play != 0U)
      if (const auto in = ReadMm3dEnvironmentGroupInputs(memory, play)) mGroup = ClassifyMm3dEnvironmentGroup(*in);
    return mGroup;
  }
  [[nodiscard]] std::optional<SceneEnvironmentGroup> Current(uint32_t play, uint16_t scene) const noexcept {
    return play != 0U && play == mPlay && scene == mScene ? mGroup : std::nullopt;
  }
  void Reset() noexcept { mPlay = 0; mScene = 0; mFrame = 0; mGroup.reset(); }
private:
  bool mGuardsHold = false;
  uint32_t mPlay = 0, mFrame = 0;
  uint16_t mScene = 0;
  std::optional<SceneEnvironmentGroup> mGroup;
};

// Lifetime check only, not a second scene/time/player sample. A menu or
// teardown with no new opaque pass must not keep exposing the old profile.
template<class Memory>
bool Mm3dSceneEnvironmentStillCurrent(Memory& memory, const NativeSceneEnvironmentState& state) {
  using namespace Mm3dSceneEnvironmentLayout;
  if (state.ContextIdentity == 0U || (state.ContextIdentity & 3U) != 0U ||
      state.ContextIdentity > std::numeric_limits<uint32_t>::max() - ContextSize)
    return false;
  uint32_t view = 0, cameraContext = 0;
  uint8_t stateType = 0;
  return memory.Read32(kMm3dCurrentViewPointer, &view) &&
         view == state.ContextIdentity + kMm3dPlayViewOffset &&
         memory.Read8(state.ContextIdentity + StateType, &stateType) && stateType == PlayStateType &&
         memory.Read32(state.ContextIdentity + MainCamera + CameraContext, &cameraContext) &&
         cameraContext == state.ContextIdentity;
}

// Opt-in proof trace at the existing opaque-world hook. Default window is
// 121 host frames, not an unbounded per-frame log. Values from failed reads
// are null. Guest addresses appear only in this MM3D diagnostic producer.
inline void TraceMm3dSceneEnvironment(const Mm3dSceneEnvironmentCapture& capture,
                                     uint64_t hostFrame, uint32_t pc, uint32_t lr) {
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  static const bool enabled = [] {
    const char* flag = std::getenv("TRIAEVUM_MM3D_SCENE_TRACE");
    return flag && *flag && *flag != '0';
  }();
  if (!enabled) return;
  static const uint64_t first = [] {
    const char* value = std::getenv("TRIAEVUM_MM3D_SCENE_TRACE_FIRST");
    return value ? uint64_t(std::strtoull(value, nullptr, 10)) : 0U;
  }();
  static const uint64_t last = [] {
    const char* value = std::getenv("TRIAEVUM_MM3D_SCENE_TRACE_LAST");
    return value ? uint64_t(std::strtoull(value, nullptr, 10)) : first + 120U;
  }();
  if (hostFrame < first || hostFrame > last) return;
  std::fprintf(stderr, "TRIAEVUM_MM3D_SCENE_TRACE {\"host_frame\":%llu,\"pc\":%u,\"lr\":%u,"
      "\"status\":\"%s\",\"view\":%u,\"play\":%u,\"player\":%u,\"reads\":[",
      static_cast<unsigned long long>(hostFrame), pc, lr, capture.Status, capture.View, capture.Play, capture.Player);
  for (size_t i = 0; i < capture.ReadCount; ++i) {
    const auto& r = capture.Reads[i];
    std::fprintf(stderr, "%s[%u,%u,", i ? "," : "", r.Address, unsigned(r.Width));
    if (r.Available) std::fprintf(stderr, "%u", r.Value); else std::fputs("null", stderr);
    std::fputc(']', stderr);
  }
  std::fputs("]}\n", stderr);
#else
  (void)capture; (void)hostFrame; (void)pc; (void)lr;
#endif
}
} // namespace Oot3dNativeGame
