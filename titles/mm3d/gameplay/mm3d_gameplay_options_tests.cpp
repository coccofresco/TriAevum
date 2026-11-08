// MM3D quick put away: the real guest-patch path (Add -> Verify against the
// original EUR/v1.1 code.bin -> Mm3dGuestPatches::Execute dispatch).
// Usage: mm3d_gameplay_options_tests <code.bin>
#include "mm3d_gameplay_options.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>
using namespace Oot3dNativeGame;
using namespace Oot3dNativeGame::Mm3dGameplay;
namespace a32 = oot3d::recomp::a32;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++fails; } }while(0)
struct Image final : a32::MemoryBus {
  std::vector<unsigned char> bytes; std::vector<std::uint32_t> writes;
  std::map<std::uint32_t, std::uint32_t> ram;  // guest data words (Input, player)
  bool Read32(std::uint32_t a, std::uint32_t* v) override {
    if (auto it = ram.find(a); it != ram.end()) { *v = it->second; return true; }
    if (a < 0x100000U || a - 0x100000U + 4 > bytes.size()) return false;
    const auto* p = &bytes[a - 0x100000U];
    *v = p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24); return true;
  }
  bool Write32(std::uint32_t a, std::uint32_t) override { writes.push_back(a); return false; }
  bool Read16(std::uint32_t a, std::uint16_t* v) override {
    std::uint32_t w = 0; if (!Read32(a & ~3U, &w)) return false;
    *v = static_cast<std::uint16_t>(w >> ((a & 2U) * 8U)); return true;
  }
  void Set16(std::uint32_t a, std::uint16_t v) {
    auto& w = ram[a & ~3U]; const auto sh = (a & 2U) * 8U;
    w = (w & ~(0xFFFFU << sh)) | (std::uint32_t(v) << sh);
  }
};
static bool Dispatch(Mm3dGuestPatches& patches, Image& image, a32::GuestState& state, a32::ExecutionResult& r) {
  return Mm3dGuestPatches::Execute(kQuickPutAwaySite, state, image, &r, &patches);
}
int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: mm3d_gameplay_options_tests <code.bin>\n"); return 2; }
  std::ifstream f(argv[1], std::ios::binary);
  Image image; image.bytes.assign(std::istreambuf_iterator<char>(f), {});
  CHECK(image.bytes.size() == 5967872);  // vanilla EUR v1.1 code.bin
  a32::GuestState state{}; a32::ExecutionResult r{};

  // Negative baseline (the code before this change): no site, no dispatch.
  { Mm3dGuestPatches none; none.Verify(image); CHECK(!Dispatch(none, image, state, r)); }

  GameplayOptions options; Mm3dGuestPatches patches; options.Register(patches);
  CHECK(patches.Verify(image) == 3);
  const auto stats = patches.Statistics();  // sorted by PC
  CHECK(stats.size() == 3 && stats[0].Enabled && stats[1].Enabled && stats[2].Enabled);
  CHECK(stats[0].Name == "mm3d.roll_dash.input" && stats[1].Name == "mm3d.quick_put_away" &&
        stats[2].Name == "mm3d.roll_dash.speed");
  auto bounds = patches.BlockBoundaries();
  std::sort(bounds.begin(), bounds.end());
  CHECK(bounds == (std::vector<std::uint32_t>{0x001F9E90U, 0x001F9E94U, 0x00218DE8U, 0x00218DECU, 0x00218DF0U,
                                              0x00231E98U, 0x00231E9CU, 0x00231EA4U, 0x00231EB4U}));

  // Off (default): the native instruction runs, nothing is redirected.
  CHECK(!options.Get().QuickPutAway);
  state.r[15] = kQuickPutAwaySite;
  CHECK(!Dispatch(patches, image, state, r));
  CHECK(state.r[15] == kQuickPutAwaySite && options.QuickPutAways() == 0);

  // On: the cooldown re-arm (mov #20; strb [r6,#0xE2F]) is skipped.
  options.Set({true, false});
  CHECK(Dispatch(patches, image, state, r));
  CHECK(state.r[15] == kQuickPutAwayContinue && r.kind == a32::ExitKind::Branch && r.pc == kQuickPutAwayContinue);
  CHECK(options.QuickPutAways() == 1 && image.writes.empty());

  // Back off: native again (no latched state).
  options.Set({false, false}); state.r[15] = kQuickPutAwaySite;
  CHECK(!Dispatch(patches, image, state, r) && state.r[15] == kQuickPutAwaySite);

  // Each guard word protects the site: a changed word disables it.
  for (const auto& [address, word] : kQuickPutAwayGuard) {
    Image wrong = image; const auto o = address - 0x100000U; wrong.bytes[o] ^= 1;
    GameplayOptions o2; Mm3dGuestPatches p2; o2.Register(p2); o2.Set({true, false});
    CHECK(p2.Verify(wrong) == 2);
    CHECK(p2.Statistics()[1].MismatchAddress == address);
    a32::GuestState s{}; CHECK(!Mm3dGuestPatches::Execute(kQuickPutAwaySite, s, wrong, &r, &p2));
  }

  // Roll Dash (2Ship DashAfterRoll, On): the input pass observes, the
  // Player_Action_13 Bunny Hood test is forced only while dashing.
  {
    constexpr std::uint32_t player = 0x08100000U;
    GameplayOptions o; Mm3dGuestPatches p; o.Register(p);
    CHECK(p.Verify(image) == 3);
    auto frame = [&](std::uint32_t action, std::uint16_t av2, bool a) {
      image.ram[player + kActionFuncOffset] = action;
      image.Set16(player + kActionVar2Offset, av2);
      image.ram[kInputHeld] = a ? kButtonA : 0U;
      a32::GuestState g{}; g.r[4] = player; g.r[15] = kRollDashInputSite; a32::ExecutionResult x{};
      const bool took = Mm3dGuestPatches::Execute(kRollDashInputSite, g, image, &x, &p);
      CHECK(!took && g.r[15] == kRollDashInputSite);  // never redirects the input pass
      a32::GuestState h{}; h.r[15] = kRollDashSpeedSite; a32::ExecutionResult y{};
      const bool forced = Mm3dGuestPatches::Execute(kRollDashSpeedSite, h, image, &y, &p);
      CHECK(forced == (h.r[15] == kRollDashSpeedApply));
      return forced;
    };
    // Off (default): a roll held into the run is the native run.
    CHECK(!frame(kActionRoll, 0, true)); CHECK(!frame(kActionRun, 0, true)); CHECK(!o.Dashing());
    o.Set({false, true});
    // Running without a roll: native.
    CHECK(!frame(kActionRun, 0, true));
    // Roll with A held, the one Idle frame, then the run: dash at 1.5x.
    CHECK(frame(kActionRoll, 0, true)); CHECK(frame(kActionIdle, 0, true)); CHECK(frame(kActionRun, 0, true));
    CHECK(frame(kActionRun, 0, true));
    // Releasing A ends it; pressing A again in the run does not restart it.
    CHECK(!frame(kActionRun, 0, false)); CHECK(!frame(kActionRun, 0, true));
    // A bonk (actionVar2 != 0) during the roll never starts a dash.
    CHECK(!frame(kActionRoll, 1, true)); CHECK(!frame(kActionRun, 0, true));
    // Idle not right after a roll ends it.
    CHECK(frame(kActionRoll, 0, true)); CHECK(frame(kActionRun, 0, true)); CHECK(!frame(kActionIdle, 0, true));
    // Turning the row off mid-dash restores native at once.
    CHECK(frame(kActionRoll, 0, true)); o.Set({false, false}); CHECK(!frame(kActionRun, 0, true));
    CHECK(o.DashFrames() == 7 && image.writes.empty());
    // Each guard word protects its roll dash site.
    std::vector<std::uint32_t> guarded;
    for (const auto& g : kRollDashInputGuard) guarded.push_back(g.first);
    for (const auto& g : kRollDashSpeedGuard) guarded.push_back(g.first);
    {
      for (const auto address : guarded) {
        Image wrong = image; wrong.bytes[address - 0x100000U] ^= 1;
        GameplayOptions o3; Mm3dGuestPatches p3; o3.Register(p3);
        CHECK(p3.Verify(wrong) == 2);
      }
    }
  }
  std::printf("%s mm3d gameplay options: put_away_dispatches=%llu\n", fails ? "FAILED" : "PASS",
              static_cast<unsigned long long>(options.QuickPutAways()));
  return fails ? 1 : 0;
}
