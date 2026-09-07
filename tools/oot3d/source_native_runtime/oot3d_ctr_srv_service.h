#pragma once

#include "oot3d_ctr_ipc_router.h"

namespace Oot3dSourceRuntime {

class CtrSrvService final : public CtrIpcSession {
  public:
    explicit CtrSrvService(CtrIpcRouter& router);

    CtrResult Dispatch(std::span<std::uint32_t> commandBuffer) override;

  private:
    CtrIpcRouter& mRouter;
};

} // namespace Oot3dSourceRuntime
