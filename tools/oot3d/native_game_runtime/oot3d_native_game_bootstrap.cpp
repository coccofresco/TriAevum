#include "oot3d_native_game_bootstrap.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

#include "fast/Fast3dWindow.h"
#include "ship/Context.h"

#include "oot3d_native_frame_rate.h"

namespace {

uint32_t ParseU32(const std::string& value, const char* name) {
    size_t parsed = 0;
    const unsigned long out = std::stoul(value, &parsed, 10);
    if (parsed != value.size() || out > UINT32_MAX) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + value);
    }
    return static_cast<uint32_t>(out);
}

double ParseDouble(const std::string& value, const char* name) {
    size_t parsed = 0;
    const double out = std::stod(value, &parsed);
    if (parsed != value.size() || !std::isfinite(out)) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + value);
    }
    return out;
}

nlohmann::json ReadJson(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("could not open JSON input: " + path.string());
    }
    nlohmann::json document;
    stream >> document;
    return document;
}

} // namespace

void PrintOot3dNativeGameUsage() {
    std::cerr << "usage: oot3d_native_game [--launch-profile <profile.json>] "
                 "[overrides]\n"
                 "       oot3d_native_game --a32-process-manifest <manifest.json> "
                 "--resource-root <directory> "
                 "[--ui-profile <oot3d|topscreen>] "
                 "[--topscreen-texture-overrides <textures.o3tu>] "
                 "[--topscreen-link-animations <zelda_link_ext.zar>] "
                 "[--topscreen-ui-atlas-hd <ui_atlas_hd.png>] "
                 "[--topscreen-config <topscreen_ui.json>] "
                 "[--romfs-overlay <romfs_overlay.o3ro>] [--ui-texture-pack <directory>] "
                 "[--controls-config <oot3d_controls.json>] "
                 "[--save-data <directory>] "
                 "[--output <summary.json>] [--screenshot "
                 "<capture.bmp>] "
                 "[--screenshot-sequence] [--screenshot-interval <frames>] "
                 "[--screenshot-start-frame <frame>] "
                 "[--extended-diagnostics] [--serial-presentation] "
                 "[--gameplay-timing <native30_interpolated|"
                 "native30_no_interpolation|enhanced60>] "
                 "[--simulation-rate <hz>] "
                 "[--presentation-rate <hz|free>] "
                 "[--disable-visual-interpolation] "
                 "[--profile-a32-blocks] "
                 "[--trace-a32-blocks <trace.jsonl>] "
                 "[--pica-semantic-trace <trace.jsonl>] [--pica-parametric-tev] "
                 "[--pica-aot-shader-pack <pack.o3ps>] "
                 "[--renderer-cache-directory <directory>] "
                 "[--water-texture-catalog <water_textures.txt>] "
                 "[--scene-light-catalog <scene_lights.json>] "
                 "[--pica-aot-shader-strict] "
                 "[--pica-effective-shader-inventory <inventory.json>] "
                 "[--pica-pipeline-inventory <inventory.json>] "
                 "[--pica-pipeline-manifest <manifest.json>] "
                 "[--pica-pipeline-prewarm] "
                 "[--scenario-catalog <catalog.json> --scenario <id>] "
                 "[--scenario-strict] [--scenario-auto-exit] "
                  "[--profile-a32-runtime] "
                  "[--disable-compiled-functions] "
                  "[--disable-typed-gameplay] "
                  "[--disable-manual-compiled-functions] "
                 "[--disable-true-aot-blocks] "
                 "[--disable-whole-aot] "
                 "[--whole-aot-block-budget <blocks>] "
                 "[--disable-opengl-pica-geometry-cache] "
                 "[--enable-mass-aot|--disable-mass-aot] "
                 "[--disable-audio] "
                 "[--audio-pcm-dump <capture.wav>] "
                 "[--load-state <checkpoint.oot3dsav> [--load-state-with-settings]] "
                 "[--quick-state <checkpoint.oot3dsav>] "
                 "[--save-state <checkpoint.oot3dsav> [--save-state-with-settings] "
                 "--save-state-frame <frame>] "
                 "[--convert-state <new.oot3dsav>] "
                 "[--renderer <nri|opengl>] "
                 "[--config <oot3d_native_game.json>] "
                 "[--frames <count>] [--benchmark-warmup-frames <count>] "
                 "[--throughput-benchmark] "
                 "[--max-seconds <seconds>] "
                 "[--fixed-delta-seconds <seconds>] "
                 "[--input-timeline <timeline.json>] "
                 "[--graphics-settings-schedule <schedule.json>] [--width "
                 "<pixels>] [--height <pixels>]\n";
}

bool ParseOot3dNativeGameArgs(int argc, char** argv, Oot3dNativeGameLaunch& launch) {
    // The window title: the Ocarina of Time 3D project of TriAevum (UTF-8).
    launch.Host.ApplicationName = "TriAevum \xE2\x80\x94 Echoes of Time";
    launch.Host.ApplicationId = "oot3d_native_game";
    launch.Host.ConfigurationPath = "oot3d_native_game.json";
    launch.Host.BackendId = 2;
    bool gameplayTimingExplicit = false;
    bool simulationRateExplicit = false;
    bool disableVisualInterpolationExplicit = false;

    for (int index = 1; index < argc; ++index) {
        const std::string arg(argv[index]);
        if (arg == "--config" && index + 1 < argc) {
            launch.Host.ConfigurationPath =
                std::filesystem::absolute(argv[++index]).string();
        } else if (arg == "--resource-root" && index + 1 < argc) {
            launch.Host.ResourceRoot = argv[++index];
        } else if (arg == "--a32-process-manifest" && index + 1 < argc) {
            launch.A32ProcessManifestPath = argv[++index];
        } else if (arg == "--ui-profile" && index + 1 < argc) {
            if (!Oot3dNativeGame::ParseOot3dUiProfile(
                    argv[++index], &launch.UiProfile)) {
                return false;
            }
        } else if (arg == "--topscreen-texture-overrides" &&
                   index + 1 < argc) {
            launch.TopScreenTextureOverridePackPath = argv[++index];
        } else if (arg == "--topscreen-link-animations" && index + 1 < argc) {
            launch.TopScreenLinkAnimationsPath = argv[++index];
        } else if (arg == "--topscreen-ui-atlas-hd" && index + 1 < argc) {
            launch.TopScreenUiAtlasHdPath = argv[++index];
        } else if (arg == "--topscreen-config" && index + 1 < argc) {
            launch.TopScreenConfigPath = argv[++index];
        } else if (arg == "--ui-texture-pack" && index + 1 < argc) {
            launch.UiTexturePackDirectory =
                std::filesystem::absolute(std::filesystem::u8path(argv[++index])).lexically_normal();
        } else if (arg == "--romfs-overlay" && index + 1 < argc) {
            launch.RomFsOverlayPath =
                std::filesystem::absolute(std::filesystem::u8path(argv[++index])).lexically_normal();
        } else if (arg == "--controls-config" && index + 1 < argc) {
#if defined(__SWITCH__)
            // devoptab paths (sdmc:/...) are absolute to Horizon even though
            // std::filesystem classifies them as relative POSIX paths.
            launch.ControlConfigPath =
                std::filesystem::path(argv[++index]).lexically_normal();
#else
            launch.ControlConfigPath =
                std::filesystem::absolute(argv[++index]).lexically_normal();
#endif
        } else if (arg == "--save-data" && index + 1 < argc) {
            launch.SaveDataDirectory = argv[++index];
        } else if (arg == "--renderer" && index + 1 < argc) {
            launch.Host.Renderer = argv[++index];
        } else if (arg == "--extended-diagnostics") {
            launch.ExtendedDiagnostics = true;
        } else if (arg == "--parallel-presentation") {
            launch.ParallelPresentation = true;
        } else if (arg == "--serial-presentation") {
            launch.ParallelPresentation = false;
        } else if (arg == "--gameplay-timing" && index + 1 < argc) {
            if (!Oot3dNativeGame::ParseGameplayTimingMode(
                    argv[++index], &launch.GameplayTiming)) {
                throw std::runtime_error(
                    "unknown OOT3D gameplay timing mode");
            }
            gameplayTimingExplicit = true;
        } else if (arg == "--simulation-rate" && index + 1 < argc) {
            launch.SimulationRateHz =
                ParseU32(argv[++index], "simulation rate");
            if (launch.SimulationRateHz == 0U) {
                throw std::runtime_error("simulation rate must be positive");
            }
            simulationRateExplicit = true;
        } else if (arg == "--presentation-rate" && index + 1 < argc) {
            const std::string value(argv[++index]);
            launch.PresentationRateHz =
                value == "free" ? 0U
                                : ParseU32(value, "presentation rate");
            if (launch.PresentationRateHz != 0U &&
                launch.PresentationRateHz <
                    Oot3dNativeGame::kOot3dOriginalSimulationRateHz) {
                throw std::runtime_error(
                    "presentation rates below the native 30 Hz simulation "
                    "clock are not supported yet");
            }
        } else if (arg == "--disable-visual-interpolation") {
            launch.DisableVisualInterpolation = true;
            disableVisualInterpolationExplicit = true;
        } else if (arg == "--profile-a32-blocks") {
            launch.ProfileA32Blocks = true;
        } else if (arg == "--trace-a32-blocks" && index + 1 < argc) {
            launch.A32BlockTracePath = argv[++index];
        } else if (arg == "--pica-semantic-trace" && index + 1 < argc) {
            launch.PicaSemanticTracePath = argv[++index];
        } else if (arg == "--pica-parametric-tev") {
            launch.PicaParametricTev = true;
        } else if (arg == "--renderer-cache-directory" && index + 1 < argc) {
            launch.RendererCacheDirectory =
                std::filesystem::absolute(std::filesystem::u8path(argv[++index])).lexically_normal();
        } else if (arg == "--water-texture-catalog" && index + 1 < argc) {
            launch.WaterTextureCatalogPath =
                std::filesystem::absolute(std::filesystem::u8path(argv[++index])).lexically_normal();
        } else if (arg == "--scene-light-catalog" && index + 1 < argc) {
            launch.SceneLightCatalogPath =
                std::filesystem::absolute(std::filesystem::u8path(argv[++index])).lexically_normal();
        } else if (arg == "--pica-aot-shader-pack" &&
                   index + 1 < argc) {
            launch.PicaAotShaderPackPath =
                std::filesystem::absolute(argv[++index]).lexically_normal();
        } else if (arg == "--pica-aot-shader-strict") {
            launch.PicaAotShaderStrict = true;
        } else if (arg == "--pica-effective-shader-inventory" &&
                   index + 1 < argc) {
            launch.PicaEffectiveShaderInventoryPath =
                std::filesystem::absolute(argv[++index]).lexically_normal();
        } else if (arg == "--pica-pipeline-inventory" &&
                   index + 1 < argc) {
            launch.PicaPipelineInventoryPath =
                std::filesystem::absolute(argv[++index]).lexically_normal();
        } else if (arg == "--pica-pipeline-manifest" &&
                   index + 1 < argc) {
            launch.PicaPipelineManifestPath =
                std::filesystem::absolute(argv[++index]).lexically_normal();
        } else if (arg == "--pica-pipeline-prewarm") {
            launch.PicaPipelinePrewarm = true;
        } else if (arg == "--scenario-catalog" && index + 1 < argc) {
            launch.ScenarioCatalogPath =
                std::filesystem::absolute(argv[++index]).lexically_normal();
        } else if (arg == "--scenario" && index + 1 < argc) {
            launch.ScenarioId = argv[++index];
        } else if (arg == "--scenario-strict") {
            launch.ScenarioStrict = true;
        } else if (arg == "--scenario-auto-exit") {
            launch.ScenarioAutoExit = true;
        } else if (arg == "--profile-a32-runtime") {
            launch.ProfileA32Runtime = true;
        } else if (arg == "--disable-compiled-functions") {
            launch.DisableCompiledFunctions = true;
        } else if (arg == "--disable-typed-gameplay") {
            launch.DisableTypedGameplay = true;
        } else if (arg == "--disable-manual-compiled-functions") {
            launch.DisableManualCompiledFunctions = true;
        } else if (arg == "--disable-true-aot-blocks") {
            launch.DisableTrueAotBlocks = true;
        } else if (arg == "--disable-whole-aot") {
            launch.DisableWholeAot = true;
        } else if (arg == "--whole-aot-block-budget" &&
                   index + 1 < argc) {
            launch.WholeAotBlockBudget =
                ParseU32(argv[++index], "whole-AOT block budget");
            if (launch.WholeAotBlockBudget == 0U) {
                throw std::runtime_error(
                    "whole-AOT block budget must be positive");
            }
        } else if (arg == "--disable-opengl-pica-geometry-cache") {
            launch.DisableOpenGlPicaGeometryCache = true;
        } else if (arg == "--disable-mass-aot") {
            launch.DisableMassAot = true;
        } else if (arg == "--enable-mass-aot") {
            launch.DisableMassAot = false;
        } else if (arg == "--disable-audio") {
            launch.DisableAudio = true;
        } else if (arg == "--audio-pcm-dump" && index + 1 < argc) {
            launch.AudioPcmDumpPath = argv[++index];
        } else if (arg == "--load-state" && index + 1 < argc) {
            launch.LoadStatePath = argv[++index];
        } else if (arg == "--load-state-with-settings") {
            launch.LoadStateWithSettings = true;
        } else if (arg == "--save-state-with-settings") {
            launch.SaveStateWithSettings = true;
        } else if (arg == "--quick-state" && index + 1 < argc) {
            launch.QuickStatePath = argv[++index];
        } else if (arg == "--save-state" && index + 1 < argc) {
            launch.SaveStatePath = argv[++index];
        } else if (arg == "--convert-state" && index + 1 < argc) {
            launch.ConvertStatePath = argv[++index];
        } else if (arg == "--savestate-action" && index + 1 < argc) {
            // FRAME:ACTION[:PATH]; the path may itself contain ':'.
            const std::string value = argv[++index];
            const auto first = value.find(':');
            const auto second = first == std::string::npos
                                    ? std::string::npos
                                    : value.find(':', first + 1U);
            const std::string action = value.substr(
                first + 1U, second == std::string::npos
                                ? std::string::npos
                                : second - first - 1U);
            Oot3dNativeGameLaunch::ScriptedSavestateAction scripted;
            scripted.RunFrame =
                ParseU32(value.substr(0, first).c_str(), "savestate action frame");
            scripted.Load = action == "load" || action == "load+settings";
            scripted.WithSettings =
                action == "save+settings" || action == "load+settings";
            if (first == std::string::npos ||
                (!scripted.Load && action != "save" &&
                 action != "save+settings")) {
                throw std::runtime_error(
                    "--savestate-action expects FRAME:save|save+settings|"
                    "load|load+settings[:PATH]");
            }
            if (second != std::string::npos) {
                scripted.Path = value.substr(second + 1U);
            }
            launch.SavestateActions.push_back(std::move(scripted));
        } else if (arg == "--save-state-frame" && index + 1 < argc) {
            launch.SaveStateFrame =
                ParseU32(argv[++index], "savestate frame");
            launch.SaveStateFrameAvailable = true;
        } else if (arg == "--output" && index + 1 < argc) {
            launch.Host.OutputPath = argv[++index];
        } else if (arg == "--screenshot" && index + 1 < argc) {
            launch.Host.ScreenshotPath = argv[++index];
        } else if (arg == "--screenshot-sequence") {
            launch.Host.ScreenshotSequence = true;
        } else if (arg == "--screenshot-rgb8") {
            launch.Host.ScreenshotRgb8 = true;
        } else if (arg == "--screenshot-start-frame" && index + 1 < argc) {
            launch.Host.ScreenshotStartFrame =
                ParseU32(argv[++index], "screenshot start frame");
        } else if (arg == "--screenshot-interval" && index + 1 < argc) {
            launch.Host.ScreenshotSequenceInterval =
                std::max<uint32_t>(1, ParseU32(argv[++index], "screenshot interval"));
        } else if (arg == "--frames" && index + 1 < argc) {
            launch.Host.FrameLimit = ParseU32(argv[++index], "frame count");
        } else if (arg == "--benchmark-warmup-frames" && index + 1 < argc) {
            launch.Host.BenchmarkWarmupFrames =
                ParseU32(argv[++index], "benchmark warm-up frame count");
        } else if (arg == "--throughput-benchmark") {
            launch.Host.ThroughputBenchmark = true;
        } else if (arg == "--max-seconds" && index + 1 < argc) {
            launch.Host.MaxSeconds = std::max(0.0, ParseDouble(argv[++index], "maximum seconds"));
        } else if (arg == "--fixed-delta-seconds" && index + 1 < argc) {
            launch.Host.FixedDeltaSeconds =
                ParseDouble(argv[++index], "fixed delta seconds");
            if (launch.Host.FixedDeltaSeconds <= 0.0 ||
                launch.Host.FixedDeltaSeconds > 0.05) {
                throw std::runtime_error("fixed delta seconds must be in (0, 0.05]");
            }
        } else if (arg == "--input-timeline" && index + 1 < argc) {
            launch.Host.InputTimelinePath = argv[++index];
        } else if (arg == "--graphics-settings-schedule" && index + 1 < argc) {
            launch.Host.GraphicsSettingsSchedulePath = argv[++index];
        } else if (arg == "--width" && index + 1 < argc) {
            launch.Host.Width = std::max<uint32_t>(1, ParseU32(argv[++index], "width"));
        } else if (arg == "--height" && index + 1 < argc) {
            launch.Host.Height = std::max<uint32_t>(1, ParseU32(argv[++index], "height"));
        } else {
            return false;
        }
    }

    if (launch.ScenarioCatalogPath.empty() != launch.ScenarioId.empty()) {
        throw std::runtime_error(
            "--scenario-catalog and --scenario must be specified together");
    }
    if (launch.ScenarioStrict && launch.ScenarioId.empty()) {
        throw std::runtime_error(
            "--scenario-strict requires a structural scenario");
    }
    if (launch.ScenarioAutoExit && launch.ScenarioId.empty()) {
        throw std::runtime_error(
            "--scenario-auto-exit requires a structural scenario");
    }

    Oot3dNativeGame::GameplayTimingMode configuredGameplayTiming =
        Oot3dNativeGame::GameplayTimingMode::Native30Interpolated;
    bool configuredGameplayTimingAvailable = false;
    const std::filesystem::path runtimeConfigPath(
        launch.Host.ConfigurationPath);
    if (std::filesystem::is_regular_file(runtimeConfigPath)) {
        const auto runtimeConfig = ReadJson(runtimeConfigPath);
        const auto timing = runtimeConfig.find("gameplay_timing");
        if (timing != runtimeConfig.end()) {
            if (!timing->is_object() || !timing->contains("mode") ||
                !timing->at("mode").is_string() ||
                !Oot3dNativeGame::ParseGameplayTimingMode(
                    timing->at("mode").get_ref<const std::string&>(),
                    &configuredGameplayTiming)) {
                throw std::runtime_error(
                    "gameplay_timing.mode in the runtime config is invalid");
            }
            configuredGameplayTimingAvailable = true;
        }
    }
    if (!gameplayTimingExplicit && configuredGameplayTimingAvailable) {
        launch.GameplayTiming = configuredGameplayTiming;
    }

    if (simulationRateExplicit) {
        Oot3dNativeGame::GameplayTimingMode legacyMode{};
        if (launch.SimulationRateHz ==
            Oot3dNativeGame::kOot3dOriginalSimulationRateHz) {
            legacyMode = disableVisualInterpolationExplicit
                             ? Oot3dNativeGame::GameplayTimingMode::
                                   Native30NoInterpolation
                             : Oot3dNativeGame::GameplayTimingMode::
                                   Native30Interpolated;
        } else if (launch.SimulationRateHz ==
                   Oot3dNativeGame::kOot3dNativeTimeUnitsPerSecond) {
            legacyMode = Oot3dNativeGame::GameplayTimingMode::Enhanced60;
        } else {
            throw std::runtime_error(
                "--simulation-rate supports only 30 or 60; use "
                "--gameplay-timing");
        }
        if (gameplayTimingExplicit &&
            launch.GameplayTiming != legacyMode) {
            throw std::runtime_error(
                "--simulation-rate conflicts with --gameplay-timing");
        }
        launch.GameplayTiming = legacyMode;
    } else if (disableVisualInterpolationExplicit &&
               launch.GameplayTiming ==
                   Oot3dNativeGame::GameplayTimingMode::
                       Native30Interpolated) {
        launch.GameplayTiming =
            Oot3dNativeGame::GameplayTimingMode::Native30NoInterpolation;
    }

    if (launch.Host.ThroughputBenchmark) {
        if (launch.Host.FrameLimit == 0U && launch.Host.MaxSeconds <= 0.0) {
            throw std::runtime_error(
                "--throughput-benchmark requires --frames or --max-seconds");
        }
        // This is a throughput test, not an accelerated duplicate-present
        // test: every measured host frame advances exactly one complete guest
        // simulation/render refresh and presentation itself remains free.
        launch.PresentationRateHz = 0U;
    }

    const auto timingContract =
        Oot3dNativeGame::ResolveNativeFrameRateContract(
            launch.GameplayTiming, launch.PresentationRateHz);
    if (launch.Host.ThroughputBenchmark) {
        if (launch.Host.FixedDeltaSeconds > 0.0 &&
            std::abs(launch.Host.FixedDeltaSeconds -
                     timingContract.StepSeconds) > 1.0e-9) {
            throw std::runtime_error(
                "--throughput-benchmark fixed delta must equal one native "
                "simulation step");
        }
        launch.Host.FixedDeltaSeconds = timingContract.StepSeconds;
    }
    launch.SimulationRateHz = timingContract.SimulationRateHz;
    launch.DisableVisualInterpolation =
        launch.GameplayTiming !=
        Oot3dNativeGame::GameplayTimingMode::Native30Interpolated;

    if (!launch.TopScreenTextureOverridePackPath.empty()) {
        if (launch.UiProfile != Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
            throw std::runtime_error(
                "--topscreen-texture-overrides requires --ui-profile "
                "topscreen");
        }
        if (!std::filesystem::is_regular_file(
                launch.TopScreenTextureOverridePackPath)) {
            throw std::runtime_error(
                "TopScreen texture override pack does not exist: " +
                launch.TopScreenTextureOverridePackPath.string());
        }
    }
    if (!launch.TopScreenLinkAnimationsPath.empty()) {
        if (launch.UiProfile != Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
            throw std::runtime_error(
                "--topscreen-link-animations requires --ui-profile topscreen");
        }
        if (!std::filesystem::is_regular_file(launch.TopScreenLinkAnimationsPath)) {
            throw std::runtime_error(
                "TopScreen Link animations do not exist: " +
                launch.TopScreenLinkAnimationsPath.string());
        }
    }
    if (!launch.TopScreenUiAtlasHdPath.empty()) {
        if (launch.UiProfile != Oot3dNativeGame::Oot3dUiProfile::TopScreen ||
            launch.TopScreenTextureOverridePackPath.empty()) {
            throw std::runtime_error(
                "--topscreen-ui-atlas-hd requires --ui-profile topscreen and "
                "--topscreen-texture-overrides");
        }
        if (!std::filesystem::is_regular_file(launch.TopScreenUiAtlasHdPath)) {
            throw std::runtime_error("TopScreen HD UI atlas does not exist: " +
                                     launch.TopScreenUiAtlasHdPath.string());
        }
    }

    if (!launch.RomFsOverlayPath.empty() &&
        !std::filesystem::is_regular_file(launch.RomFsOverlayPath)) {
        throw std::runtime_error("RomFS overlay does not exist: " +
                                 launch.RomFsOverlayPath.string());
    }

    if (!launch.UiTexturePackDirectory.empty() &&
        !std::filesystem::is_directory(launch.UiTexturePackDirectory / "common")) {
        throw std::runtime_error("UI texture pack is not a prepared pack: " +
                                 launch.UiTexturePackDirectory.string());
    }

    if (!launch.TopScreenConfigPath.empty()) {
        if (launch.UiProfile != Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
            throw std::runtime_error(
                "--topscreen-config requires --ui-profile topscreen");
        }
        if (!std::filesystem::is_regular_file(launch.TopScreenConfigPath)) {
            throw std::runtime_error(
                "TopScreen UI config does not exist: " +
                launch.TopScreenConfigPath.string());
        }
        std::string configError;
        if (!Oot3dNativeGame::LoadTopScreenUiConfig(
                launch.TopScreenConfigPath, &launch.TopScreenConfig,
                &configError)) {
            throw std::runtime_error(configError);
        }
    }

    if (launch.ControlConfigPath.empty()) {
        std::filesystem::path runtimePath(launch.Host.ConfigurationPath);
        if (runtimePath.empty()) {
            runtimePath = "oot3d_native_game.json";
        }
        runtimePath = std::filesystem::absolute(runtimePath).lexically_normal();
        launch.ControlConfigPath =
            runtimePath.parent_path() / "oot3d_controls.json";
    }
    if (std::filesystem::is_regular_file(launch.ControlConfigPath)) {
        std::string configError;
        if (!Oot3dNativeGame::LoadNativeControlConfig(
                launch.ControlConfigPath, &launch.ControlConfig,
                &configError)) {
            throw std::runtime_error(configError);
        }
    }

    if (launch.Host.Renderer == "opengl") {
        launch.Host.BackendId = Fast::WindowBackend::FAST3D_SDL_OPENGL;
    } else if (launch.Host.Renderer == "nri" || launch.Host.Renderer == "vulkan") {
        launch.Host.BackendId = Fast::WindowBackend::FAST3D_SDL_OOT3D_VULKAN;
    } else {
        return false;
    }

    if (launch.SaveStatePath.empty() !=
        !launch.SaveStateFrameAvailable) {
        throw std::runtime_error(
            "--save-state and --save-state-frame must be specified together");
    }
    if (!launch.ConvertStatePath.empty() &&
        (launch.LoadStatePath.empty() || !launch.SaveStatePath.empty() ||
         !launch.SavestateActions.empty() || launch.LoadStateWithSettings)) {
        throw std::runtime_error(
            "--convert-state requires --load-state and excludes other "
            "savestate options");
    }

    // The game runs the translated A32 process; there is no other launch mode.
    return !launch.A32ProcessManifestPath.empty() && !launch.Host.ResourceRoot.empty();
}
