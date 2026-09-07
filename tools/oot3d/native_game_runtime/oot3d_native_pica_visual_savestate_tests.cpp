#include "oot3d_native_pica_visual_savestate.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_pica_visual_savestate_tests: "
              << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) Fail(message);
}

} // namespace

int main() {
    using namespace Oot3dNativeGame;
    std::string error;
    Oot3dPicaVulkanDrawPlan draw;
    draw.SubmissionId = 7U;
    draw.CommandListAddress = 0x20001000U;
    draw.CompositionDomain = Oot3dPicaCompositionDomain::Scene;
    draw.Composition = {
        Oot3dPicaCompositionLayer::TransparentWorld,
        Oot3dPicaCompositionProvenance::NativeCmbDrawPass,
        0x0030F4D0U,
        1U,
    };
    draw.VertexShader.StateKey = 11U;
    draw.VertexShader.Source = "vertex";
    draw.FragmentShader.StateKey = 13U;
    draw.FragmentShader.Source = "fragment";
    draw.FragmentFeatures.FogEnabled = true;
    draw.FragmentFeatures.FogFlip = true;
    draw.FragmentFeatures.FogMode = 5U;
    draw.FragmentFeatures.FragmentLightingEnabled = true;
    draw.FragmentFeatures.FragmentLighting.SchemaVersion =
        Oot3d::Renderer::kPicaFragmentLightingLayoutSchemaVersion;
    draw.FragmentFeatures.FragmentLighting.ActiveLightCount = 2U;
    draw.FragmentFeatures.FragmentLighting.LightPermutation[0] = 3U;
    draw.FragmentFeatures.FragmentLighting.LightPermutation[1] = 5U;
    draw.FragmentFeatures.FragmentLighting.Lights[3].Directional = true;
    draw.FragmentFeatures.FragmentLighting.Lights[5]
        .SpotAttenuationEnabled = true;
    draw.FragmentFeatures.FragmentLighting.EnvironmentConfiguration = 5U;
    draw.FragmentFeatures.FragmentLighting.FresnelSelector = 2U;
    draw.FragmentFeatures.FragmentLighting.ShadowFactorEnabled = true;
    draw.FragmentFeatures.FragmentLighting.ShadowPrimary = true;
    draw.FragmentFeatures.FragmentLighting.LutSamplers[0].Input =
        Oot3d::Renderer::PicaFragmentLightingLutInput::LightNormal;
    draw.FragmentFeatures.FragmentLighting.LutSamplers[0].Scale = 4.0F;
    draw.FragmentFeatures.FragmentLighting.LutSamplers[0].AbsoluteInput =
        false;
    draw.VertexCount = 3U;
    draw.GeometryIdentity = 17U;
    draw.GeometryContentVersion = 19U;
    draw.GeometryIdentityAvailable = true;
    draw.FragmentShader.Uniforms.TextureLodBias =
        {-1.0F, 0.5F, 1.25F, 0.0F};
    draw.FragmentShader.Uniforms.Lighting.Diffuse[3][2] = 0.75F;
    draw.FragmentShader.Uniforms.Lighting.Position[5][1] = -12.5F;
    draw.FragmentShader.Uniforms.Lighting.GlobalAmbient =
        {0.125F, 0.25F, 0.5F, 0.0F};
    draw.FragmentShader.Uniforms.ShadowTextureBias = 0x23456;
    draw.FragmentShader.Uniforms.ShadowOrthographic = 1;
    draw.FragmentShader.Uniforms.ShadowBiasConstant = 0.75F;
    draw.FragmentShader.Uniforms.ShadowBiasLinear = 0.125F;
    auto lightingLuts = std::make_shared<Oot3dPicaLightingLutState>();
    lightingLuts->Entry(0U, 0U) = 0x00123ABCU;
    lightingLuts->Entry(23U, 255U) = 0x00FEDCBAU;
    lightingLuts->ContentHash =
        ComputeOot3dPicaLightingLutContentHash(*lightingLuts);
    lightingLuts->ContentHashAvailable = true;
    draw.LightingLuts = lightingLuts;
    Oot3dPicaVulkanTextureBinding texture;
    texture.Slot = 0U;
    texture.State.Enabled = true;
    texture.State.Width = 16U;
    texture.State.Height = 16U;
    texture.State.MipLinear = true;
    texture.State.LodBiasRaw = -256;
    texture.State.MinMipLevel = 1U;
    texture.State.MaxMipLevel = 1U;
    texture.NativeBytes.resize(1280U, 0x5AU);
    Require(ResolveOot3dPicaTextureContentIdentity(texture, &error), error);
    const uint64_t expectedBaseLevelContentHash =
        texture.NativeBaseLevelContentHash;
    draw.Textures.push_back(std::move(texture));
    Oot3dPicaVisualFrame frame;
    frame.Sequence = 5U;
    frame.TopTransfer.CompletionId = 23U;
    frame.TopTransfer.OutputPhysicalAddress = 0x20002000U;
    frame.TopTransfer.Transfer.OutputAddress = 0x20002000U;
    frame.Draws.push_back(draw);
    frame.StrictDrawIdentities.push_back(29U);

    Oot3dPicaVisualReplayState state;
    state.Scheduler.Accumulator.NextSequence = 6U;
    state.Scheduler.Accumulator.PendingDraws.push_back(draw);
    state.Scheduler.SnapshotCompletions[{31U, 0x20002000U}] = 23U;
    state.Continuity.AcceptedDeltas = {0.125, 0.25};
    state.PreviousFrame = frame;
    state.LatestFrame = frame;
    state.LatestFrame->Sequence = 6U;
    state.LatestTransitionContinuous = true;
    state.DisplayTransfersByOutput[0x20002000U] = frame.TopTransfer;
    state.LastSelectedTopTransferCompletionId = 23U;
    state.LastSubmittedDrawId = 7U;

    std::vector<uint8_t> encoded;
    Require(EncodeOot3dPicaVisualReplayState(state, encoded, &error), error);
    Oot3dPicaVisualReplayState decoded;
    Require(DecodeOot3dPicaVisualReplayState(encoded, decoded, &error), error);
    Require(decoded.Scheduler.Accumulator.NextSequence == 6U &&
                decoded.Scheduler.Accumulator.PendingDraws.size() == 1U &&
                decoded.Scheduler.Accumulator.PendingDraws[0].SubmissionId ==
                    7U &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .CompositionDomain ==
                    Oot3dPicaCompositionDomain::Scene &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .Composition.Layer ==
                    Oot3dPicaCompositionLayer::TransparentWorld &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .Composition.Provenance ==
                    Oot3dPicaCompositionProvenance::NativeCmbDrawPass &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .Composition.SourcePc == 0x0030F4D0U &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .Composition.NativeValue == 1U &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .Textures[0].State.MipLinear &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .Textures[0].State.LodBiasRaw == -256 &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .Textures[0].ResolvedNativeBytes().size() == 1280U &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .Textures[0]
                        .NativeBaseLevelContentHashAvailable &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .Textures[0].NativeBaseLevelContentHash ==
                    expectedBaseLevelContentHash &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .FragmentShader.Uniforms.TextureLodBias[2] == 1.25F &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .FragmentShader.Uniforms.Lighting.Diffuse[3][2] ==
                    0.75F &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .FragmentShader.Uniforms.Lighting.Position[5][1] ==
                    -12.5F &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .FragmentShader.Uniforms.Lighting.GlobalAmbient[2] ==
                    0.5F &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .FragmentShader.Uniforms.ShadowTextureBias ==
                    0x23456 &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .FragmentShader.Uniforms.ShadowOrthographic == 1 &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .FragmentShader.Uniforms.ShadowBiasConstant == 0.75F &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .FragmentShader.Uniforms.ShadowBiasLinear == 0.125F &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .FragmentFeatures.FogEnabled &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .FragmentFeatures.FogFlip &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .FragmentFeatures.FogMode == 5U &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .FragmentFeatures.FragmentLighting ==
                    draw.FragmentFeatures.FragmentLighting &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                    .FragmentFeatures.FragmentLighting.Valid() &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .LightingLuts != nullptr &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .LightingLuts->Entry(0U, 0U) == 0x00123ABCU &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .LightingLuts->Entry(23U, 255U) == 0x00FEDCBAU &&
                decoded.Scheduler.Accumulator.PendingDraws[0]
                        .LightingLuts->ContentHash ==
                    lightingLuts->ContentHash &&
                decoded.Scheduler.SnapshotCompletions.size() == 1U &&
                decoded.Continuity.AcceptedDeltas ==
                    std::vector<double>({0.125, 0.25}) &&
                decoded.PreviousFrame.has_value() &&
                decoded.LatestFrame.has_value() &&
                decoded.PreviousFrame->Sequence == 5U &&
                decoded.LatestFrame->Sequence == 6U &&
                decoded.LatestTransitionContinuous &&
                decoded.DisplayTransfersByOutput.size() == 1U &&
                decoded.LastSelectedTopTransferCompletionId == 23U &&
                decoded.LastSubmittedDrawId == 7U,
            "visual replay state did not round-trip");
    Require(decoded.Scheduler.Accumulator.PendingDraws[0].LightingLuts ==
                decoded.PreviousFrame->Draws[0].LightingLuts &&
                decoded.PreviousFrame->Draws[0].LightingLuts ==
                    decoded.LatestFrame->Draws[0].LightingLuts,
            "visual replay duplicated shared lighting LUT state");
    encoded.pop_back();
    Require(!DecodeOot3dPicaVisualReplayState(encoded, decoded, &error),
            "truncated visual replay state was accepted");
    std::cout << "oot3d_native_pica_visual_savestate_tests: ok\n";
    return 0;
}
