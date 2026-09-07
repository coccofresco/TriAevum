#include "triaevum_oot3d_pica_renderer.h"
#include "triaevum_oot3d_pica_host.h"

#include "triaevum/service_codec.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "triaevum_oot3d_pica_renderer_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

Oot3dNativeGame::Oot3dPicaDrawSubmission BuildRepresentativeDraw() {
    using namespace Oot3dNativeGame;
    Oot3dPicaDrawSubmission draw;
    draw.Id = 1U;
    draw.MinimumVertexIndex = 2U;
    draw.MaximumVertexIndex = 5U;
    draw.Packet.Indexed = true;
    draw.Packet.CommandListAddress = 0x14001200U;
    draw.Packet.VertexShader.Program[0] = 0x88000000U;
    draw.Packet.VertexShader.ProgramWordCount = 1U;
    draw.Packet.VertexShader.SwizzleWordCount = 1U;
    draw.Packet.Registers[0x04F] = 1U;
    draw.Packet.Registers[0x050] = 0x03020100U;
    draw.Packet.Registers[0x2BD] = 1U;
    draw.Packet.DefaultAttributes[1] = { 1.0F, 2.0F, 3.0F, 4.0F };

    auto& state = draw.State;
    state.VertexInput.Indexed = true;
    state.VertexInput.IndicesAre16Bit = false;
    state.VertexInput.VertexCount = 3U;
    state.VertexInput.AttributeCount = 2U;
    state.VertexInput.Attributes[0] = { Oot3dPicaVertexFormat::Float, 3U, false };
    state.VertexInput.Attributes[1] = { Oot3dPicaVertexFormat::Float, 4U, true };
    state.VertexInput.Loaders[0].ByteStride = 12U;
    state.VertexInput.Loaders[0].ComponentCount = 1U;
    state.VertexInput.Loaders[0].Components[0] = 0U;
    state.ShaderInterface.OutputMask = 1U;
    state.ShaderInterface.InputRegisterByAttribute[0] = 0U;
    state.ShaderInterface.InputRegisterByAttribute[1] = 2U;

    Oot3dPicaResourceSnapshot indices;
    indices.Kind = Oot3dPicaResourceKind::IndexBuffer;
    indices.Bytes = { 2U, 5U, 3U };
    indices.ContentVersion = 11U;
    indices.ContentVersionAvailable = true;
    draw.Resources.push_back(std::move(indices));

    Oot3dPicaResourceSnapshot vertices;
    vertices.Kind = Oot3dPicaResourceKind::VertexLoader;
    vertices.Slot = 0U;
    vertices.FirstElement = 2U;
    vertices.Bytes.resize(48U);
    vertices.ContentVersion = 12U;
    vertices.ContentVersionAvailable = true;
    draw.Resources.push_back(std::move(vertices));
    return draw;
}

class ImmediateBackend final : public Oot3d::Renderer::PicaRenderBackend {
  public:
    bool SubmitPicaDraw(const Oot3d::Renderer::PicaDrawView&, std::string*) override {
        Operations.push_back("draw");
        return true;
    }

    bool SubmitPicaDisplayTransfer(const Oot3d::Renderer::PicaDisplayTransferView& transfer, std::string*) override {
        Operations.push_back(transfer.Present ? "transfer-present" : "transfer");
        return true;
    }

    bool ClearPicaRenderTarget(std::uint64_t, std::uint32_t, std::string*) override {
        Operations.push_back("clear");
        return true;
    }

    bool SubmitPicaMemoryFill(const Oot3d::Renderer::PicaMemoryFillView&, std::string*) override {
        Operations.push_back("fill");
        return true;
    }

    bool QueuePicaCompletion(std::uint64_t id, std::string*) override {
        Operations.push_back("fence:" + std::to_string(id));
        (CompleteImmediately ? Completed : Queued).push_back(id);
        return true;
    }

    std::vector<std::uint64_t> TakePicaCompletions() override {
        return std::exchange(Completed, {});
    }

    void CompleteQueued() {
        Completed.insert(Completed.end(), Queued.begin(), Queued.end());
        Queued.clear();
    }

    bool CompleteImmediately = true;
    std::vector<std::string> Operations;
    std::vector<std::uint64_t> Completed;
    std::vector<std::uint64_t> Queued;
};

} // namespace

int main() {
    using namespace Oot3dNativeGame;
    ImmediateBackend backend;
    std::vector<Oot3dPicaInterruptId> interrupts;
    TriAevumOot3dPicaRendererOptions options;
    options.RenderTargetNamespace = 9U;
    options.ShouldPresentTransfer = [](const auto&) { return true; };
    TriAevumOot3dPicaRenderer renderer(
        backend,
        [&](Oot3dPicaInterruptId interrupt, std::string*) {
            interrupts.push_back(interrupt);
            return true;
        },
        std::move(options));

    Oot3dPicaSubmissionBatch batch;
    batch.DisplayTransfers.push_back(
        { 1U, 0U, 0x20000200U, 0x20000300U, { 0x14000200U, 0x14000300U, 0x00100010U, 0x00100010U, 0U }, true });
    batch.MemoryFills.push_back({ 2U, 0U, 0x20000600U, 0x20000700U, 0xAABBCCDDU, 0x0201U, Oot3dPicaInterruptId::Psc0 });
    batch.Draws.push_back(BuildRepresentativeDraw());
    batch.Completions.push_back({ 3U, 1U, Oot3dPicaInterruptId::P3d });

    std::string error;
    Require(renderer.Consume(std::move(batch), &error), error);
    const std::vector<std::string> expectedOperations = {
        "transfer-present", "fence:1", "fill", "fence:2", "draw", "fence:3",
    };
    Require(backend.Operations == expectedOperations, "NRI backend work or fence ordering changed");
    Require(interrupts == std::vector<Oot3dPicaInterruptId>{ Oot3dPicaInterruptId::Ppf, Oot3dPicaInterruptId::Psc0,
                                                             Oot3dPicaInterruptId::P3d },
            "completed NRI fences did not publish native interrupts");
    Require(renderer.PendingCompletionCount() == 0U && renderer.Stats().DisplayTransfers == 1U &&
                renderer.Stats().MemoryFills == 1U && renderer.Stats().Draws == 1U,
            "NRI batch accounting retained completed work");

    backend.CompleteImmediately = false;
    Oot3dPicaSubmissionBatch delayedBatch;
    delayedBatch.DisplayTransfers.push_back(
        { 4U, 1U, 0x20000200U, 0x20000300U, { 0x14000200U, 0x14000300U, 0x00100010U, 0x00100010U, 0U }, true });
    Require(renderer.Consume(std::move(delayedBatch), &error) && renderer.PendingCompletionCount() == 1U &&
                interrupts.size() == 3U,
            "delayed Vulkan-style completion was published too early");
    backend.CompleteQueued();
    Require(renderer.PollCompletions(&error) && renderer.PendingCompletionCount() == 0U &&
                interrupts.back() == Oot3dPicaInterruptId::Ppf && interrupts.size() == 4U,
            "delayed backend fence did not publish its module interrupt");

    std::array<std::uint8_t, 256> guestMemory{};
    triaevum::module::TamModuleMetadataV1 metadata;
    metadata.requiredServices.push_back({ TRIAEVUM_SERVICE_PICA_V1, TRIAEVUM_SERVICE_SCHEMA_V1 });
    metadata.physicalMemoryRegions.push_back({ 0x20000000U, 0x1000U, 0x100U });
    auto host = TriAevumOot3dPicaHost::Create(
        metadata,
        [&](const TriAevumGuestMemoryMapRequestV1& request, TriAevumGuestMemoryViewV1* view) {
            if (request.guest_address < 0x1000U || request.guest_address - 0x1000U > guestMemory.size() ||
                request.byte_count > guestMemory.size() - (request.guest_address - 0x1000U)) {
                return TRIAEVUM_MODULE_TITLE_ERROR_V1;
            }
            *view = { sizeof(*view),
                      TRIAEVUM_GUEST_MEMORY_READ_V1,
                      guestMemory.data() + (request.guest_address - 0x1000U),
                      request.byte_count,
                      1U,
                      1U };
            return TRIAEVUM_MODULE_OK_V1;
        },
        [](std::uint64_t, std::uint32_t) { return TRIAEVUM_MODULE_OK_V1; }, {}, true, &error);
    Require(host != nullptr, error);

    TriAevumPicaSetFramebufferRequestV1 framebuffer{};
    framebuffer.header = triaevum::module::RequestHeader<TriAevumPicaSetFramebufferRequestV1>();
    framebuffer.address_left = 0x14000300U;
    framebuffer.address_right = 0x14000300U;
    framebuffer.stride = 960U;
    TriAevumPicaSetFramebufferResponseV1 framebufferResponse{};
    std::size_t responseSize = 0U;
    Require(host->Invoke(TRIAEVUM_PICA_SET_FRAMEBUFFER_V1,
                         { reinterpret_cast<const std::uint8_t*>(&framebuffer), sizeof(framebuffer) },
                         { reinterpret_cast<std::uint8_t*>(&framebufferResponse), sizeof(framebufferResponse) },
                         &responseSize) == TRIAEVUM_MODULE_OK_V1,
            "module framebuffer state was rejected");

    ImmediateBackend scanoutBackend;
    TriAevumOot3dPicaRenderer scanoutRenderer(scanoutBackend, *host);
    Oot3dPicaSubmissionBatch scanoutBatch;
    scanoutBatch.DisplayTransfers.push_back(
        { 1U, 0U, 0x20000200U, 0x20000300U, { 0x14000200U, 0x14000300U, 0x00100010U, 0x00100010U, 0U }, true });
    Require(scanoutRenderer.Consume(std::move(scanoutBatch), &error) &&
                scanoutBackend.Operations == std::vector<std::string>{ "transfer-present", "fence:1" },
            "host-selected top framebuffer was not presented");

    TriAevumPicaSetLcdForceBlackRequestV1 forceBlack{};
    forceBlack.header = triaevum::module::RequestHeader<TriAevumPicaSetLcdForceBlackRequestV1>();
    forceBlack.force_black = 1U;
    TriAevumPicaSetLcdForceBlackResponseV1 forceBlackResponse{};
    Require(host->Invoke(TRIAEVUM_PICA_SET_LCD_FORCE_BLACK_V1,
                         { reinterpret_cast<const std::uint8_t*>(&forceBlack), sizeof(forceBlack) },
                         { reinterpret_cast<std::uint8_t*>(&forceBlackResponse), sizeof(forceBlackResponse) },
                         &responseSize) == TRIAEVUM_MODULE_OK_V1,
            "module LCD force-black state was rejected");
    Oot3dPicaSubmissionBatch blackBatch;
    blackBatch.DisplayTransfers.push_back(
        { 2U, 0U, 0x20000200U, 0x20000400U, { 0x14000200U, 0x14000400U, 0x00100010U, 0x00100010U, 0U }, true });
    Require(scanoutRenderer.Consume(std::move(blackBatch), &error) && scanoutBackend.Operations.back() == "fence:2" &&
                scanoutBackend.Operations[2] == "transfer",
            "LCD force-black did not suppress module scanout presentation");

    forceBlack.force_black = 0U;
    Require(host->Invoke(TRIAEVUM_PICA_SET_LCD_FORCE_BLACK_V1,
                         { reinterpret_cast<const std::uint8_t*>(&forceBlack), sizeof(forceBlack) },
                         { reinterpret_cast<std::uint8_t*>(&forceBlackResponse), sizeof(forceBlackResponse) },
                         &responseSize) == TRIAEVUM_MODULE_OK_V1,
            "module LCD force-black state could not be cleared");
    framebuffer.address_left = 0x14000400U;
    framebuffer.address_right = 0x14000400U;
    Require(host->Invoke(TRIAEVUM_PICA_SET_FRAMEBUFFER_V1,
                         { reinterpret_cast<const std::uint8_t*>(&framebuffer), sizeof(framebuffer) },
                         { reinterpret_cast<std::uint8_t*>(&framebufferResponse), sizeof(framebufferResponse) },
                         &responseSize) == TRIAEVUM_MODULE_OK_V1,
            "module framebuffer state could not select a completed transfer");
    Require(scanoutRenderer.Consume({}, &error) && scanoutBackend.Operations.back() == "transfer-present" &&
                scanoutBackend.Operations.size() == 5U &&
                scanoutRenderer.RetainedScanoutPresentationCount() == 1U,
            "completed transfer was not retained for a later CTR scanout selection");
    return 0;
}
