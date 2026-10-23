#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Oot3dNativeGame {

// Offline EUR 3.0 evidence: 0x005F4EE0 and 0x005F493C..0x005F58D4.
// These are title policy/data contracts, not a renderer or guest dispatcher.
// Density bounds are owned by oot3d_top_screen_config.h (25..300, step 25).
inline constexpr std::size_t kTopScreenRainCellCount = 24 * 24;
inline constexpr std::size_t kTopScreenRainMaximumDrops = 255;

struct TopScreenRainConfig {
  bool Enabled = false;
  std::uint16_t DensityPercent = 100;
};

struct TopScreenRainVec3 {
  float X = 0, Y = 0, Z = 0;
  bool operator==(const TopScreenRainVec3 &) const = default;
};

struct TopScreenRainInput {
  std::uint32_t Play = 0;
  std::uint32_t CollisionHeader = 0; // [Play+0xA98], not the context address
  std::uint32_t Update = 0;          // [Play+0x5BF4]
  bool NativeDrawReached = false;   // Gameplay_Draw's nonzero-rain branch
  std::uint8_t NativeCount = 0;      // [Play+0x326F], before temporary scaling
  std::uint16_t CameraFlags = 0;     // [[Play+0xA54]+0x194]
  std::uint8_t SnowCount = 0;        // [Play+0x3270]
  float DrawScale = 1;              // [Play+0x7F44], zero suppresses weather
  std::optional<std::int32_t> StreakCapacity; // [[Play+0x3378]+0x1F8]
  std::optional<std::int32_t> SplashCapacity; // [[Play+0x3380]+0x1F8]
  TopScreenRainVec3 Eye;            // view+0x30
  TopScreenRainVec3 At;             // view+0x3C
  std::array<std::int16_t, 3> Wind{}; // Play+0x3220, +0x3222, +0x3224
  float PlayerY = 0;                // [[Play+0x20AC]+0x2C]
  bool Adult = false;               // [0x00587958+4] == 0
};

struct TopScreenRainPolicy {
  bool NativeVisible = false;
  bool SurfaceAware = false;
  bool Intercept = false;
  std::uint8_t Count = 0;
  float HorizontalScale = 1;
};

// Original cfg+0x3D accepts 1..12 quarter-steps and legacy 25..200 multiples
// of 25. Host preferences use an ordinary percent, including 225..300.
std::uint16_t DecodeTopScreenRainDensity(std::uint8_t value) noexcept;
TopScreenRainPolicy EvaluateTopScreenRain(
    const TopScreenRainConfig &config, const TopScreenRainInput &input) noexcept;

struct TopScreenRainQuery {
  std::int32_t CellX = 0, CellZ = 0;
  TopScreenRainVec3 Origin; // (40*x+20, round(eye.y+500), 40*z+20)
  bool operator==(const TopScreenRainQuery &) const = default;
};

struct TopScreenRainSurface {
  bool HasFloor = false; // nonnull polygon AND returned height > -32000
  float FloorY = -32000;
  std::array<std::int16_t, 3> Normal{0, 1, 0};
  bool HasWater = false;
  float WaterY = 0;
};

struct TopScreenRainCell {
  std::uint8_t TileX = 0, TileZ = 0; // low byte of floor(cell/24)
  std::int16_t Height = 0, RayY = 0;
  std::int8_t SlopeX = 0, SlopeZ = 0; // round(-normal.xz/normal.y*64)
  std::uint8_t Status = 0; // 0 unknown, 1 surface, 2 no surface
  std::uint8_t Age = 0;
  bool operator==(const TopScreenRainCell &) const = default;
};

struct TopScreenRainRandomState {
  // Native seed is shared with the game: copy [0x0050C0C4] at the weather
  // call, never reset it to 1 per frame. Edge seed is 3.0's private xorshift.
  std::uint32_t Native = 1;
  std::uint32_t Edge = 0x2545F491U;
  bool operator==(const TopScreenRainRandomState &) const = default;
};

struct TopScreenRainSnapshot {
  std::uint32_t Version = 1;
  std::uint32_t Play = 0, CollisionHeader = 0;
  std::uint32_t LastUpdate = 0xFFFFFFFFU;
  std::uint8_t Epoch = 0;
  std::int16_t RayY = 0;
  std::array<TopScreenRainCell, kTopScreenRainCellCount> Cells{};
  std::array<TopScreenRainQuery, 4> Queries{};
  std::uint8_t QueryCount = 0, CompletedQueries = 0;
  TopScreenRainRandomState Random;
  bool operator==(const TopScreenRainSnapshot &) const = default;
};

struct TopScreenRainInstance {
  TopScreenRainVec3 Position;
  // Columns of the native 3x4 transform. Streak source is a thin triangle
  // (-2,32,0), (2,32,0), (0,-32,0); splashes use a unit XZ quad.
  std::array<TopScreenRainVec3, 3> Basis{};
};

struct TopScreenRainFrame {
  bool Valid = false;
  TopScreenRainPolicy Policy;
  std::array<TopScreenRainInstance, kTopScreenRainMaximumDrops> Streaks{};
  std::array<TopScreenRainInstance, kTopScreenRainMaximumDrops> Splashes{};
  std::uint16_t StreakCount = 0, SplashCount = 0;
};

class TopScreenRainRuntime final {
public:
  void SetConfig(TopScreenRainConfig config) noexcept;
  const TopScreenRainConfig &Config() const noexcept { return mConfig; }
  void Reset() noexcept;

  // Prepare once at the native weather call. Query work is exposed for the
  // title adapter's asynchronous native-call bridge; never run payload PCs.
  // Repeating the same update reuses the pending batch. A different update
  // while queries are pending fails without changing the state.
  bool PrepareQueries(const TopScreenRainInput &input);
  std::span<const TopScreenRainQuery> PendingQueries() const noexcept;
  bool CompleteQuery(const TopScreenRainSurface &surface) noexcept;

  // Native-enqueue integration can use the exact guest-built basis, avoiding
  // any host/native trig difference. A false result suppresses this instance.
  bool TransformStreak(const TopScreenRainInput &input,
                       TopScreenRainInstance &instance) const noexcept;
  bool TransformSplash(const TopScreenRainInput &input,
                       TopScreenRainInstance &instance,
                       std::uint32_t &edgeRandom) const noexcept;

  // Deterministic fresh samples, NOT persistent ballistic particles. Four
  // native RNG steps per streak, two per splash, including rejected instances.
  // Copy random for extra host presentations; commit it once per native draw.
  // This uses host trigonometry; TransformStreak preserves exact native math.
  TopScreenRainFrame BuildFrame(const TopScreenRainInput &input,
                               TopScreenRainRandomState &random) const;

  TopScreenRainSnapshot CaptureState(TopScreenRainRandomState random) const;
  // Validate atomically; preferences stay external, pending queries survive.
  bool RestoreState(const TopScreenRainSnapshot &snapshot,
                    TopScreenRainRandomState *random,
                    std::string *error = nullptr);

private:
  const TopScreenRainCell *FindCell(std::int32_t x, std::int32_t z) const noexcept;
  bool Ready(const TopScreenRainInput &input) const noexcept;
  TopScreenRainConfig mConfig;
  TopScreenRainSnapshot mState;
};

} // namespace Oot3dNativeGame
