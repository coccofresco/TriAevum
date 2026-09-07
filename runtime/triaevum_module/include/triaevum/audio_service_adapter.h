#ifndef TRIAEVUM_AUDIO_SERVICE_ADAPTER_H
#define TRIAEVUM_AUDIO_SERVICE_ADAPTER_H

#include "triaevum/service_abi.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace triaevum::module {

struct AudioPcmSubmissionV1 {
  std::uint32_t streamId = 0U;
  TriAevumAudioSampleFormatV1 sampleFormat = 0U;
  std::uint32_t channelCount = 0U;
  std::uint32_t sampleRateHz = 0U;
  std::uint32_t frameCount = 0U;
  std::span<const std::uint8_t> samples;
};

class AudioServiceBackendV1 {
public:
  virtual ~AudioServiceBackendV1() = default;

  virtual TriAevumModuleStatusV1
  SubmitPcm(const AudioPcmSubmissionV1 &submission,
            std::uint32_t *acceptedFrameCount,
            std::uint32_t *queuedFrameCount) = 0;
  virtual TriAevumModuleStatusV1
  SetStreamState(std::uint32_t streamId, TriAevumAudioStreamStateV1 state) = 0;
};

class AudioHostServiceAdapterV1 final {
public:
  explicit AudioHostServiceAdapterV1(AudioServiceBackendV1 &backend);

  static TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL Invoke(
      void *context, std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
      TriAevumMutableBytesV1 response, std::size_t *responseSize);

private:
  TriAevumModuleStatusV1 Dispatch(std::uint32_t operation,
                                  TriAevumReadOnlyBytesV1 request,
                                  TriAevumMutableBytesV1 response,
                                  std::size_t *responseSize);

  AudioServiceBackendV1 &mBackend;
};

} // namespace triaevum::module

#endif
