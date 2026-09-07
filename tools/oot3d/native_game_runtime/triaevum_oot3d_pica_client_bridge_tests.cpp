#include "triaevum_oot3d_pica_client_bridge.h"

#include "triaevum/pica_service_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace Oot3dNativeGame;
using namespace triaevum::module;

bool Expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

class FakeBackend final : public PicaServiceBackendV1 {
  public:
    TriAevumModuleStatusV1 WriteRegisters(std::uint32_t base, std::span<const std::uint32_t> values,
                                          std::span<const std::uint32_t> masks) override {
        Base = base;
        Values.assign(values.begin(), values.end());
        Masks.assign(masks.begin(), masks.end());
        return TRIAEVUM_MODULE_OK_V1;
    }

    TriAevumModuleStatusV1 SubmitGspCommand(std::uint64_t, std::uint32_t control,
                                            const std::array<std::uint32_t, 7>& parameters,
                                            std::span<const std::uint32_t> commandWords, std::uint64_t* submissionId,
                                            TriAevumPicaSubmissionFlagsV1* flags) override {
        Control = control;
        Parameters = parameters;
        Words.assign(commandWords.begin(), commandWords.end());
        *submissionId = 7U;
        *flags = TRIAEVUM_PICA_DISPLAY_TRANSFER_DEFERRED_V1 | TRIAEVUM_PICA_MEMORY_FILL_DEFERRED_V1 |
                 TRIAEVUM_PICA_DISPLAY_TRANSFER_CPU_COPY_SUPPRESSED_V1;
        return TRIAEVUM_MODULE_OK_V1;
    }

    TriAevumModuleStatusV1 SetFramebuffer(const PicaFramebufferV1& framebuffer) override {
        Framebuffer = framebuffer;
        return TRIAEVUM_MODULE_OK_V1;
    }

    TriAevumModuleStatusV1 SetLcdForceBlack(bool forceBlack) override {
        ForceBlack = forceBlack;
        return TRIAEVUM_MODULE_OK_V1;
    }

    TriAevumModuleStatusV1 TakeInterrupts(std::vector<std::uint8_t>* interrupts) override {
        *interrupts = { 1U, 4U };
        return TRIAEVUM_MODULE_OK_V1;
    }

    TriAevumModuleStatusV1 Flush(std::uint64_t, std::uint64_t* completedSubmissionId) override {
        *completedSubmissionId = 7U;
        return TRIAEVUM_MODULE_OK_V1;
    }

    std::uint32_t Base = 0U;
    std::vector<std::uint32_t> Values;
    std::vector<std::uint32_t> Masks;
    std::uint32_t Control = 0U;
    std::array<std::uint32_t, 7> Parameters{};
    std::vector<std::uint32_t> Words;
    PicaFramebufferV1 Framebuffer{};
    bool ForceBlack = false;
};

struct Fixture {
    explicit Fixture(FakeBackend& backend) : Adapter(backend) {
    }
    PicaHostServiceAdapterV1 Adapter;
};

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL InvokeService(void* context, std::uint32_t service, std::uint32_t operation,
                                                       TriAevumReadOnlyBytesV1 request, TriAevumMutableBytesV1 response,
                                                       std::size_t* responseSize) {
    if (service != TRIAEVUM_SERVICE_PICA_V1) {
        return TRIAEVUM_MODULE_SERVICE_NOT_FOUND_V1;
    }
    auto* fixture = static_cast<Fixture*>(context);
    return PicaHostServiceAdapterV1::Invoke(&fixture->Adapter, operation, request, response, responseSize);
}

} // namespace

int main() {
    bool ok = true;
    FakeBackend backend;
    Fixture fixture(backend);
    const TriAevumHostApiV1 host = {
        sizeof(TriAevumHostApiV1), TRIAEVUM_RUNTIME_ABI_V1, &fixture, nullptr, nullptr, InvokeService
    };
    PicaServiceClientV1 client(&host);
    TriAevumOot3dPicaClientBridge bridge(client);

    const std::array<std::uint32_t, 2> values{ 3U, 5U };
    const std::array<std::uint32_t, 2> masks{ 7U, 11U };
    std::string error;
    ok &= Expect(bridge.WriteHardwareRegisters(13U, values, masks, &error) && backend.Base == 13U &&
                     backend.Values.size() == 2U && backend.Masks.size() == 2U,
                 "register request did not cross the title bridge");

    const std::array<std::uint32_t, 7> parameters{ 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    NativeA32CtrPicaSubmissionResult result;
    ok &= Expect(bridge.SubmitGspCommand(17U, parameters, values, &result, &error) && backend.Control == 17U &&
                     backend.Parameters == parameters && result.DisplayTransferDeferredToGpu &&
                     result.MemoryFillDeferredToGpu && result.DisplayTransferCpuCopySuppressed,
                 "submission flags did not cross the title bridge");

    ok &= Expect(bridge.SetFramebuffer({ 0U, 1U, 2U, 3U, 4U, 5U, 1U }, &error) &&
                     backend.Framebuffer.addressLeft == 2U && backend.Framebuffer.shownBuffer == 1U,
                 "framebuffer state did not cross the title bridge");
    ok &=
        Expect(bridge.SetLcdForceBlack(true, &error) && backend.ForceBlack, "LCD state did not cross the title bridge");
    std::vector<std::uint8_t> interrupts;
    ok &=
        Expect(bridge.TakePendingInterrupts(&interrupts, &error) && interrupts == std::vector<std::uint8_t>({ 1U, 4U }),
               "interrupts did not cross the title bridge");

    const TriAevumHostApiV1 unavailableHost{};
    PicaServiceClientV1 unavailableClient(&unavailableHost);
    TriAevumOot3dPicaClientBridge unavailableBridge(unavailableClient);
    error.clear();
    ok &= Expect(!unavailableBridge.SetLcdForceBlack(false, &error) && !error.empty(),
                 "bridge hid a host-service failure");

    if (ok) {
        std::cout << "TriAevum OoT3D PICA client bridge tests passed\n";
        return 0;
    }
    return 1;
}
