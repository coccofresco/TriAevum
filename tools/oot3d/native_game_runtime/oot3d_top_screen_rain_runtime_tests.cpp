#include "oot3d_top_screen_rain_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace Oot3dNativeGame;

namespace {
unsigned checks = 0;
void Check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
void Near(float actual, float expected, const char *message, float tolerance = 0.0001F) {
  Check(std::abs(actual - expected) <= tolerance, message);
}
TopScreenRainInput Input() {
  TopScreenRainInput input;
  input.Play = 0x10000000;
  input.CollisionHeader = 0x11000000;
  input.Update = 1;
  input.NativeDrawReached = true;
  input.NativeCount = 4;
  input.Eye = {0, 100, 0};
  input.At = {0, 100, 100};
  return input;
}
TopScreenRainInstance Instance(float x, float y, float z) {
  return {{x, y, z}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}};
}
TopScreenRainCell &Cell(TopScreenRainSnapshot &state, int x, int z) {
  const auto tx = static_cast<int>(std::floor(static_cast<double>(x) / 24));
  const auto tz = static_cast<int>(std::floor(static_cast<double>(z) / 24));
  auto &cell = state.Cells[static_cast<std::size_t>((z - tz * 24) * 24 + x - tx * 24)];
  cell.TileX = static_cast<std::uint8_t>(tx);
  cell.TileZ = static_cast<std::uint8_t>(tz);
  return cell;
}
TopScreenRainSnapshot Flat(const TopScreenRainInput &input, std::int16_t height = 0) {
  TopScreenRainSnapshot state;
  state.Play = input.Play;
  state.CollisionHeader = input.CollisionHeader;
  state.LastUpdate = input.Update;
  state.Epoch = 100;
  state.RayY = 600;
  for (int z = -5; z <= 18; ++z) {
    for (int x = -12; x <= 11; ++x) {
      auto &cell = Cell(state, x, z);
      cell.Height = height;
      cell.RayY = 600;
      cell.Status = 1;
      cell.Age = 100;
    }
  }
  return state;
}
void Restore(TopScreenRainRuntime &runtime, const TopScreenRainSnapshot &state) {
  TopScreenRainRandomState random;
  Check(runtime.RestoreState(state, &random), "restore valid fixture");
}
void CompleteFlat(TopScreenRainRuntime &runtime, float height = 0) {
  while (!runtime.PendingQueries().empty()) {
    Check(runtime.CompleteQuery({true, height, {0, 1, 0}, false, 0}), "complete floor ray");
  }
}
bool SameFrame(const TopScreenRainFrame &a, const TopScreenRainFrame &b) {
  if (a.Valid != b.Valid || a.StreakCount != b.StreakCount || a.SplashCount != b.SplashCount)
    return false;
  for (unsigned i = 0; i < a.StreakCount; ++i) {
    if (a.Streaks[i].Position != b.Streaks[i].Position || a.Streaks[i].Basis != b.Streaks[i].Basis)
      return false;
  }
  for (unsigned i = 0; i < a.SplashCount; ++i) {
    if (a.Splashes[i].Position != b.Splashes[i].Position || a.Splashes[i].Basis != b.Splashes[i].Basis)
      return false;
  }
  return true;
}

void DensityAndGates() {
  const std::uint16_t encoded[] = {100,25,50,75,100,125,150,175,200,225,250,275,300};
  for (unsigned i = 0; i < std::size(encoded); ++i)
    Check(DecodeTopScreenRainDensity(static_cast<std::uint8_t>(i)) == encoded[i], "density quarter steps");
  for (unsigned i = 13; i <= 255; ++i) {
    const auto expected = i >= 25 && i <= 200 && i % 25 == 0 ? i : 100;
    Check(DecodeTopScreenRainDensity(static_cast<std::uint8_t>(i)) == expected, "legacy density bytes");
  }
  auto input = Input();
  auto p = EvaluateTopScreenRain({}, input);
  Check(p.NativeVisible && !p.Intercept && !p.SurfaceAware && p.Count == 4, "off default leaves native alone");
  input.NativeCount = 1;
  Check(EvaluateTopScreenRain({true,25}, input).Count == 1, "nonzero count never rounds to zero");
  input.NativeCount = 5;
  Check(EvaluateTopScreenRain({false,50}, input).Count == 3, "half density rounds upward at half");
  p = EvaluateTopScreenRain({false,300}, input);
  Check(p.Intercept && !p.SurfaceAware && p.Count == 15 && p.HorizontalScale == 3, "density independent of 3D toggle");
  input.NativeCount = 100;
  input.StreakCapacity = 64;
  input.SplashCapacity = 40;
  Check(EvaluateTopScreenRain({true,300}, input).Count == 40, "clamp by both model capacities");
  Check(EvaluateTopScreenRain({true,100}, input).Count == 100, "100 percent bypasses capacity clamp");
  input.StreakCapacity = 0;
  Check(EvaluateTopScreenRain({true,75}, input).Count == 1, "literal minimum one after zero capacity");
  input.StreakCapacity.reset(); input.SplashCapacity.reset(); input.NativeCount = 255;
  Check(EvaluateTopScreenRain({true,300}, input).Count == 255, "byte maximum without models");
  input.NativeCount = 0;
  Check(EvaluateTopScreenRain({true,300}, input).Count == 0, "zero rain remains zero");
  input = Input();
  for (int gate = 0; gate < 5; ++gate) {
    auto blocked = input;
    if (gate == 0) blocked.NativeDrawReached = false;
    if (gate == 1) blocked.NativeCount = 0;
    if (gate == 2) blocked.CameraFlags = 0x100;
    if (gate == 3) blocked.SnowCount = 1;
    if (gate == 4) blocked.DrawScale = 0;
    p = EvaluateTopScreenRain({true,100}, blocked);
    Check(!p.NativeVisible && !p.SurfaceAware, "each native visibility gate");
  }
  input.CameraFlags = 0x200; input.DrawScale = -1;
  Check(EvaluateTopScreenRain({true,100}, input).SurfaceAware, "other flags and nonzero draw scale do not suppress");
  TopScreenRainRuntime runtime;
  runtime.SetConfig({true,225}); Check(runtime.Config().DensityPercent == 225, "host accepts 225 percent");
  runtime.SetConfig({true,17}); Check(runtime.Config().DensityPercent == 100, "invalid preference normalizes");
  runtime.Reset(); Check(runtime.Config().Enabled, "reset does not reset preferences");
}

void QueryCadenceAndSurfaces() {
  TopScreenRainRuntime runtime;
  runtime.SetConfig({true,100});
  auto input = Input();
  Check(runtime.PrepareQueries(input), "initial query preparation");
  auto pending = runtime.PendingQueries();
  Check(pending.size() == 4, "four query budget");
  Check(pending[0] == TopScreenRainQuery{0,1,{20,600,60}}, "nearest cell first");
  Check(pending[1].CellX == 0 && pending[1].CellZ == 0, "equal priority lower Z first");
  Check(pending[2].CellX == -1 && pending[2].CellZ == 1, "equal priority lower X next");
  Check(pending[3].CellX == 1 && pending[3].CellZ == 1, "equal priority later X next");
  const auto initial = runtime.CaptureState({});
  Check(runtime.PrepareQueries(input) && runtime.CaptureState({}) == initial, "same update does not duplicate pending rays");
  ++input.Update;
  Check(!runtime.PrepareQueries(input) && runtime.CaptureState({}) == initial, "new update cannot abandon pending batch");
  --input.Update;
  TopScreenRainRandomState random;
  Check(!runtime.BuildFrame(input,random).Valid && random == TopScreenRainRandomState{}, "pending collision blocks sampling without RNG use");
  Check(!runtime.CompleteQuery({true,std::numeric_limits<float>::quiet_NaN(),{0,1,0},false,0}), "invalid result preserves query");
  Check(runtime.CompleteQuery({true,10.5F,{1,2,-1},false,0}), "sloping floor result");
  auto state = runtime.CaptureState({});
  auto cell = Cell(state,0,1);
  Check(cell.Status == 1 && cell.Height == 11 && cell.SlopeX == -32 && cell.SlopeZ == 32, "signed plane quantization");
  Check(runtime.CompleteQuery({true,-10.5F,{100,1,-100},true,20.5F}), "water above floor");
  state = runtime.CaptureState({}); cell = Cell(state,0,0);
  Check(cell.Height == 21 && cell.SlopeX == 0 && cell.SlopeZ == 0, "water replaces floor and flattens slope");
  Check(runtime.CompleteQuery({true,-10.5F,{100,1,-100},true,600}), "water at ray rejected");
  state = runtime.CaptureState({}); cell = Cell(state,-1,1);
  Check(cell.Height == -11 && cell.SlopeX == -127 && cell.SlopeZ == 127, "negative round and clamped slopes");
  Check(runtime.CompleteQuery({false,-32000,{0,1,0},false,0}), "missing floor");
  state = runtime.CaptureState({}); Check(Cell(state,1,1).Status == 2, "no floor is cached distinctly");
  Check(!runtime.CompleteQuery({}), "cannot over-complete a batch");
  Check(runtime.PrepareQueries(input) && runtime.CaptureState({}) == state, "same update preserves completed cache");
  ++input.Update; Check(runtime.PrepareQueries(input), "next update advances cache");
  Check(runtime.PendingQueries()[0].CellX == 0 && runtime.PendingQueries()[0].CellZ == 2, "next nearest missing cell");
  CompleteFlat(runtime);

  runtime.Reset(); input = Input(); input.Eye = {-0.01F,100,-0.01F}; input.At = input.Eye;
  Check(runtime.PrepareQueries(input), "degenerate view fallback");
  Check(runtime.PendingQueries()[0] == TopScreenRainQuery{-1,-1,{-20,600,-20}}, "negative coordinates floor rather than truncate");
  Check(runtime.CompleteQuery({false,-32000,{0,1,0},true,15}), "water without floor");
  state = runtime.CaptureState({}); cell = Cell(state,-1,-1);
  Check(cell.TileX == 255 && cell.TileZ == 255 && cell.Status == 1 && cell.Height == 15, "negative toroidal tile and water");
  CompleteFlat(runtime);

  runtime.Reset(); input = Input(); input.Eye = {0,100,0}; input.At = {0,200,0};
  Check(runtime.PrepareQueries(input), "vertical view");
  Check(runtime.PendingQueries()[0].CellZ == 0, "camera normalization uses full 3D direction");
  CompleteFlat(runtime);
  runtime.Reset(); input = Input(); input.Eye.Y = 32000; input.At.Y = 32000;
  Check(runtime.PrepareQueries(input) && runtime.PendingQueries()[0].Origin.Y == 32000, "ray altitude clamp");
  CompleteFlat(runtime);
}

void RefreshAndOwnership() {
  TopScreenRainRuntime runtime; runtime.SetConfig({true,100});
  auto input = Input(); auto state = Flat(input);
  Restore(runtime,state); ++input.Update;
  Check(runtime.PrepareQueries(input) && runtime.PendingQueries().empty(), "fresh complete cache needs no rays");
  state = Flat(input); Cell(state,0,1).Age = 10; Cell(state,1,1).Age = 10;
  Restore(runtime,state); ++input.Update;
  Check(runtime.PrepareQueries(input) && runtime.PendingQueries().size() == 1, "only one aged refresh");
  Check(runtime.PendingQueries()[0].CellX == 0 && runtime.PendingQueries()[0].CellZ == 1, "oldest age tie uses scan order");
  CompleteFlat(runtime);
  state = Flat(input); state.Epoch = 255;
  for (auto &cell : state.Cells) cell.Age = 254;
  Cell(state,0,1).Age = 165;
  Restore(runtime,state); ++input.Update;
  Check(runtime.PrepareQueries(input) && runtime.PendingQueries().size() == 1, "epoch age wraps as uint8");
  CompleteFlat(runtime);
  state = Flat(input); Restore(runtime,state); ++input.Update; input.Eye.Y = input.At.Y = 400;
  Check(runtime.PrepareQueries(input) && runtime.PendingQueries().empty(), "300 altitude delta is still valid");
  ++input.Update; input.Eye.Y = input.At.Y = 401;
  Check(runtime.PrepareQueries(input) && runtime.PendingQueries().size() == 4, "301 altitude delta refreshes");
  CompleteFlat(runtime);
  input = Input(); state = Flat(input); Restore(runtime,state);
  ++input.CollisionHeader;
  Check(runtime.PrepareQueries(input) && runtime.PendingQueries().empty(), "owner change does not reset last update");
  state = runtime.CaptureState({});
  Check(std::all_of(state.Cells.begin(),state.Cells.end(),[](const auto &c){ return c.Status == 0; }), "owner change clears validity");
  Check(state.Epoch == 100, "owner change retains epoch");
  ++input.Update; Check(runtime.PrepareQueries(input) && runtime.PendingQueries().size() == 4, "new owner begins work next update");
  CompleteFlat(runtime);
  ++input.Play; ++input.Update;
  Check(runtime.PrepareQueries(input) && runtime.PendingQueries().size() == 4, "play owner also invalidates");
  CompleteFlat(runtime);
}

void GroundAndEdgeTransforms() {
  TopScreenRainRuntime runtime; runtime.SetConfig({true,100});
  auto input = Input(); auto state = Flat(input,10); Restore(runtime,state);
  auto streak = Instance(20,9,60);
  Check(!runtime.TransformStreak(input,streak), "streak below floor rejected");
  streak.Position.Y = 10; const auto basis = streak.Basis;
  Check(runtime.TransformStreak(input,streak) && streak.Basis == basis, "floor equality kept, native basis preserved");
  state = Flat(input); Cell(state,0,1).Status = 0; Restore(runtime,state);
  streak = Instance(20,-100,60);
  Check(runtime.TransformStreak(input,streak), "unknown floor does not remove streak");
  std::uint32_t edge = 0x2545F491;
  auto splash = Instance(20,0,60);
  Check(!runtime.TransformSplash(input,splash,edge) && edge == 0x2545F491, "unknown floor suppresses splash without RNG");
  Cell(state,0,1).Status = 2; Restore(runtime,state);
  Check(runtime.TransformStreak(input,streak), "no-floor cell keeps streak");
  Check(!runtime.TransformSplash(input,splash,edge), "no-floor cell removes splash");

  state = Flat(input,10); Cell(state,0,1).SlopeX = 32; Restore(runtime,state);
  splash = Instance(22,999,60);
  Check(runtime.TransformSplash(input,splash,edge), "splash projected to slope");
  Near(splash.Position.Y,13,"slope plus two-unit lift"); Near(splash.Basis[0].X,6.4F,"distant splash scale");
  Check(edge == 0x2545F491, "matching neighbour planes need no jitter");
  state = Flat(input,10); Cell(state,1,1).Height = 22; Restore(runtime,state);
  splash = Instance(30,0,60); Check(runtime.TransformSplash(input,splash,edge), "12-unit seam accepted");
  Check(edge == 0x2545F491, "seam threshold is strictly greater than 12");
  Cell(state,1,1).Height = 23; Restore(runtime,state);
  splash = Instance(30,0,60); Check(runtime.TransformSplash(input,splash,edge), "height seam jitter");
  Check(edge == 0x8B9A74AB, "exact two xorshift advances");
  Near(splash.Position.X,24.5535965F,"edge jitter X golden");
  Near(splash.Position.Z,60.5439072F,"edge jitter Z golden");
  state = Flat(input,10); Cell(state,0,2).Status = 2; Restore(runtime,state);
  edge = 0x2545F491; splash = Instance(20,0,60);
  Check(runtime.TransformSplash(input,splash,edge) && edge == 0x8B9A74AB, "Z no-floor neighbour triggers both-axis jitter");
  Cell(state,0,2).Status = 0; Restore(runtime,state);
  edge = 0x2545F491; splash = Instance(20,0,60);
  Check(runtime.TransformSplash(input,splash,edge) && edge == 0x2545F491, "unknown neighbour is ignored");

  state = Flat(input,68); Restore(runtime,state); splash = Instance(20,0,60);
  Check(runtime.TransformSplash(input,splash,edge), "child at threshold"); Near(splash.Basis[0].X,6.4F,"child -30 equality uses large splash");
  input.Adult = true; splash = Instance(20,0,60);
  Check(runtime.TransformSplash(input,splash,edge), "adult different threshold"); Near(splash.Basis[0].X,1.28F,"adult -30 is close");
  state = Flat(input,98); Restore(runtime,state); splash = Instance(20,0,60);
  Check(!runtime.TransformSplash(input,splash,edge), "surface at eye rejected");
  runtime.SetConfig({false,300}); input = Input();
  streak = Instance(10,42,60); Check(runtime.TransformStreak(input,streak), "density-only streak transform");
  Check(streak.Position == TopScreenRainVec3{30,42,80}, "density expands XZ around near camera centre");
  splash = Instance(10,2,290); Check(runtime.TransformSplash(input,splash,edge), "density-only splash transform");
  Check(splash.Position == TopScreenRainVec3{30,2,310}, "splash expands around far centre without surface correction");
}

void DeterministicSamplingAndSnapshots() {
  TopScreenRainRuntime runtime; auto input = Input(); TopScreenRainRandomState random;
  auto frame = runtime.BuildFrame(input,random);
  Check(frame.Valid && frame.StreakCount == 0 && frame.SplashCount == 0 && random == TopScreenRainRandomState{}, "disabled simulator inert");
  runtime.SetConfig({false,50}); input.NativeCount = 2;
  frame = runtime.BuildFrame(input,random);
  Check(frame.Valid && frame.StreakCount == 1 && frame.SplashCount == 1, "one native streak and splash");
  Check(random.Native == 0x5E98C13F && random.Edge == 0x2545F491, "six native LCG advances golden");
  Near(frame.Streaks[0].Position.X,-35.0514297F,"native first X");
  Near(frame.Streaks[0].Position.Y,66.9270554F,"native first Y");
  Near(frame.Streaks[0].Position.Z,50.5641785F,"native first Z");
  Near(frame.Splashes[0].Position.X,-125.847809F,"native splash X");
  Near(frame.Splashes[0].Position.Z,243.465118F,"native splash Z");
  Near(frame.Streaks[0].Basis[1].Y,-1.2F,"zero-wind downward orientation");
  const auto savedRandom = random; const auto second = runtime.BuildFrame(input,random);
  Check(!SameFrame(frame,second), "draws resample instead of moving persistent drops");
  auto replayRandom = savedRandom;
  Check(SameFrame(second,runtime.BuildFrame(input,replayRandom)) && random == replayRandom, "replay sample determinism");
  input.PlayerY = input.Eye.Y; random = {};
  frame = runtime.BuildFrame(input,random);
  Check(frame.StreakCount == 1 && frame.SplashCount == 0 && random.Native == 0xB4733AC5, "native player-Y gate skips splash RNG");
  input.CameraFlags = 0x100; random = {};
  frame = runtime.BuildFrame(input,random);
  Check(frame.Valid && frame.StreakCount == 0 && random == TopScreenRainRandomState{}, "gated draw does not consume RNG");

  runtime.SetConfig({true,100}); input = Input(); input.NativeCount = 1; input.PlayerY = 1000;
  auto state = Flat(input,500); Restore(runtime,state); random = {};
  frame = runtime.BuildFrame(input,random);
  Check(frame.Valid && frame.StreakCount == 0 && frame.SplashCount == 0 && random.Native == 0x5E98C13F, "3D bypasses player gate and rejected samples still consume RNG");
  state = Flat(input); Restore(runtime,state); random = {};
  frame = runtime.BuildFrame(input,random);
  Check(frame.StreakCount == 1 && frame.SplashCount == 1 && frame.Splashes[0].Position.Y == 2, "3D surface replaces player floor");
  runtime.Reset(); input = Input(); Check(runtime.PrepareQueries(input), "pending snapshot setup");
  Check(runtime.CompleteQuery({true,12,{1,2,3},false,0}), "partial query completion");
  random = {0x12345678,0x87654321}; state = runtime.CaptureState(random);
  TopScreenRainRuntime restored; restored.SetConfig(runtime.Config()); TopScreenRainRandomState restoredRandom;
  std::string error = "stale";
  Check(restored.RestoreState(state,&restoredRandom,&error) && error.empty(), "snapshot restore succeeds");
  Check(restoredRandom == random && restored.CaptureState(restoredRandom) == state, "snapshot round-trip including pending batch");
  CompleteFlat(runtime); CompleteFlat(restored);
  Check(SameFrame(runtime.BuildFrame(input,random),restored.BuildFrame(input,restoredRandom)) && random == restoredRandom, "pending snapshot resumes same future");
  const auto before = restored.CaptureState(restoredRandom);
  for (int corruption = 0; corruption < 7; ++corruption) {
    auto bad = before;
    if (corruption == 0) bad.Version = 2;
    if (corruption == 1) bad.CompletedQueries = 5;
    if (corruption == 2) bad.Cells[0].Status = 3;
    if (corruption == 3) bad.Cells[0].SlopeX = -128;
    if (corruption == 4) bad.Queries[0].Origin.X += 1;
    if (corruption == 5) bad.Queries[1] = bad.Queries[0];
    if (corruption == 6) bad.Cells[0].RayY = -32768;
    Check(!restored.RestoreState(bad,&restoredRandom,&error) && !error.empty(), "malformed snapshot rejected");
    Check(restored.CaptureState(restoredRandom) == before, "failed restore is atomic");
  }
  Check(!restored.RestoreState(before,nullptr,&error), "missing RNG destination rejected");
  input.Eye.X = std::numeric_limits<float>::infinity();
  Check(!restored.PrepareQueries(input) && !restored.BuildFrame(input,restoredRandom).Valid, "nonfinite input rejected");
}
} // namespace

int main() {
  try {
    DensityAndGates();
    QueryCadenceAndSurfaces();
    RefreshAndOwnership();
    GroundAndEdgeTransforms();
    DeterministicSamplingAndSnapshots();
    std::cout << "SingleScreen 3.0 rain runtime tests passed (" << checks << " checks, 5 groups)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Rain test failed after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
