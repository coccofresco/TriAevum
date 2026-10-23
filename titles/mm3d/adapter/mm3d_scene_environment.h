#pragma once

#include "mm3d_title_contracts.h"
#include "native_scene_environment_state.h"
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
// and owner loss/restore still need integration coverage. The supplied
// double-time run cancelled at 06:15: it does NOT validate night switching.
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
// engine.h documents this CommonData address. Donor is_night is a bool;
// live captures confirm daytime zero only. Keep night acceptance pending a
// real gameplay 0->1 transition; the proof trace must retain the raw byte.
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
