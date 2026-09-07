#include "oot3d_demo_host_cli.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "fast/Fast3dWindow.h"

void PrintUsage() {
    std::cerr << "usage: oot3d_native_fast3d_demo --manifest <demo_manifest.json> "
                 "--resource-root <three_ds_recomp/legacy/src/fast> [--self-test] [--output <summary.json>] "
                 "[--screenshot <capture.bmp>] [--screenshot-sequence] "
                 "[--screenshot-start-frame <frame>] "
                 "[--screenshot-sequence-interval <frames>] "
                 "[--frames <count>] [--max-seconds <seconds>] "
                 "[--fixed-delta-seconds <seconds>] "
                 "[--input-timeline <timeline.json>] "
                 "[--material-animation-frame <frame>] "
                 "[--width <pixels>] [--height <pixels>] [--backend <id: 1=DX11, 2=OpenGL>] "
                 "[--renderer <nri|opengl>] "
                 "[--render-mode native_texture|native_pica_lighting_debug] [--entrance-index <index>] "
                 "[--intro-cutscene] [--intro-cutscene-source-index <index>] "
                 "[--title-intro] [--title-intro-qdb-index <index>] "
                 "[--title-intro-frame <frame>] [--title-intro-romfs-root <path>] "
                 "[--title-intro-actor-diagnostics] "
                 "[--title-intro-trace] [--title-intro-trace-start <frame>] "
                 "[--title-intro-trace-end <frame>]\n";
}

namespace {

uint32_t ParseU32(const std::string& value, const char* name) {
    size_t parsed = 0;
    const unsigned long out = std::stoul(value, &parsed, 10);
    if (parsed != value.size() || out > UINT32_MAX) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + value);
    }
    return static_cast<uint32_t>(out);
}

int32_t ParseI32(const std::string& value, const char* name) {
    size_t parsed = 0;
    const long out = std::stol(value, &parsed, 10);
    if (parsed != value.size() || out < INT32_MIN || out > INT32_MAX) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + value);
    }
    return static_cast<int32_t>(out);
}

double ParseDouble(const std::string& value, const char* name) {
    size_t parsed = 0;
    const double out = std::stod(value, &parsed);
    if (parsed != value.size() || !std::isfinite(out)) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + value);
    }
    return out;
}

} // namespace

bool ParseArgs(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--manifest" && i + 1 < argc) {
            args.ManifestPath = argv[++i];
        } else if (arg == "--resource-root" && i + 1 < argc) {
            args.ResourceRoot = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            args.OutputPath = argv[++i];
        } else if (arg == "--screenshot" && i + 1 < argc) {
            args.ScreenshotPath = argv[++i];
        } else if (arg == "--screenshot-sequence") {
            args.ScreenshotSequence = true;
        } else if (arg == "--screenshot-start-frame" && i + 1 < argc) {
            args.ScreenshotStartFrame = ParseU32(argv[++i], "screenshot start frame");
        } else if (arg == "--screenshot-sequence-interval" && i + 1 < argc) {
            args.ScreenshotSequence = true;
            args.ScreenshotSequenceInterval =
                std::max<uint32_t>(1, ParseU32(argv[++i], "screenshot sequence interval"));
        } else if (arg == "--self-test") {
            args.SelfTest = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            args.FrameLimit = ParseU32(argv[++i], "frame count");
        } else if (arg == "--max-seconds" && i + 1 < argc) {
            args.MaxSeconds = std::max(0.0, ParseDouble(argv[++i], "maximum seconds"));
        } else if (arg == "--fixed-delta-seconds" && i + 1 < argc) {
            args.FixedDeltaSeconds = ParseDouble(argv[++i], "fixed delta seconds");
            if (args.FixedDeltaSeconds <= 0.0 || args.FixedDeltaSeconds > 0.05) {
                throw std::runtime_error("fixed delta seconds must be in (0, 0.05]");
            }
        } else if (arg == "--input-timeline" && i + 1 < argc) {
            args.InputTimelinePath = argv[++i];
        } else if (arg == "--material-animation-frame" && i + 1 < argc) {
            args.MaterialAnimationFrame =
                std::max(0.0, ParseDouble(argv[++i], "material animation frame"));
        } else if (arg == "--width" && i + 1 < argc) {
            args.Width = std::max<uint32_t>(1, ParseU32(argv[++i], "width"));
        } else if (arg == "--height" && i + 1 < argc) {
            args.Height = std::max<uint32_t>(1, ParseU32(argv[++i], "height"));
        } else if (arg == "--backend" && i + 1 < argc) {
            args.BackendId = ParseI32(argv[++i], "backend id");
        } else if (arg == "--renderer" && i + 1 < argc) {
            args.Renderer = argv[++i];
        } else if (arg == "--render-mode" && i + 1 < argc) {
            args.RenderMode = argv[++i];
        } else if (arg == "--entrance-index" && i + 1 < argc) {
            args.EntranceIndex = ParseI32(argv[++i], "entrance index");
        } else if (arg == "--intro-cutscene") {
            args.IntroCutscenePlayback = true;
        } else if (arg == "--intro-cutscene-source-index" && i + 1 < argc) {
            const uint32_t sourceIndex = ParseU32(argv[++i], "intro cutscene source index");
            if (sourceIndex > UINT16_MAX) {
                throw std::runtime_error("intro cutscene source index exceeds u16 range");
            }
            args.IntroCutscenePlayback = true;
            args.IntroCutsceneSourceIndex = static_cast<uint16_t>(sourceIndex);
        } else if (arg == "--title-intro") {
            args.TitleIntroPlayback = true;
        } else if (arg == "--title-intro-qdb-index" && i + 1 < argc) {
            const uint32_t qdbIndex = ParseU32(argv[++i], "title intro QDB index");
            if (qdbIndex > UINT16_MAX) {
                throw std::runtime_error("title intro QDB index exceeds u16 range");
            }
            args.TitleIntroPlayback = true;
            args.TitleIntroQdbIndex = static_cast<uint16_t>(qdbIndex);
            args.TitleIntroQdbIndexOverride = true;
        } else if (arg == "--title-intro-frame" && i + 1 < argc) {
            args.TitleIntroPlayback = true;
            args.TitleIntroFrame = std::max(0.0, ParseDouble(argv[++i], "title intro frame"));
            args.TitleIntroFrameOverride = true;
        } else if (arg == "--title-intro-actor-diagnostics") {
            args.TitleIntroPlayback = true;
            args.TitleIntroActorDiagnostics = true;
        } else if (arg == "--title-intro-trace") {
            args.TitleIntroPlayback = true;
            args.TitleIntroTrace = true;
        } else if (arg == "--title-intro-trace-start" && i + 1 < argc) {
            args.TitleIntroPlayback = true;
            args.TitleIntroTrace = true;
            args.TitleIntroTraceStartFrame = std::max<uint32_t>(
                1u,
                ParseU32(argv[++i], "title intro trace start frame"));
        } else if (arg == "--title-intro-trace-end" && i + 1 < argc) {
            args.TitleIntroPlayback = true;
            args.TitleIntroTrace = true;
            args.TitleIntroTraceEndFrame = std::max<uint32_t>(
                1u,
                ParseU32(argv[++i], "title intro trace end frame"));
        } else if (arg == "--title-intro-romfs-root" && i + 1 < argc) {
            args.TitleIntroPlayback = true;
            args.TitleIntroRomfsRoot = argv[++i];
        } else {
            return false;
        }
    }

    if (args.Renderer != "opengl" && args.Renderer != "nri" && args.Renderer != "vulkan") {
        return false;
    }
    if (args.Renderer == "nri" || args.Renderer == "vulkan") {
        args.BackendId = Fast::WindowBackend::FAST3D_SDL_OOT3D_VULKAN;
    }
    if (args.RenderMode != "native_texture" && args.RenderMode != "native_pica_lighting_debug") {
        return false;
    }
    if (args.TitleIntroTraceEndFrame < args.TitleIntroTraceStartFrame) {
        return false;
    }
    return !args.ManifestPath.empty() && !args.ResourceRoot.empty();
}
