#pragma once

#include <bit>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace oot3d::gameplay {

inline constexpr double kNativeTimeUnitsPerSecond = 60.0;

struct TimeStep {
  double Seconds = 1.0 / 30.0;
  float NativeUpdateRate = 2.0f;
};

// Source-facing time distinguishes real simulation substeps from the
// original 30 Hz logical timeline.
struct TimeContext {
  std::uint64_t SimulationTick = 0;
  double DeltaSeconds = 1.0 / 30.0;
  float NativeUpdateRate = 2.0f;
  double PreviousLogicalFrame = 0.0;
  double CurrentLogicalFrame = 0.0;
  std::uint64_t LogicalFrameIndex = 0;
  bool CrossedLogicalFrame = false;
};

template <std::integral T>
struct LogicalFrameTimerResult {
  T Previous = 0;
  T Current = 0;
  bool Mutated = false;
  bool ReachedZero = false;
};

// Mirrors the native `if (timer != 0) --timer` form while preserving the
// integer field and mutating it only on the original logical timeline.
template <std::unsigned_integral T>
LogicalFrameTimerResult<T> TickDownIfNonzero(
    T& value, const TimeContext& time) noexcept {
  LogicalFrameTimerResult<T> result{value, value, false, false};
  if (!time.CrossedLogicalFrame || value == 0) {
    return result;
  }
  --value;
  result.Current = value;
  result.Mutated = true;
  result.ReachedZero = value == 0;
  return result;
}

// Mirrors a confirmed signed `if (timer != 0) --timer` owner without
// invoking undefined C++ signed overflow. The stored field wraps exactly as
// the native fixed-width integer.
template <std::signed_integral T>
LogicalFrameTimerResult<T> TickDownWrappingIfNonzero(
    T& value, const TimeContext& time) noexcept {
  LogicalFrameTimerResult<T> result{value, value, false, false};
  if (!time.CrossedLogicalFrame || value == 0) {
    return result;
  }
  using Unsigned = std::make_unsigned_t<T>;
  const auto currentBits = std::bit_cast<Unsigned>(value);
  value = std::bit_cast<T>(static_cast<Unsigned>(currentBits - Unsigned{1}));
  result.Current = value;
  result.Mutated = true;
  result.ReachedZero = value == 0;
  return result;
}

// Mirrors the native saturating `if (counter < limit) ++counter` form.
// Wrapping increments and other signed predicates require site-specific
// ports.
template <std::unsigned_integral T>
LogicalFrameTimerResult<T> TickUpToLimit(
    T& value, T limit, const TimeContext& time) noexcept {
  LogicalFrameTimerResult<T> result{value, value, false, false};
  if (!time.CrossedLogicalFrame || value >= limit) {
    return result;
  }
  ++value;
  result.Current = value;
  result.Mutated = true;
  return result;
}

TimeStep ResolveTimeStep(std::uint32_t simulationRateHz);

} // namespace oot3d::gameplay
