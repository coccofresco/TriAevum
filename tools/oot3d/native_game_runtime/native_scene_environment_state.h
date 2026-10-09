#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace Oot3dNativeGame {

// Host values only. Guest layouts and reads belong to the title adapter.
struct ScenePlayerPlacement {
  std::array<float, 3> Position{};
  float Yaw = 0.0F;
  bool operator==(const ScenePlayerPlacement &) const = default;
};

struct NativeSceneEnvironmentState {
  uint32_t ContextIdentity = 0;
  uint32_t GuestFrame = 0;
  uint16_t SceneId = 0;
  std::optional<bool> Night;
  std::optional<int32_t> Day;
  std::optional<uint16_t> Clock;
  std::optional<ScenePlayerPlacement> Player;
};

// A native frame counter can go backwards at restore; the host epoch cannot.
// Explicit Reset also covers restore into the same scene/frame/allocation.
class NativeSceneEnvironmentContinuity {
public:
  void Observe(std::optional<NativeSceneEnvironmentState> next) {
    if (mState.has_value() != next.has_value() ||
        (mState && next && (mState->ContextIdentity != next->ContextIdentity ||
                           mState->SceneId != next->SceneId ||
                           next->GuestFrame < mState->GuestFrame)))
      ++mEpoch;
    mState = next;
  }
  void Reset() { mState.reset(); ++mEpoch; }
  uint64_t Epoch() const { return mEpoch; }
  const std::optional<NativeSceneEnvironmentState>& State() const { return mState; }
private:
  uint64_t mEpoch = 1;
  std::optional<NativeSceneEnvironmentState> mState;
};

} // namespace Oot3dNativeGame
