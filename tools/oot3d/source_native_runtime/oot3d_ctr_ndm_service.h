#pragma once

#include "oot3d_ctr_ipc_router.h"

namespace Oot3dSourceRuntime {

class CtrNdmService final : public CtrIpcSession {
  public:
    CtrResult Dispatch(std::span<std::uint32_t> commandBuffer) override;
    bool SchedulerSuspended() const;
    bool RunsInBackground() const;

  private:
    bool mSchedulerSuspended = false;
    bool mRunsInBackground = false;
};

} // namespace Oot3dSourceRuntime
