#include "oot3d_cutscene_player_cli.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fast/Fast3dWindow.h"
#include "oot3d_demo_host_cli.h"

extern "C" {
#include "oot3d/scene_cutscene_frame_runtime.h"
}

namespace {

uint32_t ParseCutsceneU32(const std::string& value, const char* name) {
    size_t parsed = 0;
    const unsigned long out = std::stoul(value, &parsed, 10);
    if (parsed != value.size() || out > UINT32_MAX) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + value);
    }
    return static_cast<uint32_t>(out);
}

double ParseCutsceneFrame(const std::string& value) {
    size_t parsed = 0;
    const double out = std::stod(value, &parsed);
    if (parsed != value.size() || !std::isfinite(out) || out < 0.0) {
        throw std::runtime_error("invalid cutscene frame: " + value);
    }
    return out;
}

} // namespace

void PrintCutscenePlayerUsage() {
    std::cerr << "usage: oot3d_native_cutscene_player --manifest <demo_manifest.json> "
                 "--resource-root <three_ds_recomp/legacy/src/fast> --scene-id <id> "
                 "--setup-index <index> --cutscene-source-index <index> "
                 "[--cutscene-frame <frame>] [common demo capture options]\n";
}

bool ParseCutscenePlayerArgs(int argc, char** argv, Args& args) {
    uint32_t sceneId = UINT32_MAX;
    uint32_t setupIndex = UINT32_MAX;
    uint32_t sourceIndex = UINT32_MAX;
    double frame = 0.0;
    bool frameSpecified = false;
    std::vector<char*> hostArgs;
    hostArgs.reserve(static_cast<size_t>(argc));
    hostArgs.push_back(argv[0]);

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--scene-id" && i + 1 < argc) {
            sceneId = ParseCutsceneU32(argv[++i], "scene id");
        } else if (arg == "--setup-index" && i + 1 < argc) {
            setupIndex = ParseCutsceneU32(argv[++i], "setup index");
        } else if (arg == "--cutscene-source-index" && i + 1 < argc) {
            sourceIndex = ParseCutsceneU32(argv[++i], "cutscene source index");
        } else if (arg == "--cutscene-frame" && i + 1 < argc) {
            frame = ParseCutsceneFrame(argv[++i]);
            frameSpecified = true;
        } else if ((arg.rfind("--title-intro", 0) == 0 &&
                    arg != "--title-intro-actor-diagnostics") ||
                   arg.rfind("--intro-cutscene", 0) == 0) {
            return false;
        } else {
            hostArgs.push_back(argv[i]);
        }
    }

    if (sceneId > UINT8_MAX || setupIndex > UINT16_MAX || sourceIndex > UINT16_MAX) {
        return false;
    }
    if (!ParseArgs(static_cast<int>(hostArgs.size()), hostArgs.data(), args)) {
        return false;
    }

    const Oot3dSceneCutsceneFrameKey key{
        static_cast<uint8_t>(sceneId),
        static_cast<uint16_t>(setupIndex),
        static_cast<uint16_t>(sourceIndex),
    };
    Oot3dSceneCutsceneFrameAdapterSelection adapter{};
    const auto adapterStatus = Oot3d_SceneCutsceneFrameRuntimeResolveAdapter(key, &adapter);
    if (adapterStatus != OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_OK) {
        throw std::runtime_error(
            "native cutscene tuple has no migrated playback adapter: scene=" +
            std::to_string(sceneId) + " setup=" + std::to_string(setupIndex) +
            " source=" + std::to_string(sourceIndex));
    }
    args.ApplicationName = "OOT3D Native Cutscene Player";
    args.ApplicationId = "oot3d_native_cutscene_player";
    args.ConfigurationPath = "oot3d_native_cutscene_player.json";
    args.BackendId = (args.Renderer == "nri" || args.Renderer == "vulkan")
                         ? Fast::WindowBackend::FAST3D_SDL_OOT3D_VULKAN
                         : Fast::WindowBackend::FAST3D_SDL_OPENGL;
    args.TitleIntroPlayback = adapter.kind == OOT3D_SCENE_CUTSCENE_FRAME_ADAPTER_OPEN_TITLE;
    args.IntroCutscenePlayback =
        adapter.kind == OOT3D_SCENE_CUTSCENE_FRAME_ADAPTER_SCENE_CUTSCENE;
    args.IntroCutsceneSourceIndex = static_cast<uint16_t>(sourceIndex);
    args.TitleIntroFrame = frame;
    args.TitleIntroFrameOverride = frameSpecified && args.TitleIntroPlayback;
    args.CutscenePlayerRequest = true;
    args.CutscenePlayerSceneId = static_cast<uint16_t>(sceneId);
    args.CutscenePlayerSetupIndex = static_cast<uint16_t>(setupIndex);
    args.CutscenePlayerSourceIndex = static_cast<uint16_t>(sourceIndex);
    args.CutscenePlayerOrchestrationIndex = adapter.adapterIndex;
    args.CutscenePlayerAdapterKind = static_cast<uint16_t>(adapter.kind);
    args.CutscenePlayerFrameOverride = frameSpecified;
    args.CutscenePlayerFrame = frame;
    return true;
}
