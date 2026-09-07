#include "oot3d_native_pica_visual_frame.h"
#include "oot3d_native_pica_vulkan_plan.h"
#include "fast/renderer3ds/pica_camera_temporal_policy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_pica_vulkan_plan_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

void WriteFloat(std::vector<uint8_t>& bytes, size_t offset, float value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

float ReadFloat(std::span<const uint8_t> bytes, size_t offset) {
    float value = 0.0F;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

uint64_t HashBytes(std::span<const uint8_t> bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (const uint8_t value : bytes) {
        hash = (hash ^ value) * 1099511628211ULL;
    }
    return hash == 0U ? 1U : hash;
}

} // namespace

int main() {
    Oot3dNativeGame::Oot3dPicaShaderState mutableShader;
    mutableShader.Program[0] = 0x88000000U;
    const uint64_t sharedProgramIdentity =
        mutableShader.Program.MutationIdentity();
    const auto shaderSnapshot = mutableShader;
    Require(shaderSnapshot.Program.MutationIdentity() ==
                sharedProgramIdentity &&
                shaderSnapshot.Program.Values()[0] == 0x88000000U,
            "PICA shader snapshot did not share immutable backing storage");
    mutableShader.Program[0] = 0x88000001U;
    Require(mutableShader.Program.MutationIdentity() !=
                sharedProgramIdentity &&
                mutableShader.Program.Values()[0] == 0x88000001U &&
                shaderSnapshot.Program.MutationIdentity() ==
                    sharedProgramIdentity &&
                shaderSnapshot.Program.Values()[0] == 0x88000000U,
            "PICA shader copy-on-write mutation changed an existing snapshot");

    Oot3dNativeGame::Oot3dPicaDrawSubmission submission;
    submission.Id = 9U;
    submission.MinimumVertexIndex = 2U;
    submission.MaximumVertexIndex = 5U;
    submission.Packet.Indexed = true;
    submission.Packet.CommandListAddress = 0x14001200U;
    submission.Packet.CommandListOffsetWords = 0x34U;
    submission.Packet.CompositionDomain =
        Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene;
    submission.Packet.Composition = {
        Oot3dNativeGame::Oot3dPicaCompositionLayer::OpaqueWorld,
        Oot3dNativeGame::Oot3dPicaCompositionProvenance::NativeCmbDrawPass,
        0x0030F4D0U,
        0U,
    };
    submission.Packet.VertexShader.Program[0] = 0x88000000U;
    submission.Packet.VertexShader.ProgramWordCount = 1U;
    submission.Packet.VertexShader.SwizzleWordCount = 1U;
    submission.Packet.Registers[0x04F] = 1U;
    submission.Packet.Registers[0x050] = 0x03020100U;
    submission.Packet.Registers[0x2BD] = 1U;
    submission.Packet.DefaultAttributes[1] = {1.0F, 2.0F, 3.0F, 4.0F};

    auto& state = submission.State;
    state.VertexInput.Indexed = true;
    state.VertexInput.IndicesAre16Bit = false;
    state.VertexInput.VertexCount = 3U;
    state.VertexInput.AttributeCount = 2U;
    state.VertexInput.Attributes[0] = {
        Oot3dNativeGame::Oot3dPicaVertexFormat::Float, 3U, false};
    state.VertexInput.Attributes[1] = {
        Oot3dNativeGame::Oot3dPicaVertexFormat::Float, 4U, true};
    state.VertexInput.Loaders[0].ByteStride = 12U;
    state.VertexInput.Loaders[0].ComponentCount = 1U;
    state.VertexInput.Loaders[0].Components[0] = 0U;
    state.ShaderInterface.OutputMask = 1U;
    state.ShaderInterface.InputRegisterByAttribute[0] = 0U;
    state.ShaderInterface.InputRegisterByAttribute[1] = 2U;

    auto semanticState = state;
    semanticState.VertexInput.AttributeCount = 5U;
    semanticState.VertexInput.Attributes[1].ComponentCount = 3U;
    semanticState.VertexInput.Attributes[2].ComponentCount = 4U;
    semanticState.VertexInput.Attributes[3].ComponentCount = 2U;
    semanticState.VertexInput.Attributes[4].ComponentCount = 2U;
    semanticState.ShaderInterface.InputRegisterByAttribute[0] = 5U;
    semanticState.ShaderInterface.InputRegisterByAttribute[3] = 7U;
    semanticState.ShaderInterface.InputRegisterByAttribute[4] = 9U;
    const auto semantics =
        Oot3dNativeGame::ResolveOot3dCmbVertexSemanticLocations(
            semanticState);
    Require(semantics.Position == 5U && semantics.TexCoord0 == 7U,
            "CMB vertex semantics did not follow the PICA input map");

    Oot3dNativeGame::Oot3dPicaResourceSnapshot indices;
    indices.Kind = Oot3dNativeGame::Oot3dPicaResourceKind::IndexBuffer;
    indices.Bytes = {2U, 5U, 3U};
    indices.ContentVersion = 11U;
    indices.ContentVersionAvailable = true;
    submission.Resources.push_back(indices);
    Oot3dNativeGame::Oot3dPicaResourceSnapshot vertices;
    vertices.Kind = Oot3dNativeGame::Oot3dPicaResourceKind::VertexLoader;
    vertices.Slot = 0U;
    vertices.FirstElement = 2U;
    vertices.Bytes.resize(48U);
    vertices.ContentVersion = 12U;
    vertices.ContentVersionAvailable = true;
    submission.Resources.push_back(vertices);

    std::string error;
    Oot3dNativeGame::Oot3dPicaVulkanShaderSourceCache shaderCache;
    Oot3dNativeGame::Oot3dPicaVulkanDrawPlan plan;
    Require(Oot3dNativeGame::BuildOot3dPicaVulkanDrawPlan(
                submission, plan, &error, &shaderCache),
            error);
    Require(plan.SubmissionId == 9U && plan.CommandListAddress == 0x14001200U &&
                plan.CommandListOffsetWords == 0x34U && plan.Indexed &&
                plan.CompositionDomain ==
                    Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene &&
                plan.Composition.Layer ==
                    Oot3dNativeGame::Oot3dPicaCompositionLayer::OpaqueWorld &&
                plan.Composition.Provenance ==
                    Oot3dNativeGame::Oot3dPicaCompositionProvenance::
                        NativeCmbDrawPass &&
                plan.Composition.SourcePc == 0x0030F4D0U &&
                plan.Composition.NativeValue == 0U &&
                plan.BaseVertex == -2 && plan.VertexCount == 3U &&
                plan.IndexBytes == std::vector<uint8_t>({2U, 5U, 3U}) &&
                plan.VertexBindings.size() == 2U &&
                plan.VertexBindings[0].ByteStride == 12U &&
                plan.VertexBindings[0].Bytes.size() == 48U &&
                plan.VertexBindings[1].InputRate ==
                    Oot3dNativeGame::Oot3dPicaVertexInputRate::PerInstance &&
                plan.VertexBindings[1].Bytes.size() == 32U &&
                plan.VertexAttributes.size() == 16U &&
                plan.VertexAttributes[0].Binding == 0U &&
                plan.VertexAttributes[0].ComponentCount == 3U &&
                plan.VertexAttributes[1].Binding == 1U &&
                plan.VertexAttributes[1].ByteOffset == 0U &&
                plan.VertexAttributes[2].Binding == 1U &&
                plan.VertexAttributes[2].ByteOffset == 16U &&
                plan.GeometryIdentityAvailable && plan.GeometryIdentity != 0U &&
                plan.GeometryContentVersion != 0U &&
                plan.VertexShader.StateKey != 0U &&
                plan.FragmentShader.StateKey != 0U &&
                plan.VertexShader.SourceIdentity ==
                    Oot3d::Renderer::IdentifyPicaShaderSource(
                        plan.ResolvedVertexShaderSource()) &&
                plan.FragmentShader.SourceIdentity ==
                    Oot3d::Renderer::IdentifyPicaShaderSource(
                        plan.ResolvedFragmentShaderSource()) &&
                plan.VertexShader.TemporalProgram != nullptr &&
                plan.VertexShader.TemporalProgram->Hooks.ValidFor(
                    plan.ResolvedVertexShaderSource()) &&
                plan.FragmentShader.Hooks.ValidFor(
                    plan.ResolvedFragmentShaderSource()),
            "native PICA submission did not produce a complete Vulkan plan");

    const std::string vertexSource(plan.ResolvedVertexShaderSource());
    const std::string fragmentSource(plan.ResolvedFragmentShaderSource());
    submission.Packet.VertexShader.BooleanUniforms[3] = true;
    submission.Packet.Registers[0x0C3U] = 0x44332211U;
    Oot3dNativeGame::Oot3dPicaVulkanDrawPlan cachedPlan;
    Require(Oot3dNativeGame::BuildOot3dPicaVulkanDrawPlan(
                submission, cachedPlan, &error, &shaderCache),
            error);
    Require(shaderCache.VertexHits == 1U &&
                shaderCache.VertexMisses == 1U &&
                shaderCache.FragmentHits == 1U &&
                shaderCache.FragmentMisses == 1U &&
                cachedPlan.ResolvedVertexShaderSource() == vertexSource &&
                cachedPlan.ResolvedFragmentShaderSource() == fragmentSource &&
                cachedPlan.VertexShader.SourceIdentity ==
                    plan.VertexShader.SourceIdentity &&
                cachedPlan.FragmentShader.SourceIdentity ==
                    plan.FragmentShader.SourceIdentity &&
                cachedPlan.VertexShader.TemporalProgram != nullptr &&
                cachedPlan.VertexShader.TemporalProgram->Hooks.ValidFor(
                    cachedPlan.ResolvedVertexShaderSource()) &&
                cachedPlan.FragmentShader.Hooks.ValidFor(
                    cachedPlan.ResolvedFragmentShaderSource()) &&
                (cachedPlan.VertexShader.Uniforms.BooleanMask & (1U << 3U)) !=
                    0U &&
                cachedPlan.FragmentShader.Uniforms.TevConstants[0][0] > 0.0F,
            "native PICA shader cache must retain dynamic uniforms");

    const uint64_t cachedProgramIdentity =
        submission.Packet.VertexShader.Program.MutationIdentity();
    const uint32_t unchangedProgramWord =
        submission.Packet.VertexShader.Program.Values()[0];
    submission.Packet.VertexShader.Program[0] = unchangedProgramWord;
    Require(submission.Packet.VertexShader.Program.MutationIdentity() !=
                cachedProgramIdentity,
            "PICA shader mutation did not invalidate structural identity");
    Oot3dNativeGame::Oot3dPicaVulkanDrawPlan invalidatedStructuralPlan;
    Require(Oot3dNativeGame::BuildOot3dPicaVulkanDrawPlan(
                submission, invalidatedStructuralPlan, &error, &shaderCache),
            error);
    Require(shaderCache.VertexStructuralStates.size() == 2U &&
                shaderCache.VertexHits == 2U &&
                shaderCache.VertexMisses == 1U &&
                invalidatedStructuralPlan.VertexShader.StateKey ==
                    cachedPlan.VertexShader.StateKey &&
                invalidatedStructuralPlan.CanonicalIdentity.VertexProgramId ==
                    cachedPlan.CanonicalIdentity.VertexProgramId,
            "PICA structural cache ignored mutation or changed canonical identity");

    auto consumingSubmission = submission;
    consumingSubmission.State.Textures[0].Enabled = true;
    consumingSubmission.State.Textures[0].Width = 16U;
    consumingSubmission.State.Textures[0].Height = 16U;
    consumingSubmission.State.Textures[0].MaxMipLevel = 1U;
    Oot3dNativeGame::Oot3dPicaResourceSnapshot texture;
    texture.Kind = Oot3dNativeGame::Oot3dPicaResourceKind::Texture;
    texture.Slot = 0U;
    texture.Bytes.resize(1280U, 0x22U);
    std::fill_n(texture.Bytes.begin(), 1024U, 0x11U);
    const std::vector<uint8_t> expectedTextureBytes = texture.Bytes;
    const uint64_t expectedResourceHash = HashBytes(texture.Bytes);
    const uint64_t expectedBaseLevelHash =
        HashBytes(std::span<const uint8_t>(texture.Bytes).first(1024U));
    texture.ContentHash = expectedResourceHash;
    texture.ContentHashAvailable = true;
    consumingSubmission.Resources.push_back(texture);
    Oot3dNativeGame::Oot3dPicaVulkanDrawPlan consumingPlan;
    Require(Oot3dNativeGame::
                BuildOot3dPicaVulkanDrawPlanAndConsumeResources(
                    consumingSubmission, consumingPlan, &error,
                    &shaderCache),
            error);
    Require(consumingPlan.IndexBytes ==
                std::vector<uint8_t>({2U, 5U, 3U}) &&
                consumingPlan.VertexBindings[0].Bytes.size() == 48U &&
                consumingPlan.Textures.size() == 1U &&
                consumingPlan.Textures[0].NativeBytes ==
                    expectedTextureBytes &&
                consumingPlan.Textures[0].NativeContentHashAvailable &&
                consumingPlan.Textures[0].NativeContentHash ==
                    expectedResourceHash &&
                consumingPlan.Textures[0]
                    .NativeBaseLevelContentHashAvailable &&
                consumingPlan.Textures[0].NativeBaseLevelContentHash ==
                    expectedBaseLevelHash &&
                expectedResourceHash != expectedBaseLevelHash &&
                std::all_of(consumingSubmission.Resources.begin(),
                            consumingSubmission.Resources.end(),
                            [](const auto& resource) {
                                return resource.Bytes.empty();
                            }),
            "consuming Vulkan plan retained copied resource snapshots");

    Oot3dNativeGame::Oot3dPicaVulkanTextureBinding singleMipTexture;
    singleMipTexture.State.Enabled = true;
    singleMipTexture.State.Width = 8U;
    singleMipTexture.State.Height = 8U;
    singleMipTexture.NativeBytes.resize(256U, 0x5AU);
    singleMipTexture.NativeContentHash = 0x123456789ABCDEF0ULL;
    singleMipTexture.NativeContentHashAvailable = true;
    Require(Oot3dNativeGame::ResolveOot3dPicaTextureContentIdentity(
                singleMipTexture, &error) &&
                singleMipTexture.NativeBaseLevelContentHashAvailable &&
                singleMipTexture.NativeBaseLevelContentHash ==
                    singleMipTexture.NativeContentHash,
            "single-mip texture did not reuse its authoritative payload hash");

    plan.VertexShader.Uniforms.Floats[0][0] = 2.0F;
    plan.VertexShader.Uniforms.Floats[0x59U][0] = 12.0F;
    plan.FragmentShader.Uniforms.FogColor[0] = 0.2F;
    cachedPlan.VertexShader.Uniforms.Floats[0][0] = 6.0F;
    cachedPlan.VertexShader.Uniforms.Floats[0x59U][0] = 24.0F;
    cachedPlan.FragmentShader.Uniforms.FogColor[0] = 0.6F;
    WriteFloat(plan.VertexBindings[0].MutableBytes(), 0U, 2.0F);
    WriteFloat(cachedPlan.VertexBindings[0].MutableBytes(), 0U, 6.0F);
    WriteFloat(plan.VertexBindings[1].MutableBytes(), 16U, 10.0F);
    WriteFloat(cachedPlan.VertexBindings[1].MutableBytes(), 16U, 14.0F);
    Oot3dNativeGame::Oot3dPicaVulkanDrawPlan interpolated;
    Require(Oot3dNativeGame::AreOot3dPicaVisualDrawsCompatible(
                plan, cachedPlan) &&
                Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                    plan, cachedPlan, 0.5F, interpolated),
            "compatible native PICA draws were not interpolated");
    Require(interpolated.VertexShader.Uniforms.Floats[0][0] == 4.0F &&
                interpolated.VertexShader.Uniforms.Floats[0x59U][0] ==
                    24.0F &&
                std::abs(interpolated.FragmentShader.Uniforms.FogColor[0] -
                         0.4F) < 0.00001F &&
                ReadFloat(interpolated.VertexBindings[0].Bytes, 0U) ==
                    4.0F &&
                ReadFloat(interpolated.VertexBindings[1].Bytes, 16U) ==
                    12.0F &&
                interpolated.VertexShader.Uniforms.BooleanMask ==
                    cachedPlan.VertexShader.Uniforms.BooleanMask,
            "visual interpolation did not separate continuous and discrete state");

    auto atlasPrevious = plan;
    auto atlasCurrent = cachedPlan;
    auto atlasProgram = std::make_shared<Oot3dNativeGame::Oot3dPicaTemporalVertexProgram>();
    auto& coordinate = atlasProgram->Hooks.TextureCoordinates[0];
    coordinate.Operation = Fast::Renderer3ds::PicaVertexTextureCoordinateOperation::CmbAffine;
    coordinate.SourceCount = 1;
    coordinate.Sources[0].InputRegister = atlasCurrent.VertexAttributes[0].Location;
    atlasPrevious.VertexShader.TemporalProgram = atlasProgram;
    atlasCurrent.VertexShader.TemporalProgram = atlasProgram;
    for (const float alpha : {0.0F, 1.0F / 3.0F, 0.5F, 2.0F / 3.0F, 1.0F}) {
        Require(Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                    atlasPrevious, atlasCurrent, alpha, interpolated) &&
                    ReadFloat(interpolated.VertexBindings[0].ResolvedBytes(), 0U) == 6.0F &&
                    std::abs(ReadFloat(interpolated.VertexBindings[1].ResolvedBytes(), 16U) -
                             (10.0F + 4.0F * alpha)) < 0.00001F,
                "atlas selectors must remain native while geometry interpolates at x2/x3");
    }

    auto unsignedPrevious = plan;
    auto unsignedCurrent = cachedPlan;
    unsignedPrevious.VertexAttributes[0].Format =
        Oot3dNativeGame::Oot3dPicaVertexFormat::UnsignedByte;
    unsignedCurrent.VertexAttributes[0].Format =
        Oot3dNativeGame::Oot3dPicaVertexFormat::UnsignedByte;
    unsignedPrevious.VertexAttributes[0].ComponentCount = 1U;
    unsignedCurrent.VertexAttributes[0].ComponentCount = 1U;
    unsignedPrevious.VertexBindings[0].MutableBytes()[0] = 7U;
    unsignedCurrent.VertexBindings[0].MutableBytes()[0] = 19U;
    Require(Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                unsignedPrevious, unsignedCurrent, 0.5F, interpolated) &&
                interpolated.VertexBindings[0].Bytes[0] == 19U,
            "discrete unsigned vertex data was interpolated");

    Oot3dNativeGame::Oot3dPicaVisualFrameAccumulator accumulator;
    plan.State.Framebuffer.ColorPhysicalAddress = 0x18000000U;
    cachedPlan.State.Framebuffer.ColorPhysicalAddress = 0x18000000U;
    Oot3dNativeGame::Oot3dPicaMemoryFillSubmission fill;
    fill.StartPhysicalAddress = 0x18000000U;
    fill.EndPhysicalAddress = 0x18010000U;
    fill.Control = 1U;
    accumulator.Append(fill);
    accumulator.Append(plan);
    Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission transfer;
    transfer.CompletionId = 3U;
    transfer.InputPhysicalAddress = 0x18000000U;
    transfer.AfterDrawSubmissionId = plan.SubmissionId;
    accumulator.Append(transfer);
    auto previousFrame = accumulator.Finish(transfer);
    accumulator.Append(fill);
    accumulator.Append(cachedPlan);
    accumulator.Append(transfer);
    auto currentFrame = accumulator.Finish(transfer);
    Require(previousFrame.has_value() && currentFrame.has_value() &&
                previousFrame->Sequence == 1U &&
                currentFrame->Sequence == 2U &&
                accumulator.PendingDrawCount() == 0U,
            "native PICA visual frame boundaries were not retained");
    const auto transition =
        Oot3dNativeGame::AnalyzeOot3dPicaVisualTransition(
            *previousFrame, *currentFrame);
    Require(transition.PreviousDraws == 1U &&
                transition.CurrentDraws == 1U &&
                transition.MatchedDraws == 1U &&
                transition.ChangedContinuousDraws == 1U &&
                transition.ChangedPerInstanceVertexDraws == 1U &&
                transition.ChangedPerVertexDraws == 1U &&
                transition.AmbiguousPreviousDraws == 0U &&
                transition.AmbiguousCurrentDraws == 0U &&
                transition.TopTargetMatchedDraws == 1U &&
                transition.MatchedDrawCoverage == 1.0 &&
                transition.MedianNormalizedContinuousDelta > 0.6 &&
                transition.P90NormalizedContinuousDelta > 0.6,
            "native PICA visual transition coverage is incorrect");
    Oot3dNativeGame::Oot3dPicaVisualContinuityTracker continuity;
    auto smoothTransition = transition;
    smoothTransition.MedianNormalizedContinuousDelta = 0.2;
    Require(continuity.Accept(smoothTransition) &&
                continuity.CurrentThreshold() >= 0.5,
            "smooth native PICA transition was rejected");
    auto fastMotionTransition = transition;
    fastMotionTransition.MedianNormalizedContinuousDelta = 1.2;
    Require(continuity.Accept(fastMotionTransition),
            "structurally continuous native PICA motion was rejected");
    auto cutTransition = transition;
    cutTransition.PreviousDraws = 4U;
    cutTransition.CurrentDraws = 4U;
    cutTransition.MatchedDraws = 1U;
    cutTransition.MatchedDrawCoverage = 0.25;
    cutTransition.MedianNormalizedContinuousDelta = 1.2;
    Oot3dNativeGame::Oot3dPicaVisualContinuityTracker cutContinuity;
    Require(cutContinuity.Accept(smoothTransition) &&
                !cutContinuity.Accept(cutTransition) &&
                cutContinuity.Accept(smoothTransition),
            "native PICA structural discontinuity was interpolated or did not "
            "reset history");
    {
        auto before = *previousFrame, after = *previousFrame;
        before.Draws[0].Composition.Layer = Oot3dNativeGame::Oot3dPicaCompositionLayer::OpaqueWorld;
        auto program = std::make_shared<Oot3dNativeGame::Oot3dPicaTemporalVertexProgram>();
        before.Draws[0].VertexShader.TemporalProgram = program;
        auto& layout = program->Hooks.Transform;
        layout.Operation = Fast::Renderer3ds::PicaVertexTransformOperation::ModelViewProjection3x4;
        layout.ProjectionFirstUniform = 8; layout.ProjectionRowCount = 4;
        layout.ViewFirstUniform = 16; layout.ViewRowCount = 3;
        layout.ModelFirstUniform = 20; layout.ModelRowCount = 3;
        layout.PositionInputRegister = 0; layout.NormalInputRegister = 1;
        auto& floats = before.Draws[0].VertexShader.Uniforms.Floats;
        for (auto& row : floats) row = {};
        floats[8] = {1,0,0,0}; floats[9] = {0,1,0,0};
        floats[10] = {0,0,-1.002F,-20.02F}; floats[11] = {0,0,-1,0};
        floats[16] = {1,0,0,0}; floats[17] = {0,1,0,0}; floats[18] = {0,0,1,0};
        floats[20] = {1,0,0,0}; floats[21] = {0,1,0,0}; floats[22] = {0,0,1,0};
        after = before;
        auto& next = after.Draws[0].VertexShader.Uniforms.Floats;
        next[16][3] = -2;
        auto camera = Fast::Renderer3ds::MeasurePicaCameraTemporalDelta(layout, floats, next);
        Require(camera && !camera->Discontinuous(), "ordinary camera translation was rejected");
        next[16] = {-1,0,0,0}; next[18] = {0,0,-1,0};
        const auto sameSceneCut = Oot3dNativeGame::AnalyzeOot3dPicaVisualTransition(before, after);
        Oot3dNativeGame::Oot3dPicaVisualContinuityTracker cameraTracker;
        Require(sameSceneCut.MatchedDrawCoverage == 1 && sameSceneCut.CameraDiscontinuousDraws == 1 &&
                    !cameraTracker.Accept(sameSceneCut), "same-geometry camera cut was interpolated");
        next = floats;
        next[16][3] = -5000;
        camera = Fast::Renderer3ds::MeasurePicaCameraTemporalDelta(layout, floats, next);
        Require(camera && camera->Discontinuous(), "pure translation camera cut was missed");
        next = floats;
        next[20][3] = -5000;
        camera = Fast::Renderer3ds::MeasurePicaCameraTemporalDelta(layout, floats, next);
        Require(camera && !camera->Discontinuous(), "actor transform was mistaken for a camera cut");
        camera = Fast::Renderer3ds::MeasurePicaRigidViewTemporalDelta(layout, floats, next);
        Require(camera && camera->Discontinuous(), "precomposed rigid view cut was missed");
        next = floats;
        next[20] = {-1,0,0,0}; next[22] = {0,0,-1,0};
        const auto precomposedCut = Oot3dNativeGame::AnalyzeOot3dPicaVisualTransition(before, after);
        Require(precomposedCut.CameraDiscontinuousDraws == 1 && !cameraTracker.Accept(precomposedCut),
                "camera precomposed in model rows was not consumed");
        auto isolatedObject = smoothTransition;
        isolatedObject.CameraMatchedDraws = 12;
        isolatedObject.CameraDiscontinuousDraws = 1;
        Require(cameraTracker.Accept(isolatedObject), "one rigid actor reset the whole camera history");
        next = floats;
        next[16][3] = 1;
        floats[16][3] = -1;
        camera = Fast::Renderer3ds::MeasurePicaCameraTemporalDelta(layout, floats, next);
        Require(camera && !camera->Discontinuous(), "crossing the origin caused a false camera cut");
        floats[16][3] = 0;
        next = floats;
        next[16][3] = -5000;
        const auto fullScale = Fast::Renderer3ds::MeasurePicaRigidViewTemporalDelta(layout, floats, next);
        for (size_t row = 20; row < 23; ++row)
            for (size_t col = 0; col < 3; ++col) { floats[row][col] *= 0.01F; next[row][col] *= 0.01F; }
        const auto smallScale = Fast::Renderer3ds::MeasurePicaRigidViewTemporalDelta(layout, floats, next);
        Require(fullScale && smallScale && std::abs(fullScale->NearPlaneTravel-smallScale->NearPlaneTravel) < 0.01,
                "camera cut policy depends on asset model scale");
        next = floats;
        next[16][0] = 0;
        Require(!Fast::Renderer3ds::MeasurePicaCameraTemporalDelta(layout, floats, next),
                "singular view matrix was accepted");
    }
    Oot3dNativeGame::Oot3dPicaVisualFrameSample sample;
    Require(Oot3dNativeGame::SampleOot3dPicaVisualFrame(
                *previousFrame, *currentFrame, 0.5F, sample) &&
                sample.Draws.size() == 1U && sample.MatchedDraws == 1U &&
                sample.InterpolatedDraws == 1U &&
                sample.InterpolatedPerInstanceVertexDraws == 1U &&
                sample.InterpolatedPerVertexDraws == 1U &&
                sample.MemoryFills.size() == 1U &&
                sample.DisplayTransfers.size() == 1U,
            "native PICA full-frame visual sample is incorrect");
    Oot3dNativeGame::Oot3dPicaVisualTransitionStats combinedTransition;
    Oot3dNativeGame::Oot3dPicaVisualFrameSample combinedSample;
    Require(Oot3dNativeGame::AnalyzeAndSampleOot3dPicaVisualFrame(*previousFrame, *currentFrame, 0.5F,
                                                                  combinedTransition, combinedSample) &&
                combinedTransition.MatchedDraws == transition.MatchedDraws &&
                combinedTransition.ChangedContinuousDraws == transition.ChangedContinuousDraws &&
                combinedTransition.ChangedPerVertexDraws == transition.ChangedPerVertexDraws &&
                combinedSample.MatchedDraws == sample.MatchedDraws &&
                combinedSample.InterpolatedDraws == sample.InterpolatedDraws &&
                combinedSample.Draws[0].VertexShader.Uniforms.Floats[0][0] ==
                    sample.Draws[0].VertexShader.Uniforms.Floats[0][0],
            "combined native PICA analysis changed transition or sample semantics");
    Oot3dNativeGame::Oot3dPicaVisualTransitionStats viewTransition;
    Oot3dNativeGame::Oot3dPicaVisualFrameViewSample viewSample;
    Require(Oot3dNativeGame::AnalyzeAndSampleOot3dPicaVisualFrameView(*previousFrame, *currentFrame, 0.5F,
                                                                      viewTransition, viewSample) &&
                viewSample.Draws.size() == 1U && viewSample.Draws[0].BasePlan == &currentFrame->Draws[0] &&
                viewSample.MemoryFills == &currentFrame->MemoryFills &&
                viewSample.DisplayTransfers == &currentFrame->DisplayTransfers &&
                viewSample.InterpolatedDraws == sample.InterpolatedDraws &&
                viewSample.Draws[0].VertexUniforms.has_value() &&
                viewSample.Draws[0].VertexUniforms->Floats[0][0] ==
                    sample.Draws[0].VertexShader.Uniforms.Floats[0][0] &&
                viewSample.Draws[0].HasVertexBindingOverrides &&
                viewSample.Draws[0].VertexBindings.size() == sample.Draws[0].VertexBindings.size() &&
                viewSample.Draws[0].VertexBindings[0].Bytes == sample.Draws[0].VertexBindings[0].Bytes,
            "non-owning native PICA visual sample changed interpolation semantics");

    Oot3dNativeGame::Oot3dPicaPreparedVisualTransition preparedTransition;
    Oot3dNativeGame::Oot3dPicaVisualInterpolationTiming preparedTiming;
    Require(preparedTransition.Prepare(*previousFrame, *currentFrame,
                                       &preparedTiming) &&
                preparedTransition.Ready() &&
                preparedTransition.Stats().MatchedDraws ==
                    transition.MatchedDraws,
            "prepared visual transition did not retain its analysis");
    const uint64_t preparedMatchingNanoseconds =
        preparedTiming.MatchingNanoseconds;
    const uint64_t preparedAnalysisNanoseconds =
        preparedTiming.AnalysisNanoseconds;
    Oot3dNativeGame::Oot3dPicaVisualFrameViewSample x3FirstSample;
    Oot3dNativeGame::Oot3dPicaVisualFrameViewSample x3SecondSample;
    Require(preparedTransition.Sample(*previousFrame, *currentFrame,
                                      1.0F / 3.0F, x3FirstSample,
                                      &preparedTiming) &&
                preparedTransition.Sample(*previousFrame, *currentFrame,
                                          2.0F / 3.0F, x3SecondSample,
                                          &preparedTiming) &&
                preparedTiming.MatchingNanoseconds ==
                    preparedMatchingNanoseconds &&
                preparedTiming.AnalysisNanoseconds ==
                    preparedAnalysisNanoseconds &&
                x3FirstSample.InterpolatedDraws == 1U &&
                x3SecondSample.InterpolatedDraws == 1U &&
                x3FirstSample.Draws[0].VertexUniforms.has_value() &&
                x3SecondSample.Draws[0].VertexUniforms.has_value() &&
                x3FirstSample.Draws[0].VertexUniforms->Floats[0][0] <
                    x3SecondSample.Draws[0].VertexUniforms->Floats[0][0],
            "x3 visual samples repeated matching or lost temporal order");
    preparedTransition.Reset();
    Require(!preparedTransition.Ready(),
            "prepared visual transition reset retained stale state");

    cachedPlan.CommandListOffsetWords += 1U;
    Require(Oot3dNativeGame::AreOot3dPicaVisualDrawsCompatible(
                plan, cachedPlan),
            "volatile native command-list storage broke draw identity");
    cachedPlan.State.VertexInput.Loaders[0].PhysicalAddress += 0x40U;
    cachedPlan.State.VertexInput.PhysicalBaseAddress += 0x40U;
    cachedPlan.State.VertexInput.IndexPhysicalAddress += 0x40U;
    Require(Oot3dNativeGame::AreOot3dPicaVisualDrawsCompatible(
                plan, cachedPlan) &&
                Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                    plan, cachedPlan, 0.5F, interpolated),
            "rotating native buffer addresses broke structural identity");

    auto repeatedPreviousA = plan;
    auto repeatedPreviousB = plan;
    auto repeatedCurrentA = plan;
    auto repeatedCurrentB = plan;
    repeatedPreviousA.VertexShader.Uniforms.Floats[0][0] = 2.0F;
    repeatedPreviousB.VertexShader.Uniforms.Floats[0][0] = 20.0F;
    repeatedCurrentA.VertexShader.Uniforms.Floats[0][0] = 6.0F;
    repeatedCurrentB.VertexShader.Uniforms.Floats[0][0] = 28.0F;
    Oot3dNativeGame::Oot3dPicaVisualFrame repeatedPreviousFrame;
    repeatedPreviousFrame.TopTransfer = transfer;
    repeatedPreviousFrame.Draws = {repeatedPreviousA, repeatedPreviousB};
    Oot3dNativeGame::Oot3dPicaVisualFrame repeatedCurrentFrame;
    repeatedCurrentFrame.TopTransfer = transfer;
    repeatedCurrentFrame.Draws = {repeatedCurrentA, repeatedCurrentB};
    Oot3dNativeGame::Oot3dPicaVisualFrameSample repeatedSample;
    Require(Oot3dNativeGame::SampleOot3dPicaVisualFrame(
                repeatedPreviousFrame, repeatedCurrentFrame, 0.5F,
                repeatedSample) &&
                repeatedSample.MatchedDraws == 2U &&
                repeatedSample.StrictUniqueMatchedDraws == 0U &&
                repeatedSample.StrictOrdinalMatchedDraws == 2U &&
                repeatedSample.StructuralOrdinalMatchedDraws == 0U &&
                repeatedSample.Draws[0]
                        .VertexShader.Uniforms.Floats[0][0] == 4.0F &&
                repeatedSample.Draws[1]
                        .VertexShader.Uniforms.Floats[0][0] == 24.0F,
            "repeated native draws were not paired by stable occurrence");

    auto relocatedCurrentA = repeatedCurrentA;
    auto relocatedCurrentB = repeatedCurrentB;
    for (auto* relocated : {&relocatedCurrentA, &relocatedCurrentB}) {
        relocated->State.VertexInput.PhysicalBaseAddress += 0x200U;
        relocated->State.VertexInput.IndexPhysicalAddress += 0x200U;
        relocated->State.VertexInput.Loaders[0].PhysicalAddress += 0x200U;
        for (auto& texture : relocated->Textures) {
            texture.State.PhysicalAddress += 0x400U;
        }
    }
    repeatedCurrentFrame.Draws = {relocatedCurrentA, relocatedCurrentB};
    Require(Oot3dNativeGame::SampleOot3dPicaVisualFrame(
                repeatedPreviousFrame, repeatedCurrentFrame, 0.5F,
                repeatedSample) &&
                repeatedSample.MatchedDraws == 2U &&
                repeatedSample.StrictUniqueMatchedDraws == 0U &&
                repeatedSample.StrictOrdinalMatchedDraws == 0U &&
                repeatedSample.StructuralOrdinalMatchedDraws == 2U &&
                repeatedSample.Draws[0]
                        .VertexShader.Uniforms.Floats[0][0] == 4.0F &&
                repeatedSample.Draws[1]
                        .VertexShader.Uniforms.Floats[0][0] == 24.0F,
            "relocated repeated native draws lost visual interpolation");

    auto newlyVisibleDraw = relocatedCurrentB;
    newlyVisibleDraw.VertexShader.Uniforms.Floats[0][0] = 100.0F;
    repeatedCurrentFrame.Draws.push_back(newlyVisibleDraw);
    const auto changedCardinality =
        Oot3dNativeGame::AnalyzeOot3dPicaVisualTransition(
            repeatedPreviousFrame, repeatedCurrentFrame);
    Require(changedCardinality.MatchedDraws == 2U &&
                changedCardinality.StructuralOrdinalMatchedDraws == 2U &&
                changedCardinality.UnmatchedPreviousDraws == 0U &&
                changedCardinality.UnmatchedCurrentDraws == 1U,
            "changed draw cardinality interrupted stable occurrences");

    auto incompatibleGeometry = relocatedCurrentA;
    incompatibleGeometry.VertexCount += 1U;
    Require(!Oot3dNativeGame::AreOot3dPicaVisualDrawsCompatible(
                plan, incompatibleGeometry) &&
                !Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                    plan, incompatibleGeometry, 0.5F, interpolated),
            "different native pipeline layout was treated as one visual draw");

    auto uiDomainDraw = plan;
    uiDomainDraw.CompositionDomain =
        Oot3dNativeGame::Oot3dPicaCompositionDomain::Ui;
    Require(!Oot3dNativeGame::AreOot3dPicaVisualDrawsCompatible(
                plan, uiDomainDraw) &&
                !Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                    plan, uiDomainDraw, 0.5F, interpolated),
            "scene and UI draws shared one interpolation identity");

    auto changedNativeGeometry = relocatedCurrentA;
    changedNativeGeometry.IndexBytes[0] ^= 1U;
    changedNativeGeometry.IndexContentVersionAvailable = false;
    changedNativeGeometry.GeometryIdentityAvailable = false;
    repeatedPreviousFrame.Draws = {repeatedPreviousA};
    repeatedCurrentFrame.Draws = {changedNativeGeometry};
    Require(Oot3dNativeGame::SampleOot3dPicaVisualFrame(
                repeatedPreviousFrame, repeatedCurrentFrame, 0.5F,
                repeatedSample) &&
                repeatedSample.PipelineOrdinalMatchedDraws == 1U &&
                repeatedSample.Draws[0].IndexBytes ==
                    changedNativeGeometry.IndexBytes,
            "discrete native geometry change blocked continuous transforms");

    std::cout << "oot3d_native_pica_vulkan_plan_tests: ok\n";
    return 0;
}
