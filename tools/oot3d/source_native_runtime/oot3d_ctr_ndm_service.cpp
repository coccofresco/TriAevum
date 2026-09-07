#include "oot3d_ctr_ndm_service.h"

namespace Oot3dSourceRuntime {

CtrResult CtrNdmService::Dispatch(std::span<std::uint32_t> commandBuffer) {
    if (commandBuffer.size() >= 2 && commandBuffer[0] == 0x00080040U) {
        mSchedulerSuspended = true;
        mRunsInBackground = commandBuffer[1] != 0;
        commandBuffer[0] = 0x00080040U;
        commandBuffer[1] = 0;
        return 0;
    }
    if (commandBuffer.size() >= 2 && commandBuffer[0] == 0x00090000U) {
        mSchedulerSuspended = false;
        mRunsInBackground = false;
        commandBuffer[0] = 0x00090040U;
        commandBuffer[1] = 0;
        return 0;
    }
    return CtrIpcRouter::UnhandledResult;
}

bool CtrNdmService::SchedulerSuspended() const { return mSchedulerSuspended; }
bool CtrNdmService::RunsInBackground() const { return mRunsInBackground; }

} // namespace Oot3dSourceRuntime
