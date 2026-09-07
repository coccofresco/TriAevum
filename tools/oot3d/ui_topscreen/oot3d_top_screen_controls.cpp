#include "oot3d_top_screen_controls.h"

#include <algorithm>

namespace Oot3dNativeGame {
namespace {

std::size_t ActionIndex(TopScreenDpadAction action) noexcept {
  return static_cast<std::size_t>(action);
}

} // namespace

bool TopScreenDpadActionState::WasPressed(
    TopScreenDpadAction action) const noexcept {
  const auto index = ActionIndex(action);
  return index < Pressed.size() && Pressed[index];
}

bool TopScreenDpadActionState::IsHeld(
    TopScreenDpadAction action) const noexcept {
  const auto index = ActionIndex(action);
  return index < Held.size() && Held[index];
}

TopScreenDpadActionState ResolveTopScreenDpadActions(
    const TopScreenUiConfig &config,
    const TopScreenDpadPhysicalState &physical) noexcept {
  TopScreenDpadActionState resolved;
  const auto &mapping =
      physical.ChildLink ? config.ChildDpad : config.AdultDpad;
  for (std::size_t direction = 0U; direction < mapping.size(); ++direction) {
    const auto action = mapping[direction];
    const auto actionIndex = ActionIndex(action);
    if (action == TopScreenDpadAction::None ||
        actionIndex >= kTopScreenDpadActionCount) {
      continue;
    }
    resolved.Pressed[actionIndex] =
        resolved.Pressed[actionIndex] || physical.Pressed[direction];
    resolved.Held[actionIndex] =
        resolved.Held[actionIndex] || physical.Held[direction];
  }
  return resolved;
}

TopScreenCStickAimPolicy
ResolveTopScreenCStickAimPolicy(const TopScreenUiConfig &config) noexcept {
  constexpr std::size_t kDefaultSpeedIndex = 3U;
  const auto index = std::min<std::size_t>(
      config.CStickAimSpeedLevel, kTopScreenCStickAimSpeeds.size() - 1U);
  return {static_cast<float>(kTopScreenCStickAimSpeeds[index]) /
              static_cast<float>(kTopScreenCStickAimSpeeds[kDefaultSpeedIndex]),
          config.CStickAimInvertX, config.CStickAimInvertY};
}

float ResolveTopScreenCStickSmoothingCoefficient(
    TopScreenFreeCameraSmoothing smoothing) noexcept {
  constexpr std::array<std::uint16_t, 5> kSmoothingFrames{0U, 60U, 120U, 190U,
                                                          300U};
  constexpr float kNativeSampleRate = 33.3333F;
  const auto index = std::min<std::size_t>(static_cast<std::size_t>(smoothing),
                                           kSmoothingFrames.size() - 1U);
  return kNativeSampleRate /
         (static_cast<float>(kSmoothingFrames[index]) + kNativeSampleRate);
}

} // namespace Oot3dNativeGame
