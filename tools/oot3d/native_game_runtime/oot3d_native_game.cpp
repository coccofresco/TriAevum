#include <cstdlib>
#include <array>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__SWITCH__) || defined(__ANDROID__)
#include <SDL.h>
#endif

#include "oot3d_demo_host_context.h"
#if defined(__ANDROID__)
#include "android_host.h"
#endif
#include "oot3d_native_game_bootstrap.h"
#include "oot3d_native_game_launch_profile.h"
#include "triaevum_product_info.h"
#include "oot3d_native_a32_window.h"
#if defined(OOT3D_REQUIRE_WHOLE_AOT_PLUGIN_V2)
#include "oot3d_native_direct_aot.h"
#include "triaevum_title_plugin_loader.h"
#endif

namespace {

#if defined(__SWITCH__)
constexpr const char* kSwitchBootStatusPath =
    "sdmc:/switch/oot3dre/boot-status.txt";

void WriteSwitchBootStatus(const char* status, const std::string& detail = {}) {
    std::ofstream stream(kSwitchBootStatusPath, std::ios::trunc);
    if (!stream) {
        return;
    }
    stream << "status=" << status << '\n'
           << "whole_aot_functions=12419\n"
           << "host_boundaries=3\n"
           << "residual_a32=0\n";
    if (!detail.empty()) {
        stream << "detail=" << detail << '\n';
    }
}
#endif

void SetRendererEnvironment(const char* name,
                            const std::filesystem::path& value) {
    if (value.empty())
        return;
#ifdef _WIN32
    if (_wputenv_s(std::filesystem::path(name).c_str(), value.c_str()) != 0)
#else
    if (setenv(name, value.string().c_str(), 1) != 0)
#endif
        throw std::runtime_error(std::string("could not set ") + name);
}

void ConfigurePicaAotShaders(const Oot3dNativeGameLaunch& launch) {
    SetRendererEnvironment("TRIAEVUM_RENDERER_CACHE_DIR", launch.RendererCacheDirectory);
    SetRendererEnvironment("OOT3D_PICA_AOT_SHADER_PACK",
                           launch.PicaAotShaderPackPath);
    SetRendererEnvironment("OOT3D_PICA_EFFECTIVE_SHADER_INVENTORY",
                           launch.PicaEffectiveShaderInventoryPath);
    SetRendererEnvironment("OOT3D_PICA_PIPELINE_INVENTORY",
                           launch.PicaPipelineInventoryPath);
    SetRendererEnvironment("OOT3D_PICA_PIPELINE_MANIFEST",
                           launch.PicaPipelineManifestPath);
    if (launch.PicaAotShaderStrict) {
#ifdef _WIN32
        if (_putenv_s("OOT3D_PICA_AOT_SHADER_STRICT", "1") != 0)
#else
        if (setenv("OOT3D_PICA_AOT_SHADER_STRICT", "1", 1) != 0)
#endif
            throw std::runtime_error(
                "could not enable strict PICA AOT shaders");
    }
    if (launch.PicaPipelinePrewarm) {
#ifdef _WIN32
        if (_putenv_s("OOT3D_PICA_PIPELINE_PREWARM", "1") != 0)
#else
        if (setenv("OOT3D_PICA_PIPELINE_PREWARM", "1", 1) != 0)
#endif
            throw std::runtime_error(
                "could not enable PICA pipeline prewarm");
    }
}

struct NativeGameArguments {
    std::vector<std::string> Storage;
    std::vector<char*> Pointers;
};

NativeGameArguments PrepareNativeGameArguments(int argc, char** argv) {
    NativeGameArguments prepared;
    std::filesystem::path profilePath;
    int commandLineStart = argc;

    if (argc <= 1) {
        profilePath = Oot3dNativeGame::DefaultNativeGameLaunchProfilePath(
            argc > 0 ? argv[0] : "oot3d_native_game");
        commandLineStart = argc;
    } else if (std::string_view(argv[1]) == "--launch-profile") {
        if (argc <= 2) {
            throw std::runtime_error("--launch-profile requires a path");
        }
        profilePath = argv[2];
        commandLineStart = 3;
    } else {
        return prepared;
    }

    prepared.Storage.emplace_back(
        argc > 0 ? argv[0] : "oot3d_native_game");
    auto profileArguments =
        Oot3dNativeGame::LoadNativeGameLaunchProfile(profilePath);
    prepared.Storage.insert(prepared.Storage.end(),
                            std::make_move_iterator(profileArguments.begin()),
                            std::make_move_iterator(profileArguments.end()));
    for (int index = commandLineStart; index < argc; ++index) {
        prepared.Storage.emplace_back(argv[index]);
    }
    prepared.Pointers.reserve(prepared.Storage.size());
    for (auto& argument : prepared.Storage) {
        prepared.Pointers.push_back(argument.data());
    }
    std::cout << "Using native game launch profile: "
              << std::filesystem::absolute(profilePath).lexically_normal()
              << '\n';
    return prepared;
}

} // namespace

int main(int argc, char** argv) {
    try {
#if defined(__ANDROID__)
        InitializeAndroidGameHost();
#endif
#if defined(OOT3D_REQUIRE_WHOLE_AOT_PLUGIN_V2)
        if (argc == 3 && std::string_view(argv[1]) == "--verify-title-plugin") {
            Oot3dNativeGame::ConfigureTitlePlugin(argv[2]);
            if (!Oot3dNativeGame::Oot3dWholeAotPluginV2Available())
                throw std::runtime_error("Selected title plugin does not expose a valid whole-AOT ABI v2");
            WriteTriAevumProductInfo(std::cout);
            return 0;
        }
#endif
        if (argc == 2 && std::string_view(argv[1]) == "--product-info") {
            WriteTriAevumProductInfo(std::cout);
            return 0;
        }
        Oot3dNativeGameLaunch launch;
        NativeGameArguments preparedArguments;
#if defined(__SWITCH__)
        // SDL's positional face-button convention is useful for portable PC
        // layouts, but the Switch build should follow the labels printed on
        // Joy-Con and Pro Controllers: A is A, B is B, and so on.
        SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "1");
        launch.ControlConfig = Oot3dNativeGame::NativeControlPreset(
            Oot3dNativeGame::NativeControlProfile::Controller);
        std::array<char*, 21> homebrewArguments{
            argc > 0 ? argv[0] : const_cast<char*>("oot3d_native_game.nro"),
            const_cast<char*>("--a32-process-manifest"),
            const_cast<char*>(
                "sdmc:/switch/oot3dre/game/oot3d_native_process_manifest.json"),
            const_cast<char*>("--resource-root"),
            const_cast<char*>("sdmc:/switch/oot3dre/resources.o2r"),
            const_cast<char*>("--renderer"),
            const_cast<char*>("opengl"),
            const_cast<char*>("--save-data"),
            const_cast<char*>("sdmc:/switch/oot3dre/savedata"),
            const_cast<char*>("--ui-profile"),
            const_cast<char*>("topscreen"),
            const_cast<char*>("--topscreen-config"),
            const_cast<char*>(
                "sdmc:/switch/oot3dre/config/topscreen_ui.json"),
            const_cast<char*>("--topscreen-texture-overrides"),
            const_cast<char*>(
                "sdmc:/switch/oot3dre/config/atlas_overrides.o3tu"),
            const_cast<char*>("--controls-config"),
            const_cast<char*>(
                "sdmc:/switch/oot3dre/config/oot3d_controls.json"),
            const_cast<char*>("--gameplay-timing"),
            const_cast<char*>("native30_no_interpolation"),
            const_cast<char*>("--presentation-rate"),
            const_cast<char*>("30"),
        };
        if (argc <= 1) {
            argc = static_cast<int>(homebrewArguments.size());
            argv = homebrewArguments.data();
        }
#else
        preparedArguments = PrepareNativeGameArguments(argc, argv);
        if (!preparedArguments.Pointers.empty()) {
            argc = static_cast<int>(preparedArguments.Pointers.size());
            argv = preparedArguments.Pointers.data();
        }
#endif
#if defined(OOT3D_REQUIRE_WHOLE_AOT_PLUGIN_V2)
        std::vector<char*> runtimeArguments;
        bool pluginSelected = false;
        for (int index = 0; index < argc; ++index) {
            if (std::string_view(argv[index]) == "--title-plugin") {
                if (pluginSelected || index + 1 == argc)
                    throw std::runtime_error("--title-plugin requires exactly one path");
                Oot3dNativeGame::ConfigureTitlePlugin(argv[++index]);
                pluginSelected = true;
            } else {
                runtimeArguments.push_back(argv[index]);
            }
        }
        argc = static_cast<int>(runtimeArguments.size());
        argv = runtimeArguments.data();
#endif
        if (!ParseOot3dNativeGameArgs(argc, argv, launch)) {
            PrintOot3dNativeGameUsage();
            return 2;
        }

        if (launch.A32ProcessManifestPath.empty()) {
            throw std::runtime_error(
                "oot3d_native_game requires --a32-process-manifest; use "
                "oot3d_native_game_legacy_sandbox for the standalone demo");
        }
#if defined(OOT3D_REQUIRE_WHOLE_AOT_PLUGIN_V2)
        if (!Oot3dNativeGame::Oot3dWholeAotPluginV2Available()) {
            throw std::runtime_error(
                "the private whole-AOT ABI-v2 plugin is missing or invalid; "
                "run TriAevum Forge again before launching the game");
        }
#endif
#if defined(__SWITCH__)
        WriteSwitchBootStatus("launching");
#endif
        ConfigurePicaAotShaders(launch);
#ifdef OOT3D_NATIVE_A32_WINDOW_AVAILABLE
        RunOot3dNativeA32Window(launch);
        // The A32 window owns objects that refer back to the host context.
        // Destroy those locals on return before tearing the context down.
        DestroyContextForDemo();
#if defined(__SWITCH__)
        WriteSwitchBootStatus("completed");
#endif
        return 0;
#else
        throw std::runtime_error(
            "native A32 window runtime is unavailable in this build");
#endif
    } catch (const std::exception& ex) {
        std::cerr << "oot3d_native_game: " << ex.what() << '\n';
#if defined(__SWITCH__)
        WriteSwitchBootStatus("failed", ex.what());
#endif
        DestroyContextForDemo();
        return 1;
    }
}
