#include "triaevum/pica_service_client.h"

#include "triaevum/service_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace triaevum::module {
namespace {

constexpr std::uint32_t kMaximumPicaWords = 1024U * 1024U;
constexpr std::size_t kMaximumPicaInterrupts = 256U;

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

bool ValidWordCount(std::size_t count) {
  return count <= kMaximumPicaWords &&
         count <=
             std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint32_t);
}

template <typename T>
std::vector<std::uint8_t>
EncodeRequestWithWords(const T &header, std::span<const std::uint32_t> first,
                       std::span<const std::uint32_t> second = {}) {
  const std::size_t firstBytes = first.size_bytes();
  const std::size_t secondBytes = second.size_bytes();
  std::vector<std::uint8_t> encoded(sizeof(T) + firstBytes + secondBytes);
  std::memcpy(encoded.data(), &header, sizeof(T));
  if (!first.empty()) {
    std::memcpy(encoded.data() + sizeof(T), first.data(), firstBytes);
  }
  if (!second.empty()) {
    std::memcpy(encoded.data() + sizeof(T) + firstBytes, second.data(),
                secondBytes);
  }
  return encoded;
}

template <typename T> std::span<const std::uint8_t> AsBytes(const T &value) {
  return {reinterpret_cast<const std::uint8_t *>(&value), sizeof(T)};
}

} // namespace

PicaServiceClientV1::PicaServiceClientV1(const TriAevumHostApiV1 *host) {
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

bool PicaServiceClientV1::IsAvailable() const noexcept { return mAvailable; }

TriAevumModuleStatusV1 PicaServiceClientV1::Invoke(
    std::uint32_t operation, std::span<const std::uint8_t> request,
    std::span<std::uint8_t> response, std::size_t *responseSize) const {
  if (!mAvailable || responseSize == nullptr) {
    return mAvailable ? TRIAEVUM_MODULE_INVALID_ARGUMENT_V1
                      : TRIAEVUM_MODULE_INCOMPATIBLE_ABI_V1;
  }
  return mHost.invoke_service(mHost.host_context, TRIAEVUM_SERVICE_PICA_V1,
                              operation, {request.data(), request.size()},
                              {response.data(), response.size()}, responseSize);
}

TriAevumModuleStatusV1
PicaServiceClientV1::WriteRegisters(std::uint32_t base,
                                    std::span<const std::uint32_t> values,
                                    std::span<const std::uint32_t> masks) {
  if (values.empty() || !ValidWordCount(values.size()) ||
      (!masks.empty() && masks.size() != values.size())) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  TriAevumPicaWriteRegistersRequestV1 request{};
  request.header = RequestHeader<TriAevumPicaWriteRegistersRequestV1>();
  request.base_register = base;
  request.register_count = static_cast<std::uint32_t>(values.size());
  request.values = {static_cast<std::uint32_t>(sizeof(request)),
                    static_cast<std::uint32_t>(values.size_bytes())};
  if (!masks.empty()) {
    request.masks = {
        static_cast<std::uint32_t>(sizeof(request) + values.size_bytes()),
        static_cast<std::uint32_t>(masks.size_bytes())};
  }
  const auto encoded = EncodeRequestWithWords(request, values, masks);
  TriAevumPicaWriteRegistersResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  const TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_PICA_WRITE_REGISTERS_V1, encoded,
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  return DecodeFixedResponse(
      {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)},
      responseSize, &response);
}

TriAevumModuleStatusV1 PicaServiceClientV1::SubmitGspCommand(
    std::uint64_t frameSequence, std::uint32_t control,
    const std::array<std::uint32_t, 7> &parameters,
    std::span<const std::uint32_t> commandWords,
    PicaSubmissionResultV1 *result) {
  if (result == nullptr || !ValidWordCount(commandWords.size())) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  TriAevumPicaSubmitGspCommandRequestV1 request{};
  request.header = RequestHeader<TriAevumPicaSubmitGspCommandRequestV1>();
  request.frame_sequence = frameSequence;
  request.control = control;
  std::copy(parameters.begin(), parameters.end(), request.parameters);
  request.command_word_count = static_cast<std::uint32_t>(commandWords.size());
  if (!commandWords.empty()) {
    request.command_words = {
        static_cast<std::uint32_t>(sizeof(request)),
        static_cast<std::uint32_t>(commandWords.size_bytes())};
  }
  const auto encoded = EncodeRequestWithWords(request, commandWords);
  TriAevumPicaSubmitGspCommandResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_PICA_SUBMIT_GSP_COMMAND_V1, encoded,
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  status = DecodeFixedResponse(
      {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)},
      responseSize, &response);
  if (status == TRIAEVUM_MODULE_OK_V1) {
    result->submissionId = response.submission_id;
    result->flags = response.flags;
  }
  return status;
}

TriAevumModuleStatusV1 PicaServiceClientV1::SetFramebuffer(
    std::uint32_t screen, std::uint32_t activeBuffer, std::uint32_t addressLeft,
    std::uint32_t addressRight, std::uint32_t stride, std::uint32_t format,
    std::uint32_t shownBuffer) {
  const TriAevumPicaSetFramebufferRequestV1 request = {
      RequestHeader<TriAevumPicaSetFramebufferRequestV1>(),
      screen,
      activeBuffer,
      addressLeft,
      addressRight,
      stride,
      format,
      shownBuffer,
      0U,
  };
  TriAevumPicaSetFramebufferResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  const TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_PICA_SET_FRAMEBUFFER_V1, AsBytes(request),
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  return DecodeFixedResponse(
      {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)},
      responseSize, &response);
}

TriAevumModuleStatusV1 PicaServiceClientV1::SetLcdForceBlack(bool forceBlack) {
  const TriAevumPicaSetLcdForceBlackRequestV1 request = {
      RequestHeader<TriAevumPicaSetLcdForceBlackRequestV1>(),
      forceBlack ? 1U : 0U,
      0U,
  };
  TriAevumPicaSetLcdForceBlackResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  const TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_PICA_SET_LCD_FORCE_BLACK_V1, AsBytes(request),
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  return DecodeFixedResponse(
      {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)},
      responseSize, &response);
}

TriAevumModuleStatusV1
PicaServiceClientV1::TakeInterrupts(std::vector<std::uint8_t> *interrupts) {
  if (interrupts == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  const TriAevumPicaTakeInterruptsRequestV1 request = {
      RequestHeader<TriAevumPicaTakeInterruptsRequestV1>()};
  std::vector<std::uint8_t> encodedResponse(
      sizeof(TriAevumPicaTakeInterruptsResponseV1) + kMaximumPicaInterrupts);
  std::size_t responseSize = encodedResponse.size();
  TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_PICA_TAKE_INTERRUPTS_V1, AsBytes(request),
             encodedResponse, &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  TriAevumPicaTakeInterruptsResponseV1 response{};
  status = DecodeFixedResponse(encodedResponse, responseSize, &response);
  if (status != TRIAEVUM_MODULE_OK_V1 || response.reserved != 0U ||
      response.interrupt_count > kMaximumPicaInterrupts ||
      response.interrupts.size != response.interrupt_count) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  std::span<const std::uint8_t> payload;
  status = ResolveServicePayload({encodedResponse.data(), responseSize},
                                 response.header.struct_size,
                                 response.interrupts, &payload);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  interrupts->assign(payload.begin(), payload.end());
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1
PicaServiceClientV1::Flush(std::uint64_t frameSequence,
                           std::uint64_t *completedSubmissionId) {
  if (completedSubmissionId == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  const TriAevumPicaFlushRequestV1 request = {
      RequestHeader<TriAevumPicaFlushRequestV1>(), frameSequence};
  TriAevumPicaFlushResponseV1 response{};
  std::size_t responseSize = sizeof(response);
  TriAevumModuleStatusV1 status =
      Invoke(TRIAEVUM_PICA_FLUSH_V1, AsBytes(request),
             {reinterpret_cast<std::uint8_t *>(&response), sizeof(response)},
             &responseSize);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  status = DecodeFixedResponse(
      {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)},
      responseSize, &response);
  if (status == TRIAEVUM_MODULE_OK_V1) {
    *completedSubmissionId = response.completed_submission_id;
  }
  return status;
}

} // namespace triaevum::module
