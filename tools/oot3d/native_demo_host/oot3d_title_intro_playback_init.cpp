#include "oot3d_title_intro_playback_init.h"

#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "oot3d_title_intro_actor_assets.h"
#include "oot3d_title_intro_effects.h"
#include "oot3d_title_intro_camera.h"
#include "oot3d_title_intro_logo_runtime.h"
#include "oot3d_title_intro_opening_resolver.h"

namespace {

nlohmann::json ReadTitleIntroJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not read JSON file: " + path.string());
    }
    return nlohmann::json::parse(file);
}

std::string TitleIntroJsonNestedString(const nlohmann::json& object, std::initializer_list<const char*> keys) {
    const nlohmann::json* cursor = &object;
    for (const char* key : keys) {
        if (!cursor->is_object() || !cursor->contains(key)) {
            return "";
        }
        cursor = &cursor->at(key);
    }
    return cursor->is_string() ? cursor->get<std::string>() : "";
}

std::filesystem::path ResolveTitleIntroManifestRelativePath(const std::filesystem::path& manifestPath,
                                                            const std::filesystem::path& value) {
    if (value.empty() || value.is_absolute()) {
        return value;
    }
    return manifestPath.parent_path() / value;
}

const Oot3dSceneCutsceneActorResource* FindCutsceneActorResource(
    const Oot3dSceneCutsceneProgramSnapshot* program,
    uint16_t actorKind) {
    if (program == nullptr || program->valid == 0u) {
        return nullptr;
    }
    for (uint16_t i = 0; i < program->actorResourceCount; ++i) {
        if (program->actorResources[i].valid != 0u &&
            program->actorResources[i].actorKind == actorKind) {
            return &program->actorResources[i];
        }
    }
    return nullptr;
}

} // namespace

std::filesystem::path ResolveTitleIntroRomfsRoot(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene) {
    if (!args.TitleIntroRomfsRoot.empty()) {
        return args.TitleIntroRomfsRoot;
    }

    const auto manifest = ReadTitleIntroJsonFile(args.ManifestPath);
    const auto manifestRoot = TitleIntroJsonNestedString(manifest, { "sources", "title_intro", "romfs_root" });
    if (!manifestRoot.empty()) {
        return ResolveTitleIntroManifestRelativePath(args.ManifestPath, manifestRoot);
    }

    const auto roomParent = scene.RoomZsiPath.parent_path();
    if (!roomParent.empty() && roomParent.filename().string() == "scene") {
        return roomParent.parent_path();
    }

    throw std::runtime_error("title intro ROMFS root was not provided and could not be derived from the room ZSI path");
}

TitleIntroPlayback InitialTitleIntroPlayback(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene) {
    const auto romfsRoot = ResolveTitleIntroRomfsRoot(args, scene);
    const double nativeCmbToSceneScale = scene.LinkScale > 0.0 ? scene.LinkScale : 1.0;
    Oot3dSceneCutsceneProgramSnapshot cutsceneProgram{};
    Oot3dSceneCutsceneFrameRuntimeStatus programStatus =
        OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_UNINITIALIZED;
    if (args.CutscenePlayerRequest && args.TitleIntroPlayback) {
        const Oot3dSceneCutsceneFrameKey key{
            static_cast<uint8_t>(args.CutscenePlayerSceneId),
            args.CutscenePlayerSetupIndex,
            args.CutscenePlayerSourceIndex,
        };
        programStatus = Oot3d_SceneCutsceneFrameRuntimeBuildProgram(key, &cutsceneProgram);
        if (programStatus != OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_OK) {
            throw std::runtime_error(
                "failed to build native cutscene program: " +
                std::string(Oot3d_SceneCutsceneFrameRuntimeStatusName(programStatus)));
        }
    }
    auto playback = InitializeTitleIntroPlaybackFromNativeSources(
        args.TitleIntroPlayback,
        args.TitleIntroQdbIndexOverride,
        args.TitleIntroQdbIndex,
        args.TitleIntroFrame,
        romfsRoot,
        nativeCmbToSceneScale,
        args.CutscenePlayerRequest && args.TitleIntroPlayback ? &cutsceneProgram : nullptr);
    playback.GenericCutsceneRuntimeRequested =
        args.CutscenePlayerRequest && args.TitleIntroPlayback;
    playback.GenericCutsceneKey.sceneId = static_cast<uint8_t>(args.CutscenePlayerSceneId);
    playback.GenericCutsceneKey.setupIndex = args.CutscenePlayerSetupIndex;
    playback.GenericCutsceneKey.cutsceneSourceIndex = args.CutscenePlayerSourceIndex;
    playback.GenericCutsceneProgramStatus = programStatus;
    playback.GenericCutsceneProgram = cutsceneProgram;
    InitializeTitleIntroNativeEffects(scene, playback);
    return playback;
}

int ActiveSceneSetupOverrideForArgs(const Args& args) {
    if (args.NativeSceneSetupOverride >= 0) {
        return args.NativeSceneSetupOverride;
    }
    if (args.CutscenePlayerRequest) {
        return args.CutscenePlayerSetupIndex;
    }
    return ResolveTitleIntroInitialSceneSetupOverride(args.TitleIntroPlayback);
}

std::string_view ActiveSceneSetupSourceForArgs(const Args& args) {
    if (args.NativeSceneSetupOverride >= 0) {
        return args.NativeSceneSetupSource.empty()
                   ? std::string_view("native_game_semantic_route")
                   : std::string_view(args.NativeSceneSetupSource);
    }
    if (args.CutscenePlayerRequest) {
        return "native_cutscene_player_tuple";
    }
    return ResolveTitleIntroInitialSceneSetupSource(args.TitleIntroPlayback);
}

TitleIntroPlayback InitializeTitleIntroPlaybackFromNativeSources(
    bool enabled,
    bool qdbIndexOverride,
    uint16_t qdbIndexOverrideValue,
    double startFrame,
    const std::filesystem::path& romfsRoot,
    double fallbackCmbToSceneScale,
    const Oot3dSceneCutsceneProgramSnapshot* cutsceneProgram) {
    TitleIntroPlayback playback;
    playback.Enabled = enabled;
    playback.QdbIndexOverrideApplied = qdbIndexOverride;
    playback.StartFrame = startFrame;
    playback.Frame = startFrame;
    if (!playback.Enabled) {
        return playback;
    }

    playback.OpeningOrchestrationRow = ResolveTitleIntroOpeningOrchestrationFromNativeTable(
        playback.OpeningOrchestrationIndex,
        playback.OpeningOrchestrationStatus);
    playback.InitialSceneCutsceneRow = FindTitleIntroInitialSceneCutsceneRowForOpeningOrchestration(
        playback.OpeningOrchestrationRow,
        playback.InitialSceneCameraStatus);
    if (playback.InitialSceneCutsceneRow != nullptr) {
        playback.InitialSceneCutsceneSourceIndex = playback.InitialSceneCutsceneRow->cutsceneSourceIndex;
    }
    playback.QdbRow = ResolveTitleIntroOpeningQdbRow(
        playback.OpeningOrchestrationRow,
        qdbIndexOverride,
        qdbIndexOverrideValue,
        playback.QdbIndex,
        playback.QdbStatus);
    if (playback.QdbRow == nullptr) {
        throw std::runtime_error("title intro QDB index was not decoded from native opening route: " +
                                 std::to_string(playback.QdbIndex) + " status=" + playback.QdbStatus);
    }
    playback.RomfsRoot = romfsRoot;
    std::string linkBindingStatus;
    std::string eponaBindingStatus;
    std::string logoBindingStatus;
    const auto* linkBinding = ResolveTitleIntroOpeningActorBinding(
        playback.OpeningOrchestrationRow,
        OOT3D_TITLE_INTRO_OPENING_ACTOR_KIND_LINK_ADULT,
        linkBindingStatus);
    const auto* eponaBinding = ResolveTitleIntroOpeningActorBinding(
        playback.OpeningOrchestrationRow,
        OOT3D_TITLE_INTRO_OPENING_ACTOR_KIND_EPONA,
        eponaBindingStatus);
    const auto* logoBinding = ResolveTitleIntroOpeningActorBinding(
        playback.OpeningOrchestrationRow,
        OOT3D_TITLE_INTRO_OPENING_ACTOR_KIND_TITLE_LOGO,
        logoBindingStatus);
    const auto* linkResource = FindCutsceneActorResource(
        cutsceneProgram, OOT3D_TITLE_INTRO_OPENING_ACTOR_KIND_LINK_ADULT);
    const auto* eponaResource = FindCutsceneActorResource(
        cutsceneProgram, OOT3D_TITLE_INTRO_OPENING_ACTOR_KIND_EPONA);
    if (cutsceneProgram != nullptr && (linkResource == nullptr || eponaResource == nullptr)) {
        throw std::runtime_error(
            "native cutscene program is missing required actor resources: link=" +
            std::string(linkResource != nullptr ? "present" : "missing") +
            " epona=" + std::string(eponaResource != nullptr ? "present" : "missing"));
    }
    playback.LinkActor = linkResource != nullptr
                             ? LoadTitleIntroNativeActorFromCutsceneResource(
                                   playback.RomfsRoot, *linkResource)
                             : LoadTitleIntroNativeActorFromOpeningBinding(
                                   playback.RomfsRoot, linkBinding, linkBindingStatus);
    playback.EponaActor = eponaResource != nullptr
                              ? LoadTitleIntroNativeActorFromCutsceneResource(
                                    playback.RomfsRoot, *eponaResource)
                              : LoadTitleIntroNativeActorFromOpeningBinding(
                                    playback.RomfsRoot, eponaBinding, eponaBindingStatus);
    playback.LogoRuntime = LoadTitleIntroLogoRuntime(
        playback.RomfsRoot,
        playback.OpeningOrchestrationRow,
        logoBinding,
        logoBindingStatus);
    playback.LogoRuntime.AlphaState = SimulateTitleIntroLogoAlphaState(playback.LogoRuntime, playback.Frame);
    playback.LogoRuntime.AlphaReferenceSamples =
        BuildTitleIntroLogoAlphaReferenceSamples(playback.LogoRuntime);
    const auto* linkScaleRow =
        linkBinding != nullptr ? Oot3d_TitleIntroOpeningActorRuntimeGetScaleSourceRow(linkBinding->actorBindingIndex)
                               : nullptr;
    const auto* eponaScaleRow =
        eponaBinding != nullptr ? Oot3d_TitleIntroOpeningActorRuntimeGetScaleSourceRow(eponaBinding->actorBindingIndex)
                                : nullptr;
    if (linkResource == nullptr) {
        ApplyTitleIntroNativeActorScale(playback.LinkActor, linkScaleRow, fallbackCmbToSceneScale);
    }
    if (eponaResource == nullptr) {
        ApplyTitleIntroNativeActorScale(playback.EponaActor, eponaScaleRow, fallbackCmbToSceneScale);
    }
    playback.Initialized = playback.OpeningOrchestrationRow != nullptr &&
                           playback.LinkActor.Loaded && playback.EponaActor.Loaded && playback.LogoRuntime.Loaded;
    playback.Status = Oot3d_TitleIntroOpeningResolverPlaybackStatus(playback.Initialized ? 1u : 0u);
    if (!playback.Initialized) {
        throw std::runtime_error("failed to initialize native title intro actors: orchestration=" +
                                 playback.OpeningOrchestrationStatus + ", link_binding=" +
                                 playback.LinkActor.OpeningActorBindingStatus + ", link=" +
                                 playback.LinkActor.Status + ", epona_binding=" +
                                 playback.EponaActor.OpeningActorBindingStatus + ", epona=" +
                                 playback.EponaActor.Status +
                                 ", logo_binding=" + playback.LogoRuntime.OpeningActorBindingStatus +
                                 ", logo=" + playback.LogoRuntime.Status);
    }
    return playback;
}
