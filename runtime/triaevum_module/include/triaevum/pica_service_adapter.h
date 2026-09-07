#ifndef TRIAEVUM_PICA_SERVICE_ADAPTER_H
#define TRIAEVUM_PICA_SERVICE_ADAPTER_H

#include "triaevum/service_abi.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace triaevum::module {

struct PicaFramebufferV1 {
  std::uint32_t screen = 0U;
  std::uint32_t activeBuffer = 0U;
  std::uint32_t addressLeft = 0U;
  std::uint32_t addressRight = 0U;
  std::uint32_t stride = 0U;
  std::uint32_t format = 0U;
  std::uint32_t shownBuffer = 0U;
};

class PicaServiceBackendV1 {
public:
  virtual ~PicaServiceBackendV1() = default;

  virtual TriAevumModuleStatusV1
  WriteRegisters(std::uint32_t base, std::span<const std::uint32_t> values,
                 std::span<const std::uint32_t> masks) = 0;
  virtual TriAevumModuleStatusV1 SubmitGspCommand(
      std::uint64_t frameSequence, std::uint32_t control,
      const std::array<std::uint32_t, 7> &parameters,
      std::span<const std::uint32_t> commandWords,
      std::uint64_t *submissionId,
      TriAevumPicaSubmissionFlagsV1 *flags) = 0;
  virtual TriAevumModuleStatusV1
  SetFramebuffer(const PicaFramebufferV1 &framebuffer) = 0;
  virtual TriAevumModuleStatusV1 SetLcdForceBlack(bool forceBlack) = 0;
  virtual TriAevumModuleStatusV1
  TakeInterrupts(std::vector<std::uint8_t> *interrupts) = 0;
  virtual TriAevumModuleStatusV1 Flush(std::uint64_t frameSequence,
                                      std::uint64_t *completedSubmissionId) = 0;
};

class PicaHostServiceAdapterV1 final {
public:
  explicit PicaHostServiceAdapterV1(PicaServiceBackendV1 &backend);

  static TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL Invoke(
      void *context, std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
      TriAevumMutableBytesV1 response, std::size_t *responseSize);

private:
  TriAevumModuleStatusV1 Dispatch(std::uint32_t operation,
                                 TriAevumReadOnlyBytesV1 request,
                                 TriAevumMutableBytesV1 response,
                                 std::size_t *responseSize);

  PicaServiceBackendV1 &mBackend;
};

} // namespace triaevum::module

#endif
