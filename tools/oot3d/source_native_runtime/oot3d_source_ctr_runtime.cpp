#include "oot3d_source_ctr_runtime.h"

#include <stdexcept>
#include <utility>

namespace Oot3dSourceRuntime {

SourceCtrRuntime::SourceCtrRuntime(GuestAddressSpace& memory,
                                   SourceCtrRuntimeProfile profile) {
    mConfigService = std::make_shared<CtrConfigService>(
        memory, std::move(profile.Config));
    mFsService = std::make_shared<CtrFsUserService>(
        memory, mRouter, std::move(profile.Fs));
    mAptService = std::make_shared<CtrAptService>(
        memory, mRouter, profile.Apt);
    mHidService = std::make_shared<CtrHidService>(
        mRouter, std::move(profile.Hid));
    mGspService = std::make_shared<CtrGspService>(
        memory, mRouter, profile.GpuBackend);
    mDspService = std::make_shared<CtrDspService>(
        memory, mRouter, profile.Dsp);
    mNdmService = std::make_shared<CtrNdmService>();
    mSrvService = std::make_shared<CtrSrvService>(mRouter);
    if (!mRouter.RegisterPort("APT:U", mAptService) ||
        !mRouter.RegisterPort("cfg:u", mConfigService) ||
        !mRouter.RegisterPort("fs:USER", mFsService) ||
        !mRouter.RegisterPort("hid:USER", mHidService) ||
        !mRouter.RegisterPort("gsp::Gpu", mGspService) ||
        !mRouter.RegisterPort("dsp::DSP", mDspService) ||
        !mRouter.RegisterPort("ndm:u", mNdmService) ||
        !mRouter.RegisterPort("srv:", mSrvService)) {
        throw std::runtime_error("cannot register core CTR services");
    }
}

CtrIpcRouter& SourceCtrRuntime::Router() {
    return mRouter;
}

const CtrIpcRouter& SourceCtrRuntime::Router() const {
    return mRouter;
}

CtrHidService& SourceCtrRuntime::Hid() {
    return *mHidService;
}

const CtrHidService& SourceCtrRuntime::Hid() const {
    return *mHidService;
}

} // namespace Oot3dSourceRuntime
