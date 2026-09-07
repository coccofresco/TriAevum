#ifndef TRIAEVUM_AUDIO_SERVICE_CLIENT_H
#define TRIAEVUM_AUDIO_SERVICE_CLIENT_H

#include "triaevum/service_abi.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace triaevum::module {

struct AudioSubmissionResultV1 {
  std::uint32_t acceptedFrameCount = 0U;
  std::uint32_t queuedFrameCount = 0U;
};

class AudioServiceClientV1 final {
public:
  explicit AudioServiceClientV1(const TriAevumHostApiV1 *host);

  [[nodiscard]] bool IsAvailable() const noexcept;

  TriAevumModuleStatusV1
  SubmitInterleavedS16(std::uint32_t streamId, std::uint32_t channelCount,
                       std::uint32_t sampleRateHz,
                       std::span<const std::int16_t> samples,
                       AudioSubmissionResultV1 *result);
  TriAevumModuleStatusV1 SubmitInterleavedF32(std::uint32_t streamId,
                                              std::uint32_t channelCount,
                                              std::uint32_t sampleRateHz,
                                              std::span<const float> samples,
                                              AudioSubmissionResultV1 *result);
  TriAevumModuleStatusV1 SetStreamState(std::uint32_t streamId,
                                        TriAevumAudioStreamStateV1 state);

private:
  TriAevumModuleStatusV1
  SubmitPcm(std::uint32_t streamId, TriAevumAudioSampleFormatV1 sampleFormat,
            std::uint32_t channelCount, std::uint32_t sampleRateHz,
            std::uint32_t frameCount, std::span<const std::uint8_t> samples,
            AudioSubmissionResultV1 *result);
  TriAevumModuleStatusV1 Invoke(std::uint32_t operation,
                                std::span<const std::uint8_t> request,
                                std::span<std::uint8_t> response,
                                std::size_t *responseSize) const;

  TriAevumHostApiV1 mHost{};
  bool mAvailable = false;
};

} // namespace triaevum::module

#endif
