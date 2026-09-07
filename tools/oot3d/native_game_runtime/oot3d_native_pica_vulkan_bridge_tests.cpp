#include "oot3d_native_pica_vulkan_bridge.h"
#include "oot3d_native_pica_presentation_scheduler.h"

#include "fast/oot3d/pica_uniform_layout.h"

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_pica_vulkan_bridge_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

class RecordingBackend final : public Oot3d::Renderer::PicaRenderBackend {
  public:
    bool PublishPicaCompositionSequence(
        const Oot3d::Renderer::PicaCompositionSequenceView& sequence,
        std::string*) override {
        Operations.push_back(
            "composition:" + std::to_string(sequence.SequenceId) + ":" +
            std::to_string(sequence.Draws.size()));
        CompositionSequenceId = sequence.SequenceId;
        CompositionDraws.assign(sequence.Draws.begin(),
                                sequence.Draws.end());
        return true;
    }

    bool SubmitPicaDraw(
        const Oot3d::Renderer::PicaDrawView& draw,
        std::string*) override {
        Operations.push_back("draw:" + std::to_string(draw.SubmissionId));
        DrawCalled = true;
        DrawSubmissionId = draw.SubmissionId;
        DrawCommandListAddress = draw.CommandListAddress;
        DrawCompositionDomain = draw.CompositionDomain;
        DrawComposition = draw.Composition;
        DrawTopology = draw.Topology;
        DrawVertexCount = draw.VertexCount;
        DrawWBuffering = draw.WBuffering;
        DrawFragmentFeatures = draw.FragmentFeatures;
        DrawFragmentUniformBytes.assign(draw.FragmentUniformBytes.begin(),
                                        draw.FragmentUniformBytes.end());
        DrawVertexBytes.assign(draw.VertexBindings[0].Bytes.begin(),
                               draw.VertexBindings[0].Bytes.end());
        DrawTextureBytes.assign(draw.Textures[0].NativeBytes.begin(),
                                draw.Textures[0].NativeBytes.end());
        DrawTextureMipLinear = draw.Textures[0].MipLinear;
        DrawTextureLodBiasRaw = draw.Textures[0].LodBiasRaw;
        DrawTextureMinMip = draw.Textures[0].MinMipLevel;
        DrawTextureMaxMip = draw.Textures[0].MaxMipLevel;
        DrawTextureContentHash = draw.Textures[0].NativeContentHash;
        DrawTextureBaseLevelContentHash =
            draw.Textures[0].NativeBaseLevelContentHash;
        DrawTextureBaseLevelContentHashAvailable =
            draw.Textures[0].NativeBaseLevelContentHashAvailable;
        return true;
    }

    bool SubmitPicaDisplayTransfer(
        const Oot3d::Renderer::PicaDisplayTransferView& transfer,
        std::string*) override {
        Operations.push_back(
            std::string(transfer.Present ? "present:" : "transfer:") +
            std::to_string(transfer.CompletionId));
        TransferCalled = true;
        Transfer = transfer;
        return true;
    }

    bool ClearPicaRenderTarget(
        uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress,
        std::string*) override {
        ClearCalled = true;
        ClearNamespace = renderTargetNamespace;
        ClearAddress = colorPhysicalAddress;
        return true;
    }

    bool SubmitPicaMemoryFill(
        const Oot3d::Renderer::PicaMemoryFillView& fill,
        std::string*) override {
        Operations.push_back("fill:" +
                             std::to_string(fill.StartPhysicalAddress));
        FillCalled = true;
        Fill = fill;
        return true;
    }

    bool DrawCalled = false;
    uint64_t DrawSubmissionId = 0;
    uint32_t DrawCommandListAddress = 0;
    Oot3d::Renderer::PicaCompositionDomain DrawCompositionDomain =
        Oot3d::Renderer::PicaCompositionDomain::Unknown;
    Oot3d::Renderer::PicaCompositionAttribution DrawComposition;
    Oot3d::Renderer::PicaTopology DrawTopology =
        Oot3d::Renderer::PicaTopology::TriangleList;
    uint32_t DrawVertexCount = 0;
    bool DrawWBuffering = false;
    Oot3d::Renderer::PicaFragmentFeatureView DrawFragmentFeatures;
    std::vector<uint8_t> DrawFragmentUniformBytes;
    std::vector<uint8_t> DrawVertexBytes;
    std::vector<uint8_t> DrawTextureBytes;
    bool DrawTextureMipLinear = false;
    int16_t DrawTextureLodBiasRaw = 0;
    uint8_t DrawTextureMinMip = 0;
    uint8_t DrawTextureMaxMip = 0;
    uint64_t DrawTextureContentHash = 0;
    uint64_t DrawTextureBaseLevelContentHash = 0;
    bool DrawTextureBaseLevelContentHashAvailable = false;
    bool TransferCalled = false;
    Oot3d::Renderer::PicaDisplayTransferView Transfer;
    bool ClearCalled = false;
    uint64_t ClearNamespace = 0;
    uint32_t ClearAddress = 0;
    bool FillCalled = false;
    Oot3d::Renderer::PicaMemoryFillView Fill;
    std::vector<std::string> Operations;
    uint64_t CompositionSequenceId = 0U;
    std::vector<Oot3d::Renderer::PicaCompositionDrawReference>
        CompositionDraws;
};

} // namespace

int main() {
    RecordingBackend backend;
    Oot3dNativeGame::Oot3dPicaVulkanDrawPlan plan;
    plan.SubmissionId = 17;
    plan.CommandListAddress = 0x14003000U;
    plan.CompositionDomain =
        Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene;
    plan.Composition = {
        Oot3dNativeGame::Oot3dPicaCompositionLayer::TransparentWorld,
        Oot3dNativeGame::Oot3dPicaCompositionProvenance::NativeCmbDrawPass,
        0x0030F4D0U,
        1U,
    };
    plan.VertexCount = 3;
    plan.FragmentFeatures.FogEnabled = true;
    plan.FragmentFeatures.FogFlip = true;
    plan.FragmentFeatures.FogMode = 5U;
    plan.FragmentFeatures.FragmentLightingEnabled = true;
    plan.FragmentFeatures.FragmentLighting.SchemaVersion =
        Oot3d::Renderer::kPicaFragmentLightingLayoutSchemaVersion;
    plan.FragmentFeatures.FragmentLighting.ActiveLightCount = 2U;
    plan.FragmentFeatures.FragmentLighting.LightPermutation[0] = 3U;
    plan.FragmentFeatures.FragmentLighting.LightPermutation[1] = 5U;
    plan.FragmentFeatures.FragmentLighting.Lights[3].Directional = true;
    plan.FragmentFeatures.FragmentLighting.Lights[5]
        .DistanceAttenuationEnabled = true;
    plan.FragmentShader.Uniforms.Lighting.Diffuse[3][2] = 0.75F;
    plan.FragmentShader.Uniforms.Lighting.GlobalAmbient[1] = 0.25F;
    plan.FragmentShader.Uniforms.ShadowTextureBias = 0x123456;
    plan.FragmentShader.Uniforms.ShadowOrthographic = 1;
    plan.FragmentShader.Uniforms.ShadowBiasConstant = 0.75F;
    plan.FragmentShader.Uniforms.ShadowBiasLinear = 0.125F;
    plan.State.Topology =
        Oot3dNativeGame::Oot3dPicaPrimitiveTopology::TriangleStrip;
    plan.State.Framebuffer.ColorPhysicalAddress = 0x18000000U;
    plan.State.Framebuffer.DepthPhysicalAddress = 0x18080000U;
    plan.State.Framebuffer.Width = 400U;
    plan.State.Framebuffer.Height = 240U;
    plan.State.Framebuffer.ColorFormat = 0U;
    plan.State.Framebuffer.DepthFormat = 2U;
    plan.VertexBindings.push_back(
        {2, 12, Oot3dNativeGame::Oot3dPicaVertexInputRate::PerVertex,
         {1, 2, 3, 4}});
    plan.VertexAttributes.push_back(
        {0, 2, Oot3dNativeGame::Oot3dPicaVertexFormat::Float, 3, 0});
    Oot3dNativeGame::Oot3dPicaVulkanTextureBinding texture;
    texture.Slot = 1;
    texture.State.Width = 2;
    texture.State.Height = 2;
    texture.State.MipLinear = true;
    texture.State.LodBiasRaw = -384;
    texture.State.MinMipLevel = 1U;
    texture.State.MaxMipLevel = 3U;
    texture.NativeBytes = {5, 6, 7, 8};
    texture.NativeContentHash = 101U;
    texture.NativeContentHashAvailable = true;
    texture.NativeBaseLevelContentHash = 103U;
    texture.NativeBaseLevelContentHashAvailable = true;
    plan.Textures.push_back(std::move(texture));

    std::string error;
    Require(Oot3dNativeGame::SubmitOot3dPicaVulkanDrawPlan(
                backend, plan, &error, 9),
            error);
    float packedDiffuse = 0.0F;
    float packedGlobalAmbient = 0.0F;
    int32_t packedShadowTextureBias = 0;
    int32_t packedShadowOrthographic = 0;
    float packedShadowBiasConstant = 0.0F;
    float packedShadowBiasLinear = 0.0F;
    Require(backend.DrawFragmentUniformBytes.size() ==
                Fast::Oot3d::kPicaPackedFragmentUniformSize,
            "native fragment uniform block size is incorrect");
    std::memcpy(&packedDiffuse,
                backend.DrawFragmentUniformBytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentLightingDiffuseOffset +
                    (3U * 4U + 2U) * sizeof(float),
                sizeof(packedDiffuse));
    std::memcpy(&packedGlobalAmbient,
                backend.DrawFragmentUniformBytes.data() +
                    Fast::Oot3d::
                        kPicaPackedFragmentLightingGlobalAmbientOffset +
                    sizeof(float),
                sizeof(packedGlobalAmbient));
    std::memcpy(&packedShadowTextureBias,
                backend.DrawFragmentUniformBytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentShadowTextureBiasOffset,
                sizeof(packedShadowTextureBias));
    std::memcpy(&packedShadowOrthographic,
                backend.DrawFragmentUniformBytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentShadowOrthographicOffset,
                sizeof(packedShadowOrthographic));
    std::memcpy(&packedShadowBiasConstant,
                backend.DrawFragmentUniformBytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentShadowBiasConstantOffset,
                sizeof(packedShadowBiasConstant));
    std::memcpy(&packedShadowBiasLinear,
                backend.DrawFragmentUniformBytes.data() +
                    Fast::Oot3d::kPicaPackedFragmentShadowBiasLinearOffset,
                sizeof(packedShadowBiasLinear));
    Require(backend.DrawCalled && backend.DrawSubmissionId == 17 &&
                backend.DrawCommandListAddress == 0x14003000U &&
                backend.DrawCompositionDomain ==
                    Oot3d::Renderer::PicaCompositionDomain::Scene &&
                backend.DrawComposition.Layer ==
                    Oot3d::Renderer::PicaCompositionLayer::TransparentWorld &&
                backend.DrawComposition.Provenance ==
                    Oot3d::Renderer::PicaCompositionProvenance::
                        NativeCmbDrawPass &&
                backend.DrawComposition.SourcePc == 0x0030F4D0U &&
                backend.DrawComposition.NativeValue == 1U &&
                backend.DrawTopology ==
                    Oot3d::Renderer::PicaTopology::TriangleStrip &&
                backend.DrawVertexCount == 3 &&
                backend.DrawWBuffering &&
                backend.DrawVertexBytes == std::vector<uint8_t>({1, 2, 3, 4}) &&
                backend.DrawTextureBytes ==
                    std::vector<uint8_t>({5, 6, 7, 8}) &&
                backend.DrawTextureMipLinear &&
                backend.DrawTextureLodBiasRaw == -384 &&
                backend.DrawTextureMinMip == 1U &&
                backend.DrawTextureMaxMip == 3U &&
                backend.DrawTextureContentHash == 101U &&
                backend.DrawTextureBaseLevelContentHash == 103U &&
                backend.DrawTextureBaseLevelContentHashAvailable &&
                backend.DrawFragmentFeatures.Valid() &&
                backend.DrawFragmentFeatures.SchemaVersion ==
                    Oot3d::Renderer::kPicaFragmentFeatureSchemaVersion &&
                backend.DrawFragmentFeatures.FragmentLightingEnabled &&
                backend.DrawFragmentFeatures.FragmentLighting.Valid() &&
                backend.DrawFragmentFeatures.FragmentLighting
                        .ActiveLightCount == 2U &&
                backend.DrawFragmentFeatures.FragmentLighting
                        .LightPermutation[1] == 5U &&
                backend.DrawFragmentFeatures.FragmentLighting.Lights[3]
                    .Directional &&
                backend.DrawFragmentFeatures.FogEnabled &&
                backend.DrawFragmentFeatures.FogFlip &&
                packedDiffuse == 0.75F &&
                packedGlobalAmbient == 0.25F &&
                packedShadowTextureBias == 0x123456 &&
                packedShadowOrthographic == 1 &&
                packedShadowBiasConstant == 0.75F &&
                packedShadowBiasLinear == 0.125F,
            "native draw plan was not translated through the neutral backend");

    Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission transfer;
    transfer.CompletionId = 23;
    transfer.InputPhysicalAddress = 0x18000000;
    transfer.OutputPhysicalAddress = 0x18100000;
    transfer.Transfer.InputSize = 240U | (400U << 16U);
    transfer.Transfer.OutputSize = 400U | (240U << 16U);
    transfer.Transfer.Flags = 1U << 24U;
    Require(Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                backend, transfer, true, &error, 11,
                Oot3d::Renderer::PicaPresentationMode::AlphaOverlay),
            error);
    Require(backend.TransferCalled && backend.Transfer.CompletionId == 23 &&
                backend.Transfer.RenderTargetNamespace == 11 &&
                backend.Transfer.OutputWidth == 200 &&
                backend.Transfer.OutputHeight == 240 &&
                backend.Transfer.PresentationMode ==
                    Oot3d::Renderer::PicaPresentationMode::AlphaOverlay,
            "native display transfer lost its source contract");

    Oot3dNativeGame::Oot3dPicaMemoryFillSubmission fill;
    fill.StartPhysicalAddress = 0x18200000;
    fill.EndPhysicalAddress = 0x18201000;
    fill.Value = 0x44332211;
    fill.Control = 0x101;
    Require(Oot3dNativeGame::SubmitOot3dPicaVulkanMemoryFill(
                backend, fill, &error, 13),
            error);
    Require(backend.FillCalled &&
                backend.Fill.RenderTargetNamespace == 13 &&
                backend.Fill.StartPhysicalAddress == 0x18200000 &&
                backend.Fill.EndPhysicalAddress == 0x18201000 &&
                backend.Fill.Value == 0x44332211 &&
                backend.Fill.Control == 0x101,
            "native memory fill lost its source contract");

    Require(Oot3dNativeGame::ClearOot3dPicaVulkanRenderTarget(
                backend, 15, 0x18300000, &error),
            error);
    Require(backend.ClearCalled && backend.ClearNamespace == 15 &&
                backend.ClearAddress == 0x18300000,
            "native render-target clear lost its source contract");

    backend.Operations.clear();
    Oot3dNativeGame::Oot3dPicaPresentationScheduler scheduler;
    auto firstPlan = plan;
    firstPlan.SubmissionId = 31U;
    auto secondPlan = plan;
    secondPlan.SubmissionId = 32U;
    Oot3dNativeGame::Oot3dPicaMemoryFillSubmission scheduledFill;
    scheduledFill.BeforeDrawSubmissionId = firstPlan.SubmissionId;
    scheduledFill.StartPhysicalAddress = 0x18400000U;
    scheduledFill.EndPhysicalAddress = 0x18401000U;
    scheduledFill.Control = 1U;
    Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission intermediateTransfer;
    intermediateTransfer.CompletionId = 40U;
    intermediateTransfer.AfterDrawSubmissionId = firstPlan.SubmissionId;
    intermediateTransfer.InputPhysicalAddress = 0x18000000U;
    intermediateTransfer.OutputPhysicalAddress = 0x18500000U;
    intermediateTransfer.Transfer.InputSize = 240U | (400U << 16U);
    intermediateTransfer.Transfer.OutputSize = 400U | (240U << 16U);
    Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission topTransfer =
        intermediateTransfer;
    topTransfer.CompletionId = 41U;
    topTransfer.AfterDrawSubmissionId = secondPlan.SubmissionId;
    topTransfer.OutputPhysicalAddress = 0x18600000U;

    scheduler.Capture(scheduledFill);
    scheduler.Capture(firstPlan);
    scheduler.Capture(intermediateTransfer);
    scheduler.Capture(secondPlan);
    scheduler.Capture(topTransfer);
    auto scheduledFrame = scheduler.FinishFrame(topTransfer);
    Require(scheduledFrame.has_value(),
            "presentation scheduler did not close a captured visual frame");
    Oot3dNativeGame::Oot3dPicaVisualFrameViewSample scheduledSample;
    scheduledSample.TopTransfer = scheduledFrame->TopTransfer;
    scheduledSample.MemoryFills = &scheduledFrame->MemoryFills;
    scheduledSample.DisplayTransfers = &scheduledFrame->DisplayTransfers;
    for (const auto& draw : scheduledFrame->Draws) {
        scheduledSample.Draws.push_back({.BasePlan = &draw});
    }
    scheduler.BeginPresentation(100U);
    Require(scheduler.Execute(
                backend, scheduledSample, 7U,
                Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::
                    CurrentFrame,
                true, &error),
            error);
    const std::vector<std::string> expectedOperations{
        "composition:1:2", "fill:406847488", "draw:31", "transfer:40",
        "draw:32", "transfer:41", "present:41"};
    Require(backend.Operations == expectedOperations,
            "presentation scheduler changed PICA operation order or copied "
            "the top transfer twice");
    Require(backend.CompositionSequenceId == 1U &&
                backend.CompositionDraws.size() == 2U &&
                backend.CompositionDraws[0].SubmissionId == 31U &&
                backend.CompositionDraws[1].SubmissionId == 32U &&
                backend.CompositionDraws[0].Target.RenderTargetNamespace ==
                    7U &&
                backend.CompositionDraws[0].Target.ColorPhysicalAddress ==
                    0x18000000U &&
                backend.CompositionDraws[0].Target.DepthPhysicalAddress ==
                    0x18080000U &&
                backend.CompositionDraws[0].Target.FramebufferWidth == 400U &&
                backend.CompositionDraws[0].Target.FramebufferHeight == 240U &&
                backend.CompositionDraws[0].Domain ==
                    Oot3d::Renderer::PicaCompositionDomain::Scene &&
                backend.CompositionDraws[0].Composition ==
                    backend.DrawComposition,
            "presentation scheduler lost the exact composition sequence");
    const size_t operationCount = backend.Operations.size();
    Require(!scheduler.Execute(
                backend, scheduledSample, 7U,
                Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::
                    RepeatedFrame,
                true, &error) &&
                backend.Operations.size() == operationCount,
            "presentation scheduler allowed duplicate draw execution");
    scheduler.BeginPresentation(101U);
    Require(scheduler.PresentExisting(backend, topTransfer, 7U, &error) &&
                backend.Operations.back() == "present:41",
            "presentation scheduler did not reuse an executed snapshot");

    auto replacementTransfer = topTransfer;
    replacementTransfer.CompletionId = 42U;
    auto replacementFrame = *scheduledFrame;
    replacementFrame.TopTransfer = replacementTransfer;
    replacementFrame.DisplayTransfers = {replacementTransfer};
    replacementFrame.Draws[0].SubmissionId = 33U;
    replacementFrame.Draws.resize(1U);
    Oot3dNativeGame::Oot3dPicaVisualFrameViewSample replacementSample;
    replacementSample.TopTransfer = replacementFrame.TopTransfer;
    replacementSample.MemoryFills = &replacementFrame.MemoryFills;
    replacementSample.DisplayTransfers = &replacementFrame.DisplayTransfers;
    replacementSample.Draws.push_back(
        {.BasePlan = &replacementFrame.Draws[0]});
    scheduler.BeginPresentation(102U);
    Require(scheduler.Execute(
                backend, replacementSample, 7U,
                Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::
                    CurrentFrame,
                false, &error),
            error);
    Require(!scheduler.HasSnapshot(topTransfer, 7U) &&
                scheduler.HasSnapshot(replacementTransfer, 7U),
            "presentation scheduler retained an obsolete completion for a "
            "reused display target");

    const auto& schedulerStats = scheduler.Stats();
    Require(schedulerStats.ProducedGuestDraws == 2U &&
                schedulerStats.ExecutedDraws == 3U &&
                schedulerStats.DuplicateDrawAttempts == 1U &&
                schedulerStats.RedundantTopSnapshotCopiesAvoided == 2U &&
                schedulerStats.ReusedSnapshots == 1U &&
                schedulerStats.CompositionSequencesPublished == 2U &&
                schedulerStats.CompositionDrawReferencesPublished == 3U &&
                schedulerStats.CompositionPublicationFailures == 0U,
            "presentation scheduler exactly-once counters are incorrect");

    std::cout << "oot3d_native_pica_vulkan_bridge_tests: ok\n";
    return 0;
}
