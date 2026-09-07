#pragma once

#include "oot3d_native_a32_ctr_pica_bridge.h"
#include "triaevum/pica_service_client.h"

namespace Oot3dNativeGame {

class TriAevumOot3dPicaClientBridge final : public NativeA32CtrPicaBridge {
public:
  explicit TriAevumOot3dPicaClientBridge(
      triaevum::module::PicaServiceClientV1 &client);

  bool WriteHardwareRegisters(uint32_t base, std::span<const uint32_t> values,
                              std::span<const uint32_t> masks,
                              std::string *error) override;
  bool SubmitGspCommand(uint32_t control,
                        const std::array<uint32_t, 7> &parameters,
                        std::span<const uint32_t> commandWords,
                        NativeA32CtrPicaSubmissionResult *result,
                        std::string *error) override;
  bool SetFramebuffer(const NativeA32CtrPicaFramebuffer &framebuffer,
                      std::string *error) override;
  bool SetLcdForceBlack(bool forceBlack, std::string *error) override;
  bool TakePendingInterrupts(std::vector<uint8_t> *interrupts,
                             std::string *error) override;

  [[nodiscard]] uint64_t SubmissionSequence() const noexcept;
  void RestoreSubmissionSequence(uint64_t sequence) noexcept;

private:
  static bool Complete(TriAevumModuleStatusV1 status, const char *operation,
                       std::string *error);

  triaevum::module::PicaServiceClientV1 &mClient;
  uint64_t mSubmissionSequence = 0U;
};

} // namespace Oot3dNativeGame
