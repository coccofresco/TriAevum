#pragma once

#include "oot3d_ctr_ipc_router.h"

#include <array>

namespace Oot3dSourceRuntime {

struct CtrHidProfile {
    float GyroscopeRawToDpsCoefficient = 14.375F;
    std::array<std::int16_t, 9> GyroscopeCalibration{
        0, 6700, -6700, 0, 6700, -6700, 0, 6700, -6700};
};

class CtrHidService final : public CtrIpcSession {
  public:
    explicit CtrHidService(CtrIpcRouter& router, CtrHidProfile profile = {});
    CtrResult Dispatch(std::span<std::uint32_t> commandBuffer) override;

    std::shared_ptr<CtrKernelObject> SharedMemory() const;
    std::span<std::byte> SharedMemoryBytes();
    bool AccelerometerEnabled() const;
    bool GyroscopeEnabled() const;
    void SignalPadEvents();

  private:
    void EnsureObjects();

    CtrIpcRouter& mRouter;
    CtrHidProfile mProfile;
    std::shared_ptr<CtrKernelObject> mSharedMemory;
    std::array<std::shared_ptr<CtrKernelObject>, 5> mEvents;
    std::uint32_t mAccelerometerEnableCount = 0;
    std::uint32_t mGyroscopeEnableCount = 0;
};

} // namespace Oot3dSourceRuntime
