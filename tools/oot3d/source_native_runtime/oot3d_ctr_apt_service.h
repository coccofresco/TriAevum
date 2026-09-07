#pragma once

#include "oot3d_ctr_ipc_router.h"

namespace Oot3dSourceRuntime {

struct CtrAptProfile {
    GuestAddress StaticBufferTableAddress = 0;
};

class CtrAptService final : public CtrIpcSession {
  public:
    CtrAptService(GuestAddressSpace& memory, CtrIpcRouter& router,
                  CtrAptProfile profile = {});
    CtrResult Dispatch(std::span<std::uint32_t> commandBuffer) override;

  private:
    GuestAddressSpace& mMemory;
    CtrIpcRouter& mRouter;
    CtrAptProfile mProfile;
    std::shared_ptr<CtrKernelObject> mLock;
    std::shared_ptr<CtrKernelObject> mNotificationEvent;
    std::shared_ptr<CtrKernelObject> mParameterEvent;
    std::uint32_t mApplicationId = 0;
    bool mWakeupPending = false;
};

} // namespace Oot3dSourceRuntime
