#include "oot3d_demo_host_context.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "fast/Fast3dWindow.h"
#include "fast/WindowExtentPolicy.h"
#include "fast/backends/gfx_rendering_api.h"
#include "fast/resource/ResourceType.h"
#include "fast/resource/factory/TextureFactory.h"
#include "three_ds_recomp/legacy/controller/controldeck/ControlDeck.h"
#include "ship/Context.h"
#include "ship/audio/AudioPlayer.h"
#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#include "ship/debug/CrashHandler.h"
#include "ship/resource/ResourceLoader.h"
#include "triaevum_window_icon.h"

#include <SDL2/SDL_messagebox.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace {

// TRIAEVUM_TEST_FATAL (docs/TRIAEVUM_DIAGNOSTICS.md): a simulated failure at
// the point a real one happens, for testing the error reports end to end.
bool TestFatal(const char* kind) {
    const char* requested = std::getenv("TRIAEVUM_TEST_FATAL");
    return requested != nullptr && std::string(requested) == kind;
}

std::string ResolveHostResourcePath(const std::filesystem::path& path) {
    const std::string encoded = path.generic_string();
    if (encoded.find(":/") != std::string::npos) {
        return path.lexically_normal().generic_string();
    }
    return std::filesystem::absolute(path).string();
}

#if defined(__SWITCH__)
void RecordSwitchHostInitStage(const char* stage) {
    std::ofstream stream("sdmc:/switch/oot3dre/boot-status.txt",
                         std::ios::trunc);
    if (!stream) {
        return;
    }
    stream << "status=launching\n"
           << "stage=" << stage << '\n'
           << "whole_aot_functions=12419\n"
           << "host_boundaries=3\n"
           << "residual_a32=0\n";
}
#else
void RecordSwitchHostInitStage(const char*) {
}
#endif

std::filesystem::path& LaunchMarker() {
    static std::filesystem::path marker;
    return marker;
}

// A launch marker beside the configuration lives for the session's first
// minute. Finding it means the previous launch ended before that (a crash or
// an error at startup, as a too-demanding resolution causes at every launch
// once it is saved): the graphics settings then restart their presentation
// from the defaults (graphics_settings_runtime.cpp).
void ArmLaunchMarker(const std::string& configurationPath) {
    std::error_code error;
    auto marker = std::filesystem::path(configurationPath);
    marker += ".launching";
    if (std::filesystem::exists(marker, error)) {
#if defined(_WIN32)
        _putenv_s("TRIAEVUM_SAFE_PRESENTATION", "the previous launch ended within its first minute");
#else
        setenv("TRIAEVUM_SAFE_PRESENTATION", "the previous launch ended within its first minute", 1);
#endif
    }
    if (!marker.parent_path().empty())
        std::filesystem::create_directories(marker.parent_path(), error);
    std::ofstream(marker) << "TriAevum launch in progress\n";
    LaunchMarker() = marker;
    std::thread([marker] {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        std::error_code removed;
        std::filesystem::remove(marker, removed);
    }).detach();
}

} // namespace

void ClearLaunchMarkerForDemo() noexcept {
    std::error_code error;
    if (!LaunchMarker().empty())
        std::filesystem::remove(LaunchMarker(), error);
}

void InitContextForDemo(const Args& args) {
    RecordSwitchHostInitStage("host_create_context");
    ArmLaunchMarker(args.ConfigurationPath);
    // The log folder: --log-directory (D/logs), else logs/ in the application
    // directory. The native game routes and checks it before this
    // (oot3d_native_game.cpp PrepareLogDirectory, with a temporary fallback);
    // a folder that cannot be created surfaces as the logger's own failure.
    {
        std::error_code logError;
        std::filesystem::create_directories(Ship::Context::GetLogDirectory(), logError);
    }
    auto* context = Ship::Context::CreateUninitializedInstance(
        args.ApplicationName, args.ApplicationId, args.ConfigurationPath);
    if (context == nullptr) {
        throw std::runtime_error("failed to create runtime/three_ds_recomp context");
    }
    if (!context->InitLogging() || !context->InitConfiguration() || !context->InitConsoleVariables()) {
        throw std::runtime_error("failed to initialize base runtime/three_ds_recomp context services");
    }
    if (args.ThroughputBenchmark) {
        context->GetConsoleVariables()->SetInteger(CVAR_VSYNC_ENABLED, 0);
    }
    RecordSwitchHostInitStage("host_base_services");

    auto config = context->GetConfig();
    config->SetBool("Window.Fullscreen.Enabled", false);
    // The window host owns the output extent: the size chosen in F1 Display
    // (Graphics.Output) wins, and the launch arguments only seed a
    // configuration that stores none yet. The window backend then fits it to
    // the display's work area (gfx_sdl2.cpp).
    const auto storedOutput = [&]() -> std::optional<Fast::WindowExtent> {
        const int32_t width = config->GetInt("Graphics.Output.Width", 0);
        const int32_t height = config->GetInt("Graphics.Output.Height", 0);
        if (width <= 0 || height <= 0) return std::nullopt;
        return Fast::WindowExtent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    }();
    const auto extent = Fast::ResolveInitialWindowExtent(storedOutput, {args.Width, args.Height});
    config->SetInt("Window.Width", static_cast<int32_t>(extent.Width));
    config->SetInt("Window.Height", static_cast<int32_t>(extent.Height));
    // A size nobody chose (the product's fresh default, a configuration
    // without one, or the safe presentation after a launch that ended in its
    // first minute) opens at the default fit of the screen, windowed and
    // centred; a size the user chose opens as stored. Scripted sessions keep
    // the stored size so their captures reproduce.
    const auto storedProvenance = [&] {
        if (!storedOutput.has_value()) return Fast::WindowExtentProvenance::Default;
        const auto provenance = config->GetString("Graphics.Output.Provenance", "");
        if (provenance == "default") return Fast::WindowExtentProvenance::Default;
        if (provenance == "user") return Fast::WindowExtentProvenance::User;
        return Fast::InferWindowExtentProvenance(config->GetString("Graphics.Window.Mode", "Windowed") == "Windowed",
                                                 *storedOutput);
    }();
    const char* safePresentation = std::getenv("TRIAEVUM_SAFE_PRESENTATION");
    const bool initialRequestIsDefault = storedProvenance == Fast::WindowExtentProvenance::Default ||
                                         (safePresentation != nullptr && *safePresentation != '\0');
    if (args.BackendId >= 0) {
        config->SetInt("Window.Backend.Id", args.BackendId);
    }

    std::vector<std::string> resourceRoots = {
        ResolveHostResourcePath(args.ResourceRoot)
    };
    for (const auto& archive : args.ResourceArchives) {
        resourceRoots.push_back(ResolveHostResourcePath(archive));
    }
    if (!context->InitResourceManager(resourceRoots, {}, 1, false)) {
        throw std::runtime_error("failed to mount native demo resources");
    }
    RecordSwitchHostInitStage("host_resources");
    auto loader = context->GetResourceManager()->GetResourceLoader();
    loader->RegisterResourceFactory(
        std::make_shared<Fast::ResourceFactoryBinaryTextureV0>(),
        RESOURCE_FORMAT_BINARY, "Texture",
        static_cast<uint32_t>(Fast::ResourceType::Texture), 0);
    loader->RegisterResourceFactory(
        std::make_shared<Fast::ResourceFactoryBinaryTextureV1>(),
        RESOURCE_FORMAT_BINARY, "Texture",
        static_cast<uint32_t>(Fast::ResourceType::Texture), 1);
    if (!context->InitControlDeck(std::make_shared<ThreeDsRecomp::Legacy::ControlDeck>())) {
        throw std::runtime_error("failed to initialize control deck");
    }
    RecordSwitchHostInitStage("host_control_deck");
    if (!context->InitCrashHandler() || !context->InitConsole() || !context->InitEventSystem() ||
        !context->InitFileDropMgr()) {
        throw std::runtime_error("failed to initialize runtime/three_ds_recomp support services");
    }
    RecordSwitchHostInitStage("host_support_services");

    auto window = std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>{});
    if (args.Renderer == "nri" || args.Renderer == "vulkan") {
        window->EnableOot3dVulkanBackend();
    }
    window->ConfigureDefaultWindowExtent(!args.Scripted(), initialRequestIsDefault);
    window->ConfigureWindowIcon(&ResolveWindowIcon(args.WindowIcon));
    if (TestFatal("vulkan-device")) {
        throw std::runtime_error(
            "the Vulkan device could not be created (simulated by TRIAEVUM_TEST_FATAL=vulkan-device)");
    }
    if (!context->InitWindow(window)) {
        throw std::runtime_error("failed to initialize Fast3dWindow");
    }
    if (TestFatal("crash")) {
        // Reaches the installed crash handlers exactly like a real fault.
        volatile int* const fault = nullptr;
        *fault = 1;
    }
    RecordSwitchHostInitStage("host_window");
    // Video initialization does not initialize SDL's joystick/controller API.
    // The device manager owns that subsystem independently of legacy osContInit.
    const auto controllerDatabase = std::filesystem::is_directory(args.ResourceRoot)
        ? (args.ResourceRoot / "gamecontrollerdb.txt").string()
        : Ship::Context::LocateFileAcrossAppDirs("gamecontrollerdb.txt");
    if (!context->GetControlDeck()->GetConnectedPhysicalDeviceManager()->Initialize(controllerDatabase)) {
        throw std::runtime_error("failed to initialize SDL controller input");
    }
    RecordSwitchHostInitStage("host_gamepads");
    if (args.AudioSampleRate == 0 || args.AudioSampleLength == 0 ||
        args.AudioDesiredBuffered <= 0) {
        throw std::runtime_error("native demo audio format is invalid");
    }
    Ship::AudioSettings audioSettings;
    audioSettings.SampleRate = static_cast<int32_t>(args.AudioSampleRate);
    audioSettings.SampleLength = static_cast<int32_t>(args.AudioSampleLength);
    audioSettings.DesiredBuffered = args.AudioDesiredBuffered;
    if (!context->InitAudio(audioSettings)) {
        throw std::runtime_error("failed to initialize native demo audio output");
    }
    RecordSwitchHostInitStage("host_audio");
    window->SetCursorVisibility(true);
}

void DestroyContextForDemo() noexcept {
    if (auto* context = Ship::Context::GetRawInstance(); context != nullptr && context->GetControlDeck() != nullptr) {
        context->GetControlDeck()->GetConnectedPhysicalDeviceManager()->Shutdown();
    }
    Ship::Context::DestroyInstance();
}

bool LogFatalErrorForDemo(const char* message) noexcept {
    try {
        if (auto* context = Ship::Context::GetRawInstance(); context != nullptr && context->GetLogger() != nullptr) {
            context->GetLogger()->critical("oot3d_native_game: {}", message != nullptr ? message : "unknown error");
            context->GetLogger()->flush();
            return true;
        }
    } catch (...) {
    }
    return false;
}

std::string FatalLogDirectoryForDemo() {
    try {
        std::error_code error;
        const auto logs = std::filesystem::path(Ship::Context::GetLogDirectory());
        const auto absolute = std::filesystem::absolute(logs, error);
        const auto utf8 = (error ? logs : absolute).lexically_normal().u8string();
        return std::string(utf8.begin(), utf8.end());
    } catch (...) {
        return "logs";
    }
}

void ShowFatalErrorForDemo(const std::string& title, const std::string& text) noexcept {
#if defined(__ANDROID__) || defined(__SWITCH__)
    (void)title;
    (void)text;
#else
    if (Ship::CrashHandler::DialogShown() || Ship::CrashHandler::DialogsSuppressed()) return;
    // SDL shows a message box with or without SDL_Init.
    if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), nullptr) == 0) return;
#ifdef _WIN32
    const auto wide = [](const std::string& utf8) {
        std::wstring result(static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0)), L'\0');
        if (!result.empty()) {
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, result.data(), static_cast<int>(result.size()));
            result.pop_back();
        }
        return result;
    };
    try {
        MessageBoxW(nullptr, wide(text).c_str(), wide(title).c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    } catch (...) {
    }
#endif
#endif
}

Fast::Fast3dWindow& GetActiveFast3dWindowForDemo() {
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    if (window == nullptr) {
        throw std::runtime_error("active runtime/three_ds_recomp window is not Fast3dWindow");
    }
    return *window;
}

Fast::GfxRenderingAPI& GetActiveRenderingApiForDemo(Fast::Fast3dWindow& window) {
    Fast::GfxRenderingAPI* api = window.GetCurrentRenderingAPI();
    if (api == nullptr) {
        throw std::runtime_error("Fast3dWindow did not expose a rendering API");
    }
    return *api;
}
