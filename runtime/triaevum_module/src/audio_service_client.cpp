#include "triaevum/audio_service_client.h"

#include "triaevum/service_codec.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace triaevum::module {
namespace {

template <typename T>
TriAevumModuleStatusV1
DecodeFixedResponse(std::span<const std::uint8_t> encoded,
                    std::size_t responseSize, T *decoded) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (decoded == nullptr || responseSize < sizeof(T) ||
      responseSize > encoded.size()) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  std::memcpy(decoded, encoded.data(), sizeof(T));
  if (decoded->header.schema_version != TRIAEVUM_SERVICE_SCHEMA_V1 ||
      decoded->header.struct_size < sizeof(T) ||
      decoded->header.struct_size > responseSize) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  return TRIAEVUM_MODULE_OK_V1;
}

template <typename T> std::span<const std::uint8_t> AsBytes(const T &value) {
  return {reinterpret_cast<const std::uint8_t *>(&value), sizeof(T)};
}

} // namespace

AudioServiceClientV1::AudioServiceClientV1(const TriAevumHostApiV1 *host) {
  constexpr std::size_t requiredSize =
      offsetof(TriAevumHostApiV1, invoke_service) +
      sizeof(TriAevumInvokeServiceFnV1);
  if (host == nullptr || host->struct_size < requiredSize ||
      host->abi_version != TRIAEVUM_RUNTIME_ABI_V1 ||
      host->invoke_service == nullptr) {
    return;
  }
  mHost = *host;
  mAvailable = true;
}

bool AudioServiceClientV1::IsAvailable() const noexcept { return mAvailable; }

TriAevumModuleStatusV1 AudioServiceClientV1::Invoke(
    std::uint32_t operation, std::span<const std::uint8_t> request,
    std::span<std::uint8_t> response, std::size_t *responseSize) const {
  if (!mAvailable || responseSize == nullptr) {
    return mAvailable ? TRIAEVUM_MODULE_INVALID_ARGUMENT_V1
                      : TRIAEVUM_MODULE_INCOMPATIBLE_ABI_V1;
  }
  return mHost.invoke_service(mHost.host_context, TRIAEVUM_SERVICE_AUDIO_V1,
                              operation, {request.data(), request.size()},
                              {response.data(), response.size()}, responseSize);
}

TriAevumModuleStatusV1 AudioServiceClientV1::SubmitPcm(
    std::uint32_t streamId, TriAevumAudioSampleFormatV1 sampleFormat,
    std::uint32_t channelCount, std::uint32_t sampleRateHz,
    std::uint32_t frameCount, std::span<const std::uint8_t> samples,
    AudioSubmissionResultV1 *result) {
  if (result == nullptr || channelCount == 0U || sampleRateHz == 0U ||
      frameCount == 0U || samples.empty() ||
      samples.size() > std::numeric_limits<std::uint32_t>::max()) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  TriAevumAudioSubmitPcmRequestV1 request{};
  request.header = RequestHeader<TriAevumAudioSubmitPcmRequestV1>();
  request.stream_id = streamId;
  request.sample_format = sampleFormat;
  request.channel_count = channelCount;
  request.sample_rate_hz = sampleRateHz;
  request.frame_count = frameCount;
  request.samples = {static_cast<std::uint32_t>(sizeof(request)),
                     static_cast<std::uint32_t>(samples.size())};
  std::vector<std::uint8_t> encoded(sizeof(request) + samples.size());
  std::memcpy(encoded.data(), &request, sizeof(request));
  std::memcpy(encoded.data() + sizeof(request), samples.data(), samples.size());

  TriAevumAudioSubmitPcmResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_AUDIO_SUBMIT_PCM_V1, encoded,
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  status = DecodeFixedResponse(
      {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)},
      responseSize, &response);
  if (status != TRIAEVUM_MODULE_OK_V1 ||
      response.accepted_frame_count > frameCount) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  result->acceptedFrameCount = response.accepted_frame_count;
  result->queuedFrameCount = response.queued_frame_count;
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1 AudioServiceClientV1::SubmitInterleavedS16(
    std::uint32_t streamId, std::uint32_t channelCount,
    std::uint32_t sampleRateHz, std::span<const std::int16_t> samples,
    AudioSubmissionResultV1 *result) {
  if (channelCount == 0U || samples.empty() ||
      samples.size() % channelCount != 0U ||
      samples.size() / channelCount >
          std::numeric_limits<std::uint32_t>::max()) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  return SubmitPcm(streamId, TRIAEVUM_AUDIO_S16_V1, channelCount, sampleRateHz,
                   static_cast<std::uint32_t>(samples.size() / channelCount),
                   {reinterpret_cast<const std::uint8_t *>(samples.data()),
                    samples.size_bytes()},
                   result);
}

TriAevumModuleStatusV1 AudioServiceClientV1::SubmitInterleavedF32(
    std::uint32_t streamId, std::uint32_t channelCount,
    std::uint32_t sampleRateHz, std::span<const float> samples,
    AudioSubmissionResultV1 *result) {
  if (channelCount == 0U || samples.empty() ||
      samples.size() % channelCount != 0U ||
      samples.size() / channelCount >
          std::numeric_limits<std::uint32_t>::max()) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  return SubmitPcm(streamId, TRIAEVUM_AUDIO_F32_V1, channelCount, sampleRateHz,
                   static_cast<std::uint32_t>(samples.size() / channelCount),
                   {reinterpret_cast<const std::uint8_t *>(samples.data()),
                    samples.size_bytes()},
                   result);
}

TriAevumModuleStatusV1
AudioServiceClientV1::SetStreamState(std::uint32_t streamId,
                                     TriAevumAudioStreamStateV1 state) {
  if (state > TRIAEVUM_AUDIO_STREAM_PAUSED_V1) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  const TriAevumAudioSetStreamStateRequestV1 request = {
      RequestHeader<TriAevumAudioSetStreamStateRequestV1>(), streamId, state};
  TriAevumAudioSetStreamStateResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  const TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_AUDIO_SET_STREAM_STATE_V1, AsBytes(request),
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  return DecodeFixedResponse(
      {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)},
      responseSize, &response);
}

} // namespace triaevum::module
