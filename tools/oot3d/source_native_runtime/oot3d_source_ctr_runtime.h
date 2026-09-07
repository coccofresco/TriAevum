#pragma once

#include "oot3d_ctr_apt_service.h"
#include "oot3d_ctr_config_service.h"
#include "oot3d_ctr_dsp_service.h"
#include "oot3d_ctr_fs_service.h"
#include "oot3d_ctr_hid_service.h"
#include "oot3d_ctr_gsp_service.h"
#include "oot3d_ctr_ipc_router.h"
#include "oot3d_ctr_ndm_service.h"
#include "oot3d_ctr_srv_service.h"

#include <memory>

namespace Oot3dSourceRuntime {

struct SourceCtrRuntimeProfile {
    CtrConfigServiceProfile Config;
    CtrFsProfile Fs;
    CtrHidProfile Hid;
    CtrAptProfile Apt;
    CtrDspProfile Dsp;
    CtrGpuBackend* GpuBackend = nullptr;
};

class SourceCtrRuntime {
  public:
    SourceCtrRuntime(GuestAddressSpace& memory,
                     SourceCtrRuntimeProfile profile = {});

    CtrIpcRouter& Router();
    const CtrIpcRouter& Router() const;
    CtrHidService& Hid();
    const CtrHidService& Hid() const;

  private:
    CtrIpcRouter mRouter;
    std::shared_ptr<CtrAptService> mAptService;
    std::shared_ptr<CtrConfigService> mConfigService;
    std::shared_ptr<CtrDspService> mDspService;
    std::shared_ptr<CtrFsUserService> mFsService;
    std::shared_ptr<CtrHidService> mHidService;
    std::shared_ptr<CtrGspService> mGspService;
    std::shared_ptr<CtrSrvService> mSrvService;
    std::shared_ptr<CtrNdmService> mNdmService;
};

} // namespace Oot3dSourceRuntime
