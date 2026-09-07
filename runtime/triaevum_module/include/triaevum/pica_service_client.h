#ifndef TRIAEVUM_PICA_SERVICE_CLIENT_H
#define TRIAEVUM_PICA_SERVICE_CLIENT_H

#include "triaevum/service_abi.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace triaevum::module {

struct PicaSubmissionResultV1 {
  std::uint64_t submissionId = 0U;
  TriAevumPicaSubmissionFlagsV1 flags = 0U;
};

// Module-side client for the public PICA host service. Requests own their
// payload for the duration of the ABI call; no title pointer crosses or is
// retained by the runtime boundary.
class PicaServiceClientV1 final {
public:
  explicit PicaServiceClientV1(const TriAevumHostApiV1 *host);

  [[nodiscard]] bool IsAvailable() const noexcept;

  TriAevumModuleStatusV1
  WriteRegisters(std::uint32_t base, std::span<const std::uint32_t> values,
                 std::span<const std::uint32_t> masks = {});
  TriAevumModuleStatusV1
  SubmitGspCommand(std::uint64_t frameSequence, std::uint32_t control,
                   const std::array<std::uint32_t, 7> &parameters,
                   std::span<const std::uint32_t> commandWords,
                   PicaSubmissionResultV1 *result);
  TriAevumModuleStatusV1
  SetFramebuffer(std::uint32_t screen, std::uint32_t activeBuffer,
                 std::uint32_t addressLeft, std::uint32_t addressRight,
                 std::uint32_t stride, std::uint32_t format,
                 std::uint32_t shownBuffer);
  TriAevumModuleStatusV1 SetLcdForceBlack(bool forceBlack);
  TriAevumModuleStatusV1 TakeInterrupts(std::vector<std::uint8_t> *interrupts);
  TriAevumModuleStatusV1 Flush(std::uint64_t frameSequence,
                               std::uint64_t *completedSubmissionId);

private:
  TriAevumModuleStatusV1 Invoke(std::uint32_t operation,
                                std::span<const std::uint8_t> request,
                                std::span<std::uint8_t> response,
                                std::size_t *responseSize) const;

  TriAevumHostApiV1 mHost{};
  bool mAvailable = false;
};

} // namespace triaevum::module

#endif
