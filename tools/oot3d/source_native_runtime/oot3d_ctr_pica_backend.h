#pragma once

#include "oot3d_ctr_gsp_service.h"
#include "oot3d_native_pica_frontend.h"

namespace Oot3dSourceRuntime {

class CtrPresentationBackend {
  public:
    virtual ~CtrPresentationBackend() = default;
    virtual bool SetFramebuffer(const CtrGspFramebuffer& framebuffer) = 0;
    virtual void SetLcdForceBlack(bool forceBlack) = 0;
};

class CtrPicaBackend final : public CtrGpuBackend {
  public:
    CtrPicaBackend(Oot3dNativeGame::Oot3dNativePicaFrontend& frontend,
                   CtrPresentationBackend* presentation = nullptr);

    bool WriteRegisters(std::uint32_t base,
                        std::span<const std::uint32_t> values,
                        std::span<const std::uint32_t> masks) override;
    bool SubmitCommand(const CtrGspCommand& command,
                       std::span<const std::uint32_t> commandList) override;
    bool SetFramebuffer(const CtrGspFramebuffer& framebuffer) override;
    void SetLcdForceBlack(bool forceBlack) override;
    std::vector<std::uint8_t> TakeInterrupts() override;

  private:
    Oot3dNativeGame::Oot3dNativePicaFrontend& mFrontend;
    CtrPresentationBackend* mPresentation = nullptr;
};

} // namespace Oot3dSourceRuntime
