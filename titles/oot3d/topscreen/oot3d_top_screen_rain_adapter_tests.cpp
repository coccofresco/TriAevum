#include "oot3d_top_screen_rain_adapter.h"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace Oot3dNativeGame;
using GuestState = oot3d::recomp::a32::GuestState;
using ExecutionResult = oot3d::recomp::a32::ExecutionResult;
constexpr std::uint32_t kPlay = 0x01000000U;
constexpr std::uint32_t kView = kPlay + 0x188U;
constexpr std::uint32_t kCamera = kPlay + 0x9000U;
constexpr std::uint32_t kStreakModel = kPlay + 0x9200U;
constexpr std::uint32_t kSplashModel = kPlay + 0x9400U;
constexpr std::uint32_t kPlayer = kPlay + 0x9600U;
constexpr std::uint32_t kPolygon = kPlay + 0x9800U;
constexpr std::uint32_t kStack = 0x02008000U;
constexpr std::uint32_t kMatrix = 0x02004000U;
constexpr std::uint32_t kWeatherBlock = 0x002E2CC0U;
constexpr std::uint32_t kWeatherReturn = 0x002E2CD0U;
constexpr std::uint32_t kEnqueue = 0x00371F1CU;
constexpr std::uint32_t kSplashGate = 0x00463ADCU;

void Require(bool condition, const char *message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

struct Fixture {
  NativeA32Memory Memory;
  TopScreenRainAdapter Rain{Memory};
  ExecutionResult Result{};
  std::uint32_t Blocks = 0;
  Fixture() {
    std::string error;
    Require(Memory.MapRegion({"globals", 0x00587000U, 0x1000U, true, false, {}}, &error),
            "cannot map globals");
    Require(Memory.MapRegion({"play", kPlay, 0x10000U, true, false, {}}, &error),
            "cannot map play");
    Require(Memory.MapRegion({"stack", 0x02000000U, 0x10000U, true, false, {}}, &error),
            "cannot map stack");
    W32(0x0058795CU, 1U);  // child
    W8(kPlay + 0x326FU, 10U);
    W32(kPlay + 0xA54U, kCamera);
    W32(kPlay + 0xA98U, 0x00ABCDEFU);
    W32(kPlay + 0x5BF4U, 7U);
    F(kPlay + 0x7F44U, 1.0F);
    W32(kPlay + 0x3378U, kStreakModel);
    W32(kPlay + 0x3380U, kSplashModel);
    W32(kStreakModel + 0x1F8U, 64U);
    W32(kSplashModel + 0x1F8U, 64U);
    // Eye (0,100,0) looking along +Z: near centre (0,100,50), cell (0,1).
    F(kView + 0x30U, 0.0F); F(kView + 0x34U, 100.0F); F(kView + 0x38U, 0.0F);
    F(kView + 0x3CU, 0.0F); F(kView + 0x40U, 100.0F); F(kView + 0x44U, 100.0F);
    W32(kPlay + 0x20ACU, kPlayer);
    F(kPlayer + 0x2CU, 0.0F);
    W32(kStack + 0x1ECU, 0x0000FEEDU);
    Require(Memory.Write16(kPolygon + 0xCU, 0x7FFFU), "cannot write polygon normal");
  }
  void W8(std::uint32_t a, std::uint8_t v) { Require(Memory.Write8(a, v), "byte write failed"); }
  void W32(std::uint32_t a, std::uint32_t v) { Require(Memory.Write32(a, v), "word write failed"); }
  void F(std::uint32_t a, float v) { W32(a, std::bit_cast<std::uint32_t>(v)); }
  std::uint8_t R8(std::uint32_t a) {
    std::uint8_t v = 0;
    Require(Memory.Read8(a, &v), "byte read failed");
    return v;
  }
  float RF(std::uint32_t a) {
    std::uint32_t v = 0;
    Require(Memory.Read32(a, &v), "word read failed");
    return std::bit_cast<float>(v);
  }
  bool Run(std::uint32_t pc, GuestState &state) {
    state.r[15] = pc;
    return Rain.Execute(pc, state, &Result, &Blocks);
  }
  GuestState Weather() {
    GuestState state;
    state.r[4] = kPlay;
    state.r[13] = kStack;
    state.thread_pointer = 0x1FF82000U;
    return state;
  }
  void Matrix(float x, float y, float z) {
    const float m[12]{1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z};
    for (std::uint32_t i = 0; i < 12U; ++i) F(kMatrix + i * 4U, m[i]);
  }
  // Native enqueue from the streak (0x004638C8) or splash (0x00463CB4) loop.
  bool Enqueue(std::uint32_t caller, GuestState &state) {
    state.r[2] = kMatrix;
    state.r[14] = caller;
    return Run(kEnqueue, state);
  }
};

void TestOffIsInert() {
  Fixture f;
  auto state = f.Weather();
  Require(!f.Rain.ShouldObserve(kWeatherBlock) && !f.Rain.ShouldObserve(kEnqueue) &&
              !f.Rain.ShouldObserve(kSplashGate) && !f.Rain.ShouldObserve(kWeatherReturn),
          "rain off observes native weather");
  Require(!f.Run(kWeatherBlock, state) && f.R8(kPlay + 0x326FU) == 10U && !f.Rain.Pending(),
          "rain off changed the native weather draw");
}

void TestDensityOnly() {
  Fixture f;
  f.Rain.SetConfig({false, 200U});
  auto state = f.Weather();
  Require(f.Run(kWeatherBlock, state) && f.Result.pc == 0x00463544U &&
              state.r[0] == kPlay && state.r[1] == kView && state.r[2] == 0x0000FEEDU &&
              state.r[14] == kWeatherReturn && state.r[13] == kStack,
          "density did not call the native weather draw like the replaced block");
  Require(f.R8(kPlay + 0x326FU) == 20U && f.Rain.Pending(), "200% did not double the rain count");
  Require(!f.Rain.ShouldObserve(kSplashGate), "density-only rain replaced the splash gate");
  // The streak spreads twice as wide about the near centre (0,100,50).
  f.Matrix(10.0F, 50.0F, 60.0F);
  Require(!f.Enqueue(0x004638C8U, state), "density-only streak did not reach native enqueue");
  Require(f.RF(kMatrix + 0xCU) == 20.0F && f.RF(kMatrix + 0x2CU) == 70.0F &&
              f.RF(kMatrix + 0x1CU) == 50.0F,
          "density-only streak was not spread about the near centre");
  // Other enqueue callers pass through untouched.
  f.Matrix(10.0F, 50.0F, 60.0F);
  Require(!f.Enqueue(0x00400000U, state) && f.RF(kMatrix + 0xCU) == 10.0F,
          "rain changed an unrelated model enqueue");
  Require(!f.Run(kWeatherReturn, state) && f.R8(kPlay + 0x326FU) == 10U && !f.Rain.Pending(),
          "the weather return did not restore the native rain count");
  Require(!f.Rain.ShouldObserve(kEnqueue), "enqueue stays observed after the weather draw");

  Fixture low;
  low.Rain.SetConfig({false, 50U});
  auto lowState = low.Weather();
  Require(low.Run(kWeatherBlock, lowState) && low.R8(kPlay + 0x326FU) == 5U &&
              !low.Rain.ShouldObserve(kEnqueue),
          "50% did not halve the count without per-drop work");
}

void TestSurfaceAware() {
  Fixture f;
  f.Rain.SetConfig({true, 100U});
  auto state = f.Weather();
  // Four budgeted cells: floor ray then water query each, re-entering 0x002E2CC0.
  Require(f.Run(kWeatherBlock, state), "3D rain did not start its surface queries");
  unsigned queries = 0;
  while (f.Result.pc == 0x00358410U) {
    const auto scratch = state.r[3];
    Require(state.r[0] == kPlay + 0xA98U && state.r[13] == scratch &&
                state.r[14] == kWeatherBlock && f.RF(scratch + 4U) == 600.0F,
            "floor query arguments are wrong");
    f.W32(scratch + 0xCU, kPolygon);
    state.vfp[0] = std::bit_cast<std::uint32_t>(0.0F);
    Require(f.Run(kWeatherBlock, state) && f.Result.pc == 0x0035E8A0U &&
                state.r[0] == kPlay && state.r[1] == kPlay + 0xA98U &&
                state.vfp[0] == std::bit_cast<std::uint32_t>(f.RF(scratch)),
            "water query arguments are wrong");
    state.r[0] = 0U;
    Require(f.Run(kWeatherBlock, state), "water query did not resume");
    ++queries;
  }
  Require(queries == 4U && f.Result.pc == 0x00463544U && state.r[13] == kStack &&
              state.r[14] == kWeatherReturn && f.R8(kPlay + 0x326FU) == 10U,
          "3D rain did not query four cells before the weather draw");
  // Re-entry from a foreign frame is refused.
  Fixture g;
  g.Rain.SetConfig({true, 100U});
  auto other = g.Weather();
  Require(g.Run(kWeatherBlock, other), "second fixture did not start");
  other.r[13] -= 8U;
  bool threw = false;
  try { g.Run(kWeatherBlock, other); } catch (const std::runtime_error &) { threw = true; }
  Require(threw, "rain accepted a re-entry from another stack frame");

  // Cell (0,1) has a floor at Y=0: a streak starting below it is dropped.
  f.Matrix(10.0F, -10.0F, 60.0F);
  Require(f.Enqueue(0x004638C8U, state) && f.Result.pc == 0x004638C8U,
          "streak below the cached floor was enqueued");
  f.Matrix(10.0F, 50.0F, 60.0F);
  Require(!f.Enqueue(0x004638C8U, state), "streak above the floor was dropped");
  // The splash loop runs regardless of the player height ...
  state.r[4] = kView;
  Require(f.Run(kSplashGate, state) && f.Result.pc == 0x00463AF0U &&
              std::bit_cast<float>(state.vfp[0]) == -1.0e30F &&
              (state.cpsr & 0xF0000000U) == 0x80000000U,
          "3D rain did not replace the splash precondition");
  // ... and each splash lands on the cached floor, sized by its distance.
  f.Matrix(20.0F, 0.0F, 60.0F);
  Require(!f.Enqueue(0x00463CB4U, state) && f.RF(kMatrix + 0x1CU) == 2.0F &&
              f.RF(kMatrix) == 6.4F && f.RF(kMatrix + 0x14U) == 6.4F,
          "splash was not placed on the cached floor");
  // No cached surface: the splash is dropped.
  f.Matrix(500.0F, 0.0F, 500.0F);
  Require(f.Enqueue(0x00463CB4U, state) && f.Result.pc == 0x00463CB4U,
          "splash without a cached surface was enqueued");
  Require(!f.Run(kWeatherReturn, state) && !f.Rain.Pending(), "3D rain did not finish");
  const auto &stats = f.Rain.Stats();
  Require(stats.Draws == 1U && stats.SurfaceDraws == 1U && stats.Queries == 4U &&
              stats.StreaksKept == 1U && stats.StreaksDropped == 1U &&
              stats.SplashesKept == 1U && stats.SplashesDropped == 1U,
          "rain statistics are wrong");

  // The same update reuses the cache: no queries before the next draw.
  state = f.Weather();
  Require(f.Run(kWeatherBlock, state) && f.Result.pc == 0x00463544U,
          "the same update queried the surfaces again");
  Require(!f.Run(kWeatherReturn, state), "second draw did not finish");
  f.Rain.Reset();
  Require(!f.Rain.Pending() && f.Rain.Stats().Queries == 4U, "reset changed statistics");
}

void TestNativeGates() {
  Fixture f;
  f.Rain.SetConfig({true, 100U});
  f.W8(kPlay + 0x3270U, 1U);  // snowing: native weather draws nothing
  auto state = f.Weather();
  Require(f.Run(kWeatherBlock, state) && f.Result.pc == 0x00463544U &&
              !f.Rain.ShouldObserve(kSplashGate),
          "snow did not skip the surface work");
  Require(!f.Run(kWeatherReturn, state), "gated draw did not finish");
}
} // namespace

int main() {
  TestOffIsInert();
  TestDensityOnly();
  TestSurfaceAware();
  TestNativeGates();
  std::cout << "SingleScreen 3.0 rain adapter tests passed\n";
  return 0;
}
