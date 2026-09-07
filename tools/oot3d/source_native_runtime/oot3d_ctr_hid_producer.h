#pragma once

#include "oot3d_ctr_hid_service.h"

namespace Oot3dSourceRuntime {

struct CtrHidState {
    std::uint32_t Buttons = 0;
    std::int16_t CirclePadX = 0;
    std::int16_t CirclePadY = 0;
    std::uint16_t TouchX = 0;
    std::uint16_t TouchY = 0;
    bool TouchPressed = false;
};

class CtrHidProducer {
  public:
    explicit CtrHidProducer(CtrHidService& service);
    bool Submit(const CtrHidState& state, std::uint64_t sampleTick);

  private:
    CtrHidService& mService;
    std::uint32_t mPadIndex = 0;
    std::uint32_t mTouchIndex = 0;
};

} // namespace Oot3dSourceRuntime
