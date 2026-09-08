#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "oot3d_demo_host_types.h"
#include "oot3d_native_control_config.h"
#include "oot3d_native_frame_rate.h"
#include "oot3d_link_runtime_types.h"
#include "oot3d_top_screen_config.h"
#include "oot3d_top_screen_mod_profile.h"

struct Oot3dNativeGameLaunch {
    Args Host;
    std::filesystem::path AssetCatalogPath;
    std::filesystem::path PlayablePackPath;
    std::filesystem::path RouteCatalogPath;
    std::filesystem::path PlayerAnimationContractPath;
    std::filesystem::path PlayerRuntimeSemanticsPath;
    std::filesystem::path PlayerCollisionActionContractPath;
    std::filesystem::path ActorCoreContractPath;
    std::filesystem::path ActorShardPath;
    std::filesystem::path RoomCompilationUnitPath;
    std::filesystem::path NativeClosureManifestPath;
    std::filesystem::path A32ProcessManifestPath;
    std::filesystem::path TopScreenTextureOverridePackPath;
    std::filesystem::path TopScreenConfigPath;
    std::filesystem::path ControlConfigPath;
    std::filesystem::path SaveDataDirectory;
    std::filesystem::path AudioPcmDumpPath;
    std::filesystem::path A32BlockTracePath;
    std::filesystem::path PicaSemanticTracePath;
    std::filesystem::path PicaAotShaderPackPath;
    std::filesystem::path PicaEffectiveShaderInventoryPath;
    std::filesystem::path PicaPipelineInventoryPath;
    std::filesystem::path PicaPipelineManifestPath;
    std::filesystem::path ScenarioCatalogPath;
    std::filesystem::path LoadStatePath;
    std::filesystem::path QuickStatePath;
    std::filesystem::path SaveStatePath;
    std::string RouteId;
    std::string ScenarioId;
    Oot3dNativeGame::Oot3dUiProfile UiProfile =
        Oot3dNativeGame::Oot3dUiProfile::Oot3d;
    Oot3dNativeGame::TopScreenUiConfig TopScreenConfig;
    Oot3dNativeGame::NativeControlConfig ControlConfig =
        Oot3dNativeGame::NativeControlDefaults();
    std::vector<int32_t> RoomRequestSmokeSequence;
    bool ValidateOnly = false;
    bool ExtendedDiagnostics = false;
    Oot3dNativeGame::GameplayTimingMode GameplayTiming =
        Oot3dNativeGame::GameplayTimingMode::Native30Interpolated;
    // Legacy aliases retained for existing launch scripts. Parsing resolves
    // them into GameplayTiming before the runtime starts.
    bool DisableVisualInterpolation = false;
    uint32_t SimulationRateHz =
        Oot3dNativeGame::kOot3dOriginalSimulationRateHz;
    // Zero selects presentation as fast as the active swapchain permits.
    uint32_t PresentationRateHz = 60U;
    bool ProfileA32Blocks = false;
    bool ProfileA32Runtime = false;
    bool PicaAotShaderStrict = false;
    bool PicaPipelinePrewarm = false;
    bool ScenarioStrict = false;
    bool ScenarioAutoExit = false;
    bool DisableCompiledFunctions = false;
    bool DisableTypedGameplay = false;
    bool EnableSourceGameplayProfile = false;
    bool EnableSourceActorInitContext = false;
    bool EnableSourceActorUpdateAll = false;
    bool EnableSourceCutsceneUpdateFrame = false;
    bool EnableSourceCutsceneProcessCommands = false;
    bool EnableSourceCameraUpdate = false;
    bool EnableSourcePlayerUpdate = false;
    bool EnableSourcePlayerUpdateCommon = false;
    bool EnableSourceCsabCurves = false;
    bool DisableManualCompiledFunctions = false;
    bool DisableTrueAotBlocks = false;
    bool DisableWholeAot = false;
    bool DisableOpenGlPicaGeometryCache = false;
#if defined(__SWITCH__)
    // Bound the native C++ guest call chain on Horizon's smaller homebrew
    // main stack. The command-line override is also available on desktop so
    // the exact Switch dispatch policy can be benchmarked before packaging.
    uint32_t WholeAotBlockBudget = 128U;
#else
    uint32_t WholeAotBlockBudget = 1'000'000U;
#endif
    // Whole-AOT is both faster and complete for the current source closure.
    // Keep the older region-based mass-AOT available only for diagnostics.
    bool DisableMassAot = true;
    bool DisableAudio = false;
    uint32_t SaveStateFrame = 0;
    bool SaveStateFrameAvailable = false;
    LinkNativeCollisionActionConfig PlayerCollisionActionConfig;
    bool PlayerCollisionActionConfigAvailable = false;
    LinkNativePlayerActionConfig PlayerActionConfig;
    bool PlayerActionConfigAvailable = false;
};

struct Oot3dNativeGameBootstrap {
    std::string RouteId;
    std::string SemanticKey;
    std::string VariantKey;
    std::string NativeAssetId;
    std::string NativeScenePath;
    std::filesystem::path NativeCodeBinPath;
    std::filesystem::path NativeAudioArchivePath;
    std::filesystem::path NativeStreamArchivePath;
    uint32_t NativeAudioSampleRate = 0;
    uint32_t NativeAudioFrameSamples = 0;
    std::string NativeAudioClockSource;
    int32_t NativeSceneId = -1;
    int32_t NativeSetupIndex = -1;
    int32_t NativeGlobalEntranceIndex = -1;
    int32_t NativeLocalEntranceIndex = -1;
    std::vector<std::string> ManifestSourceKinds;
    uint32_t AssetCatalogRecordCount = 0;
    std::string SceneShardName;
    std::filesystem::path CoreArchive;
    std::vector<std::filesystem::path> SceneShardArchives;
    uint32_t RouteCatalogRecordCount = 0;
    uint32_t PlayerAnimationGroupCount = 0;
    uint32_t PlayerAnimationTypeCount = 0;
    uint32_t PlayerAnimationCatalogUniqueCsabCount = 0;
    uint32_t PlayerAnimationResidentBindingCount = 0;
    std::vector<uint32_t> PlayerAnimationTypeCandidates;
    std::string PlayerAnimationSelectionSource;
    std::string PlayerPoseSamplingSource;
    std::string PlayerCollisionActionCodeBinSha256;
    std::string ActorCoreSnapshotId;
    std::string ActorCoreSourceRevision;
    uint32_t ActorCategoryListCount = 0;
    uint32_t ActorSpawnTotalGuardValue = 0;
    std::string RoomCompilationUnitId;
    std::string RoomCompilationPayloadSha256;
    std::string RoomCompilationCodeBinSha256;
    uint32_t RoomCompilationRoomCount = 0;
    uint32_t RoomCompilationActorInstanceCount = 0;
    uint32_t RoomCompilationActorProfileCount = 0;
    uint32_t RoomCompilationObjectDependencyCount = 0;
    std::string RoomLifecycleStatus;
    std::string RoomLifecycleSourceSnapshotId;
    std::string NativeClosureSourceRevision;
    uint32_t NativeClosureFunctionCount = 0;
    uint32_t NativeClosureExtractedBodyCount = 0;
    uint32_t NativeClosureAotBoundFunctionCount = 0;
    bool NativeClosureAotAvailable = false;
};

bool ParseOot3dNativeGameArgs(int argc, char** argv, Oot3dNativeGameLaunch& launch);
void PrintOot3dNativeGameUsage();

Oot3dNativeGameBootstrap ResolveOot3dNativeGameBootstrap(
    Oot3dNativeGameLaunch& launch);

nlohmann::json Oot3dNativeGameBootstrapToJson(
    const Oot3dNativeGameLaunch& launch,
    const Oot3dNativeGameBootstrap& bootstrap);

void WriteOot3dNativeGameBootstrapJson(
    const std::filesystem::path& path,
    const nlohmann::json& document);
