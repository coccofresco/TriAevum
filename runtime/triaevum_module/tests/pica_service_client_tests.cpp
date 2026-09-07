#include "triaevum/pica_service_adapter.h"
#include "triaevum/pica_service_client.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using triaevum::module::PicaFramebufferV1;
using triaevum::module::PicaHostServiceAdapterV1;
using triaevum::module::PicaServiceBackendV1;
using triaevum::module::PicaServiceClientV1;
using triaevum::module::PicaSubmissionResultV1;

bool Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

class FakeBackend final : public PicaServiceBackendV1 {
public:
  TriAevumModuleStatusV1
  WriteRegisters(std::uint32_t base, std::span<const std::uint32_t> values,
                 std::span<const std::uint32_t> masks) override {
    registerBase = base;
    registerValues.assign(values.begin(), values.end());
    registerMasks.assign(masks.begin(), masks.end());
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1
  SubmitGspCommand(std::uint64_t frameSequence, std::uint32_t control,
                   const std::array<std::uint32_t, 7> &parameters,
                   std::span<const std::uint32_t> commandWords,
                   std::uint64_t *submissionId,
                   TriAevumPicaSubmissionFlagsV1 *flags) override {
    submittedFrame = frameSequence;
    submittedControl = control;
    submittedParameters = parameters;
    submittedWords.assign(commandWords.begin(), commandWords.end());
    *submissionId = 41U;
    *flags = TRIAEVUM_PICA_DISPLAY_TRANSFER_DEFERRED_V1 |
             TRIAEVUM_PICA_MEMORY_FILL_DEFERRED_V1;
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
  TakeInterrupts(std::vector<std::uint8_t> *values) override {
    *values = {2U, 5U, 7U};
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 Flush(std::uint64_t frameSequence,
                               std::uint64_t *completedSubmissionId) override {
    flushedFrame = frameSequence;
    *completedSubmissionId = 41U;
    return TRIAEVUM_MODULE_OK_V1;
  }

  std::uint32_t registerBase = 0U;
  std::vector<std::uint32_t> registerValues;
  std::vector<std::uint32_t> registerMasks;
  std::uint64_t submittedFrame = 0U;
  std::uint32_t submittedControl = 0U;
  std::array<std::uint32_t, 7> submittedParameters{};
  std::vector<std::uint32_t> submittedWords;
  PicaFramebufferV1 framebuffer{};
  bool forceBlack = false;
  std::uint64_t flushedFrame = 0U;
};

struct HostFixture {
  explicit HostFixture(FakeBackend &backend) : adapter(backend) {}
  PicaHostServiceAdapterV1 adapter;
};

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
InvokeService(void *context, std::uint32_t service, std::uint32_t operation,
              TriAevumReadOnlyBytesV1 request, TriAevumMutableBytesV1 response,
              std::size_t *responseSize) {
  if (service != TRIAEVUM_SERVICE_PICA_V1) {
    return TRIAEVUM_MODULE_SERVICE_NOT_FOUND_V1;
  }
  auto *fixture = static_cast<HostFixture *>(context);
  return PicaHostServiceAdapterV1::Invoke(&fixture->adapter, operation, request,
                                          response, responseSize);
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
MalformedService(void *, std::uint32_t, std::uint32_t, TriAevumReadOnlyBytesV1,
                 TriAevumMutableBytesV1 response, std::size_t *responseSize) {
  if (response.data == nullptr ||
      response.size < sizeof(TriAevumPicaFlushResponseV1)) {
    return TRIAEVUM_MODULE_RESPONSE_TOO_SMALL_V1;
  }
  TriAevumPicaFlushResponseV1 malformed{};
  malformed.header.struct_size = sizeof(malformed);
  malformed.header.schema_version = 99U;
  std::memcpy(response.data, &malformed, sizeof(malformed));
  *responseSize = sizeof(malformed);
  return TRIAEVUM_MODULE_OK_V1;
}

} // namespace

int main() {
  bool ok = true;
  FakeBackend backend;
  HostFixture fixture(backend);
  const TriAevumHostApiV1 host = {sizeof(TriAevumHostApiV1),
                                  TRIAEVUM_RUNTIME_ABI_V1,
                                  &fixture,
                                  nullptr,
                                  nullptr,
                                  InvokeService};
  PicaServiceClientV1 client(&host);
  ok &= Expect(client.IsAvailable(), "valid PICA host was rejected");

  const std::array<std::uint32_t, 3> values{10U, 20U, 30U};
  const std::array<std::uint32_t, 3> masks{1U, 2U, 3U};
  ok &= Expect(client.WriteRegisters(0x100U, values, masks) ==
                   TRIAEVUM_MODULE_OK_V1,
               "register write failed");
  ok &=
      Expect(backend.registerBase == 0x100U &&
                 backend.registerValues ==
                     std::vector<std::uint32_t>(values.begin(), values.end()) &&
                 backend.registerMasks ==
                     std::vector<std::uint32_t>(masks.begin(), masks.end()),
             "register payload changed across the service boundary");
  ok &= Expect(
      client.WriteRegisters(0x100U, values,
                            std::span<const std::uint32_t>(masks).first(2U)) ==
          TRIAEVUM_MODULE_INVALID_ARGUMENT_V1,
      "mismatched masks were accepted");

  const std::array<std::uint32_t, 7> parameters{1U, 2U, 3U, 4U, 5U, 6U, 7U};
  const std::array<std::uint32_t, 2> words{0x12345678U, 0x9ABCDEF0U};
  PicaSubmissionResultV1 submission;
  ok &= Expect(client.SubmitGspCommand(9U, 4U, parameters, words,
                                       &submission) == TRIAEVUM_MODULE_OK_V1,
               "GSP submission failed");
  ok &= Expect(backend.submittedFrame == 9U && backend.submittedControl == 4U &&
                   backend.submittedParameters == parameters &&
                   backend.submittedWords ==
                       std::vector<std::uint32_t>(words.begin(), words.end()) &&
                   submission.submissionId == 41U &&
                   submission.flags ==
                       (TRIAEVUM_PICA_DISPLAY_TRANSFER_DEFERRED_V1 |
                        TRIAEVUM_PICA_MEMORY_FILL_DEFERRED_V1),
               "GSP response or payload changed across the service boundary");

  ok &= Expect(client.SetFramebuffer(0U, 1U, 2U, 3U, 4U, 5U, 1U) ==
                       TRIAEVUM_MODULE_OK_V1 &&
                   backend.framebuffer.addressLeft == 2U &&
                   backend.framebuffer.shownBuffer == 1U,
               "framebuffer state did not reach the host");
  ok &= Expect(client.SetLcdForceBlack(true) == TRIAEVUM_MODULE_OK_V1 &&
                   backend.forceBlack,
               "LCD state did not reach the host");

  std::vector<std::uint8_t> interrupts;
  ok &= Expect(client.TakeInterrupts(&interrupts) == TRIAEVUM_MODULE_OK_V1 &&
                   interrupts == std::vector<std::uint8_t>({2U, 5U, 7U}),
               "interrupt response was not decoded");
  std::uint64_t completed = 0U;
  ok &= Expect(client.Flush(12U, &completed) == TRIAEVUM_MODULE_OK_V1 &&
                   backend.flushedFrame == 12U && completed == 41U,
               "flush response was not decoded");

  TriAevumHostApiV1 unavailableHost{};
  PicaServiceClientV1 unavailable(&unavailableHost);
  ok &= Expect(!unavailable.IsAvailable() &&
                   unavailable.SetLcdForceBlack(false) ==
                       TRIAEVUM_MODULE_INCOMPATIBLE_ABI_V1,
               "incomplete host API was accepted");

  TriAevumHostApiV1 malformedHost = host;
  malformedHost.invoke_service = MalformedService;
  PicaServiceClientV1 malformed(&malformedHost);
  completed = 0U;
  ok &= Expect(malformed.Flush(1U, &completed) == TRIAEVUM_MODULE_HOST_ERROR_V1,
               "malformed host response was accepted");

  if (ok) {
    std::cout << "TriAevum PICA service client tests passed\n";
    return 0;
  }
  return 1;
}
