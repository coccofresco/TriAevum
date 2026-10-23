#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "oot3d_demo_host_types.h"
#include "oot3d_native_control_config.h"
#include "oot3d_native_frame_rate.h"
#include "oot3d_top_screen_config.h"
#include "oot3d_top_screen_mod_profile.h"

struct Oot3dNativeGameLaunch {
    Args Host;
    std::filesystem::path A32ProcessManifestPath;
    std::filesystem::path TopScreenTextureOverridePackPath;
    std::filesystem::path TopScreenConfigPath;
    std::filesystem::path ControlConfigPath;
    std::filesystem::path SaveDataDirectory;
    std::filesystem::path AudioPcmDumpPath;
    std::filesystem::path A32BlockTracePath;
    std::filesystem::path PicaSemanticTracePath;
    std::filesystem::path PicaAotShaderPackPath;
    std::filesystem::path RendererCacheDirectory;
    // The installer's local water texture catalog (water_textures.txt).
    std::filesystem::path WaterTextureCatalogPath;
    // The installer's local scene light catalog (scene_lights.json).
    std::filesystem::path SceneLightCatalogPath;
    std::filesystem::path PicaEffectiveShaderInventoryPath;
    std::filesystem::path PicaPipelineInventoryPath;
    std::filesystem::path PicaPipelineManifestPath;
    std::filesystem::path ScenarioCatalogPath;
    std::filesystem::path LoadStatePath;
    std::filesystem::path QuickStatePath;
    std::filesystem::path SaveStatePath;
    // --convert-state: re-save the --load-state moment in the current format
    // before any guest frame runs, keeping its stored settings, then exit.
    std::filesystem::path ConvertStatePath;
    // The command-line forms of Shift+F8 / Shift+F5.
    bool LoadStateWithSettings = false;
    bool SaveStateWithSettings = false;
    std::string ScenarioId;
    Oot3dNativeGame::Oot3dUiProfile UiProfile =
        Oot3dNativeGame::Oot3dUiProfile::Oot3d;
    Oot3dNativeGame::TopScreenUiConfig TopScreenConfig;
    Oot3dNativeGame::NativeControlConfig ControlConfig =
        Oot3dNativeGame::NativeControlDefaults();
    bool ExtendedDiagnostics = false;
    // Presents frame N on a render thread while the guest simulates N+1.
    // --serial-presentation restores the single-threaded loop.
    bool ParallelPresentation = true;
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
    bool PicaParametricTev = false;
    bool PicaPipelinePrewarm = false;
    bool ScenarioStrict = false;
    bool ScenarioAutoExit = false;
    bool DisableCompiledFunctions = false;
    bool DisableTypedGameplay = false;
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
    // Scripted F5/F8 requests (--savestate-action FRAME:ACTION[:PATH]): the
    // same mailbox the hotkeys and the F1 tab use, at a zero-based run frame.
    struct ScriptedSavestateAction {
        uint32_t RunFrame = 0;
        bool Load = false;
        bool WithSettings = false;
        std::filesystem::path Path;
    };
    std::vector<ScriptedSavestateAction> SavestateActions;
};

bool ParseOot3dNativeGameArgs(int argc, char** argv, Oot3dNativeGameLaunch& launch);
void PrintOot3dNativeGameUsage();
