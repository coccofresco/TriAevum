// MM3D quick put away: the real guest-patch path (Add -> Verify against the
// original EUR/v1.1 code.bin -> Mm3dGuestPatches::Execute dispatch).
// Usage: mm3d_gameplay_options_tests <code.bin>
#include "mm3d_gameplay_options.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace Oot3dNativeGame;
using namespace Oot3dNativeGame::Mm3dGameplay;
namespace a32 = oot3d::recomp::a32;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++fails; } }while(0)
struct Image final : a32::MemoryBus {
  std::vector<unsigned char> bytes; std::vector<std::uint32_t> writes;
  bool Read32(std::uint32_t a, std::uint32_t* v) override {
    if (a < 0x100000U || a - 0x100000U + 4 > bytes.size()) return false;
    const auto* p = &bytes[a - 0x100000U];
    *v = p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24); return true;
  }
  bool Write32(std::uint32_t a, std::uint32_t) override { writes.push_back(a); return false; }
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
  CHECK(patches.Verify(image) == 1);
  const auto stats = patches.Statistics();
  CHECK(stats.size() == 1 && stats[0].Enabled && stats[0].Name == "mm3d.quick_put_away");
  const auto bounds = patches.BlockBoundaries();  // must all be registry blocks
  CHECK(bounds == (std::vector<std::uint32_t>{0x00218DE8U, 0x00218DECU, 0x00218DF0U}));

  // Off (default): the native instruction runs, nothing is redirected.
  CHECK(!options.Get().QuickPutAway);
  state.r[15] = kQuickPutAwaySite;
  CHECK(!Dispatch(patches, image, state, r));
  CHECK(state.r[15] == kQuickPutAwaySite && options.QuickPutAways() == 0);

  // On: the cooldown re-arm (mov #20; strb [r6,#0xE2F]) is skipped.
  options.Set({true});
  CHECK(Dispatch(patches, image, state, r));
  CHECK(state.r[15] == kQuickPutAwayContinue && r.kind == a32::ExitKind::Branch && r.pc == kQuickPutAwayContinue);
  CHECK(options.QuickPutAways() == 1 && image.writes.empty());

  // Back off: native again (no latched state).
  options.Set({false}); state.r[15] = kQuickPutAwaySite;
  CHECK(!Dispatch(patches, image, state, r) && state.r[15] == kQuickPutAwaySite);

  // Each guard word protects the site: a changed word disables it.
  for (const auto& [address, word] : kQuickPutAwayGuard) {
    Image wrong = image; const auto o = address - 0x100000U; wrong.bytes[o] ^= 1;
    GameplayOptions o2; Mm3dGuestPatches p2; o2.Register(p2); o2.Set({true});
    CHECK(p2.Verify(wrong) == 0);
    CHECK(p2.Statistics()[0].MismatchAddress == address);
    a32::GuestState s{}; CHECK(!Mm3dGuestPatches::Execute(kQuickPutAwaySite, s, wrong, &r, &p2));
  }
  std::printf("%s mm3d quick put away: dispatches=%llu\n", fails ? "FAILED" : "PASS",
              static_cast<unsigned long long>(options.QuickPutAways()));
  return fails ? 1 : 0;
}
