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
    const auto initialVertexGeneration =
        physicalMemory.RangeWriteGeneration(0x20000108U, 16U);
    constexpr std::array<uint8_t, 1> distantPageWrite{0x44U};
    Require(initialVertexGeneration.has_value() &&
                memory.WriteBytes(0x14001100U, distantPageWrite) &&
                physicalMemory.RangeWriteGeneration(0x20000108U, 16U) ==
                    memory.WriteGeneration() &&
                physicalMemory.RangeWriteGeneration(0x20000108U, 16U) !=
                    initialVertexGeneration,
            "conservative geometry generation did not observe guest write");
    constexpr std::array<uint8_t, 1> samePageWrite{0x55U};
    Require(memory.WriteBytes(0x14000000U, samePageWrite) &&
                physicalMemory.RangeWriteGeneration(0x20000108U, 16U) !=
                    initialVertexGeneration,
            "geometry page write did not advance its generation");
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
    const auto profile = queue.RuntimeProfile();
    Require(queue.PendingDraws().front().Resources[0].SharedBytes ==
                    cachedIndices &&
                changedVertexResource.SharedBytes != cachedVertices &&
                changedVertexResource.ResolvedBytes()[1] == 0x6DU &&
                changedVertexResource.ContentVersion != cachedVertexVersion &&
                changedResource.SharedBytes != cachedTexture &&
                changedResource.ResolvedBytes()[1] == 0x7EU &&
                changedResource.ContentHash != cachedHash &&
                transformedTextures == 2U &&
                profile.TextureSnapshotCacheHits == 1U &&
                profile.TextureSnapshotCacheMisses == 1U &&
                profile.TextureSnapshotComparedBytes == texture.size() * 2U &&
                profile.TextureSnapshotCopiedBytes == texture.size() &&
                profile.GeometrySnapshotCacheHits == 3U &&
                profile.GeometrySnapshotCacheMisses == 1U &&
                profile.GeometrySnapshotVersionHits == 2U &&
                profile.GeometrySnapshotVersionMisses == 2U &&
                profile.GeometrySnapshotComparedBytes == indices.size() + 16U &&
                profile.GeometrySnapshotCopiedBytes == 16U,
            "native geometry or texture snapshot versioning is incorrect");
    queue.TakePendingDraws();

    const auto savedSubmissionState = queue.CaptureState();
    Require(savedSubmissionState.at("format") ==
                "oot3d_native_pica_submission_state_v3" &&
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

    std::cout << "oot3d_native_pica_submission_tests: ok\n";
    return 0;
}
