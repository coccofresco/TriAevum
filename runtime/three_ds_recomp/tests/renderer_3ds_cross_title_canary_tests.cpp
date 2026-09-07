#include "fast/renderer3ds/pica_composition_schedule.h"
#include "fast/renderer3ds/pica_render_backend.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Fast::Renderer3ds {
namespace {

enum class RecordedOperation : uint8_t {
    Clear,
    Fill,
    Draw,
    Transfer,
};

class RecordingPicaBackend final : public PicaRenderBackend {
  public:
    bool PublishPicaCompositionSequence(
        const PicaCompositionSequenceView& sequence,
        std::string* error) override {
        return Schedule.Compile(sequence, error);
    }

    bool SubmitPicaDraw(
        const PicaDrawView& draw, std::string* error) override {
        if (!draw.FragmentShaderSourceIdentity.Available() ||
            !draw.FragmentFeatures.Valid() || draw.Textures.empty() ||
            !draw.Textures.front().NativeContentHashAvailable) {
            if (error != nullptr) {
                *error = "representative PICA material contract is incomplete";
            }
            return false;
        }
        if (!Schedule.ConsumeDraw(
                BuildPicaCompositionDrawReference(draw), error)) {
            return false;
        }
        Operations.push_back(RecordedOperation::Draw);
        DrawLayers.push_back(draw.Composition.Layer);
        FragmentProgramIds.push_back(draw.CanonicalFragmentProgramId);
        TextureHashes.push_back(draw.Textures.front().NativeContentHash);
        TextureFormats.push_back(draw.Textures.front().NativeFormat);
        FogEnabled.push_back(draw.FragmentFeatures.FogEnabled);
        FragmentLightingEnabled.push_back(
            draw.FragmentFeatures.FragmentLightingEnabled);
        Topologies.push_back(draw.Topology);
        StencilEnabled.push_back(draw.Stencil.Enabled);
        return true;
    }

    bool SubmitPicaDisplayTransfer(
        const PicaDisplayTransferView& transfer,
        std::string*) override {
        Operations.push_back(RecordedOperation::Transfer);
        if (transfer.Present) {
            Presentation.LastPresentedTransfer = transfer;
            Presentation.DisplayImages = {
                {
                    transfer.RenderTargetNamespace,
                    transfer.OutputPhysicalAddress,
                    transfer.OutputWidth,
                    transfer.OutputHeight,
                    true,
                },
            };
        }
        return true;
    }

    bool ClearPicaRenderTarget(
        uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress,
        std::string*) override {
        Operations.push_back(RecordedOperation::Clear);
        ClearedTarget = {renderTargetNamespace, colorPhysicalAddress};
        return true;
    }

    bool SubmitPicaMemoryFill(
        const PicaMemoryFillView& fill, std::string*) override {
        Operations.push_back(RecordedOperation::Fill);
        LastFill = fill;
        return true;
    }

    bool CapturePicaPresentationState(
        PicaPresentationStateSnapshot& snapshot,
        std::string*) override {
        snapshot = Presentation;
        return true;
    }

    bool QueuePicaCompletion(
        uint64_t completionId, std::string*) override {
        CompletionIds.push_back(completionId);
        return true;
    }

    std::vector<uint64_t> TakePicaCompletions() override {
        return std::exchange(CompletionIds, {});
    }

    bool ResetPicaState(std::string*) override {
        Schedule.Reset();
        Operations.clear();
        DrawLayers.clear();
        FragmentProgramIds.clear();
        TextureHashes.clear();
        TextureFormats.clear();
        FogEnabled.clear();
        FragmentLightingEnabled.clear();
        Topologies.clear();
        StencilEnabled.clear();
        ClearedTarget.reset();
        LastFill.reset();
        Presentation = {};
        CompletionIds.clear();
        TemporalSample.reset();
        return true;
    }

    bool PublishPicaFrameTemporalSample(
        const PicaFrameTemporalSample& sample) override {
        if (!sample.Available()) {
            return false;
        }
        TemporalSample = sample;
        return true;
    }

    PicaCompositionSchedule Schedule;
    std::vector<RecordedOperation> Operations;
    std::vector<PicaCompositionLayer> DrawLayers;
    std::vector<uint64_t> FragmentProgramIds;
    std::vector<uint64_t> TextureHashes;
    std::vector<uint8_t> TextureFormats;
    std::vector<bool> FogEnabled;
    std::vector<bool> FragmentLightingEnabled;
    std::vector<PicaTopology> Topologies;
    std::vector<bool> StencilEnabled;
    std::optional<std::pair<uint64_t, uint32_t>> ClearedTarget;
    std::optional<PicaMemoryFillView> LastFill;
    PicaPresentationStateSnapshot Presentation;
    std::vector<uint64_t> CompletionIds;
    std::optional<PicaFrameTemporalSample> TemporalSample;
};

// Models an independent title frontend. It owns title state and guest
// completion tokens, but uses only the shared Nintendo 3DS PICA contract.
class IndependentTitleFrameAdapter final {
  public:
    bool SubmitRepresentativeFrame(
        PicaRenderBackend& backend, std::string* error) const {
        constexpr std::string_view fragmentShader =
            "void main() { native_color_output(); }";
        const auto fragmentShaderIdentity =
            IdentifyPicaShaderSource(fragmentShader);
        const std::array<uint8_t, 8U> textureBytes{
            0x20U, 0x60U, 0xA0U, 0xFFU,
            0x10U, 0x50U, 0x90U, 0xF0U};
        std::array<PicaTextureView, 1U> textures{};
        textures[0].Slot = 0U;
        textures[0].Width = 4U;
        textures[0].Height = 4U;
        textures[0].NativeFormat = 12U;
        textures[0].PhysicalAddress = 0x18600000U;
        textures[0].NativeBytes = textureBytes;
        textures[0].NativeContentHash = 0x8F391BC17D42A615ULL;
        textures[0].NativeContentHashAvailable = true;

        std::array<PicaDrawView, 4U> draws{};
        const std::array layers{
            PicaCompositionLayer::OpaqueWorld,
            PicaCompositionLayer::TransparentWorld,
            PicaCompositionLayer::Atmosphere,
            PicaCompositionLayer::Ui,
        };
        const std::array domains{
            PicaCompositionDomain::Scene,
            PicaCompositionDomain::Scene,
            PicaCompositionDomain::Scene,
            PicaCompositionDomain::Ui,
        };
        const std::array provenance{
            PicaCompositionProvenance::NativeControlFlow,
            PicaCompositionProvenance::NativeControlFlow,
            PicaCompositionProvenance::NativeControlFlow,
            PicaCompositionProvenance::NativeUiLifecycle,
        };

        for (size_t index = 0U; index < draws.size(); ++index) {
            auto& draw = draws[index];
            draw.SubmissionId = 101U + index;
            draw.RenderTargetNamespace = 9U;
            draw.CommandListAddress = 0x14000000U;
            draw.CommandListOffsetWords =
                static_cast<uint32_t>(index * 16U);
            draw.CanonicalFragmentProgramId = 0x5000U + index;
            draw.CanonicalPipelineId = 0x6000U + index;
            draw.FragmentShaderSource = fragmentShader;
            draw.FragmentShaderSourceIdentity = fragmentShaderIdentity;
            draw.FragmentFeatures.SchemaVersion =
                kPicaFragmentFeatureSchemaVersion;
            draw.FragmentFeatures.FragmentLightingEnabled =
                domains[index] == PicaCompositionDomain::Scene;
            if (draw.FragmentFeatures.FragmentLightingEnabled) {
                auto& lighting =
                    draw.FragmentFeatures.FragmentLighting;
                lighting.SchemaVersion =
                    kPicaFragmentLightingLayoutSchemaVersion;
                lighting.ActiveLightCount = 1U;
                lighting.LightPermutation[0] = 0U;
            }
            draw.FragmentFeatures.FogEnabled =
                domains[index] == PicaCompositionDomain::Scene;
            draw.FragmentFeatures.FogMode =
                draw.FragmentFeatures.FogEnabled ? 5U : 0U;
            draw.Textures = textures;
            draw.CompositionDomain = domains[index];
            draw.Composition = {
                layers[index],
                provenance[index],
                0x00100000U + static_cast<uint32_t>(index * 4U),
                static_cast<uint32_t>(index),
            };
            draw.FramebufferWidth = 400U;
            draw.FramebufferHeight = 240U;
            draw.FramebufferColorPhysicalAddress = 0x18000000U;
            draw.FramebufferDepthPhysicalAddress = 0x18100000U;
            draw.FramebufferColorFormat = 0U;
            draw.FramebufferDepthFormat = 3U;
            draw.Topology = index == 0U
                                ? PicaTopology::GeometryShader
                                : PicaTopology::TriangleList;
            draw.Indexed = index == 0U;
            draw.CullMode = index == 0U
                                ? NativeCullMode::KeepCounterClockwise
                                : NativeCullMode::KeepAll;
            draw.DepthTestEnabled = index < 3U;
            draw.DepthWriteEnabled = index == 0U;
            draw.Blend.Enabled = index == 1U;
            draw.AlphaTestEnabled = index == 1U;
            draw.Stencil.Enabled = index == 0U;
            draw.Stencil.Compare = PicaCompareFunction::Always;
            draw.Stencil.WriteMask = 0xFFU;
        }

        std::array<PicaCompositionDrawReference, 4U> references{};
        for (size_t index = 0U; index < draws.size(); ++index) {
            references[index] =
                BuildPicaCompositionDrawReference(draws[index]);
        }
        if (!backend.PublishPicaCompositionSequence(
                {kPicaCompositionSequenceSchemaVersion, 41U, references},
                error)) {
            return false;
        }

        const auto temporalSample = BuildPicaFrameTemporalSample(
            {PicaVisualInterpolationMode::Fixed2x, 30U, 60U, 2U, true},
            PicaFrameTemporalSampleKind::Transition,
            700U, 701U, 5U, 0.5F);
        if (!backend.PublishPicaFrameTemporalSample(temporalSample)) {
            return false;
        }
        if (!backend.ClearPicaRenderTarget(
                9U, 0x18000000U, error)) {
            return false;
        }
        if (!backend.SubmitPicaMemoryFill(
                {9U, 0x18000000U, 0x1805DC00U, 0x10203040U, 1U},
                error)) {
            return false;
        }
        for (const auto& draw : draws) {
            if (!backend.SubmitPicaDraw(draw, error)) {
                return false;
            }
        }
        if (!backend.SubmitPicaDisplayTransfer(
                {801U, 9U, 0x18000000U, 0x1F000000U,
                 400U, 240U, 400U, 240U, 0U, true,
                 PicaPresentationMode::Replace},
                error)) {
            return false;
        }
        return backend.QueuePicaCompletion(801U, error);
    }
};

TEST(Renderer3dsCrossTitleCanary,
     ReplaysRepresentativeFrameThroughOnlySharedPicaContracts) {
    RecordingPicaBackend backend;
    IndependentTitleFrameAdapter adapter;
    std::string error;

    ASSERT_TRUE(adapter.SubmitRepresentativeFrame(backend, &error))
        << error;
    EXPECT_EQ(backend.Schedule.SequenceId(), 41U);
    EXPECT_EQ(backend.Schedule.Stats().DrawCount, 4U);
    EXPECT_EQ(backend.Schedule.Stats().ConsumedDrawCount, 4U);
    ASSERT_EQ(backend.DrawLayers.size(), 4U);
    EXPECT_EQ(backend.DrawLayers[0], PicaCompositionLayer::OpaqueWorld);
    EXPECT_EQ(backend.DrawLayers[1],
              PicaCompositionLayer::TransparentWorld);
    EXPECT_EQ(backend.DrawLayers[2], PicaCompositionLayer::Atmosphere);
    EXPECT_EQ(backend.DrawLayers[3], PicaCompositionLayer::Ui);
    EXPECT_EQ(backend.FragmentProgramIds,
              (std::vector<uint64_t>{0x5000U, 0x5001U,
                                     0x5002U, 0x5003U}));
    EXPECT_EQ(backend.TextureHashes,
              (std::vector<uint64_t>(4U, 0x8F391BC17D42A615ULL)));
    EXPECT_EQ(backend.TextureFormats,
              (std::vector<uint8_t>(4U, 12U)));
    EXPECT_EQ(backend.FogEnabled,
              (std::vector<bool>{true, true, true, false}));
    EXPECT_EQ(backend.FragmentLightingEnabled,
              (std::vector<bool>{true, true, true, false}));
    EXPECT_EQ(backend.Topologies,
              (std::vector<PicaTopology>{
                  PicaTopology::GeometryShader,
                  PicaTopology::TriangleList,
                  PicaTopology::TriangleList,
                  PicaTopology::TriangleList}));
    EXPECT_EQ(backend.StencilEnabled,
              (std::vector<bool>{true, false, false, false}));

    const std::array expectedOperations{
        RecordedOperation::Clear,
        RecordedOperation::Fill,
        RecordedOperation::Draw,
        RecordedOperation::Draw,
        RecordedOperation::Draw,
        RecordedOperation::Draw,
        RecordedOperation::Transfer,
    };
    EXPECT_EQ(backend.Operations,
              std::vector<RecordedOperation>(expectedOperations.begin(),
                                             expectedOperations.end()));
    ASSERT_TRUE(backend.TemporalSample.has_value());
    EXPECT_TRUE(backend.TemporalSample->Synthetic);
    EXPECT_EQ(backend.TemporalSample->TargetPresentationRateHz, 60U);
    EXPECT_EQ(backend.TakePicaCompletions(),
              (std::vector<uint64_t>{801U}));
    EXPECT_TRUE(backend.TakePicaCompletions().empty());

    PicaPresentationStateSnapshot snapshot;
    ASSERT_TRUE(backend.CapturePicaPresentationState(snapshot, &error))
        << error;
    ASSERT_TRUE(snapshot.LastPresentedTransfer.has_value());
    EXPECT_EQ(snapshot.LastPresentedTransfer->CompletionId, 801U);
    ASSERT_EQ(snapshot.DisplayImages.size(), 1U);
    EXPECT_EQ(snapshot.DisplayImages.front().ImageWidth, 400U);
    EXPECT_TRUE(snapshot.DisplayImages.front().Initialized);

    ASSERT_TRUE(backend.ResetPicaState(&error)) << error;
    EXPECT_FALSE(backend.Schedule.Valid());
    EXPECT_TRUE(backend.Operations.empty());
    EXPECT_FALSE(backend.TemporalSample.has_value());
    EXPECT_TRUE(backend.Presentation.DisplayImages.empty());
}

} // namespace
} // namespace Fast::Renderer3ds
