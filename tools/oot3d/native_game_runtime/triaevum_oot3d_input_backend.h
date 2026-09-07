#pragma once

#include "oot3d_native_a32_input.h"
#include "triaevum/input_service_adapter.h"

#include <cstdint>

namespace Fast {
class Fast3dWindow;
}

namespace Oot3dNativeGame {

struct TriAevumOot3dInputStats {
  std::uint64_t HostPolls = 0U;
  std::uint64_t ServiceReads = 0U;
};

// SDL/Ship input remains a host concern. This adapter publishes only the
// normalized 3DS control surface through TriAevum's title-neutral service ABI.
class TriAevumOot3dInputBackend final
    : public triaevum::module::InputServiceBackendV1 {
public:
  explicit TriAevumOot3dInputBackend(NativeControlConfig config);

  // Returns false when the host-level exit binding was pressed.
  bool Poll(Fast::Fast3dWindow &window, double samplePeriodSeconds);

  TriAevumModuleStatusV1
  ReadState(std::uint32_t playerIndex,
            triaevum::module::InputStateV1 *state) override;

  [[nodiscard]] const TriAevumOot3dInputStats &Stats() const noexcept;

private:
  NativeControlConfig mConfig;
  NativeRightStickProfileState mRightStickProfile;
  triaevum::module::InputStateV1 mState;
  TriAevumOot3dInputStats mStats;
};

} // namespace Oot3dNativeGame
