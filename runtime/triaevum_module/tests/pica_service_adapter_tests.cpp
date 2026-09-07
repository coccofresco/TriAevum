#include "triaevum/pica_service_adapter.h"
#include "triaevum/service_codec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace triaevum::module;

bool Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

class FakePicaBackend final : public PicaServiceBackendV1 {
public:
  TriAevumModuleStatusV1
  WriteRegisters(std::uint32_t base, std::span<const std::uint32_t> values,
                 std::span<const std::uint32_t> masks) override {
    registerBase = base;
    registerValues.assign(values.begin(), values.end());
    registerMasks.assign(masks.begin(), masks.end());
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 SubmitGspCommand(
      std::uint64_t frameSequence, std::uint32_t control,
      const std::array<std::uint32_t, 7> &parameters,
      std::span<const std::uint32_t> commandWords,
      std::uint64_t *submissionId,
      TriAevumPicaSubmissionFlagsV1 *flags) override {
    submittedFrame = frameSequence;
    submittedControl = control;
    submittedParameters = parameters;
    submittedWords.assign(commandWords.begin(), commandWords.end());
    *submissionId = 88U;
    *flags = TRIAEVUM_PICA_DISPLAY_TRANSFER_DEFERRED_V1;
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1
  SetFramebuffer(const PicaFramebufferV1 &value) override {
    framebuffer = value;
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 SetLcdForceBlack(bool value) override {
    forceBlack = value;
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1
  TakeInterrupts(std::vector<std::uint8_t> *output) override {
    *output = {2U, 3U};
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1
  Flush(std::uint64_t frameSequence,
        std::uint64_t *completedSubmissionId) override {
    flushedFrame = frameSequence;
    *completedSubmissionId = 88U;
    return TRIAEVUM_MODULE_OK_V1;
  }

  std::uint32_t registerBase = 0U;
  std::vector<std::uint32_t> registerValues;
  std::vector<std::uint32_t> registerMasks;
  std::uint64_t submittedFrame = 0U;
  std::uint32_t submittedControl = 0U;
  std::array<std::uint32_t, 7> submittedParameters{};
  std::vector<std::uint32_t> submittedWords;
  PicaFramebufferV1 framebuffer;
  bool forceBlack = false;
  std::uint64_t flushedFrame = 0U;
};

template <typename T>
TriAevumReadOnlyBytesV1 RequestBytes(const T &request) {
  return {reinterpret_cast<const std::uint8_t *>(&request), sizeof(request)};
}

std::vector<std::uint8_t> BuildRegisterRequest() {
  const std::array<std::uint32_t, 2> values = {0x11223344U, 0x55667788U};
  const std::array<std::uint32_t, 2> masks = {0xFFFFFFFFU, 0x0000FFFFU};
  TriAevumPicaWriteRegistersRequestV1 request{};
  request.header = RequestHeader<TriAevumPicaWriteRegistersRequestV1>();
  request.base_register = 0x120U;
  request.register_count = static_cast<std::uint32_t>(values.size());
  request.values = {sizeof(request), sizeof(values)};
  request.masks = {sizeof(request) + sizeof(values), sizeof(masks)};
  std::vector<std::uint8_t> bytes(sizeof(request) + sizeof(values) +
                                  sizeof(masks));
  std::memcpy(bytes.data(), &request, sizeof(request));
  std::memcpy(bytes.data() + sizeof(request), values.data(), sizeof(values));
  std::memcpy(bytes.data() + sizeof(request) + sizeof(values), masks.data(),
              sizeof(masks));
  return bytes;
}

std::vector<std::uint8_t> BuildGspRequest() {
  const std::array<std::uint32_t, 3> words = {1U, 2U, 3U};
  TriAevumPicaSubmitGspCommandRequestV1 request{};
  request.header = RequestHeader<TriAevumPicaSubmitGspCommandRequestV1>();
  request.frame_sequence = 9U;
  request.control = 4U;
  request.parameters[3] = 0xAABBCCDDU;
  request.command_word_count = static_cast<std::uint32_t>(words.size());
  request.command_words = {sizeof(request), sizeof(words)};
  std::vector<std::uint8_t> bytes(sizeof(request) + sizeof(words));
  std::memcpy(bytes.data(), &request, sizeof(request));
  std::memcpy(bytes.data() + sizeof(request), words.data(), sizeof(words));
  return bytes;
}

} // namespace

int main() {
  bool ok = true;
  FakePicaBackend backend;
  PicaHostServiceAdapterV1 adapter(backend);

  auto request = BuildRegisterRequest();
  TriAevumPicaWriteRegistersResponseV1 writeResponse{};
  std::size_t responseSize = 0U;
  auto status = PicaHostServiceAdapterV1::Invoke(
      &adapter, TRIAEVUM_PICA_WRITE_REGISTERS_V1,
      {request.data(), request.size()},
      {reinterpret_cast<std::uint8_t *>(&writeResponse), sizeof(writeResponse)},
      &responseSize);
  ok &= Expect(status == TRIAEVUM_MODULE_OK_V1 &&
                   backend.registerBase == 0x120U &&
                   backend.registerValues.size() == 2U &&
                   backend.registerMasks.back() == 0x0000FFFFU,
               "typed PICA register write was not dispatched");

  request = BuildGspRequest();
  TriAevumPicaSubmitGspCommandResponseV1 submitResponse{};
  status = PicaHostServiceAdapterV1::Invoke(
      &adapter, TRIAEVUM_PICA_SUBMIT_GSP_COMMAND_V1,
      {request.data(), request.size()},
      {reinterpret_cast<std::uint8_t *>(&submitResponse),
       sizeof(submitResponse)},
      &responseSize);
  ok &= Expect(status == TRIAEVUM_MODULE_OK_V1 &&
                   submitResponse.submission_id == 88U &&
                   backend.submittedFrame == 9U &&
                   backend.submittedParameters[3] == 0xAABBCCDDU &&
                   backend.submittedWords.size() == 3U,
               "typed PICA GSP command was not dispatched");

  TriAevumPicaSetFramebufferRequestV1 framebufferRequest{};
  framebufferRequest.header =
      RequestHeader<TriAevumPicaSetFramebufferRequestV1>();
  framebufferRequest.screen = 1U;
  framebufferRequest.address_left = 0x18000000U;
  TriAevumPicaSetFramebufferResponseV1 framebufferResponse{};
  status = PicaHostServiceAdapterV1::Invoke(
      &adapter, TRIAEVUM_PICA_SET_FRAMEBUFFER_V1,
      RequestBytes(framebufferRequest),
      {reinterpret_cast<std::uint8_t *>(&framebufferResponse),
       sizeof(framebufferResponse)},
      &responseSize);
  ok &= Expect(status == TRIAEVUM_MODULE_OK_V1 &&
                   backend.framebuffer.screen == 1U &&
                   backend.framebuffer.addressLeft == 0x18000000U,
               "typed PICA framebuffer update was not dispatched");

  TriAevumPicaSetLcdForceBlackRequestV1 blackRequest{};
  blackRequest.header =
      RequestHeader<TriAevumPicaSetLcdForceBlackRequestV1>();
  blackRequest.force_black = 1U;
  TriAevumPicaSetLcdForceBlackResponseV1 blackResponse{};
  status = PicaHostServiceAdapterV1::Invoke(
      &adapter, TRIAEVUM_PICA_SET_LCD_FORCE_BLACK_V1,
      RequestBytes(blackRequest),
      {reinterpret_cast<std::uint8_t *>(&blackResponse), sizeof(blackResponse)},
      &responseSize);
  ok &= Expect(status == TRIAEVUM_MODULE_OK_V1 && backend.forceBlack,
               "typed PICA force-black state was not dispatched");

  TriAevumPicaTakeInterruptsRequestV1 interruptRequest{};
  interruptRequest.header =
      RequestHeader<TriAevumPicaTakeInterruptsRequestV1>();
  std::array<std::uint8_t, 64> interruptResponse{};
  status = PicaHostServiceAdapterV1::Invoke(
      &adapter, TRIAEVUM_PICA_TAKE_INTERRUPTS_V1,
      RequestBytes(interruptRequest),
      {interruptResponse.data(), interruptResponse.size()}, &responseSize);
  TriAevumPicaTakeInterruptsResponseV1 decodedInterrupts{};
  std::memcpy(&decodedInterrupts, interruptResponse.data(),
              sizeof(decodedInterrupts));
  ok &= Expect(status == TRIAEVUM_MODULE_OK_V1 &&
                   decodedInterrupts.interrupt_count == 2U &&
                   interruptResponse[decodedInterrupts.interrupts.offset] ==
                       2U,
               "typed PICA interrupts were not returned inline");

  TriAevumPicaFlushRequestV1 flushRequest{};
  flushRequest.header = RequestHeader<TriAevumPicaFlushRequestV1>();
  flushRequest.frame_sequence = 9U;
  TriAevumPicaFlushResponseV1 flushResponse{};
  status = PicaHostServiceAdapterV1::Invoke(
      &adapter, TRIAEVUM_PICA_FLUSH_V1, RequestBytes(flushRequest),
      {reinterpret_cast<std::uint8_t *>(&flushResponse), sizeof(flushResponse)},
      &responseSize);
  ok &= Expect(status == TRIAEVUM_MODULE_OK_V1 &&
                   flushResponse.completed_submission_id == 88U &&
                   backend.flushedFrame == 9U,
               "typed PICA flush was not dispatched");

  request = BuildRegisterRequest();
  auto *malformed = reinterpret_cast<TriAevumPicaWriteRegistersRequestV1 *>(
      request.data());
  malformed->values.size -= 1U;
  status = PicaHostServiceAdapterV1::Invoke(
      &adapter, TRIAEVUM_PICA_WRITE_REGISTERS_V1,
      {request.data(), request.size()},
      {reinterpret_cast<std::uint8_t *>(&writeResponse), sizeof(writeResponse)},
      &responseSize);
  ok &= Expect(status == TRIAEVUM_MODULE_MALFORMED_REQUEST_V1,
               "malformed PICA payload reached its backend");
  return ok ? 0 : 1;
}
