#include "oot3d_native_pica_vulkan_bridge.h"
#include "oot3d_native_pica_presentation_scheduler.h"
#include "oot3d_static_shadow_actor_catalog.h"

#include "fast/oot3d/pica_uniform_layout.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <optional>
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
        DrawRoomShadows = draw.LightBinding.RoomShadows;
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
        DrawTextureCopyReferences.assign(
            draw.Textures[0].CopyReferences.begin(),
            draw.Textures[0].CopyReferences.end());
        DrawAlphaTestEnabled = draw.AlphaTestEnabled;
        DrawAlphaCompare = draw.AlphaCompare;
        DrawAlphaReference = draw.AlphaReference;
        return true;
    }

    bool WantsPicaSequenceDrawPreparation(
        const Oot3d::Renderer::PicaCompositionDrawReference& draw) const override {
        return PreparedLayer.has_value() && draw.Composition.Layer == *PreparedLayer;
    }

    bool PicaSequenceDrawPreparationNeedsFragmentState() const override {
        return PreparationNeedsFragmentState;
    }

    bool PreparePicaSequenceDraw(
        const Oot3d::Renderer::PicaDrawView& draw, std::string*) override {
        Operations.push_back("prepare:" + std::to_string(draw.SubmissionId));
        PreparedVertexUniformBytes = draw.VertexUniformBytes.size();
        PreparedFragmentUniformBytes = draw.FragmentUniformBytes.size();
        PreparedTextureCount = draw.Textures.size();
        PreparedAlphaTestEnabled = draw.AlphaTestEnabled;
        PreparedRoomShadows = draw.LightBinding.RoomShadows;
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
        if (transfer.TextureCopyBytes != 0U && transfer.MaterializeGuest)
            ++GuestCopyCompletions;
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
    std::vector<Fast::Renderer3ds::PicaTextureCopyReference>
        DrawTextureCopyReferences;
    bool DrawAlphaTestEnabled = false;
    std::shared_ptr<const Fast::Renderer3ds::RoomShadowShellScene> DrawRoomShadows;
    std::shared_ptr<const Fast::Renderer3ds::RoomShadowShellScene> PreparedRoomShadows;
    Oot3d::Renderer::PicaCompareFunction DrawAlphaCompare =
        Oot3d::Renderer::PicaCompareFunction::Always;
    uint8_t DrawAlphaReference = 0U;
    std::optional<Oot3d::Renderer::PicaCompositionLayer> PreparedLayer;
    bool PreparationNeedsFragmentState = false;
    size_t PreparedVertexUniformBytes = 0U;
    size_t PreparedFragmentUniformBytes = 0U;
    size_t PreparedTextureCount = 0U;
    bool PreparedAlphaTestEnabled = false;
    bool TransferCalled = false;
    Oot3d::Renderer::PicaDisplayTransferView Transfer;
    uint64_t GuestCopyCompletions = 0U;
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
    auto roomShadows = std::make_shared<Fast::Renderer3ds::RoomShadowShellScene>();
    roomShadows->ScopeIdentity = 123;
    plan.LightBinding.RoomShadows = roomShadows;
    plan.CommandListAddress = 0x14003000U;
    plan.CompositionDomain =
        Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene;
    plan.Composition = {
        Oot3dNativeGame::Oot3dPicaCompositionLayer::TransparentWorld,
        Oot3dNativeGame::Oot3dPicaCompositionProvenance::NativeCmbDrawPass,
        0x0030F4D0U,
        1U,
    };
    plan.Composition.TemporalIdentity = {9, 10, 11, 12, 13};
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
    // Register 0x104: alpha test enabled, Greater (6), reference 0x7F.
    plan.FragmentShader.Uniforms.FragmentControl[1] = 0x7F61U;
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
    texture.CopyReferences = {
        {31U, 29U, 0x18000000U, 64U},
        {31U, 29U, 0x18000080U, 16U},
    };
    plan.Textures.push_back(std::move(texture));

    const auto alphaTest = Oot3dNativeGame::DecodeOot3dPicaAlphaTest(0x7F61U);
    const auto alphaTestDisabled = Oot3dNativeGame::DecodeOot3dPicaAlphaTest(0xFF10U);
    Require(alphaTest.Enabled && alphaTest.Compare == 6U && alphaTest.Reference == 0x7FU &&
                !alphaTestDisabled.Enabled && alphaTestDisabled.Compare == 1U &&
                alphaTestDisabled.Reference == 0xFFU,
            "PICA alpha-test register was decoded incorrectly");

    std::string error;
    Require(Oot3dNativeGame::SubmitOot3dPicaVulkanDrawPlan(
                backend, plan, &error, 9),
            error);
    Require(backend.DrawRoomShadows == roomShadows, "draw bridge lost room shell publication");
    Require(backend.DrawComposition.TemporalIdentity == plan.Composition.TemporalIdentity,
            "draw bridge lost native temporal identity");
    Require(!backend.DrawComposition.StaticShadowCacheEligible,
            "draw bridge made non-actor geometry static shadow cache eligible");
    {
        // The title catalog decides static shadow cache eligibility from the
        // native actor profile; the player and horses never qualify.
        using Oot3dNativeGame::IsStaticShadowCacheEligibleActor;
        using Oot3dNativeGame::StaticShadowActorProfile;
        constexpr StaticShadowActorProfile catalog[] = {{0x0077U, "En_Wood02"}};
        const uint64_t tree = (uint64_t{0x0077U} << 32U) | 0x08123400U;
        Require(IsStaticShadowCacheEligibleActor(true, tree, catalog), "catalogued actor is not eligible");
        Require(!IsStaticShadowCacheEligibleActor(false, tree, catalog), "non-actor geometry is eligible");
        Require(!IsStaticShadowCacheEligibleActor(true, 0U, catalog), "anonymous actor geometry is eligible");
        Require(!IsStaticShadowCacheEligibleActor(true, (uint64_t{0x0078U} << 32U) | 0x1000U, catalog),
                "uncatalogued actor is eligible");
        Require(!IsStaticShadowCacheEligibleActor(true, (uint64_t{0x10077U} << 32U) | 0x1000U, catalog),
                "malformed actor identity is eligible");
        constexpr StaticShadowActorProfile excluded[] = {{0x0000U, "Player"}, {0x0014U, "En_Horse"},
                                                         {0x009AU, "En_Horse_Link_Child"}};
        for (const auto& entry : excluded)
            Require(!IsStaticShadowCacheEligibleActor(true, (uint64_t{entry.Id} << 32U) | 0x1000U, excluded),
                    "player or horse is static shadow cache eligible");
        for (const auto& entry : Oot3dNativeGame::StaticShadowActorCatalog()) {
            RecordingBackend actorBackend;
            auto actorPlan = plan;
            actorPlan.Composition.NativeActorGeometry = true;
            actorPlan.Composition.NativeActorIdentity = (uint64_t{entry.Id} << 32U) | 0x08000000U;
            Require(Oot3dNativeGame::SubmitOot3dPicaVulkanDrawPlan(actorBackend, actorPlan, &error, 9), error);
            Require(actorBackend.DrawComposition.StaticShadowCacheEligible,
                    "draw bridge lost catalogued static shadow cache eligibility");
        }
    }
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
                backend.DrawTextureCopyReferences.size() == 2U &&
                backend.DrawTextureCopyReferences[0].CompletionId == 31U &&
                backend.DrawTextureCopyReferences[0].AfterDrawSubmissionId ==
                    29U &&
                backend.DrawTextureCopyReferences[0].PhysicalAddress ==
                    0x18000000U &&
                backend.DrawTextureCopyReferences[0].ByteCount == 64U &&
                backend.DrawTextureCopyReferences[1].PhysicalAddress ==
                    0x18000080U &&
                backend.DrawTextureCopyReferences[1].ByteCount == 16U &&
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
                packedShadowBiasLinear == 0.125F &&
                backend.DrawAlphaTestEnabled &&
                backend.DrawAlphaCompare ==
                    Oot3d::Renderer::PicaCompareFunction::Greater &&
                backend.DrawAlphaReference == 0x7FU,
            "native draw plan was not translated through the neutral backend");

    Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission transfer;
    transfer.CompletionId = 23;
    transfer.InputPhysicalAddress = 0x18000000;
    transfer.OutputPhysicalAddress = 0x18100000;
    transfer.Transfer.InputSize = 240U | (400U << 16U);
    transfer.Transfer.OutputSize = 400U | (240U << 16U);
    transfer.Transfer.Flags = 1U << 24U;
    Require(Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                backend, transfer, true, &error, 11, false,
                Oot3d::Renderer::PicaPresentationMode::AlphaOverlay),
            error);
    Require(backend.TransferCalled && backend.Transfer.CompletionId == 23 &&
                backend.Transfer.RenderTargetNamespace == 11 &&
                backend.Transfer.OutputWidth == 200 &&
                backend.Transfer.OutputHeight == 240 &&
                !backend.Transfer.MaterializeGuest &&
                backend.Transfer.PresentationMode ==
                    Oot3d::Renderer::PicaPresentationMode::AlphaOverlay,
            "native display transfer lost its source contract");

    // Only the real ordered execution of a raw copy materializes guest memory;
    // an interpolated or repeated replay recreates the GPU version only.
    Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission rawTransfer;
    rawTransfer.CompletionId = 24;
    rawTransfer.SignalInterrupt = true;
    rawTransfer.InputPhysicalAddress = 0x18000000;
    rawTransfer.OutputPhysicalAddress = 0x18100000;
    rawTransfer.Transfer.OutputSize = 0x00000002U;
    rawTransfer.Transfer.TextureCopyBytes = 32U;
    Require(Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                backend, rawTransfer, false, &error, 11, true),
            error);
    Require(backend.TransferCalled && backend.Transfer.MaterializeGuest &&
                backend.Transfer.TextureCopyBytes == 32U &&
                backend.Transfer.AfterDrawSubmissionId == 0U &&
                backend.Transfer.OutputWidth == 2U &&
                backend.Transfer.OutputHeight == 0U,
            "real raw copy execution did not request guest materialization");
    Require(Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                backend, rawTransfer, false, &error, 11, false),
            error);
    Require(!backend.Transfer.MaterializeGuest,
            "a replayed raw copy asked to materialize guest memory again");

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
    scheduledSample.MemoryFills = scheduledFrame->MemoryFills;
    scheduledSample.DisplayTransfers = scheduledFrame->DisplayTransfers;
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
    replacementSample.MemoryFills = replacementFrame.MemoryFills;
    replacementSample.DisplayTransfers = replacementFrame.DisplayTransfers;
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
                schedulerStats.CompositionPublicationFailures == 0U &&
                schedulerStats.PreparedDraws == 0U,
            "presentation scheduler exactly-once counters are incorrect");

    // A backend that prepares receives the draws it selects after the sequence
    // is published and before it executes; each draw is still submitted once,
    // in native order. Native-memory execution never prepares.
    auto opaquePlan = plan;
    opaquePlan.SubmissionId = 61U;
    opaquePlan.Composition.Layer =
        Oot3dNativeGame::Oot3dPicaCompositionLayer::OpaqueWorld;
    auto transparentPlan = plan;
    transparentPlan.SubmissionId = 62U;
    auto preparedTop = topTransfer;
    preparedTop.CompletionId = 63U;
    preparedTop.AfterDrawSubmissionId = transparentPlan.SubmissionId;
    const auto executePrepared =
        [&](RecordingBackend& target,
            Oot3dNativeGame::Oot3dPicaPresentationExecutionKind kind,
            bool present, uint64_t& preparedDraws) {
            Oot3dNativeGame::Oot3dPicaPresentationScheduler preparing;
            preparing.Capture(opaquePlan);
            preparing.Capture(transparentPlan);
            preparing.Capture(preparedTop);
            const auto frame = preparing.FinishFrame(preparedTop);
            Oot3dNativeGame::Oot3dPicaVisualFrameViewSample sample;
            Require(frame.has_value() &&
                        Oot3dNativeGame::ViewOot3dPicaVisualFrame(*frame, sample),
                    "preparation fixture has no visual sample");
            preparing.BeginPresentation(300U);
            Require(preparing.Execute(target, sample, 7U, kind, present, &error),
                    error);
            preparedDraws = preparing.Stats().PreparedDraws;
        };
    RecordingBackend preparingBackend;
    preparingBackend.PreparedLayer =
        Oot3d::Renderer::PicaCompositionLayer::OpaqueWorld;
    uint64_t preparedDraws = 0U;
    executePrepared(preparingBackend,
                    Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::CurrentFrame,
                    true, preparedDraws);
    Require(preparingBackend.Operations ==
                    std::vector<std::string>{"composition:1:2", "prepare:61",
                                             "draw:61", "draw:62", "transfer:63",
                                             "present:63"} &&
                preparedDraws == 1U,
            "presentation preparation was not selected, ordered or counted");
    // The alpha-tested caster runs its native alpha test in the map, so its
    // preparation carries the fragment uniforms and textures.
    Require(preparingBackend.PreparedVertexUniformBytes != 0U &&
                preparingBackend.PreparedFragmentUniformBytes ==
                    Fast::Oot3d::kPicaPackedFragmentUniformSize &&
                preparingBackend.PreparedTextureCount == plan.Textures.size() &&
                preparingBackend.PreparedAlphaTestEnabled,
            "an alpha-tested prepared draw lost its fragment state or alpha test");
    opaquePlan.FragmentShader.Uniforms.FragmentControl[1] = 0x7F60U;
    RecordingBackend opaqueBackend;
    opaqueBackend.PreparedLayer =
        Oot3d::Renderer::PicaCompositionLayer::OpaqueWorld;
    executePrepared(opaqueBackend,
                    Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::CurrentFrame,
                    true, preparedDraws);
    Require(preparedDraws == 1U &&
                opaqueBackend.PreparedVertexUniformBytes != 0U &&
                opaqueBackend.PreparedFragmentUniformBytes == Fast::Oot3d::kPicaPackedFragmentUniformSize &&
                opaqueBackend.PreparedTextureCount == plan.Textures.size() &&
                !opaqueBackend.PreparedAlphaTestEnabled,
            "an opaque prepared draw lost source-classification fragment state");
    // A backend reading native overlay alpha during preparation receives the
    // fragment state of draws without an alpha test too.
    RecordingBackend overlayBackend;
    overlayBackend.PreparedLayer =
        Oot3d::Renderer::PicaCompositionLayer::OpaqueWorld;
    overlayBackend.PreparationNeedsFragmentState = true;
    executePrepared(overlayBackend,
                    Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::CurrentFrame,
                    true, preparedDraws);
    Require(preparedDraws == 1U &&
                overlayBackend.PreparedFragmentUniformBytes ==
                    Fast::Oot3d::kPicaPackedFragmentUniformSize &&
                overlayBackend.PreparedTextureCount == plan.Textures.size() &&
                !overlayBackend.PreparedAlphaTestEnabled,
            "a prepared draw lost the fragment state its backend asked for");
    Require(opaqueBackend.PreparedRoomShadows == roomShadows && opaqueBackend.DrawRoomShadows == roomShadows,
            "presentation preparation or submission lost room shell publication");
    RecordingBackend nativeMemoryBackend;
    nativeMemoryBackend.PreparedLayer =
        Oot3d::Renderer::PicaCompositionLayer::OpaqueWorld;
    executePrepared(nativeMemoryBackend,
                    Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::NativeFrame,
                    false, preparedDraws);
    Require(preparedDraws == 0U &&
                std::none_of(nativeMemoryBackend.Operations.begin(),
                             nativeMemoryBackend.Operations.end(),
                             [](const std::string& operation) {
                                 return operation.starts_with("prepare:");
                             }),
            "native-memory execution prepared a draw");

    Oot3dNativeGame::Oot3dPicaPresentationScheduler dependencyScheduler;
    auto copy = topTransfer;
    copy.CompletionId = 51U;
    copy.Transfer = {0x1F2447C0U, 0x1F3E3300U, 0x3C0U, 0x4003C0U, 12U, 768000U};
    dependencyScheduler.Capture(copy);
    Oot3dNativeGame::Oot3dPicaVisualFrameViewSample dependencySample;
    Require(dependencyScheduler.ViewDependencyWork(dependencySample) &&
                dependencySample.Draws.empty() &&
                dependencySample.DisplayTransfers.size() == 1,
            "transfer-only GPU dependency was lost while awaiting scanout");
    dependencyScheduler.BeginPresentation(1U);
    backend.Operations.clear();
    Require(dependencyScheduler.Execute(backend, dependencySample, 7U,
                Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::DependencyFlush, false, &error), error);
    Require(backend.Operations == std::vector<std::string>{"composition:1:0", "transfer:51"} &&
                backend.Transfer.TextureCopyBytes == 768000U && backend.Transfer.InputWidth == 960U &&
                backend.Transfer.InputHeight == 0U && backend.Transfer.OutputWidth == 960U &&
                backend.Transfer.OutputHeight == 64U && !backend.Transfer.Present,
            "dependency flush invented a scanout or changed raw width/gap fields");
    Require(!dependencyScheduler.ViewDependencyWork(dependencySample),
            "dependency executed twice");
    // A native copy may unblock the guest in the middle of a frame. Its source
    // must still participate in interpolation, without another guest write.
    Oot3dNativeGame::Oot3dPicaPresentationScheduler splitScheduler;
    RecordingBackend splitBackend;
    splitScheduler.BeginPresentation(200U);
    splitScheduler.Capture(firstPlan);
    copy.AfterDrawSubmissionId = firstPlan.SubmissionId;
    splitScheduler.Capture(copy);
    Require(splitScheduler.ViewDependencyWork(dependencySample), "missing native prefix");
    Require(splitScheduler.Execute(splitBackend, dependencySample, 1U,
                Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::DependencyFlush,
                false, &error), error);
    Require(splitBackend.GuestCopyCompletions == 1U &&
                !splitScheduler.HasPendingRawCopyCompletion(),
            "native dependency did not discharge its single guest completion");
    auto resumedSchedulerState = splitScheduler.CaptureState();
    Require(splitScheduler.RestoreState(std::move(resumedSchedulerState)) &&
                !splitScheduler.HasPendingRawCopyCompletion(),
            "restoring a completed prefix manufactured another guest write");
    splitScheduler.BeginPresentation(200U);
    splitScheduler.Capture(secondPlan);
    auto completedTransfer = topTransfer;
    completedTransfer.CompletionId = 52U;
    splitScheduler.Capture(completedTransfer);
    auto completeFrame = splitScheduler.FinishFrame(completedTransfer);
    Require(completeFrame && completeFrame->Draws.size() == 2U &&
                completeFrame->Draws.front().SubmissionId == firstPlan.SubmissionId &&
                completeFrame->Draws.back().SubmissionId == secondPlan.SubmissionId,
            "dependency execution consumed the interpolation source");
    Oot3dNativeGame::Oot3dPicaVisualFrameViewSample nativeRemainder;
    Require(Oot3dNativeGame::ViewOot3dPicaVisualFrame(
                *completeFrame, nativeRemainder, true) &&
                nativeRemainder.Draws.size() == 1U &&
                nativeRemainder.Draws.front().BasePlan->SubmissionId == secondPlan.SubmissionId,
            "native continuation would redraw an already executed prefix");
    Require(splitScheduler.Execute(splitBackend, nativeRemainder, 1U,
                Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::NativeFrame,
                false, &error), error);
    Oot3dNativeGame::Oot3dPicaVisualFrameViewSample fullSample;
    Require(Oot3dNativeGame::ViewOot3dPicaVisualFrame(*completeFrame, fullSample),
            "complete visual sample missing");
    Require(splitScheduler.Execute(splitBackend, fullSample, 2U,
                Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::InterpolatedFrame,
                true, &error), error);
    Require(splitBackend.GuestCopyCompletions == 1U &&
                splitScheduler.Stats().DuplicateDrawAttempts == 0U,
            "visual replay duplicated native execution side effects");
    {
        // The guest ran one frame ahead and issued a raw copy in that next
        // frame: its dependency flush executed both frames' work up to the
        // copy before the host closed the selected frame. Closing it splits the
        // next frame off together with the copy, and neither part may execute
        // the flushed prefix again (the Sheik revelation under Original30).
        Oot3dNativeGame::Oot3dPicaPresentationScheduler aheadScheduler;
        RecordingBackend aheadBackend;
        aheadScheduler.BeginPresentation(300U);
        auto selectedWorld = firstPlan;
        selectedWorld.SubmissionId = 61U;
        selectedWorld.State.Framebuffer.ColorPhysicalAddress = 0x18000000U;
        auto nextWorld = selectedWorld;
        nextWorld.SubmissionId = 62U;
        auto nextEffect = selectedWorld;
        nextEffect.SubmissionId = 63U;
        auto selectedTop = topTransfer;
        selectedTop.CompletionId = 71U;
        selectedTop.AfterDrawSubmissionId = selectedWorld.SubmissionId;
        auto nextCopy = copy;
        nextCopy.CompletionId = 72U;
        nextCopy.InputPhysicalAddress = 0x18000000U;
        nextCopy.AfterDrawSubmissionId = nextWorld.SubmissionId;
        auto nextTop = topTransfer;
        nextTop.CompletionId = 73U;
        nextTop.AfterDrawSubmissionId = nextEffect.SubmissionId;
        aheadScheduler.Capture(selectedWorld);
        aheadScheduler.Capture(selectedTop);
        aheadScheduler.Capture(nextWorld);
        aheadScheduler.Capture(nextCopy);
        Require(aheadScheduler.ViewDependencyWork(dependencySample) &&
                    dependencySample.Draws.size() == 2U,
                "the copy's dependency did not cover both frames' draws");
        Require(aheadScheduler.Execute(aheadBackend, dependencySample, 1U,
                    Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::DependencyFlush,
                    false, &error), error);
        aheadScheduler.Capture(nextEffect);
        aheadScheduler.Capture(nextTop);
        const std::array<uint32_t, 1> outputs{topTransfer.OutputPhysicalAddress};
        auto frames = aheadScheduler.FinishFrames(selectedTop, outputs);
        Require(frames.size() == 1U && frames[0].Draws.size() == 1U &&
                    frames[0].Draws[0].SubmissionId == selectedWorld.SubmissionId,
                "the selected frame did not keep only its own draw");
        Oot3dNativeGame::Oot3dPicaVisualFrameViewSample remainder;
        (void)Oot3dNativeGame::ViewOot3dPicaVisualFrame(frames[0], remainder, true);
        Require(remainder.Draws.empty() && remainder.DisplayTransfers.empty(),
                "the selected frame would redraw work its next frame's copy flushed");
        Require(aheadScheduler.Execute(aheadBackend, remainder, 1U,
                    Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::NativeFrame,
                    false, &error), error);
        Oot3dNativeGame::Oot3dPicaVisualFrameViewSample whole;
        Require(Oot3dNativeGame::ViewOot3dPicaVisualFrame(frames[0], whole) &&
                    whole.Draws.size() == 1U && whole.DisplayTransfers.size() == 1U,
                "the presented frame lost its flushed work");
        frames = aheadScheduler.FinishFrames(nextTop, outputs);
        Require(frames.size() == 1U && frames[0].Draws.size() == 2U,
                "the next frame did not keep the work split off with its copy");
        (void)Oot3dNativeGame::ViewOot3dPicaVisualFrame(frames[0], remainder, true);
        Require(remainder.Draws.size() == 1U &&
                    remainder.Draws[0].BasePlan->SubmissionId == nextEffect.SubmissionId &&
                    remainder.DisplayTransfers.size() == 1U &&
                    remainder.DisplayTransfers[0].CompletionId == nextTop.CompletionId,
                "the next frame would redraw its flushed prefix or lose its suffix");
        Require(aheadScheduler.Execute(aheadBackend, remainder, 1U,
                    Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::NativeFrame,
                    false, &error), error);
        Require(aheadScheduler.Stats().DuplicateDrawAttempts == 0U &&
                    aheadBackend.GuestCopyCompletions == 1U,
                "split frames executed native work twice");
    }
    std::cout << "oot3d_native_pica_vulkan_bridge_tests: ok\n";
    return 0;
}
