#include "oot3d_top_screen_rain_runtime.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Oot3dNativeGame {
namespace {

bool ValidDensity(std::uint16_t density) noexcept {
  return density >= 25 && density <= 300 && density % 25 == 0;
}

bool Finite(TopScreenRainVec3 v) noexcept {
  return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
}

bool ValidInput(const TopScreenRainInput &input) noexcept {
  // Reject corrupt host inputs before float-to-integer conversion or grid
  // walks. This broad coordinate bound is a host guard, not a weather gate.
  return Finite(input.Eye) && Finite(input.At) &&
      std::abs(input.Eye.X) <= 1000000 && std::abs(input.Eye.Y) <= 1000000 &&
      std::abs(input.Eye.Z) <= 1000000 &&
      std::abs(input.At.X) <= 1000000 && std::abs(input.At.Y) <= 1000000 &&
      std::abs(input.At.Z) <= 1000000 && std::isfinite(input.PlayerY) &&
      std::isfinite(input.DrawScale);
}

std::int32_t Grid(float position) noexcept {
  return static_cast<std::int32_t>(std::floor(position * 0.025F));
}

std::int32_t Tile(std::int32_t cell) noexcept {
  const auto quotient = cell / 24;
  return quotient - (cell < quotient * 24 ? 1 : 0);
}

std::size_t Index(std::int32_t x, std::int32_t z) noexcept {
  return static_cast<std::size_t>((z - Tile(z) * 24) * 24 + x - Tile(x) * 24);
}

std::int16_t RoundHeight(float value) noexcept {
  return static_cast<std::int16_t>(std::round(std::clamp(value, -32000.0F, 32000.0F)));
}

std::int8_t Slope(std::int16_t normal, std::int16_t y) noexcept {
  const float value = -static_cast<float>(normal) / static_cast<float>(y) * 64.0F;
  return static_cast<std::int8_t>(std::round(std::clamp(value, -127.0F, 127.0F)));
}

float SurfaceY(const TopScreenRainCell &cell, std::int32_t x, std::int32_t z,
               float worldX, float worldZ) noexcept {
  return static_cast<float>(cell.Height) +
      ((worldX - (40.0F * static_cast<float>(x) + 20.0F)) * cell.SlopeX +
       (worldZ - (40.0F * static_cast<float>(z) + 20.0F)) * cell.SlopeZ) / 64.0F;
}

TopScreenRainVec3 Direction(const TopScreenRainInput &input) noexcept {
  TopScreenRainVec3 v{input.At.X - input.Eye.X, input.At.Y - input.Eye.Y,
                     input.At.Z - input.Eye.Z};
  const float length = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
  if (length <= 0.001F) return {};
  return {v.X / length, v.Y / length, v.Z / length};
}

TopScreenRainVec3 Centre(const TopScreenRainInput &input, float distance) noexcept {
  const auto d = Direction(input);
  return {input.Eye.X + d.X * distance, input.Eye.Y + d.Y * distance,
          input.Eye.Z + d.Z * distance};
}

TopScreenRainVec3 NativeCentre(const TopScreenRainInput &input, float distance) noexcept {
  const TopScreenRainVec3 v{input.At.X - input.Eye.X, input.At.Y - input.Eye.Y,
                           input.At.Z - input.Eye.Z};
  const float length = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
  // Native weather assumes a nondegenerate camera; the payload's cache has
  // this fallback. Avoid generating NaNs from a corrupt/degenerate camera.
  if (length <= 0.001F) return input.Eye;
  const float inverse = 1.0F / length;
  return {input.Eye.X + v.X * inverse * distance,
          input.Eye.Y + v.Y * inverse * distance,
          input.Eye.Z + v.Z * inverse * distance};
}

float NativeRandom(std::uint32_t &seed) noexcept {
  // 0x003759D0. The float mantissa construction discards NINE low bits.
  seed = seed * 1664525U + 1013904223U;
  return std::bit_cast<float>(0x3F800000U | (seed >> 9U)) - 1.0F;
}

float EdgeRandom(std::uint32_t &seed) noexcept {
  // 0x005F49C0, used only when splash neighbours disagree at an edge.
  seed ^= seed << 13U;
  seed ^= seed >> 17U;
  seed ^= seed << 5U;
  return static_cast<float>(seed >> 8U) * (1.0F / 16777216.0F);
}

} // namespace

std::uint16_t DecodeTopScreenRainDensity(std::uint8_t value) noexcept {
  if (value >= 1 && value <= 12) return static_cast<std::uint16_t>(value * 25);
  if (value >= 25 && value <= 200 && value % 25 == 0) return value;
  return 100;
}

TopScreenRainPolicy EvaluateTopScreenRain(const TopScreenRainConfig &config,
                                         const TopScreenRainInput &input) noexcept {
  const auto density = ValidDensity(config.DensityPercent) ? config.DensityPercent : 100;
  TopScreenRainPolicy policy;
  policy.Count = input.NativeCount;
  // 0x005F4F04..70. At exactly 100 percent the capacity clamp is bypassed.
  if (density != 100 && policy.Count != 0) {
    const auto cap = std::min({255, input.StreakCapacity.value_or(255),
                             input.SplashCapacity.value_or(255)});
    const auto scaled = (density * static_cast<int>(policy.Count) + 50) / 100;
    policy.Count = static_cast<std::uint8_t>(std::max(1, std::min(cap, scaled)));
  }
  policy.HorizontalScale = density > 100 ? static_cast<float>(density) * 0.01F : 1.0F;
  policy.NativeVisible = input.NativeDrawReached && input.NativeCount != 0 &&
      (input.CameraFlags & 0x100U) == 0 && input.SnowCount == 0 &&
      input.DrawScale != 0 && ValidInput(input);
  policy.SurfaceAware = config.Enabled && policy.NativeVisible;
  policy.Intercept = input.NativeDrawReached && (config.Enabled || density != 100);
  return policy;
}

void TopScreenRainRuntime::SetConfig(TopScreenRainConfig config) noexcept {
  if (!ValidDensity(config.DensityPercent)) config.DensityPercent = 100;
  mConfig = config;
}

void TopScreenRainRuntime::Reset() noexcept { mState = {}; }

const TopScreenRainCell *TopScreenRainRuntime::FindCell(std::int32_t x,
                                                      std::int32_t z) const noexcept {
  const auto &cell = mState.Cells[Index(x, z)];
  if (cell.Status == 0 || cell.TileX != static_cast<std::uint8_t>(Tile(x)) ||
      cell.TileZ != static_cast<std::uint8_t>(Tile(z))) return nullptr;
  return &cell;
}

bool TopScreenRainRuntime::PrepareQueries(const TopScreenRainInput &input) {
  if (!ValidInput(input)) return false;
  if (!PendingQueries().empty()) {
    return input.Play == mState.Play && input.CollisionHeader == mState.CollisionHeader &&
        input.Update == mState.LastUpdate;
  }
  const auto policy = EvaluateTopScreenRain(mConfig, input);
  if (!policy.SurfaceAware) return true;
  if (input.Play != mState.Play || input.CollisionHeader != mState.CollisionHeader) {
    // 0x005F50F0 clears validity only. Epoch, last tick and RNG are retained.
    for (auto &cell : mState.Cells) cell.Status = 0;
    mState.Play = input.Play;
    mState.CollisionHeader = input.CollisionHeader;
  }
  if (input.Update == mState.LastUpdate) return true;
  mState.LastUpdate = input.Update;
  ++mState.Epoch;
  mState.RayY = RoundHeight(input.Eye.Y + 500.0F);
  mState.QueryCount = 0;
  mState.CompletedQueries = 0;
  mState.Queries = {};

  struct Candidate { std::int32_t X, Z; float Priority; };
  std::vector<Candidate> nearest;
  nearest.reserve(4);
  std::optional<Candidate> oldest;
  std::uint8_t oldestAge = 90;
  const auto consider = [&](std::int32_t x, std::int32_t z, float priority) {
    const auto *cell = FindCell(x, z);
    if (cell != nullptr && std::abs(static_cast<int>(cell->RayY) - mState.RayY) <= 300) {
      const auto age = static_cast<std::uint8_t>(mState.Epoch - cell->Age);
      if (age > oldestAge) { oldestAge = age; oldest = Candidate{x, z, priority}; }
      return;
    }
    if (std::any_of(nearest.begin(), nearest.end(), [=](const Candidate &c) {
          return c.X == x && c.Z == z;
        })) return;
    // 0x005F4B64: equal distances retain the Z-then-X scan order.
    const auto at = std::find_if(nearest.begin(), nearest.end(), [=](const Candidate &c) {
      return c.Priority > priority;
    });
    if (at == nearest.end() && nearest.size() == 4) return;
    nearest.insert(at, {x, z, priority});
    if (nearest.size() > 4) nearest.pop_back();
  };
  const auto near = Centre(input, 50.0F);
  const auto nx = Grid(near.X), nz = Grid(near.Z);
  const auto radius = static_cast<std::int32_t>(policy.HorizontalScale * 66.5F * 0.025F) + 1;
  for (auto z = nz - radius; z <= nz + radius; ++z) {
    for (auto x = nx - radius; x <= nx + radius; ++x) {
      consider(x, z, static_cast<float>((x - nx) * (x - nx) + (z - nz) * (z - nz)) - 1000000.0F);
    }
  }
  const auto far = Centre(input, 280.0F);
  const auto extent = policy.HorizontalScale * 140.0F;
  for (auto z = Grid(far.Z - extent); z <= Grid(far.Z + extent); ++z) {
    for (auto x = Grid(far.X - extent); x <= Grid(far.X + extent); ++x) {
      const auto dx = static_cast<float>(x) * 40.0F + 20.0F - input.Eye.X;
      const auto dz = static_cast<float>(z) * 40.0F + 20.0F - input.Eye.Z;
      consider(x, z, dx * dx + dz * dz);
    }
  }
  if (nearest.size() < 4 && oldest) nearest.push_back(*oldest);
  for (const auto &c : nearest) {
    mState.Queries[mState.QueryCount++] = {c.X, c.Z,
        {static_cast<float>(c.X) * 40.0F + 20.0F, static_cast<float>(mState.RayY),
         static_cast<float>(c.Z) * 40.0F + 20.0F}};
  }
  return true;
}

std::span<const TopScreenRainQuery> TopScreenRainRuntime::PendingQueries() const noexcept {
  return std::span(mState.Queries).subspan(mState.CompletedQueries,
                                        mState.QueryCount - mState.CompletedQueries);
}

bool TopScreenRainRuntime::CompleteQuery(const TopScreenRainSurface &surface) noexcept {
  if (PendingQueries().empty() || (surface.HasFloor && !std::isfinite(surface.FloorY)) ||
      (surface.HasWater && !std::isfinite(surface.WaterY))) return false;
  const auto &q = mState.Queries[mState.CompletedQueries];
  auto &cell = mState.Cells[Index(q.CellX, q.CellZ)];
  cell.TileX = static_cast<std::uint8_t>(Tile(q.CellX));
  cell.TileZ = static_cast<std::uint8_t>(Tile(q.CellZ));
  cell.RayY = mState.RayY;
  cell.Age = mState.Epoch;
  cell.SlopeX = cell.SlopeZ = 0;
  cell.Status = 2;
  if (surface.HasFloor && surface.FloorY > -32000.0F) {
    cell.Status = 1;
    cell.Height = RoundHeight(surface.FloorY);
    if (surface.Normal[1] > 0) {
      cell.SlopeX = Slope(surface.Normal[0], surface.Normal[1]);
      cell.SlopeZ = Slope(surface.Normal[2], surface.Normal[1]);
    }
  }
  // Water replaces only a lower floor and must be strictly below the ray.
  if (surface.HasWater && surface.WaterY < q.Origin.Y &&
      (cell.Status != 1 || surface.WaterY > surface.FloorY)) {
    cell.Status = 1;
    cell.Height = RoundHeight(surface.WaterY);
    cell.SlopeX = cell.SlopeZ = 0;
  }
  ++mState.CompletedQueries;
  return true;
}

bool TopScreenRainRuntime::Ready(const TopScreenRainInput &input) const noexcept {
  if (!ValidInput(input)) return false;
  return !EvaluateTopScreenRain(mConfig, input).SurfaceAware ||
      (input.Play == mState.Play && input.CollisionHeader == mState.CollisionHeader &&
       input.Update == mState.LastUpdate && PendingQueries().empty());
}

bool TopScreenRainRuntime::TransformStreak(const TopScreenRainInput &input,
                                          TopScreenRainInstance &instance) const noexcept {
  const auto policy = EvaluateTopScreenRain(mConfig, input);
  if (!policy.NativeVisible || !Ready(input) || !Finite(instance.Position)) return false;
  if (policy.HorizontalScale > 1) {
    const auto centre = Centre(input, 50.0F);
    instance.Position.X = centre.X + (instance.Position.X - centre.X) * policy.HorizontalScale;
    instance.Position.Z = centre.Z + (instance.Position.Z - centre.Z) * policy.HorizontalScale;
  }
  if (policy.SurfaceAware) {
    if (std::abs(instance.Position.X) > 1000000 || std::abs(instance.Position.Z) > 1000000) return false;
    const auto x = Grid(instance.Position.X), z = Grid(instance.Position.Z);
    const auto *cell = FindCell(x, z);
    if (cell != nullptr && cell->Status == 1 &&
        SurfaceY(*cell, x, z, instance.Position.X, instance.Position.Z) > instance.Position.Y) return false;
  }
  return true;
}

bool TopScreenRainRuntime::TransformSplash(const TopScreenRainInput &input,
                                          TopScreenRainInstance &instance,
                                          std::uint32_t &edgeRandom) const noexcept {
  const auto policy = EvaluateTopScreenRain(mConfig, input);
  if (!policy.NativeVisible || !Ready(input) || !Finite(instance.Position)) return false;
  if (policy.HorizontalScale > 1) {
    const auto centre = Centre(input, 280.0F);
    instance.Position.X = centre.X + (instance.Position.X - centre.X) * policy.HorizontalScale;
    instance.Position.Z = centre.Z + (instance.Position.Z - centre.Z) * policy.HorizontalScale;
  }
  if (!policy.SurfaceAware) return true;
  if (std::abs(instance.Position.X) > 1000000 || std::abs(instance.Position.Z) > 1000000) return false;
  const auto x = Grid(instance.Position.X), z = Grid(instance.Position.Z);
  const auto *cell = FindCell(x, z);
  if (cell == nullptr || cell->Status != 1) return false;
  const float cx = static_cast<float>(x) * 40.0F + 20.0F;
  const float cz = static_cast<float>(z) * 40.0F + 20.0F;
  const auto sx = instance.Position.X >= cx ? 1 : -1;
  const auto sz = instance.Position.Z >= cz ? 1 : -1;
  const auto discontinuity = [&](std::int32_t otherX, std::int32_t otherZ, float px, float pz) {
    const auto *other = FindCell(otherX, otherZ);
    if (other == nullptr) return false;
    if (other->Status != 1) return true;
    return std::abs(SurfaceY(*cell, x, z, px, pz) -
                    SurfaceY(*other, otherX, otherZ, px, pz)) > 12.0F;
  };
  if (discontinuity(x + sx, z, cx + static_cast<float>(sx) * 20.0F, instance.Position.Z) ||
      discontinuity(x, z + sz, instance.Position.X, cz + static_cast<float>(sz) * 20.0F)) {
    instance.Position.X = cx + (EdgeRandom(edgeRandom) - 0.5F) * 12.0F;
    instance.Position.Z = cz + (EdgeRandom(edgeRandom) - 0.5F) * 12.0F;
  }
  instance.Position.Y = SurfaceY(*cell, x, z, instance.Position.X, instance.Position.Z) + 2.0F;
  if (input.Eye.Y <= instance.Position.Y) return false;
  const float threshold = input.Adult ? -48.0F : -30.0F;
  const float scale = instance.Position.Y - input.Eye.Y > threshold ? 1.28F : 6.4F;
  instance.Basis = {{{scale, 0, 0}, {0, scale, 0}, {0, 0, scale}}};
  return true;
}

TopScreenRainFrame TopScreenRainRuntime::BuildFrame(const TopScreenRainInput &input,
                                                   TopScreenRainRandomState &random) const {
  TopScreenRainFrame frame;
  frame.Policy = EvaluateTopScreenRain(mConfig, input);
  if (!Ready(input)) return frame;
  frame.Valid = true;
  if (!frame.Policy.Intercept || !frame.Policy.NativeVisible) return frame;
  const auto near = NativeCentre(input, 50.0F);
  constexpr float halfPi = 1.5707963705062866F;
  for (unsigned i = 0; i < frame.Policy.Count; ++i) {
    TopScreenRainInstance instance;
    instance.Position.X = near.X + (NativeRandom(random.Native) - 0.5F) * 133.0F;
    instance.Position.Y = near.Y + (NativeRandom(random.Native) - 0.7F) * 100.0F;
    instance.Position.Z = near.Z + (NativeRandom(random.Native) - 0.5F) * 133.0F;
    const float wx = input.Wind[0], wy = input.Wind[1], wz = input.Wind[2];
    const float downward = -((500.0F + NativeRandom(random.Native) * 200.0F) + wy);
    // 0x003675F8 takes (x,y), returning atan2(y,x) via a native angle table.
    const float pitch = halfPi - std::atan2(downward, std::sqrt(wx * wx + wz * wz));
    const float yaw = -std::atan2(wx, wz);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cx = std::cos(pitch), sx = std::sin(pitch);
    instance.Basis = {{{cy * 0.2F, 0, -sy * 0.2F},
                       {sy * sx * 1.2F, cx * 1.2F, cy * sx * 1.2F},
                       {sy * cx * 0.2F, -sx * 0.2F, cy * cx * 0.2F}}};
    if (TransformStreak(input, instance)) frame.Streaks[frame.StreakCount++] = instance;
  }
  // 0x005F5530 replaces the player-Y gate with -1e30 only in 3D mode.
  if (frame.Policy.SurfaceAware || input.PlayerY < input.Eye.Y) {
    const auto far = NativeCentre(input, 280.0F);
    for (unsigned i = 0; i < frame.Policy.Count; ++i) {
      TopScreenRainInstance instance;
      instance.Position.X = far.X + (NativeRandom(random.Native) - 0.5F) * 280.0F;
      instance.Position.Y = input.PlayerY + 2.0F;
      instance.Position.Z = far.Z + (NativeRandom(random.Native) - 0.5F) * 280.0F;
      const float threshold = input.Adult ? -48.0F : -30.0F;
      const float scale = instance.Position.Y - input.Eye.Y > threshold ? 1.28F : 6.4F;
      instance.Basis = {{{scale, 0, 0}, {0, scale, 0}, {0, 0, scale}}};
      if (TransformSplash(input, instance, random.Edge)) frame.Splashes[frame.SplashCount++] = instance;
    }
  }
  return frame;
}

TopScreenRainSnapshot TopScreenRainRuntime::CaptureState(TopScreenRainRandomState random) const {
  auto state = mState;
  state.Random = random;
  return state;
}

bool TopScreenRainRuntime::RestoreState(const TopScreenRainSnapshot &snapshot,
                                        TopScreenRainRandomState *random,
                                        std::string *error) {
  const auto fail = [&](const char *reason) { if (error) *error = reason; return false; };
  if (random == nullptr) return fail("Missing rain RNG destination");
  if (snapshot.Version != 1) return fail("Unsupported rain snapshot version");
  if (snapshot.QueryCount > 4 || snapshot.CompletedQueries > snapshot.QueryCount ||
      snapshot.RayY < -32000 || snapshot.RayY > 32000) return fail("Invalid rain query batch");
  for (const auto &cell : snapshot.Cells) {
    if (cell.Status > 2 || cell.SlopeX == -128 || cell.SlopeZ == -128 ||
        cell.Height < -32000 || cell.Height > 32000 ||
        cell.RayY < -32000 || cell.RayY > 32000) return fail("Invalid rain cache cell");
  }
  for (unsigned i = 0; i < snapshot.QueryCount; ++i) {
    const auto &q = snapshot.Queries[i];
    if (q.CellX < -25032 || q.CellX > 25032 || q.CellZ < -25032 || q.CellZ > 25032 ||
        !Finite(q.Origin) || q.Origin.X != static_cast<float>(q.CellX) * 40.0F + 20.0F ||
        q.Origin.Z != static_cast<float>(q.CellZ) * 40.0F + 20.0F ||
        q.Origin.Y != static_cast<float>(snapshot.RayY)) return fail("Invalid rain query coordinates");
    for (unsigned j = 0; j < i; ++j) {
      if (q.CellX == snapshot.Queries[j].CellX && q.CellZ == snapshot.Queries[j].CellZ)
        return fail("Duplicate rain query");
    }
  }
  mState = snapshot;
  *random = snapshot.Random;
  if (error) error->clear();
  return true;
}

} // namespace Oot3dNativeGame
