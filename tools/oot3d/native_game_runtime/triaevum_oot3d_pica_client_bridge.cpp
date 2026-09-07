#include "triaevum_oot3d_pica_client_bridge.h"

#include <string>

namespace Oot3dNativeGame {

TriAevumOot3dPicaClientBridge::TriAevumOot3dPicaClientBridge(
    triaevum::module::PicaServiceClientV1 &client)
    : mClient(client) {}

bool TriAevumOot3dPicaClientBridge::Complete(TriAevumModuleStatusV1 status,
                                             const char *operation,
                                             std::string *error) {
  if (status == TRIAEVUM_MODULE_OK_V1) {
    return true;
  }
  if (error != nullptr) {
    *error = std::string(operation) +
             " failed across the TriAevum PICA service (status " +
             std::to_string(status) + ")";
  }
  return false;
}

bool TriAevumOot3dPicaClientBridge::WriteHardwareRegisters(
    uint32_t base, std::span<const uint32_t> values,
    std::span<const uint32_t> masks, std::string *error) {
  return Complete(mClient.WriteRegisters(base, values, masks),
                  "WriteHardwareRegisters", error);
}

bool TriAevumOot3dPicaClientBridge::SubmitGspCommand(
    uint32_t control, const std::array<uint32_t, 7> &parameters,
    std::span<const uint32_t> commandWords,
    NativeA32CtrPicaSubmissionResult *result, std::string *error) {
  if (result == nullptr) {
    if (error != nullptr) {
      *error = "SubmitGspCommand result is null";
    }
    return false;
  }
  triaevum::module::PicaSubmissionResultV1 serviceResult;
  const TriAevumModuleStatusV1 status = mClient.SubmitGspCommand(
      ++mSubmissionSequence, control, parameters, commandWords, &serviceResult);
  if (!Complete(status, "SubmitGspCommand", error)) {
    return false;
  }
  result->DisplayTransferDeferredToGpu =
      (serviceResult.flags & TRIAEVUM_PICA_DISPLAY_TRANSFER_DEFERRED_V1) != 0U;
  result->MemoryFillDeferredToGpu =
      (serviceResult.flags & TRIAEVUM_PICA_MEMORY_FILL_DEFERRED_V1) != 0U;
  result->DisplayTransferCpuCopySuppressed =
      (serviceResult.flags &
       TRIAEVUM_PICA_DISPLAY_TRANSFER_CPU_COPY_SUPPRESSED_V1) != 0U;
  return true;
}

bool TriAevumOot3dPicaClientBridge::SetFramebuffer(
    const NativeA32CtrPicaFramebuffer &framebuffer, std::string *error) {
  return Complete(
      mClient.SetFramebuffer(framebuffer.Screen, framebuffer.ActiveBuffer,
                             framebuffer.AddressLeft, framebuffer.AddressRight,
                             framebuffer.Stride, framebuffer.Format,
                             framebuffer.ShownBuffer),
      "SetFramebuffer", error);
}

bool TriAevumOot3dPicaClientBridge::SetLcdForceBlack(bool forceBlack,
                                                     std::string *error) {
  return Complete(mClient.SetLcdForceBlack(forceBlack), "SetLcdForceBlack",
                  error);
}

bool TriAevumOot3dPicaClientBridge::TakePendingInterrupts(
    std::vector<uint8_t> *interrupts, std::string *error) {
  return Complete(mClient.TakeInterrupts(interrupts), "TakePendingInterrupts",
                  error);
}

uint64_t TriAevumOot3dPicaClientBridge::SubmissionSequence() const noexcept {
  return mSubmissionSequence;
}

void TriAevumOot3dPicaClientBridge::RestoreSubmissionSequence(
    uint64_t sequence) noexcept {
  mSubmissionSequence = sequence;
}

} // namespace Oot3dNativeGame
