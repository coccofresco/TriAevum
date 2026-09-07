#pragma once

#include "oot3d_ctr_ipc_router.h"

#include <array>

namespace Oot3dSourceRuntime {

struct CtrGspFramebuffer {
    std::uint32_t Screen = 0;
    std::uint32_t ActiveBuffer = 0;
    std::uint32_t AddressLeft = 0;
    std::uint32_t AddressRight = 0;
    std::uint32_t Stride = 0;
    std::uint32_t Format = 0;
    std::uint32_t ShownBuffer = 0;
};

struct CtrGspCommand {
    std::uint32_t Control = 0;
    std::array<std::uint32_t, 7> Parameters{};
};

class CtrGpuBackend {
  public:
    virtual ~CtrGpuBackend() = default;
    virtual bool WriteRegisters(std::uint32_t base,
                                std::span<const std::uint32_t> values,
                                std::span<const std::uint32_t> masks) = 0;
    virtual bool SubmitCommand(const CtrGspCommand& command,
                               std::span<const std::uint32_t> commandList) = 0;
    virtual bool SetFramebuffer(const CtrGspFramebuffer& framebuffer) = 0;
    virtual void SetLcdForceBlack(bool forceBlack) = 0;
    virtual std::vector<std::uint8_t> TakeInterrupts() { return {}; }
};

class CtrGspService final : public CtrIpcSession {
  public:
    CtrGspService(GuestAddressSpace& memory, CtrIpcRouter& router,
                  CtrGpuBackend* backend = nullptr);
    CtrResult Dispatch(std::span<std::uint32_t> commandBuffer) override;
    std::shared_ptr<CtrKernelObject> SharedMemory() const;

  private:
    bool DispatchRegisterWrite(std::span<std::uint32_t> commandBuffer,
                               bool masked);
    bool TriggerCommandQueue();
    bool QueueInterrupt(std::uint8_t interrupt);

    GuestAddressSpace& mMemory;
    CtrIpcRouter& mRouter;
    CtrGpuBackend* mBackend = nullptr;
    std::shared_ptr<CtrKernelObject> mSharedMemory;
    std::shared_ptr<CtrKernelObject> mInterruptEvent;
    std::uint32_t mPriority = 0;
    std::uint32_t mPriorityWithRights = 0;
};

} // namespace Oot3dSourceRuntime
