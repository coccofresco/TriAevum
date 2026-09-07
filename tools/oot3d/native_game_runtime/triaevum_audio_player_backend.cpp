#include "triaevum_audio_player_backend.h"

#include "ship/audio/AudioPlayer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace Oot3dNativeGame {

TriAevumAudioPlayerBackend::TriAevumAudioPlayerBackend(
    std::shared_ptr<Ship::AudioPlayer> player)
    : mPlayer(std::move(player)) {}

TriAevumModuleStatusV1 TriAevumAudioPlayerBackend::SubmitPcm(
    const triaevum::module::AudioPcmSubmissionV1 &submission,
    std::uint32_t *acceptedFrameCount, std::uint32_t *queuedFrameCount) {
  if (acceptedFrameCount == nullptr || queuedFrameCount == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  *acceptedFrameCount = 0U;
  *queuedFrameCount = 0U;
  if (submission.streamId != 0U ||
      submission.sampleFormat != TRIAEVUM_AUDIO_S16_V1 ||
      submission.channelCount != 2U || submission.frameCount == 0U ||
      submission.samples.size() !=
          static_cast<std::size_t>(submission.frameCount) * 2U *
              sizeof(std::int16_t)) {
    return TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1;
  }
  if (mPlayer == nullptr || !mPlayer->IsInitialized() ||
      submission.sampleRateHz !=
          static_cast<std::uint32_t>(mPlayer->GetSampleRate())) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  if (mState != TRIAEVUM_AUDIO_STREAM_PLAYING_V1) {
    return TRIAEVUM_MODULE_SERVICE_BUSY_V1;
  }

  mPlayer->Play(submission.samples.data(), submission.samples.size());
  const int32_t buffered = std::max(mPlayer->Buffered(), 0);
  const std::uint32_t queued = static_cast<std::uint32_t>(
      std::min<int64_t>(buffered, std::numeric_limits<std::uint32_t>::max()));
  *acceptedFrameCount = submission.frameCount;
  *queuedFrameCount = queued;
  ++mStats.Submissions;
  mStats.AcceptedFrames += submission.frameCount;
  mStats.AcceptedBytes += submission.samples.size();
  mStats.LastQueuedFrames = queued;
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1
TriAevumAudioPlayerBackend::SetStreamState(std::uint32_t streamId,
                                           TriAevumAudioStreamStateV1 state) {
  if (streamId != 0U || state > TRIAEVUM_AUDIO_STREAM_PAUSED_V1) {
    return TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1;
  }
  if (mPlayer == nullptr || !mPlayer->IsInitialized()) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  if (state == TRIAEVUM_AUDIO_STREAM_STOPPED_V1) {
    mPlayer->Flush();
  }
  mState = state;
  ++mStats.StateChanges;
  return TRIAEVUM_MODULE_OK_V1;
}

const TriAevumAudioPlayerStats &
TriAevumAudioPlayerBackend::Stats() const noexcept {
  return mStats;
}

} // namespace Oot3dNativeGame
