#pragma once

#include "oot3d_ctr_ipc_router.h"

#include <array>

namespace Oot3dSourceRuntime {

struct CtrDspProfile {
    GuestAddress StaticBufferTableAddress = 0;
    bool HeadphonesConnected = false;
};

class CtrDspService final : public CtrIpcSession {
  public:
    CtrDspService(GuestAddressSpace& memory, CtrIpcRouter& router,
                  CtrDspProfile profile = {});
    CtrResult Dispatch(std::span<std::uint32_t> commandBuffer) override;

    std::span<const std::byte> Component() const;
    bool AudioRunning() const;
    std::uint16_t SemaphoreValue() const;

  private:
    GuestAddressSpace& mMemory;
    CtrIpcRouter& mRouter;
    CtrDspProfile mProfile;
    std::vector<std::byte> mComponent;
    std::array<std::array<std::shared_ptr<CtrKernelObject>, 8>, 3> mInterrupts;
    std::shared_ptr<CtrKernelObject> mSemaphoreEvent;
    std::array<std::vector<std::byte>, 8> mPipeOutput;
    std::uint16_t mProgramMask = 0;
    std::uint16_t mDataMask = 0;
    std::uint16_t mSemaphoreMask = 0;
    std::uint16_t mSemaphoreValue = 0;
    bool mAudioRunning = false;
};

} // namespace Oot3dSourceRuntime
