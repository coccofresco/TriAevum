#include "triaevum/audio_service_adapter.h"
#include "triaevum/audio_service_client.h"
#include "triaevum/service_codec.h"
#include "triaevum/service_registry.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {

using namespace triaevum::module;

bool Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

class RecordingAudioBackend final : public AudioServiceBackendV1 {
public:
  TriAevumModuleStatusV1 SubmitPcm(const AudioPcmSubmissionV1 &submission,
                                   std::uint32_t *acceptedFrameCount,
                                   std::uint32_t *queuedFrameCount) override {
    if (acceptedFrameCount == nullptr || queuedFrameCount == nullptr) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    LastStream = submission.streamId;
    LastFormat = submission.sampleFormat;
    LastChannels = submission.channelCount;
    LastRate = submission.sampleRateHz;
    LastFrames = submission.frameCount;
    LastSamples.assign(submission.samples.begin(), submission.samples.end());
    ++Submissions;
    *acceptedFrameCount = submission.frameCount;
    *queuedFrameCount = 37U;
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1
  SetStreamState(std::uint32_t streamId,
                 TriAevumAudioStreamStateV1 state) override {
    LastStream = streamId;
    LastState = state;
    ++StateChanges;
    return TRIAEVUM_MODULE_OK_V1;
  }

  std::uint32_t LastStream = 0U;
  TriAevumAudioSampleFormatV1 LastFormat = 0U;
  std::uint32_t LastChannels = 0U;
  std::uint32_t LastRate = 0U;
  std::uint32_t LastFrames = 0U;
  TriAevumAudioStreamStateV1 LastState = 0U;
  std::vector<std::uint8_t> LastSamples;
  std::uint32_t Submissions = 0U;
  std::uint32_t StateChanges = 0U;
};

} // namespace

int main() {
  RecordingAudioBackend backend;
  AudioHostServiceAdapterV1 adapter(backend);
  HostServiceRegistry registry({});
  bool ok = true;
  ok &= Expect(registry.Register(TRIAEVUM_SERVICE_AUDIO_V1,
                                 AudioHostServiceAdapterV1::Invoke, &adapter) ==
                   ServiceRegistrationResult::Registered,
               "audio service registration failed");
  const TriAevumHostApiV1 host = registry.SealAndCreateHostApi();
  AudioServiceClientV1 client(&host);
  ok &= Expect(client.IsAvailable(), "audio client is unavailable");

  const std::array<std::int16_t, 8> s16{
      1, -2, 3, -4, 5, -6, 7, -8,
  };
  AudioSubmissionResultV1 result;
  ok &= Expect(client.SubmitInterleavedS16(4U, 2U, 32728U, s16, &result) ==
                       TRIAEVUM_MODULE_OK_V1 &&
                   result.acceptedFrameCount == 4U &&
                   result.queuedFrameCount == 37U && backend.LastStream == 4U &&
                   backend.LastFormat == TRIAEVUM_AUDIO_S16_V1 &&
                   backend.LastChannels == 2U && backend.LastRate == 32728U &&
                   backend.LastFrames == 4U &&
                   backend.LastSamples.size() == s16.size() * sizeof(s16[0]) &&
                   std::memcmp(backend.LastSamples.data(), s16.data(),
                               backend.LastSamples.size()) == 0,
               "S16 PCM did not cross the audio service boundary");

  const std::array<float, 2> f32{0.25F, -0.5F};
  ok &= Expect(client.SubmitInterleavedF32(5U, 1U, 48000U, f32, &result) ==
                       TRIAEVUM_MODULE_OK_V1 &&
                   backend.LastFormat == TRIAEVUM_AUDIO_F32_V1 &&
                   backend.LastFrames == 2U,
               "F32 PCM did not cross the audio service boundary");
  ok &= Expect(client.SetStreamState(5U, TRIAEVUM_AUDIO_STREAM_PAUSED_V1) ==
                       TRIAEVUM_MODULE_OK_V1 &&
                   backend.LastState == TRIAEVUM_AUDIO_STREAM_PAUSED_V1 &&
                   backend.StateChanges == 1U,
               "audio stream state did not cross the service boundary");
  ok &=
      Expect(client.SubmitInterleavedS16(
                 0U, 2U, 32728U, std::span<const std::int16_t>(s16.data(), 7U),
                 &result) == TRIAEVUM_MODULE_INVALID_ARGUMENT_V1,
             "client accepted a non-interleaved sample count");

  TriAevumAudioSubmitPcmRequestV1 malformed{};
  malformed.header = RequestHeader<TriAevumAudioSubmitPcmRequestV1>();
  malformed.sample_format = TRIAEVUM_AUDIO_S16_V1;
  malformed.channel_count = 2U;
  malformed.sample_rate_hz = 32728U;
  malformed.frame_count = 1U;
  malformed.samples = {static_cast<std::uint32_t>(sizeof(malformed)), 2U};
  std::vector<std::uint8_t> malformedBytes(sizeof(malformed) + 2U);
  std::memcpy(malformedBytes.data(), &malformed, sizeof(malformed));
  TriAevumAudioSubmitPcmResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  ok &= Expect(
      host.invoke_service(
          host.host_context, TRIAEVUM_SERVICE_AUDIO_V1,
          TRIAEVUM_AUDIO_SUBMIT_PCM_V1,
          {malformedBytes.data(), malformedBytes.size()},
          {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
          &responseSize) == TRIAEVUM_MODULE_MALFORMED_REQUEST_V1 &&
          backend.Submissions == 2U,
      "audio adapter accepted a truncated PCM payload");
  return ok ? 0 : 1;
}
