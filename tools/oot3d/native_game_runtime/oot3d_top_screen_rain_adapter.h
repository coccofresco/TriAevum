#pragma once

#include "oot3d_native_a32_memory.h"
#include "oot3d_top_screen_rain_runtime.h"

#include <cstdint>
#include <span>
#include <string>

namespace Oot3dNativeGame {

// Gameplay_Draw's return from the native weather draw. It is also the native
// precipitation scope's return, so the block policy observes composition
// there before the adapter's observable exit.
inline constexpr std::uint32_t kTopScreenRainWeatherReturn = 0x002E2CD0U;

// SingleScreen 3.0 rain (0x005F4EE0 and its enqueue hooks) as whole-AOT
// block hooks around the native weather draw 0x00463544. The game keeps its
// own RNG, wind basis, lighting and materials: the adapter only scales the
// native count for one draw, issues the native floor/water queries for the
// surface cache, and adjusts or drops each streak/splash transform before
// the original native enqueue. No renderer state is involved.
struct TopScreenRainAdapterStats {
  std::uint64_t Draws = 0;
  std::uint64_t SurfaceDraws = 0;
  std::uint64_t Queries = 0;
  std::uint64_t StreaksKept = 0;
  std::uint64_t StreaksDropped = 0;
  std::uint64_t SplashesKept = 0;
  std::uint64_t SplashesDropped = 0;
  std::uint8_t LastNativeCount = 0;
  std::uint8_t LastCount = 0;
};

class TopScreenRainAdapter final {
public:
  explicit TopScreenRainAdapter(NativeA32Memory &memory) : mMemory(memory) {}

  // Latched at the next native weather draw; a draw in progress keeps the
  // configuration it started with.
  void SetConfig(TopScreenRainConfig config) noexcept { mPendingConfig = config; }
  static std::span<const std::uint32_t> HookPcs() noexcept;
  static std::span<const std::uint32_t> ResumePcs() noexcept;
  static std::span<const std::uint32_t> NativeCallPcs() noexcept;
  // Off/100% observes nothing. The enqueue and splash-gate hooks are only
  // observed inside an intercepted weather draw.
  [[nodiscard]] bool ShouldObserve(std::uint32_t pc) const noexcept;
  // True from the weather block until the native draw returns. Process
  // snapshots defer while pending: the temporary rain count and the outer
  // CPU frame live only here.
  [[nodiscard]] bool Pending() const noexcept { return mPhase != Phase::Idle; }
  bool Execute(std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
               oot3d::recomp::a32::ExecutionResult *result,
               std::uint32_t *blocksConsumed);

  // After a state load: the surface cache refills at four cells per update
  // and 3.0's edge RNG restarts from its seed. The native RNG is in guest
  // memory.
  void Reset() noexcept;
  const TopScreenRainAdapterStats &Stats() const noexcept { return mStats; }

private:
  enum class Phase : std::uint8_t { Idle, Floor, Water, Weather };

  bool Begin(oot3d::recomp::a32::GuestState &state,
             oot3d::recomp::a32::ExecutionResult *result,
             std::uint32_t *blocksConsumed);
  bool StepQuery(oot3d::recomp::a32::GuestState &state,
                 oot3d::recomp::a32::ExecutionResult *result,
                 std::uint32_t *blocksConsumed);
  bool NextQuery(oot3d::recomp::a32::GuestState &state,
                 oot3d::recomp::a32::ExecutionResult *result,
                 std::uint32_t *blocksConsumed);
  bool CallWeather(oot3d::recomp::a32::GuestState &state,
                   oot3d::recomp::a32::ExecutionResult *result,
                   std::uint32_t *blocksConsumed);
  bool Enqueue(oot3d::recomp::a32::GuestState &state,
               oot3d::recomp::a32::ExecutionResult *result,
               std::uint32_t *blocksConsumed);
  bool SplashGate(oot3d::recomp::a32::GuestState &state,
                  oot3d::recomp::a32::ExecutionResult *result,
                  std::uint32_t *blocksConsumed);
  TopScreenRainInput ReadInput(std::uint32_t play) const;
  std::uint8_t Read8(std::uint32_t address) const;
  std::uint16_t Read16(std::uint32_t address) const;
  std::uint32_t Read32(std::uint32_t address) const;
  float ReadFloat(std::uint32_t address) const;
  void Write8(std::uint32_t address, std::uint8_t value);
  void Write32(std::uint32_t address, std::uint32_t value);
  void WriteFloat(std::uint32_t address, float value);

  NativeA32Memory &mMemory;
  TopScreenRainConfig mPendingConfig;
  TopScreenRainRuntime mRuntime;
  std::uint32_t mEdgeRandom = 0x2545F491U;
  Phase mPhase = Phase::Idle;
  oot3d::recomp::a32::GuestState mCaller{};
  std::uint32_t mScratch = 0;
  TopScreenRainInput mInput;
  TopScreenRainPolicy mPolicy;
  TopScreenRainSurface mSurface;
  TopScreenRainAdapterStats mStats;
};

} // namespace Oot3dNativeGame
