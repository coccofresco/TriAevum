#include "oot3d_title_intro_effects.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "oot3d_demo_math.h"

namespace {

ThreeDsRecomp::Oot3d::Vec3f ToNativeVec3(const Vec3& value) {
    return {
        static_cast<float>(value.X),
        static_cast<float>(value.Y),
        static_cast<float>(value.Z),
    };
}

ThreeDsRecomp::Oot3d::Vec3f ToNativeVec3(const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& value) {
    return {
        static_cast<float>(value.X),
        static_cast<float>(value.Y),
        static_cast<float>(value.Z),
    };
}

std::filesystem::path ResolveTitleIntroCodeBinPath(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene) {
    if (!scene.NativePicaLighting.CodeBinPath.empty()) {
        return scene.NativePicaLighting.CodeBinPath;
    }
    if (!scene.PlayerStart.GlobalEntranceCodeBinPath.empty()) {
        return scene.PlayerStart.GlobalEntranceCodeBinPath;
    }
    return {};
}

} // namespace

void InitializeTitleIntroNativeEffects(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    TitleIntroPlayback& playback) {
    playback.NativeEffectSsInitialized = false;
    playback.NativeEffectSsStatus = "native_effect_ss_code_bin_path_missing";
    const auto codeBinPath = ResolveTitleIntroCodeBinPath(scene);
    if (codeBinPath.empty()) {
        return;
    }
    try {
        const auto layout = ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsEuCodeLayout();
        auto dustProfile = ThreeDsRecomp::Oot3d::DecodeOot3dNativeEffectSsDustProfile(
            codeBinPath, playback.RomfsRoot, layout);
        playback.HorseDustEmitterProfile =
            ThreeDsRecomp::Oot3d::DecodeOot3dNativeHorseDustEmitterProfile(codeBinPath, layout);
        ThreeDsRecomp::Oot3d::ResetOot3dNativeEffectSsRuntime(playback.NativeEffects, dustProfile);
        playback.NativeEffectSsInitialized =
            dustProfile.Decoded && dustProfile.TextureLoaded &&
            playback.HorseDustEmitterProfile.Decoded;
        playback.NativeEffectSsStatus = playback.NativeEffectSsInitialized
                                            ? "decoded_native_effect_ss_type0_and_enhorse_emitter"
                                            : "native_effect_ss_decode_incomplete";
    } catch (const std::exception& ex) {
        playback.NativeEffectSsStatus = std::string("native_effect_ss_decode_failed:") + ex.what();
    }
}

void UpdateAndAppendTitleIntroNativeEffects(
    TitleIntroPlayback& playback,
    const TitleIntroActorVisualAppendResult& eponaVisual,
    const Camera& camera,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) {
    if (!playback.NativeEffectSsInitialized) {
        return;
    }
    auto& runtime = playback.NativeEffects;
    const int64_t currentTick = static_cast<int64_t>(std::floor(playback.Frame));
    if (runtime.LastUpdateTick > currentTick) {
        ThreeDsRecomp::Oot3d::ResetOot3dNativeEffectSsRuntime(runtime, runtime.DustProfile);
    }
    if (runtime.LastUpdateTick >= 0 && currentTick > runtime.LastUpdateTick) {
        for (int64_t tick = runtime.LastUpdateTick + 1; tick <= currentTick; ++tick) {
            ThreeDsRecomp::Oot3d::UpdateOot3dNativeEffectSs(runtime);
        }
    }

    if (currentTick != runtime.LastUpdateTick && eponaVisual.HorseDustEmitterInputValid &&
        eponaVisual.PoseFrameValid) {
        const auto* contact = ThreeDsRecomp::Oot3d::Oot3dNativeHorseDustContactAtFrame(
            playback.HorseDustEmitterProfile, eponaVisual.AnimationIndex,
            eponaVisual.PoseFrame);
        if (contact != nullptr) {
            const auto* firstContact = playback.HorseDustEmitterProfile.Contacts.data();
            const size_t contactIndex = static_cast<size_t>(contact - firstContact);
            if (contactIndex < eponaVisual.HorseDustHoofWorldPositions.size() &&
                eponaVisual.HorseDustHoofWorldPositionValid[contactIndex]) {
                auto position = ToNativeVec3(eponaVisual.HorseDustHoofWorldPositions[contactIndex]);
                position.X += ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsNextRandomCentered(
                    runtime, contact->PositionJitter);
                position.Y += ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsNextRandomCentered(
                    runtime, contact->PositionJitter);
                position.Z += ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsNextRandomCentered(
                    runtime, contact->PositionJitter);
                ThreeDsRecomp::Oot3d::SpawnOot3dNativeEffectSsDust(
                    runtime, playback.HorseDustEmitterProfile.DustSpawn, position);
                ++playback.NativeHorseDustSpawnCount;
            }
        }
    }
    runtime.LastUpdateTick = currentTick;

    const Vec3 forward = Normalize(Subtract(camera.Target, camera.Position));
    const Vec3 right = Normalize(Cross(forward, camera.Up));
    const Vec3 billboardUp = Normalize(Cross(right, forward));
    const auto& resolvedLighting = renderScene.PicaLighting.ResolvedRuntimeLightSetting;
    playback.NativeEffectSsRenderEnvironment =
        ThreeDsRecomp::Oot3d::ResolveOot3dNativeEffectSsRenderEnvironment(
            runtime.DustProfile,
            resolvedLighting.Available
                ? resolvedLighting.AmbientColor
                : renderScene.PicaLighting.AmbientColor,
            renderScene.EnvironmentBackground.SkyboxCommandArgument >= 0
                ? static_cast<uint32_t>(renderScene.EnvironmentBackground.SkyboxCommandArgument)
                : 0u);
    auto model = ThreeDsRecomp::Oot3d::MaterializeOot3dNativeEffectSsDust(
        runtime, ToNativeVec3(right), ToNativeVec3(billboardUp),
        &playback.NativeEffectSsRenderEnvironment);
    if (model.Batches.empty()) {
        return;
    }
    ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(renderScene.Bounds, model.Bounds);
    renderScene.ActorVisuals.push_back(std::move(model));
}
