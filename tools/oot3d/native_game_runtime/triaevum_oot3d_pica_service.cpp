#include "triaevum_oot3d_pica_service.h"

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {

TriAevumOot3dPicaServiceBackend::TriAevumOot3dPicaServiceBackend(
    Oot3dNativePicaFrontend& frontend,
    TriAevumOot3dPicaHostCallbacks callbacks)
    : mFrontend(frontend), mCallbacks(std::move(callbacks)) {}

TriAevumModuleStatusV1 TriAevumOot3dPicaServiceBackend::WriteRegisters(
    std::uint32_t base, std::span<const std::uint32_t> values,
    std::span<const std::uint32_t> masks) {
    mLastError.clear();
    const bool accepted =
        masks.empty()
            ? mFrontend.WriteHardwareRegisters(base, values, &mLastError)
            : mFrontend.WriteHardwareRegistersWithMask(base, values, masks,
                                                       &mLastError);
    return accepted ? TRIAEVUM_MODULE_OK_V1 : TRIAEVUM_MODULE_HOST_ERROR_V1;
}

TriAevumModuleStatusV1 TriAevumOot3dPicaServiceBackend::SubmitGspCommand(
    std::uint64_t, std::uint32_t control,
    const std::array<std::uint32_t, 7>& parameters,
    std::span<const std::uint32_t> commandWords,
    std::uint64_t* submissionId, TriAevumPicaSubmissionFlagsV1* flags) {
    if (submissionId == nullptr || flags == nullptr) {
        return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    if (static_cast<bool>(mCallbacks.BeginSubmissionMemoryAccess) !=
        static_cast<bool>(mCallbacks.EndSubmissionMemoryAccess)) {
        return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    if (mCallbacks.BeginSubmissionMemoryAccess) {
        const TriAevumModuleStatusV1 beginStatus =
            mCallbacks.BeginSubmissionMemoryAccess();
        if (beginStatus != TRIAEVUM_MODULE_OK_V1) {
            return beginStatus;
        }
    }
    Oot3dGspCommandPacket packet;
    packet.Control = control;
    packet.Parameters = parameters;
    bool displayTransferDeferred = false;
    bool memoryFillDeferred = false;
    bool displayTransferCpuCopySuppressed = false;
    mLastError.clear();
    const bool accepted = mFrontend.SubmitGspCommand(
            packet, commandWords, &mLastError, &displayTransferDeferred,
            &memoryFillDeferred, &displayTransferCpuCopySuppressed);
    TriAevumModuleStatusV1 endStatus = TRIAEVUM_MODULE_OK_V1;
    if (mCallbacks.EndSubmissionMemoryAccess) {
        endStatus = mCallbacks.EndSubmissionMemoryAccess();
    }
    if (!accepted) {
        return TRIAEVUM_MODULE_HOST_ERROR_V1;
    }
    if (endStatus != TRIAEVUM_MODULE_OK_V1) {
        return endStatus;
    }
    *submissionId = mNextSubmissionId++;
    mLastSubmissionId = *submissionId;
    *flags = 0U;
    if (displayTransferDeferred) {
        *flags |= TRIAEVUM_PICA_DISPLAY_TRANSFER_DEFERRED_V1;
    }
    if (memoryFillDeferred) {
        *flags |= TRIAEVUM_PICA_MEMORY_FILL_DEFERRED_V1;
    }
    if (displayTransferCpuCopySuppressed) {
        *flags |= TRIAEVUM_PICA_DISPLAY_TRANSFER_CPU_COPY_SUPPRESSED_V1;
    }
    return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1 TriAevumOot3dPicaServiceBackend::SetFramebuffer(
    const triaevum::module::PicaFramebufferV1& framebuffer) {
    return mCallbacks.SetFramebuffer
               ? mCallbacks.SetFramebuffer(framebuffer)
               : TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1;
}

TriAevumModuleStatusV1
TriAevumOot3dPicaServiceBackend::SetLcdForceBlack(bool forceBlack) {
    return mCallbacks.SetLcdForceBlack
               ? mCallbacks.SetLcdForceBlack(forceBlack)
               : TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1;
}

TriAevumModuleStatusV1 TriAevumOot3dPicaServiceBackend::TakeInterrupts(
    std::vector<std::uint8_t>* interrupts) {
    if (interrupts == nullptr) {
        return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    interrupts->clear();
    for (const Oot3dPicaInterruptId interrupt :
         mFrontend.TakePendingInterrupts()) {
        interrupts->push_back(static_cast<std::uint8_t>(interrupt));
    }
    {
        std::lock_guard lock(mCompletedInterruptMutex);
        for (const Oot3dPicaInterruptId interrupt : mCompletedInterrupts) {
            interrupts->push_back(static_cast<std::uint8_t>(interrupt));
        }
        mCompletedInterrupts.clear();
    }
    return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1 TriAevumOot3dPicaServiceBackend::Flush(
    std::uint64_t frameSequence, std::uint64_t* completedSubmissionId) {
    if (completedSubmissionId == nullptr) {
        return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    if (mCallbacks.Flush) {
        return mCallbacks.Flush(frameSequence, completedSubmissionId);
    }
    *completedSubmissionId = mLastSubmissionId;
    return TRIAEVUM_MODULE_OK_V1;
}

void TriAevumOot3dPicaServiceBackend::PublishCompletedInterrupt(
    Oot3dPicaInterruptId interrupt) {
    std::lock_guard lock(mCompletedInterruptMutex);
    mCompletedInterrupts.push_back(interrupt);
}

const std::string&
TriAevumOot3dPicaServiceBackend::LastError() const noexcept {
    return mLastError;
}

} // namespace Oot3dNativeGame
