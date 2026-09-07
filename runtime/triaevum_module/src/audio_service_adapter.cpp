#include "triaevum/audio_service_adapter.h"

#include "triaevum/service_codec.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace triaevum::module {
namespace {

constexpr std::uint32_t kMaximumAudioChannels = 8U;
constexpr std::uint32_t kMaximumAudioSampleRate = 768000U;
constexpr std::uint32_t kMaximumAudioFramesPerSubmission = 1024U * 1024U;
constexpr std::uint64_t kMaximumAudioPayloadBytes = 16ULL * 1024ULL * 1024ULL;

std::uint32_t BytesPerSample(TriAevumAudioSampleFormatV1 format) {
  switch (format) {
  case TRIAEVUM_AUDIO_S16_V1:
    return sizeof(std::int16_t);
  case TRIAEVUM_AUDIO_F32_V1:
    return sizeof(float);
  default:
    return 0U;
  }
}

template <typename T>
TriAevumModuleStatusV1 EmptyResponse(TriAevumMutableBytesV1 destination,
                                     std::size_t *responseSize) {
  const T response = {ResponseHeader<T>()};
  return EncodeServiceResponse(response, destination, responseSize);
}

} // namespace

AudioHostServiceAdapterV1::AudioHostServiceAdapterV1(
    AudioServiceBackendV1 &backend)
    : mBackend(backend) {}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL AudioHostServiceAdapterV1::Invoke(
    void *context, std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
    TriAevumMutableBytesV1 response, std::size_t *responseSize) {
  if (context == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  return static_cast<AudioHostServiceAdapterV1 *>(context)->Dispatch(
      operation, request, response, responseSize);
}

TriAevumModuleStatusV1 AudioHostServiceAdapterV1::Dispatch(
    std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
    TriAevumMutableBytesV1 response, std::size_t *responseSize) {
  if (responseSize == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  if (operation == TRIAEVUM_AUDIO_SUBMIT_PCM_V1) {
    TriAevumAudioSubmitPcmRequestV1 decoded{};
    TriAevumModuleStatusV1 status = DecodeServiceRequest(request, &decoded);
    const std::uint32_t sampleBytes = BytesPerSample(decoded.sample_format);
    if (status != TRIAEVUM_MODULE_OK_V1 || decoded.reserved != 0U ||
        sampleBytes == 0U || decoded.channel_count == 0U ||
        decoded.channel_count > kMaximumAudioChannels ||
        decoded.sample_rate_hz == 0U ||
        decoded.sample_rate_hz > kMaximumAudioSampleRate ||
        decoded.frame_count == 0U ||
        decoded.frame_count > kMaximumAudioFramesPerSubmission) {
      return status == TRIAEVUM_MODULE_OK_V1
                 ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
                 : status;
    }
    const std::uint64_t expectedBytes =
        static_cast<std::uint64_t>(decoded.frame_count) *
        decoded.channel_count * sampleBytes;
    if (expectedBytes > kMaximumAudioPayloadBytes ||
        expectedBytes > std::numeric_limits<std::uint32_t>::max() ||
        decoded.samples.size != expectedBytes) {
      return TRIAEVUM_MODULE_MALFORMED_REQUEST_V1;
    }
    std::span<const std::uint8_t> samples;
    status = ResolveServicePayload(request, decoded.header.struct_size,
                                   decoded.samples, &samples);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    const AudioPcmSubmissionV1 submission = {
        decoded.stream_id,      decoded.sample_format, decoded.channel_count,
        decoded.sample_rate_hz, decoded.frame_count,   samples,
    };
    std::uint32_t accepted = 0U;
    std::uint32_t queued = 0U;
    status = mBackend.SubmitPcm(submission, &accepted, &queued);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    if (accepted > decoded.frame_count) {
      return TRIAEVUM_MODULE_HOST_ERROR_V1;
    }
    const TriAevumAudioSubmitPcmResponseV1 result = {
        ResponseHeader<TriAevumAudioSubmitPcmResponseV1>(), accepted, queued};
    return EncodeServiceResponse(result, response, responseSize);
  }

  if (operation == TRIAEVUM_AUDIO_SET_STREAM_STATE_V1) {
    TriAevumAudioSetStreamStateRequestV1 decoded{};
    const TriAevumModuleStatusV1 status =
        DecodeServiceRequest(request, &decoded);
    if (status != TRIAEVUM_MODULE_OK_V1 ||
        decoded.state > TRIAEVUM_AUDIO_STREAM_PAUSED_V1) {
      return status == TRIAEVUM_MODULE_OK_V1
                 ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
                 : status;
    }
    const TriAevumModuleStatusV1 backendStatus =
        mBackend.SetStreamState(decoded.stream_id, decoded.state);
    return backendStatus == TRIAEVUM_MODULE_OK_V1
               ? EmptyResponse<TriAevumAudioSetStreamStateResponseV1>(
                     response, responseSize)
               : backendStatus;
  }

  return TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1;
}

} // namespace triaevum::module
