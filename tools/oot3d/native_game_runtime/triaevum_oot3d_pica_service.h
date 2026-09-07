#pragma once

#include "oot3d_native_pica_frontend.h"
#include "triaevum/pica_service_adapter.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

struct TriAevumOot3dPicaHostCallbacks {
    std::function<TriAevumModuleStatusV1()> BeginSubmissionMemoryAccess;
    std::function<TriAevumModuleStatusV1()> EndSubmissionMemoryAccess;
    std::function<TriAevumModuleStatusV1(
        const triaevum::module::PicaFramebufferV1&)>
        SetFramebuffer;
    std::function<TriAevumModuleStatusV1(bool)> SetLcdForceBlack;
    std::function<TriAevumModuleStatusV1(std::uint64_t, std::uint64_t*)> Flush;
};

class TriAevumOot3dPicaServiceBackend final
    : public triaevum::module::PicaServiceBackendV1 {
  public:
    explicit TriAevumOot3dPicaServiceBackend(
        Oot3dNativePicaFrontend& frontend,
        TriAevumOot3dPicaHostCallbacks callbacks = {});

    TriAevumModuleStatusV1 WriteRegisters(
        std::uint32_t base, std::span<const std::uint32_t> values,
        std::span<const std::uint32_t> masks) override;
    TriAevumModuleStatusV1 SubmitGspCommand(
        std::uint64_t frameSequence, std::uint32_t control,
        const std::array<std::uint32_t, 7>& parameters,
        std::span<const std::uint32_t> commandWords,
        std::uint64_t* submissionId,
        TriAevumPicaSubmissionFlagsV1* flags) override;
    TriAevumModuleStatusV1 SetFramebuffer(
        const triaevum::module::PicaFramebufferV1& framebuffer) override;
    TriAevumModuleStatusV1 SetLcdForceBlack(bool forceBlack) override;
    TriAevumModuleStatusV1 TakeInterrupts(
        std::vector<std::uint8_t>* interrupts) override;
    TriAevumModuleStatusV1 Flush(
        std::uint64_t frameSequence,
        std::uint64_t* completedSubmissionId) override;

    void PublishCompletedInterrupt(Oot3dPicaInterruptId interrupt);

    const std::string& LastError() const noexcept;

  private:
    Oot3dNativePicaFrontend& mFrontend;
    TriAevumOot3dPicaHostCallbacks mCallbacks;
    std::string mLastError;
    std::uint64_t mNextSubmissionId = 1U;
    std::uint64_t mLastSubmissionId = 0U;
    std::mutex mCompletedInterruptMutex;
    std::vector<Oot3dPicaInterruptId> mCompletedInterrupts;
};

} // namespace Oot3dNativeGame
