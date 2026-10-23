#include "oot3d_top_screen_gameplay_runtime.h"

#include <bit>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using namespace Oot3dNativeGame;
using GuestState = oot3d::recomp::a32::GuestState;
using ExecutionResult = oot3d::recomp::a32::ExecutionResult;

constexpr std::uint32_t kPlay = 0x01000000U;
constexpr std::uint32_t kPlayer = 0x01100000U;
constexpr std::uint32_t kWeb = 0x01200000U;
constexpr std::uint32_t kRollTable = 0x0053A790U;
constexpr std::uint32_t kRollAction = 0x1234U;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

struct Fixture {
  NativeA32Memory Memory;
  TopScreenGameplayRuntime Runtime{Memory};
  TopScreenUiConfig Config{};
  ExecutionResult Result{};
  std::uint32_t Blocks = 0;
  Fixture() {
    std::string error;
    Require(Memory.MapRegion({"globals", 0x00504000U, 0x1000U, true, false, {}}, &error) &&
                Memory.MapRegion({"roll table", 0x0053A000U, 0x1000U, true, false, {}}, &error) &&
                Memory.MapRegion({"play", kPlay, 0x10000U, true, false, {}}, &error) &&
                Memory.MapRegion({"player", kPlayer, 0x3000U, true, false, {}}, &error) &&
                Memory.MapRegion({"web", kWeb, 0x1000U, true, false, {}}, &error),
            "cannot map the fixture");
    W32(kPlay + 0x20ACU, kPlayer);
    W32(kRollTable + 4U, kRollAction); // child landing roll
    W8(kPlayer + 0x1B3U, 1U);
  }
  void W8(std::uint32_t address, std::uint8_t value) {
    Require(Memory.Write8(address, value), "fixture byte write failed");
  }
  void W16(std::uint32_t address, std::uint16_t value) {
    Require(Memory.Write16(address, value), "fixture halfword write failed");
  }
  void W32(std::uint32_t address, std::uint32_t value) {
    Require(Memory.Write32(address, value), "fixture word write failed");
  }
  void Configure(bool landingRoll, bool rollDash) {
    Config.LandingRollFix = landingRoll;
    Config.RollDash = rollDash;
    Runtime.SetConfig(Config);
  }
  bool Execute(std::uint32_t pc, GuestState &state) {
    Blocks = 0U;
    if (!Runtime.ShouldObserve(pc)) return false;
    const bool handled = Runtime.Execute(pc, state, &Result, &Blocks);
    if (handled)
      Require(Blocks == 1U && Result.kind == oot3d::recomp::a32::ExitKind::Branch,
              "a handled hook did not branch once");
    return handled;
  }
};

GuestState WebCaller() {
  GuestState state;
  for (unsigned i = 0; i < state.r.size(); ++i) state.r[i] = 0xAB000000U + i;
  state.r[4] = kWeb;
  state.r[8] = kPlay + 0x2000U;
  state.r[13] = 0x02000000U;
  state.r[15] = kTopScreenLandingRollWebSite;
  state.cpsr = 0x10U;
  return state;
}

void FloorWebAcceptsTheLandingRoll() {
  Fixture f;
  auto state = WebCaller();
  const auto caller = state;
  f.W8(kWeb + 0x2C9U, 1U);
  f.W32(kPlayer + 0x284U, kRollAction);
  f.Configure(false, false);
  Require(!f.Execute(kTopScreenLandingRollWebSite, state), "the web hook ran without the option");

  f.Configure(true, false);
  Require(f.Execute(kTopScreenLandingRollWebSite, state) &&
              state.r[15] == kTopScreenLandingRollWebBreak && state.r[0] == kPlayer &&
              state.r[1] == kRollAction && f.Runtime.LandingRollWebBreaks() == 1U,
          "the landing roll did not break the web");
  // cmp r1, #0x95 on the roll action: C set, Z and N clear.
  Require((state.cpsr & 0xF0000000U) == 0x20000000U && state.r[4] == caller.r[4] &&
              state.r[8] == caller.r[8],
          "the web hook did not leave the native block's flags and registers");

  // The native landings, Link on the ground, Link not above the web and
  // other actions all keep the native block.
  for (const std::uint32_t action : {0x6BU, 0x95U, 0x77U}) {
    state = caller;
    f.W32(kPlayer + 0x284U, action);
    Require(!f.Execute(kTopScreenLandingRollWebSite, state), "a native action left the block");
  }
  f.W32(kPlayer + 0x284U, kRollAction);
  f.W16(kPlayer + 0x90U, 1U);
  state = caller;
  Require(!f.Execute(kTopScreenLandingRollWebSite, state), "a grounded roll broke the web");
  f.W16(kPlayer + 0x90U, 0U);
  f.W8(kWeb + 0x2C9U, 0U);
  state = caller;
  Require(!f.Execute(kTopScreenLandingRollWebSite, state), "a roll beside the web broke it");
  Require(f.Runtime.LandingRollWebBreaks() == 1U, "a refused roll was counted");
}

void RollCallWithoutLinkAnimations() {
  // Without Link's dash animations the block's call runs as native, scaled
  // by the roll scale (1 outside a dash), and returns to its continuation.
  Fixture f;
  GuestState state;
  state.r[4] = kPlayer;
  state.r[9] = kPlay;
  state.r[8] = 0x6DU;
  state.r[13] = 0x02000000U;
  state.r[15] = kTopScreenDashRollSpeedSite;
  state.vfp[0] = std::bit_cast<std::uint32_t>(1.5F);
  f.Configure(false, false);
  Require(!f.Execute(kTopScreenDashRollSpeedSite, state), "the roll hook ran without Roll Dash");
  f.Configure(false, true);
  Require(f.Execute(kTopScreenDashRollSpeedSite, state) && state.r[15] == 0x002DD8B0U &&
              state.r[14] == 0x002BE8BCU && state.r[0] == kPlayer && state.r[1] == kPlay &&
              std::bit_cast<float>(state.vfp[0]) == 1.5F && f.Runtime.DashBlends() == 0U,
          "the roll call did not run as the native block");
  Require(!f.Memory.IsMapped(kTopScreenDashBlendBase, 1U), "the blend buffer was mapped unused");
}
} // namespace

int main() {
  FloorWebAcceptsTheLandingRoll();
  RollCallWithoutLinkAnimations();
  std::cout << "SingleScreen gameplay runtime tests passed\n";
}
