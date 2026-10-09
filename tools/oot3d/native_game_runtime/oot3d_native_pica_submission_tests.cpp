#include "oot3d_native_pica_submission.h"
#include "oot3d_native_a32_memory.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_pica_submission_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

// Add in the anonymous namespace of oot3d_native_pica_submission_tests.cpp.
// This tests the production submission queue, not a second ownership model.
// A colour target published lazily, at the first read after its writes, owns
// only what no newer producer took: a later transfer into its memory keeps its
// own bytes. The capture itself is an ordered record with no guest completion.
void TestColorTargetCaptureKeepsNewerProducers() {
    using namespace Oot3dNativeGame;
    NativeA32Memory memory;
    std::string error;
    Require(memory.MapRegion({"capture", 0x14000000U, 0x200000U, true, false, {}}, &error), error);
    Oot3dNativePicaSubmissionQueue queue(
        Oot3dPicaPhysicalMemoryView(memory, {{0x20000000U, 0x14000000U, 0x200000U}}), true);
    queue.BindGuestWriteObservation([](uint32_t, size_t) { return true; });
    Oot3dPicaDrawPacket packet{};
    packet.Registers[0x200] = 0x20000000U / 8;
    packet.Registers[0x201] = 3U;
    packet.Registers[0x203] = 0x100U;
    packet.Registers[0x205] = 0x10040000U;
    packet.Registers[0x228] = 3U;
    packet.Registers[0x113] = 15U;
    packet.Registers[0x107] = 0xF00U;
    const auto target = [&](uint32_t address, uint32_t w, uint32_t h) {
        packet.Registers[0x11D] = address / 8;
        packet.Registers[0x11E] = (1U << 24) | ((h - 1) << 12) | w;
    };
    const auto texture = [&](uint32_t address, uint32_t w, uint32_t h) {
        packet.Registers[0x080] = 1U;
        packet.Registers[0x082] = (w << 16) | h;
        packet.Registers[0x085] = address / 8;
        packet.Registers[0x08E] = 0U;
    };
    // A 64x64 target A, then a 64x64 target B downscaled 2x2 into A's first
    // 32x32 texels: the transfer is newer than A's write.
    target(0x20100000U, 64, 64);
    Require(queue.SubmitDrawPacket(packet, &error), error);
    target(0x20120000U, 64, 64);
    Require(queue.SubmitDrawPacket(packet, &error), error);
    bool deferred = false;
    Require(queue.SubmitDisplayTransfer(
        {0x14120000U, 0x14100000U, (64U << 16) | 64, (64U << 16) | 64, 0x02000020U},
        &deferred, &error) && deferred, error);
    const auto transferId = queue.PendingDisplayTransfers().back().CompletionId;
    target(0x20140000U, 64, 64);
    texture(0x20100000U, 64, 64);
    Require(queue.SubmitDrawPacket(packet, &error), error);
    auto refs = queue.PendingDraws().back().Resources.back().CopyReferences;
    std::sort(refs.begin(), refs.end(), [](const auto& a, const auto& b) {
        return a.PhysicalAddress < b.PhysicalAddress;
    });
    Require(refs.size() == 2 && refs[0].CompletionId == transferId &&
                refs[0].PhysicalAddress == 0x20100000U && refs[0].ByteCount == 4096U &&
                refs[1].CompletionId != transferId &&
                refs[1].PhysicalAddress == 0x20101000U && refs[1].ByteCount == 12288U,
            "a lazily published colour target took a newer transfer's bytes");
    const auto& capture = queue.PendingDisplayTransfers().back();
    Require(capture.RenderTargetCapture && !capture.SignalInterrupt &&
                capture.CompletionId == refs[1].CompletionId &&
                capture.InputPhysicalAddress == 0x20100000U &&
                capture.Transfer.InputSize == ((64U << 16) | 64U) &&
                capture.AfterDrawSubmissionId < queue.PendingDraws().back().Id,
            "the colour-target capture is not an ordered, completion-free record");
    // Read again without a write in between: the same version, no new capture.
    const size_t records = queue.PendingDisplayTransfers().size();
    Require(queue.SubmitDrawPacket(packet, &error), error);
    Require(queue.PendingDisplayTransfers().size() == records,
            "an unchanged colour target was captured twice");
    // A guest load into the memory of a written, not yet sampled target (a
    // scene change reusing it for an asset) takes those bytes back: a later
    // read samples the asset, and only the untouched rest stays GPU-owned.
    target(0x20160000U, 64, 64);
    texture(0x20140000U, 64, 64);
    Require(queue.SubmitDrawPacket(packet, &error), error);
    std::array<uint8_t, 8192> asset{};
    Require(memory.WriteBytes(0x14160000U, asset), "asset load");
    queue.NotifyGuestWrite(0x14160000U, asset.size());
    target(0x20180000U, 64, 64);
    texture(0x20160000U, 64, 64);
    Require(queue.SubmitDrawPacket(packet, &error), error);
    const auto reused = queue.PendingDraws().back().Resources.back().CopyReferences;
    Require(reused.size() == 1 && reused[0].PhysicalAddress == 0x20162000U &&
                reused[0].ByteCount == 8192U,
            "a colour target kept bytes the guest overwrote before the read");
    // Overwritten entirely: no GPU producer remains and nothing is captured.
    target(0x201A0000U, 64, 64);
    texture(0x20180000U, 64, 64);
    std::array<uint8_t, 16384> whole{};
    Require(queue.SubmitDrawPacket(packet, &error), error);
    const size_t before = queue.PendingDisplayTransfers().size();
    Require(memory.WriteBytes(0x141A0000U, whole), "whole asset load");
    queue.NotifyGuestWrite(0x141A0000U, whole.size());
    texture(0x201A0000U, 64, 64);
    Require(queue.SubmitDrawPacket(packet, &error), error);
    Require(queue.PendingDraws().back().Resources.back().CopyReferences.empty() &&
                queue.PendingDisplayTransfers().size() == before,
            "a fully overwritten colour target was still captured");
}

// Review counterexamples, each on a fresh queue: a fill never becomes GPU
// ownership (the host also fills guest memory, an observed write), a draw
// with a zero colour mask writes no colour, and a tiled overwrite with no GPU
// version still ends the older producer's ownership.
void TestFeedbackOwnershipReviewRegressions() {
    using namespace Oot3dNativeGame;
    for (unsigned scenario = 0; scenario != 4; ++scenario) {
        NativeA32Memory memory;
        std::string error;
        Require(memory.MapRegion({"review", 0x14000000U, 0x200000U, true, false, {}}, &error), error);
        Oot3dNativePicaSubmissionQueue queue(
            Oot3dPicaPhysicalMemoryView(memory, {{0x20000000U, 0x14000000U, 0x200000U}}), true);
        queue.BindGuestWriteObservation([](uint32_t, size_t) { return true; });
        Oot3dPicaDrawPacket packet{};
        packet.Registers[0x200] = 0x20000000U / 8;
        packet.Registers[0x201] = 3U;
        packet.Registers[0x203] = 0x100U;
        packet.Registers[0x205] = 0x10040000U;
        packet.Registers[0x228] = 3U;
        packet.Registers[0x113] = 15U;
        packet.Registers[0x107] = 0xF00U;
        const auto target = [&](uint32_t address) {
            packet.Registers[0x11D] = address / 8;
            packet.Registers[0x11E] = (1U << 24) | (63U << 12) | 64U;
        };
        const auto sampleRefs = [&](uint32_t address) {
            target(0x20120000U);
            packet.Registers[0x107] = 0xF00U;
            packet.Registers[0x080] = 1U;
            packet.Registers[0x082] = (64U << 16) | 64U;
            packet.Registers[0x085] = address / 8;
            packet.Registers[0x08E] = 0U;
            Require(queue.SubmitDrawPacket(packet, &error), error);
            return queue.PendingDraws().back().Resources.back().CopyReferences;
        };
        target(0x20100000U);
        Require(queue.SubmitDrawPacket(packet, &error), error);
        if (scenario < 3) {
            // The guest now owns the whole target; same-value writes count.
            std::array<uint8_t, 16384> bytes{};
            Require(memory.WriteBytes(0x14100000U, bytes), "review CPU overwrite");
            queue.NotifyGuestWrite(0x14100000U, bytes.size());
            if (scenario < 2) {
                const uint32_t offset = scenario == 0 ? 64U : 0U;
                Oot3dPicaMemoryFillCommand fill{};
                fill.Fills[0] = {0x14100000U + offset, 0x14100020U + offset,
                                 0x11223344U, 0x0201U};
                bool deferred = false;
                Require(queue.SubmitMemoryFill(fill, &deferred, &error) && deferred, error);
                Require(sampleRefs(0x20100000U).empty(),
                        "a partial fill claimed GPU ownership of guest bytes");
            } else {
                // Colour writes are allowed globally, but this draw's mask is zero.
                packet.Registers[0x107] = 0U;
                Require(queue.SubmitDrawPacket(packet, &error), error);
                Require(sampleRefs(0x20100000U).empty(),
                        "a zero-colour-mask draw stole the guest's ownership");
            }
        } else {
            bool deferred = false;
            const uint32_t extent = (64U << 16) | 64U;
            Require(queue.SubmitDisplayTransfer(
                        {0x14100000U, 0x14120000U, extent, extent, 0x20U},
                        &deferred, &error) && deferred, error);
            // A flipped tiled output has no GPU producer here: the older
            // transfer must not keep claiming the bytes it overwrites.
            Require(queue.SubmitDisplayTransfer(
                        {0x14100000U, 0x14120000U, extent, extent, 0x21U},
                        &deferred, &error), error);
            Require(sampleRefs(0x20120000U).empty(),
                    "an unsupported tiled overwrite left the older producer owning its bytes");
        }
    }
}

void TestTiledDisplayAndRasterFeedback() {
    using namespace Oot3dNativeGame;
    NativeA32Memory memory;
    std::string error;
    Require(memory.MapRegion({"feedback", 0x14000000U, 0x200000U, true, false, {}}, &error), error);
    Oot3dNativePicaSubmissionQueue queue(
        Oot3dPicaPhysicalMemoryView(memory, {{0x20000000U, 0x14000000U, 0x200000U}}), true);
    queue.BindGuestWriteObservation([](uint32_t, size_t) { return true; });
    Oot3dPicaDrawPacket packet{};
    packet.Registers[0x200] = 0x20000000U / 8;
    packet.Registers[0x201] = 3U;
    packet.Registers[0x203] = 0x100U;
    packet.Registers[0x205] = 0x10040000U;
    packet.Registers[0x228] = 3U;
    packet.Registers[0x112] = 15U;
    // Colour writes allowed: these draws produce their target on the GPU.
    packet.Registers[0x113] = 15U;
    packet.Registers[0x107] = 0xF00U;
    const auto target = [&](uint32_t address, uint32_t w, uint32_t h) {
        packet.Registers[0x11D] = address / 8;
        packet.Registers[0x11E] = (1U << 24) | ((h - 1) << 12) | w;
    };
    const auto texture = [&](uint32_t address, uint32_t w, uint32_t h) {
        packet.Registers[0x080] = 1U;
        packet.Registers[0x082] = (w << 16) | h;
        packet.Registers[0x085] = address / 8;
        packet.Registers[0x08E] = 0U;
    };
    const auto refs = [&]() {
        for (const auto& r : queue.PendingDraws().back().Resources)
            if (r.Kind == Oot3dPicaResourceKind::Texture) return r.CopyReferences;
        return std::vector<Fast::Renderer3ds::PicaTextureCopyReference>{};
    };
    // The 512x400 source spans 0x20020000..0x200E8000; every surface below
    // lies outside it, so ownership is never shared with the source target.
    target(0x20020000U, 512, 400);
    Require(queue.SubmitDrawPacket(packet, &error), error);
    bool deferred = false;
    Require(queue.SubmitDisplayTransfer(
        {0x14020000U, 0x14100000U, (400U << 16) | 512, (400U << 16) | 512, 0x02000020U},
        &deferred, &error) && deferred, error);
    const auto transferId = queue.PendingDisplayTransfers().back().CompletionId;
    // Zero backing RAM is intentional: a valid GPU producer must win anyway.
    target(0x20140000U, 32, 64);
    texture(0x20100000U, 256, 256);
    Require(queue.SubmitDrawPacket(packet, &error), error);
    const auto history = refs();
    Require(history.size() == 1 && history[0].CompletionId == transferId &&
                history[0].PhysicalAddress == 0x20100000U && history[0].ByteCount == 204800,
            "tiled DisplayTransfer feedback sampled stale CPU bytes or claimed padding");
    const auto firstRasterWrite = queue.PendingDraws().back().Id;
    target(0x20148000U, 32, 64);
    texture(0x20140000U, 32, 64);
    Require(queue.SubmitDrawPacket(packet, &error), error);
    const auto ping = refs();
    Require(ping.size() == 1 && ping[0].PhysicalAddress == 0x20140000U &&
                ping[0].ByteCount == 8192 && ping[0].AfterDrawSubmissionId == firstRasterWrite,
            "color target ping-pong has no ordered GPU producer");
    // Self feedback reads the preceding version, then publishes its write.
    const auto secondRasterWrite = queue.PendingDraws().back().Id;
    texture(0x20148000U, 32, 64);
    Require(queue.SubmitDrawPacket(packet, &error), error);
    Require(refs().size() == 1 && refs()[0].AfterDrawSubmissionId == secondRasterWrite,
            "self feedback incorrectly references the consuming draw's future write");
    // A byte-identical CPU write must remove only its overlapping ownership.
    std::array<uint8_t, 32> zero{};
    Require(memory.WriteBytes(0x14100040U, zero), "same-value CPU write");
    queue.NotifyGuestWrite(0x14100040U, zero.size());
    texture(0x20100000U, 256, 256);
    Require(queue.SubmitDrawPacket(packet, &error), error);
    auto partial = refs();
    std::sort(partial.begin(), partial.end(), [](const auto& a, const auto& b) {
        return a.PhysicalAddress < b.PhysicalAddress;
    });
    Require(partial.size() == 2 && partial[0].ByteCount == 64 &&
                partial[1].PhysicalAddress == 0x20100060U && partial[1].ByteCount == 204704,
            "CPU write discarded too much/little transfer-result provenance");
    const auto saved = queue.CaptureState();
    Oot3dNativePicaSubmissionQueue restored(
        Oot3dPicaPhysicalMemoryView(memory, {{0x20000000U, 0x14000000U, 0x200000U}}), true);
    restored.BindGuestWriteObservation([](uint32_t, size_t) { return true; });
    Require(restored.RestoreState(saved, &error), error);
    restored.TakePendingDraws();
    Require(restored.SubmitDrawPacket(packet, &error), error);
    const auto& resources = restored.PendingDraws().back().Resources;
    const auto resource = std::find_if(resources.begin(), resources.end(), [](const auto& r) {
        return r.Kind == Oot3dPicaResourceKind::Texture;
    });
    Require(resource != resources.end() && resource->CopyReferences.size() == 2,
            "savestate lost tiled transfer-result ownership");
}

} // namespace

int main() {
    Oot3dNativeGame::NativeA32Memory memory;
    std::string error;
    Require(memory.MapRegion({"linear", 0x14000000U, 0x2000U, true, false, {}},
                             &error),
            error);
    constexpr std::array<uint8_t, 3> indices{2U, 5U, 3U};
    Require(memory.WriteBytes(0x14000080U, indices),
            "cannot write synthetic index buffer");
    std::array<uint8_t, 24> vertices{};
    for (size_t index = 0; index < vertices.size(); ++index) {
        vertices[index] = static_cast<uint8_t>(index + 1U);
    }
    Require(memory.WriteBytes(0x14000100U, vertices),
            "cannot write synthetic vertex buffer");
    std::array<uint8_t, 256> texture{};
    texture.front() = 0x12U;
    Require(memory.WriteBytes(0x14000A00U, texture),
            "cannot write synthetic texture payload");
    std::array<uint8_t, 1280> mipTexture{};
    std::fill(mipTexture.begin(), mipTexture.begin() + 1024U, 0x31U);
    std::fill(mipTexture.begin() + 1024U, mipTexture.end(), 0x72U);
    Require(memory.WriteBytes(0x14000B00U, mipTexture),
            "cannot write synthetic native mip chain");

    Oot3dNativeGame::Oot3dPicaPhysicalMemoryView physicalMemory(
        memory, {{0x20000000U, 0x14000000U, 0x2000U}});
    Oot3dNativeGame::Oot3dNativePicaSubmissionQueue queue(
        std::move(physicalMemory), true);
    std::uint32_t transformedTextures = 0U;
    queue.SetTexturePayloadTransform(
        [&](const Oot3dNativeGame::Oot3dPicaTextureState& state,
            std::span<std::uint8_t> payload) {
            Require(state.PhysicalAddress == 0x20000A00U &&
                        state.Width == 8U && state.Height == 8U &&
                        state.Format == 0U && payload.size() == texture.size(),
                    "texture transform did not receive decoded PICA identity");
            payload.front() = 0xA5U;
            ++transformedTextures;
        });
    Oot3dNativeGame::Oot3dPicaDrawPacket packet{};
    packet.Indexed = true;
    packet.Registers[0x200] = 0x04000000U;
    packet.Registers[0x201] = 0x00000003U;
    packet.Registers[0x202] = 0x00000000U;
    packet.Registers[0x203] = 0x00000100U;
    packet.Registers[0x205] = 0x10040000U;
    packet.Registers[0x227] = 0x00000080U;
    packet.Registers[0x228] = 3U;
    packet.Registers[0x11D] = 0x04000040U;
    packet.Registers[0x080] = 1U;
    packet.Registers[0x082] = (8U << 16U) | 8U;
    packet.Registers[0x085] = 0x04000140U;
    packet.Registers[0x08E] = 0U;
    Require(queue.SubmitDrawPacket(packet, &error), error);
    Require(queue.PendingDraws().size() == 1U,
            "draw submission was not queued");
    const auto& submission = queue.PendingDraws()[0];
    Require(
        submission.Id == 1U && submission.MinimumVertexIndex == 2U &&
            submission.MaximumVertexIndex == 5U && transformedTextures == 1U &&
            submission.Resources.size() == 3U &&
            submission.Resources[0].Kind ==
                Oot3dNativeGame::Oot3dPicaResourceKind::IndexBuffer &&
            std::equal(submission.Resources[0].ResolvedBytes().begin(),
                       submission.Resources[0].ResolvedBytes().end(),
                       indices.begin(), indices.end()) &&
            submission.Resources[0].ContentVersionAvailable &&
            submission.Resources[1].Kind ==
                Oot3dNativeGame::Oot3dPicaResourceKind::VertexLoader &&
            submission.Resources[1].PhysicalAddress == 0x20000108U &&
            submission.Resources[1].FirstElement == 2U &&
            submission.Resources[1].ResolvedBytes().size() == 16U &&
            submission.Resources[1].ResolvedBytes().front() == 9U &&
            submission.Resources[1].ResolvedBytes().back() == 24U &&
            submission.Resources[1].ContentVersionAvailable &&
            submission.Resources[2].Kind ==
                Oot3dNativeGame::Oot3dPicaResourceKind::Texture &&
            submission.Resources[2].PhysicalAddress == 0x20000A00U &&
            submission.Resources[2].ResolvedBytes().size() == texture.size() &&
            submission.Resources[2].ResolvedBytes().front() == 0xA5U &&
            submission.Resources[2].ContentHashAvailable,
        "draw resources were not snapshotted at native submission time");
    const auto taken = queue.TakePendingDraws();
    Require(taken.size() == 1U && queue.PendingDraws().empty(),
            "draw submission queue did not drain");

    Require(queue.SubmitInterruptAfterGpuWork(
                Oot3dNativeGame::Oot3dPicaInterruptId::P3d, &error),
            error);
    Require(queue.PendingCompletions().size() == 1U &&
                queue.PendingCompletions()[0].Id == 1U &&
                queue.PendingCompletions()[0].AfterDrawSubmissionId ==
                    taken[0].Id &&
                queue.PendingCompletions()[0].Interrupt ==
                    Oot3dNativeGame::Oot3dPicaInterruptId::P3d,
            "PICA completion was not ordered after the captured draw");
    const auto completions = queue.TakePendingCompletions();
    Require(completions.size() == 1U && queue.PendingCompletions().empty(),
            "PICA completion queue did not drain");

    bool deferredToGpu = false;
    Require(queue.SubmitDisplayTransfer(
                {0x14000200U, 0x14000300U, 0x00100010U, 0x00100010U, 0U},
                &deferredToGpu, &error),
            error);
    Require(deferredToGpu && queue.PendingDisplayTransfers().size() == 1U &&
                queue.PendingDisplayTransfers()[0].CompletionId == 2U &&
                queue.PendingDisplayTransfers()[0].InputPhysicalAddress ==
                    0x20000200U &&
                queue.PendingDisplayTransfers()[0].OutputPhysicalAddress ==
                    0x20000300U &&
                queue.PendingDisplayTransfers()[0].AfterDrawSubmissionId ==
                    taken[0].Id,
            "GPU-backed display transfer was not captured by physical address");
    const auto transfers = queue.TakePendingDisplayTransfers();
    Require(transfers.size() == 1U &&
                queue.PendingDisplayTransfers().empty(),
            "PICA display transfer queue did not drain");

    Require(queue.SubmitDisplayTransfer(
                {0x14000400U, 0x14000500U, 0x00100010U, 0x00100010U, 0U},
                &deferredToGpu, &error) &&
                !deferredToGpu,
            "non-render-target transfer must remain on the CTR CPU path");

    Oot3dNativeGame::Oot3dPicaMemoryFillCommand fillCommand;
    fillCommand.Fills[0] =
        {0x14000600U, 0x14000700U, 0xAABBCCDDU, 0x0201U};
    fillCommand.Fills[1] =
        {0x14000800U, 0x14000900U, 0x00112233U, 0x0101U};
    Require(queue.SubmitMemoryFill(fillCommand, &deferredToGpu, &error) &&
                deferredToGpu && queue.PendingMemoryFills().size() == 2U,
            "mapped dual PICA memory fill was not deferred to the GPU");
    const auto& fills = queue.PendingMemoryFills();
    Require(fills[0].BeforeDrawSubmissionId == 2U &&
                fills[0].StartPhysicalAddress == 0x20000600U &&
                fills[0].EndPhysicalAddress == 0x20000700U &&
                fills[0].CompletionId == 0U &&
                !fills[0].Interrupt.has_value() &&
                fills[1].CompletionId == 3U &&
                fills[1].Interrupt ==
                    Oot3dNativeGame::Oot3dPicaInterruptId::Psc0,
            "dual PICA memory fill ordering or PSC0 completion is wrong");
    const auto takenFills = queue.TakePendingMemoryFills();
    Require(takenFills.size() == 2U && queue.PendingMemoryFills().empty(),
            "PICA memory fill queue did not drain");

    Require(queue.SubmitDrawPacket(packet, &error), error);
    Require(queue.SubmitInterruptAfterGpuWork(
                Oot3dNativeGame::Oot3dPicaInterruptId::P3d, &error),
            error);
    Require(queue.SubmitDisplayTransfer(
                {0x14000200U, 0x14000300U, 0x00100010U, 0x00100010U, 0U},
                &deferredToGpu, &error) && deferredToGpu,
            error);
    Require(queue.SubmitMemoryFill(fillCommand, &deferredToGpu, &error) &&
                deferredToGpu,
            error);
    auto batch = queue.TakePendingBatch();
    Require(batch.Draws.size() == 1U && batch.Completions.size() == 1U &&
                batch.DisplayTransfers.size() == 1U &&
                batch.MemoryFills.size() == 2U && queue.IsQuiescent(),
            "atomic PICA submission batch did not drain every queue");

    queue.SetRuntimeProfilingEnabled(true);
    Require(queue.SubmitDrawPacket(packet, &error), error);
    const auto cachedIndices =
        queue.PendingDraws().front().Resources[0].SharedBytes;
    const auto cachedVertices =
        queue.PendingDraws().front().Resources[1].SharedBytes;
    const uint64_t cachedVertexVersion =
        queue.PendingDraws().front().Resources[1].ContentVersion;
    const auto cachedTexture =
        queue.PendingDraws().front().Resources.back().SharedBytes;
    const uint64_t cachedHash =
        queue.PendingDraws().front().Resources.back().ContentHash;
    Require(cachedTexture != nullptr && transformedTextures == 1U,
            "unchanged native texture did not reuse its immutable snapshot");
    queue.TakePendingDraws();

    constexpr std::array<uint8_t, 1> changedVertexByte{0x6DU};
    Require(memory.WriteBytes(0x14000109U, changedVertexByte),
            "cannot mutate synthetic vertex payload");
    constexpr std::array<uint8_t, 1> changedTextureByte{0x7EU};
    Require(memory.WriteBytes(0x14000A01U, changedTextureByte),
            "cannot mutate synthetic texture payload");
    Require(queue.SubmitDrawPacket(packet, &error), error);
    const auto& changedResource =
        queue.PendingDraws().front().Resources.back();
    const auto &changedVertexResource =
        queue.PendingDraws().front().Resources[1];
    const uint64_t changedIndexVersion =
        queue.PendingDraws().front().Resources[0].ContentVersion;
    const uint64_t changedVertexVersion =
        changedVertexResource.ContentVersion;
    const uint64_t changedTextureHash = changedResource.ContentHash;
    Require(queue.PendingDraws().front().Resources[0].SharedBytes ==
                    cachedIndices &&
                changedVertexResource.SharedBytes != cachedVertices &&
                changedVertexResource.ResolvedBytes()[1] == 0x6DU &&
                changedVertexResource.ContentVersion != cachedVertexVersion &&
                changedResource.SharedBytes != cachedTexture &&
                changedResource.ResolvedBytes()[1] == 0x7EU &&
                changedResource.ContentHash != cachedHash &&
                transformedTextures == 2U,
            "native geometry or texture snapshot versioning is incorrect");
    queue.TakePendingDraws();

    const auto savedSubmissionState = queue.CaptureState();
    Require(savedSubmissionState.at("format") ==
                "oot3d_native_pica_submission_state_v4" &&
                !savedSubmissionState.at("geometry_snapshot_cache").empty() &&
                !savedSubmissionState.at("texture_snapshot_cache").empty(),
            "portable PICA snapshot caches were not captured");
    Oot3dNativeGame::Oot3dPicaPhysicalMemoryView snapshottedMemory(
        memory, {{0x20000000U, 0x14000000U, 0x2000U}});
    Oot3dNativeGame::Oot3dNativePicaSubmissionQueue snapshottedQueue(
        std::move(snapshottedMemory), true);
    Require(snapshottedQueue.RestoreState(savedSubmissionState, &error),
            error);
    constexpr std::array<uint32_t, 1> restoredGpuTargets{0x20000200U};
    snapshottedQueue.RestoreGpuColorRenderTargetOwnership(
        restoredGpuTargets);
    deferredToGpu = false;
    bool snapshottedCpuCopySuppressed = true;
    Require(snapshottedQueue.SubmitDisplayTransfer(
                {0x14000200U, 0x14000300U, 0x00100010U,
                 0x00100010U, 0U},
                &deferredToGpu, &error,
                &snapshottedCpuCopySuppressed) &&
                deferredToGpu && !snapshottedCpuCopySuppressed &&
                snapshottedQueue.PendingDisplayTransfers().size() == 1U,
            "restored GPU snapshot did not reacquire transfer ownership");

    Oot3dNativeGame::Oot3dPicaPhysicalMemoryView restoredMemory(
        memory, {{0x20000000U, 0x14000000U, 0x2000U}});
    Oot3dNativeGame::Oot3dNativePicaSubmissionQueue restoredQueue(
        std::move(restoredMemory), true);
    restoredQueue.SetTexturePayloadTransform(
        [](const Oot3dNativeGame::Oot3dPicaTextureState&,
           std::span<std::uint8_t> payload) { payload.front() = 0xA5U; });
    Require(restoredQueue.RestoreState(savedSubmissionState, &error), error);
    deferredToGpu = true;
    bool cpuCopySuppressed = false;
    Require(restoredQueue.SubmitDisplayTransfer(
                {0x14000200U, 0x14000300U, 0x00100010U, 0x00100010U, 0U},
                &deferredToGpu, &error, &cpuCopySuppressed) &&
                !deferredToGpu &&
                cpuCopySuppressed &&
                restoredQueue.PendingDisplayTransfers().empty(),
            "restored host-only render target was treated as live GPU state");
    Require(restoredQueue.SubmitDrawPacket(packet, &error), error);
    const auto& restoredDraw = restoredQueue.PendingDraws().front();
    Require(restoredDraw.Resources[0].ContentVersion ==
                    changedIndexVersion &&
                restoredDraw.Resources[1].ContentVersion ==
                    changedVertexVersion &&
                restoredDraw.Resources.back().ContentHash ==
                    changedTextureHash &&
                restoredQueue.PendingDisplayTransfers().size() == 1U &&
                !restoredQueue.PendingDisplayTransfers()[0].SignalInterrupt,
            "restored cache identities or display transfer replay diverged");
    restoredQueue.TakePendingDraws();
    restoredQueue.TakePendingDisplayTransfers();
    cpuCopySuppressed = true;
    Require(restoredQueue.SubmitDisplayTransfer(
                {0x14000200U, 0x14000300U, 0x00100010U, 0x00100010U, 0U},
                &deferredToGpu, &error, &cpuCopySuppressed) &&
                deferredToGpu &&
                !cpuCopySuppressed &&
                restoredQueue.PendingDisplayTransfers().size() == 1U &&
                restoredQueue.PendingDisplayTransfers()[0].SignalInterrupt,
            "post-restore draw did not re-establish GPU transfer ownership");

    Oot3dNativeGame::Oot3dPicaPhysicalMemoryView mipMemory(
        memory, {{0x20000000U, 0x14000000U, 0x2000U}});
    Oot3dNativeGame::Oot3dNativePicaSubmissionQueue mipQueue(
        std::move(mipMemory), true);
    auto mipPacket = packet;
    mipPacket.Registers[0x082] = (16U << 16U) | 16U;
    mipPacket.Registers[0x083] =
        (1U << 1U) | (1U << 2U) | (1U << 24U);
    mipPacket.Registers[0x084] =
        0x1F00U | (1U << 16U) | (1U << 24U);
    mipPacket.Registers[0x085] = 0x04000160U;
    Require(mipQueue.SubmitDrawPacket(mipPacket, &error), error);
    const auto& mipDraw = mipQueue.PendingDraws().front();
    const auto& mipState = mipDraw.State.Textures[0];
    const auto& mipResource = mipDraw.Resources.back();
    Require(mipState.MipLinear && mipState.LodBiasRaw == -256 &&
                mipState.MinMipLevel == 1U &&
                mipState.MaxMipLevel == 1U &&
                Oot3dNativeGame::Oot3dPicaTextureMipLevelCount(mipState) ==
                    2U &&
                mipResource.ResolvedBytes().size() == mipTexture.size() &&
                mipResource.ResolvedBytes()[1023] == 0x31U &&
                mipResource.ResolvedBytes()[1024] == 0x72U,
            "native PICA mip chain was not captured as one texture resource");

    const auto hashBytes = [](std::span<const uint8_t> bytes) {
        uint64_t hash = 1469598103934665603ULL;
        for (const auto byte : bytes) hash = (hash ^ byte) * 1099511628211ULL;
        return hash == 0U ? 1U : hash;
    };
    const uint64_t baseHash = hashBytes(mipResource.ResolvedBytes().first(1024U));
    Require(mipResource.BaseLevelContentHashAvailable &&
                mipResource.BaseLevelContentHash == baseHash &&
                mipResource.ContentHash != baseHash,
            "snapshot did not version the base mip independently");
    const auto payload = mipResource.SharedBytes;
    mipQueue.TakePendingDraws();
    Require(mipQueue.SubmitDrawPacket(mipPacket, &error), error);
    Require(mipQueue.PendingDraws().front().Resources.back().SharedBytes == payload &&
                mipQueue.PendingDraws().front().Resources.back().BaseLevelContentHash == baseHash,
            "unchanged mip payload lost its cached base identity");
    mipQueue.TakePendingDraws();
    const auto mipSave = mipQueue.CaptureState();
    Require(mipQueue.RestoreState(mipSave, &error), error);
    Require(mipQueue.SubmitDrawPacket(mipPacket, &error), error);
    Require(mipQueue.PendingDraws().front().Resources.back().BaseLevelContentHash == baseHash,
            "savestate restore did not rebuild the derived mip identity");
    mipQueue.TakePendingDraws();
    const std::array<uint8_t, 1> changedBase{0x27U};
    Require(memory.WriteBytes(0x14000B00U, changedBase), "cannot change base mip");
    Require(mipQueue.SubmitDrawPacket(mipPacket, &error), error);
    Require(mipQueue.PendingDraws().front().Resources.back().BaseLevelContentHash != baseHash,
            "changed mip payload retained a stale base identity");

    Oot3dNativeGame::Oot3dNativePicaSubmissionQueue copyQueue(
        Oot3dNativeGame::Oot3dPicaPhysicalMemoryView(memory, {{0x20000000U, 0x14000000U, 0x2000U}}), true);
    copyQueue.BindGuestWriteObservation(
        [](uint32_t, size_t) { return true; });
    Require(copyQueue.SubmitDrawPacket(packet, &error), error);
    Oot3dNativeGame::Oot3dNativePicaFrontend copyFrontend(&copyQueue);
    const Oot3dNativeGame::Oot3dGspCommandPacket rawCopy{
        4U, {0x14000200U, 0x14000500U, 64U, 0U, 0x00010002U, 12U, 0U}};
    Require(copyFrontend.SubmitGspCommand(rawCopy, {}, &error, &deferredToGpu) &&
                deferredToGpu,
            error);
    const auto rawTransfers = copyQueue.TakePendingDisplayTransfers();
    Require(rawTransfers.size() == 1 && rawTransfers[0].Transfer.TextureCopyBytes == 64U &&
                rawTransfers[0].Transfer.InputSize == 0U && rawTransfers[0].Transfer.OutputSize == 0x00010002U &&
                rawTransfers[0].AfterDrawSubmissionId == copyQueue.PendingDraws().front().Id,
            "raw copy lost its operation or command ordering");
    // --- raw copy provenance: explicit producers through snapshot and restore ---
    const auto textureReferences =
        [](const Oot3dNativeGame::Oot3dPicaDrawSubmission& draw) {
            return draw.Resources.back().CopyReferences;
        };
    Oot3dNativeGame::Oot3dNativePicaSubmissionQueue provenanceQueue(
        Oot3dNativeGame::Oot3dPicaPhysicalMemoryView(
            memory, {{0x20000000U, 0x14000000U, 0x2000U}}),
        true);
    bool observationAvailable = true;
    provenanceQueue.BindGuestWriteObservation(
        [&](uint32_t, size_t) {
            return observationAvailable;
        });

    Require(provenanceQueue.SubmitDrawPacket(packet, &error), error);
    Require(textureReferences(provenanceQueue.PendingDraws().front()).empty(),
            "a draw before the raw copy claimed GPU copy provenance");
    provenanceQueue.TakePendingDraws();

    deferredToGpu = false;
    Require(provenanceQueue.SubmitDisplayTransfer(
                {0x14000200U, 0x14000A00U, 0U, 0x00010002U, 12U, 64U},
                &deferredToGpu, &error) &&
                deferredToGpu,
            error);

    Require(provenanceQueue.SubmitDrawPacket(packet, &error), error);
    {
        const auto references =
            textureReferences(provenanceQueue.PendingDraws().front());
        Require(references.size() == 2U && references[0].CompletionId == 1U &&
                    references[0].AfterDrawSubmissionId == 1U &&
                    references[0].PhysicalAddress == 0x20000A00U &&
                    references[0].ByteCount == 32U &&
                    references[1].CompletionId == 1U &&
                    references[1].PhysicalAddress == 0x20000A30U &&
                    references[1].ByteCount == 32U,
                "a draw after the raw copy did not reference its exact "
                "destination subranges");
    }
    provenanceQueue.TakePendingDraws();

    // A byte-identical CPU write still owns its bytes: ownership is a write
    // signal, never a byte comparison.
    std::array<uint8_t, 16> sameValue{};
    Require(memory.ReadBytes(0x14000A00U, sameValue),
            "cannot read the raw copy destination");
    Require(memory.WriteBytes(0x14000A00U, sameValue),
            "cannot rewrite the raw copy destination");
    provenanceQueue.NotifyGuestWrite(0x14000A00U, sameValue.size());
    Require(provenanceQueue.SubmitDrawPacket(packet, &error), error);
    {
        const auto references =
            textureReferences(provenanceQueue.PendingDraws().front());
        Require(references.size() == 2U &&
                    references[0].PhysicalAddress == 0x20000A10U &&
                    references[0].ByteCount == 16U &&
                    references[1].PhysicalAddress == 0x20000A30U &&
                    references[1].ByteCount == 32U,
                "a same-value CPU write did not invalidate exactly its own bytes");
    }
    provenanceQueue.TakePendingDraws();

    deferredToGpu = false;
    Require(provenanceQueue.SubmitDisplayTransfer(
                {0x14000200U, 0x14000A00U, 0U, 0x00000002U, 12U, 32U},
                &deferredToGpu, &error) &&
                deferredToGpu,
            error);
    Require(provenanceQueue.SubmitDrawPacket(packet, &error), error);
    {
        const auto references =
            textureReferences(provenanceQueue.PendingDraws().front());
        Require(references.size() == 2U && references[0].CompletionId == 2U &&
                    references[0].PhysicalAddress == 0x20000A00U &&
                    references[0].ByteCount == 32U &&
                    references[1].CompletionId == 1U &&
                    references[1].PhysicalAddress == 0x20000A30U &&
                    references[1].ByteCount == 32U,
                "a newer raw copy did not take only its own bytes from the "
                "older producer");
    }
    provenanceQueue.TakePendingDraws();

    std::vector<Fast::Renderer3ds::PicaTextureCopyReference> liveReferences;
    provenanceQueue.CollectLiveCopyReferences(liveReferences);
    Require(liveReferences.size() == 2U &&
                liveReferences[0].CompletionId == 2U &&
                liveReferences[1].CompletionId == 1U,
            "live copy ownership did not follow the newest producers");
    const auto provenanceState = provenanceQueue.CaptureState();
    Require(provenanceState.at("format") ==
                    "oot3d_native_pica_submission_state_v4" &&
                provenanceState.at("raw_copy_producers").size() == 2U,
            "raw copy ownership was not persisted with the submission state");

    Oot3dNativeGame::Oot3dNativePicaSubmissionQueue unobservedProvenanceQueue(
        Oot3dNativeGame::Oot3dPicaPhysicalMemoryView(
            memory, {{0x20000000U, 0x14000000U, 0x2000U}}),
        true);
    Require(!unobservedProvenanceQueue.RestoreState(provenanceState, &error) &&
                !error.empty(),
            "restore accepted copy ownership without write observation");
    unobservedProvenanceQueue.BindGuestWriteObservation(
        [](uint32_t, size_t) { return true; });
    Require(unobservedProvenanceQueue.RestoreState(provenanceState, &error),
            error);
    Require(unobservedProvenanceQueue.SubmitDrawPacket(packet, &error), error);
    {
        const auto references = textureReferences(
            unobservedProvenanceQueue.PendingDraws().front());
        Require(references.size() == 2U && references[0].CompletionId == 2U &&
                    references[1].CompletionId == 1U,
                "restore lost the copy provenance of the sampled texture");
    }
    unobservedProvenanceQueue.TakePendingDraws();

    observationAvailable = false;
    deferredToGpu = false;
    Require(!provenanceQueue.SubmitDisplayTransfer(
                {0x14000200U, 0x14000A00U, 0U, 0x00000002U, 12U, 32U},
                &deferredToGpu, &error) &&
                !error.empty(),
            "a raw copy destination that cannot be observed was accepted");
    observationAvailable = true;

    Oot3dNativeGame::Oot3dNativePicaSubmissionQueue unbindableQueue(
        Oot3dNativeGame::Oot3dPicaPhysicalMemoryView(
            memory, {{0x20000000U, 0x14000000U, 0x2000U}}),
        true);
    Require(unbindableQueue.SubmitDrawPacket(packet, &error), error);
    deferredToGpu = false;
    Require(!unbindableQueue.SubmitDisplayTransfer(
                {0x14000200U, 0x14000A00U, 0U, 0x00000002U, 12U, 32U},
                &deferredToGpu, &error) &&
                !error.empty(),
            "a GPU-backed raw copy was accepted without write observation");


    // Cache identity follows observed physical bytes, not unrelated guest
    // writes. Escaped pointers and device materialization must never go stale.
    {
        using namespace Oot3dNativeGame;
        NativeA32Memory observed(memory);
        Require(observed.MapRegion({"alias", 0x15000000U, 0x2000U, true, false, {}}, &error), error);
        Oot3dPicaPhysicalMemoryView view(observed, {
            {0x20000000U, 0x14000000U, 0x2000U},
            {0x20000000U, 0x15000000U, 0x2000U}});
        Oot3dNativePicaSubmissionQueue cached(view, true);
        const auto observer = [&](uint32_t address, size_t size) {
            Require(view.SynchronizeGuestWrite(observed, address, size), "snapshot alias visibility");
            cached.NotifyGuestWrite(address, size);
        };
        const auto registrar = [&](uint32_t address, size_t size) {
            return observed.ObserveWrites(address, size);
        };
        observed.SetWriteObserver(observer);
        cached.BindGuestWriteObservation(registrar);
        Require(cached.SubmitDrawPacket(packet, &error), error);
        const auto original = cached.TakePendingDraws().front();
        cached.SetRuntimeProfilingEnabled(true);
        Require(observed.WriteFast<uint32_t>(0x14001100U, 0x11223344U), "unrelated page write");
        Require(cached.SubmitDrawPacket(packet, &error), error);
        const auto unchanged = cached.TakePendingDraws().front();
        Require(unchanged.Resources[1].SharedBytes == original.Resources[1].SharedBytes &&
                    unchanged.Resources.back().SharedBytes == original.Resources.back().SharedBytes &&
                    cached.RuntimeProfile().GeometrySnapshotComparedBytes == 0U &&
                    cached.RuntimeProfile().TextureSnapshotComparedBytes == 0U,
                "unrelated writes must not rescan immutable resource bytes");

        observed.SetWriteObserver({});
        Require(observed.WriteFast<uint8_t>(0x14000A06U, 0x28U), "write while observation is absent");
        observed.SetWriteObserver(observer);
        cached.BindGuestWriteObservation(registrar);
        Require(cached.SubmitDrawPacket(packet, &error), error);
        const auto rebound = cached.TakePendingDraws().front();
        Require(rebound.Resources.back().ResolvedBytes()[6] == 0x28U &&
                    original.Resources.back().ResolvedBytes()[6] != 0x28U,
                "rebinding observation must not reuse stale source tokens");

        Require(observed.WriteFast<uint8_t>(0x15000109U, 0x4DU), "alias vertex write");
        Require(cached.SubmitDrawPacket(packet, &error), error);
        const auto aliased = cached.TakePendingDraws().front();
        Require(aliased.Resources[1].ResolvedBytes()[1] == 0x4DU &&
                    aliased.Resources[1].ContentVersion != original.Resources[1].ContentVersion &&
                    original.Resources[1].ResolvedBytes()[1] == 0x6DU,
                "alias writes must update the next draw without mutating retained snapshots");
        {
            NativeA32Memory::RangeWatchSuppression materialization(observed);
            Require(observed.WriteFast<uint8_t>(0x14000A05U, 0x3FU), "device texture writeback");
        }
        Require(cached.SubmitDrawPacket(packet, &error), error);
        Require(cached.TakePendingDraws().front().Resources.back().ResolvedBytes()[5] == 0x3FU,
                "device writeback must invalidate CPU snapshot content");

        auto* escaped = observed.GetWritePointer(0x14000109U, 1U);
        Require(escaped != nullptr, "writable resource pointer");
        *escaped = 0x52U;
        Require(cached.SubmitDrawPacket(packet, &error), error);
        Require(cached.TakePendingDraws().front().Resources[1].ResolvedBytes()[1] == 0x52U,
                "first escaped-pointer edit was lost");
        *escaped = 0x61U;
        Require(cached.SubmitDrawPacket(packet, &error), error);
        Require(cached.TakePendingDraws().front().Resources[1].ResolvedBytes()[1] == 0x61U,
                "repeated pointer writes require content comparison, not an observed token");
        const auto saved = cached.CaptureState();
        *escaped = 0x72U;
        Require(cached.RestoreState(saved, &error), error);
        Require(cached.SubmitDrawPacket(packet, &error), error);
        Require(cached.TakePendingDraws().front().Resources[1].ResolvedBytes()[1] == 0x72U,
                "portable metadata must not certify current guest bytes after restore");
        observed.SetWriteObserver({});
    }
    // Real guarded memory, two virtual aliases, and an actual consumer
    // snapshot: submission must not force CPU visibility for GPU sampling.
    {
        using namespace Oot3dNativeGame;
        NativeA32Memory guarded(memory);
        Require(guarded.MapRegion({"alias", 0x15000000U, 0x2000U, true, false, {}}, &error), error);
        Oot3dPicaPhysicalMemoryView view(guarded, {
            {0x20000000U, 0x14000000U, 0x2000U},
            {0x20000000U, 0x15000000U, 0x2000U}});
        Oot3dNativePicaSubmissionQueue deviceQueue(view, true);
        guarded.SetWriteObserver([&](uint32_t address, size_t size) {
            Require(view.SynchronizeGuestWrite(guarded, address, size), "alias write visibility");
            deviceQueue.NotifyGuestWrite(address, size);
        });
        deviceQueue.BindGuestWriteObservation([&](uint32_t address, size_t size) {
            return guarded.ObserveWrites(address, size);
        });
        Require(deviceQueue.SubmitDrawPacket(packet, &error), error);
        deviceQueue.TakePendingDraws();
        Require(deviceQueue.SubmitDisplayTransfer(
                    {0x14000200U, 0x14000A00U, 0U, 0U, 12U, 64U},
                    &deferredToGpu, &error) && deferredToGpu, error);
        const std::array ranges{
            std::pair<uint32_t, size_t>{0x14000A00U, 64U},
            std::pair<uint32_t, size_t>{0x15000A00U, 64U}};
        size_t fetched = 0;
        Require(guarded.DeferWrites(ranges, [&](uint32_t, std::span<uint8_t> bytes) {
            fetched += bytes.size();
            std::fill(bytes.begin(), bytes.end(), 0x6DU);
            return true;
        }), "defer producer through both aliases");
        Require(deviceQueue.SubmitDrawPacket(packet, &error) && fetched == 0, error);
        const auto& resource = deviceQueue.PendingDraws().front().Resources.back();
        Require(!resource.ContentHashAvailable && resource.CopyReferences.size() == 1U &&
                    resource.CopyReferences.front().ByteCount == 64U,
                "GPU sampler must not identify an incomplete CPU payload as native content");
        Require(guarded.WriteFast<uint32_t>(0x15000A04U, 0U) && fetched == 0,
                "same-value alias overwrite must discard only its own device bytes");
        std::array<uint8_t, 64> primary{}, alias{};
        Require(guarded.ReadBytes(0x14000A00U, primary) &&
                    guarded.ReadBytes(0x15000A00U, alias) && primary == alias,
                "both aliases must observe the same partial CPU overwrite");
        std::array<uint8_t, 64> expected;
        expected.fill(0x6D);
        std::fill(expected.begin() + 4, expected.begin() + 8, 0U);
        Require(primary == expected && fetched == 120U,
                "alias materialization must retain untouched producer bytes only");
        // A CPU/device-copy consumer really needs bytes and must serialize the
        // producer before forwarding them; no stale-RAM shortcut for chains.
        Require(guarded.WriteBytes(0x14000B00U, primary), "dependent copy");
        std::array<uint8_t, 64> chained{};
        Require(view.Read(0x20000B00U, chained) && chained == expected,
                "a dependent byte copy must observe the same coherent result");
        guarded.SetWriteObserver({});
    }

    TestTiledDisplayAndRasterFeedback();
    TestColorTargetCaptureKeepsNewerProducers();
    TestFeedbackOwnershipReviewRegressions();
    std::cout << "oot3d_native_pica_submission_tests: ok\n";
    return 0;
}
