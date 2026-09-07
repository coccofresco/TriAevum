#include "triaevum/pica_service_adapter.h"

#include "triaevum/service_codec.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace triaevum::module {
namespace {

constexpr std::uint32_t kMaximumPicaWords = 1024U * 1024U;
constexpr std::size_t kMaximumPicaInterrupts = 256U;

TriAevumModuleStatusV1 DecodeWords(TriAevumReadOnlyBytesV1 request,
                                   std::uint32_t minimumOffset,
                                   TriAevumPayloadRangeV1 range,
                                   std::uint32_t count, bool optional,
                                   std::vector<std::uint32_t> *words) {
  if (words == nullptr || count > kMaximumPicaWords) {
    return TRIAEVUM_MODULE_MALFORMED_REQUEST_V1;
  }
  if (count == 0U) {
    if (!optional || range.offset != 0U || range.size != 0U) {
      return TRIAEVUM_MODULE_MALFORMED_REQUEST_V1;
    }
    words->clear();
    return TRIAEVUM_MODULE_OK_V1;
  }
  constexpr std::uint32_t wordBytes = sizeof(std::uint32_t);
  if (count > std::numeric_limits<std::uint32_t>::max() / wordBytes ||
      range.size != count * wordBytes) {
    return TRIAEVUM_MODULE_MALFORMED_REQUEST_V1;
  }
  std::span<const std::uint8_t> payload;
  const TriAevumModuleStatusV1 status =
      ResolveServicePayload(request, minimumOffset, range, &payload);
  if (status != TRIAEVUM_MODULE_OK_V1) {
    return status;
  }
  words->resize(count);
  std::memcpy(words->data(), payload.data(), payload.size());
  return TRIAEVUM_MODULE_OK_V1;
}

template <typename T>
TriAevumModuleStatusV1 EmptyResponse(TriAevumMutableBytesV1 destination,
                                     std::size_t *responseSize) {
  const T response = {ResponseHeader<T>()};
  return EncodeServiceResponse(response, destination, responseSize);
}

} // namespace

PicaHostServiceAdapterV1::PicaHostServiceAdapterV1(
    PicaServiceBackendV1 &backend)
    : mBackend(backend) {}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL PicaHostServiceAdapterV1::Invoke(
    void *context, std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
    TriAevumMutableBytesV1 response, std::size_t *responseSize) {
  if (context == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  return static_cast<PicaHostServiceAdapterV1 *>(context)->Dispatch(
      operation, request, response, responseSize);
}

TriAevumModuleStatusV1 PicaHostServiceAdapterV1::Dispatch(
    std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
    TriAevumMutableBytesV1 response, std::size_t *responseSize) {
  if (responseSize == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  if (operation == TRIAEVUM_PICA_WRITE_REGISTERS_V1) {
    TriAevumPicaWriteRegistersRequestV1 decoded{};
    TriAevumModuleStatusV1 status = DecodeServiceRequest(request, &decoded);
    if (status != TRIAEVUM_MODULE_OK_V1 || decoded.register_count == 0U) {
      return status == TRIAEVUM_MODULE_OK_V1
                 ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
                 : status;
    }
    std::vector<std::uint32_t> values;
    std::vector<std::uint32_t> masks;
    status = DecodeWords(request, decoded.header.struct_size, decoded.values,
                         decoded.register_count, false, &values);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    const bool hasMasks = decoded.masks.size != 0U;
    status = DecodeWords(request, decoded.header.struct_size, decoded.masks,
                         hasMasks ? decoded.register_count : 0U, true, &masks);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    status = mBackend.WriteRegisters(decoded.base_register, values, masks);
    return status == TRIAEVUM_MODULE_OK_V1
               ? EmptyResponse<TriAevumPicaWriteRegistersResponseV1>(
                     response, responseSize)
               : status;
  }

  if (operation == TRIAEVUM_PICA_SUBMIT_GSP_COMMAND_V1) {
    TriAevumPicaSubmitGspCommandRequestV1 decoded{};
    TriAevumModuleStatusV1 status = DecodeServiceRequest(request, &decoded);
    if (status != TRIAEVUM_MODULE_OK_V1 || decoded.reserved != 0U) {
      return status == TRIAEVUM_MODULE_OK_V1
                 ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
                 : status;
    }
    std::vector<std::uint32_t> words;
    status = DecodeWords(request, decoded.header.struct_size,
                         decoded.command_words, decoded.command_word_count,
                         true, &words);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    std::array<std::uint32_t, 7> parameters{};
    std::copy(std::begin(decoded.parameters), std::end(decoded.parameters),
              parameters.begin());
    std::uint64_t submissionId = 0U;
    TriAevumPicaSubmissionFlagsV1 flags = 0U;
    status = mBackend.SubmitGspCommand(
        decoded.frame_sequence, decoded.control, parameters, words,
        &submissionId, &flags);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    const TriAevumPicaSubmitGspCommandResponseV1 result = {
        ResponseHeader<TriAevumPicaSubmitGspCommandResponseV1>(), submissionId,
        flags, 0U};
    return EncodeServiceResponse(result, response, responseSize);
  }

  if (operation == TRIAEVUM_PICA_SET_FRAMEBUFFER_V1) {
    TriAevumPicaSetFramebufferRequestV1 decoded{};
    const TriAevumModuleStatusV1 status =
        DecodeServiceRequest(request, &decoded);
    if (status != TRIAEVUM_MODULE_OK_V1 || decoded.reserved != 0U) {
      return status == TRIAEVUM_MODULE_OK_V1
                 ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
                 : status;
    }
    const PicaFramebufferV1 framebuffer = {
        decoded.screen,       decoded.active_buffer, decoded.address_left,
        decoded.address_right, decoded.stride,        decoded.format,
        decoded.shown_buffer,
    };
    const TriAevumModuleStatusV1 backendStatus =
        mBackend.SetFramebuffer(framebuffer);
    return backendStatus == TRIAEVUM_MODULE_OK_V1
               ? EmptyResponse<TriAevumPicaSetFramebufferResponseV1>(
                     response, responseSize)
               : backendStatus;
  }

  if (operation == TRIAEVUM_PICA_SET_LCD_FORCE_BLACK_V1) {
    TriAevumPicaSetLcdForceBlackRequestV1 decoded{};
    const TriAevumModuleStatusV1 status =
        DecodeServiceRequest(request, &decoded);
    if (status != TRIAEVUM_MODULE_OK_V1 || decoded.reserved != 0U ||
        decoded.force_black > 1U) {
      return status == TRIAEVUM_MODULE_OK_V1
                 ? TRIAEVUM_MODULE_MALFORMED_REQUEST_V1
                 : status;
    }
    const TriAevumModuleStatusV1 backendStatus =
        mBackend.SetLcdForceBlack(decoded.force_black != 0U);
    return backendStatus == TRIAEVUM_MODULE_OK_V1
               ? EmptyResponse<TriAevumPicaSetLcdForceBlackResponseV1>(
                     response, responseSize)
               : backendStatus;
  }

  if (operation == TRIAEVUM_PICA_TAKE_INTERRUPTS_V1) {
    TriAevumPicaTakeInterruptsRequestV1 decoded{};
    TriAevumModuleStatusV1 status = DecodeServiceRequest(request, &decoded);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    std::vector<std::uint8_t> interrupts;
    status = mBackend.TakeInterrupts(&interrupts);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    if (interrupts.size() > kMaximumPicaInterrupts) {
      return TRIAEVUM_MODULE_HOST_ERROR_V1;
    }
    const std::size_t required =
        sizeof(TriAevumPicaTakeInterruptsResponseV1) + interrupts.size();
    *responseSize = required;
    if (response.data == nullptr || response.size < required) {
      return TRIAEVUM_MODULE_RESPONSE_TOO_SMALL_V1;
    }
    const TriAevumPicaTakeInterruptsResponseV1 result = {
        ResponseHeader<TriAevumPicaTakeInterruptsResponseV1>(),
        static_cast<std::uint32_t>(interrupts.size()),
        0U,
        {static_cast<std::uint32_t>(
             sizeof(TriAevumPicaTakeInterruptsResponseV1)),
         static_cast<std::uint32_t>(interrupts.size())},
    };
    std::memcpy(response.data, &result, sizeof(result));
    if (!interrupts.empty()) {
      std::memcpy(response.data + sizeof(result), interrupts.data(),
                  interrupts.size());
    }
    return TRIAEVUM_MODULE_OK_V1;
  }

  if (operation == TRIAEVUM_PICA_FLUSH_V1) {
    TriAevumPicaFlushRequestV1 decoded{};
    TriAevumModuleStatusV1 status = DecodeServiceRequest(request, &decoded);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    std::uint64_t completed = 0U;
    status = mBackend.Flush(decoded.frame_sequence, &completed);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      return status;
    }
    const TriAevumPicaFlushResponseV1 result = {
        ResponseHeader<TriAevumPicaFlushResponseV1>(), completed};
    return EncodeServiceResponse(result, response, responseSize);
  }

  return TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1;
}

} // namespace triaevum::module
