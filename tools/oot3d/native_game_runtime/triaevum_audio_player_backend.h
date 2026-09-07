#pragma once

#include "triaevum/audio_service_adapter.h"

#include <cstdint>
#include <memory>

namespace Ship {
class AudioPlayer;
}

namespace Oot3dNativeGame {

struct TriAevumAudioPlayerStats {
  std::uint64_t Submissions = 0U;
  std::uint64_t AcceptedFrames = 0U;
  std::uint64_t AcceptedBytes = 0U;
  std::uint64_t StateChanges = 0U;
  std::uint32_t LastQueuedFrames = 0U;
};

class TriAevumAudioPlayerBackend final
    : public triaevum::module::AudioServiceBackendV1 {
public:
  explicit TriAevumAudioPlayerBackend(
      std::shared_ptr<Ship::AudioPlayer> player);

  TriAevumModuleStatusV1
  SubmitPcm(const triaevum::module::AudioPcmSubmissionV1 &submission,
            std::uint32_t *acceptedFrameCount,
            std::uint32_t *queuedFrameCount) override;
  TriAevumModuleStatusV1
  SetStreamState(std::uint32_t streamId,
                 TriAevumAudioStreamStateV1 state) override;

  [[nodiscard]] const TriAevumAudioPlayerStats &Stats() const noexcept;

private:
  std::shared_ptr<Ship::AudioPlayer> mPlayer;
  TriAevumAudioStreamStateV1 mState = TRIAEVUM_AUDIO_STREAM_STOPPED_V1;
  TriAevumAudioPlayerStats mStats;
};

} // namespace Oot3dNativeGame
