#include "oot3d_demo_host_context.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "fast/Fast3dWindow.h"
#include "fast/backends/gfx_rendering_api.h"
#include "fast/resource/ResourceType.h"
#include "fast/resource/factory/TextureFactory.h"
#include "three_ds_recomp/legacy/controller/controldeck/ControlDeck.h"
#include "ship/Context.h"
#include "ship/audio/AudioPlayer.h"
#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#include "ship/resource/ResourceLoader.h"

namespace {

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

} // namespace

void InitContextForDemo(const Args& args) {
    RecordSwitchHostInitStage("host_create_context");
    std::filesystem::create_directories("logs");
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
    config->SetInt("Window.Width", static_cast<int32_t>(args.Width));
    config->SetInt("Window.Height", static_cast<int32_t>(args.Height));
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
    if (!context->InitWindow(window)) {
        throw std::runtime_error("failed to initialize Fast3dWindow");
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
