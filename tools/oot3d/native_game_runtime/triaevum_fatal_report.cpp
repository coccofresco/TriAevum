#include "triaevum_fatal_report.h"

#include <array>
#include <cstdlib>

namespace TriAevum {
namespace {

// Options of scripted runs and tool modes (oot3d_native_game_bootstrap.cpp,
// RunTriAevumToolMode): nobody is there to dismiss a dialog.
constexpr std::array<std::string_view, 15> kUnattendedOptions{
    "--input-timeline",
    "--frames",
    "--max-seconds",
    "--fixed-delta-seconds",
    "--graphics-settings-schedule",
    "--screenshot",
    "--screenshot-sequence",
    "--throughput-benchmark",
    "--scenario-auto-exit",
    "--convert-state",
    "--product-info",
    "--game-language-info",
    "--prebuild-interior-sun",
    "--verify-title-plugin",
    "--audio-pcm-dump",
};

constexpr size_t kMessageLimit = 1500;

} // namespace

bool FatalDialogWanted(int argc, const char* const* argv, const char* noDialog) {
#if defined(__ANDROID__) || defined(__SWITCH__)
    (void)argc;
    (void)argv;
    (void)noDialog;
    return false;
#else
    if (noDialog != nullptr && *noDialog != '\0' && std::string_view(noDialog) != "0")
        return false;
    for (int index = 1; index < argc && argv != nullptr; ++index) {
        if (argv[index] == nullptr) continue;
        const std::string_view argument(argv[index]);
        for (const auto option : kUnattendedOptions)
            if (argument == option) return false;
    }
    return true;
#endif
}

bool FatalDialogWanted(int argc, const char* const* argv) {
    return FatalDialogWanted(argc, argv, std::getenv("TRIAEVUM_NO_FATAL_DIALOG"));
}

std::string FatalLine(std::string_view message) {
    std::string line(kFatalLinePrefix);
    for (const char character : message)
        line.push_back(character == '\n' || character == '\r' ? ' ' : character);
    return line;
}

std::string FatalDialogText(std::string_view message, std::string_view logDirectory) {
    std::string text(message.substr(0, kMessageLimit));
    if (message.size() > kMessageLimit) text += "...";
    if (!logDirectory.empty()) {
        text += "\n\nDetails were written to the log folder:\n";
        text += logDirectory;
    }
    text += "\n\nOpen Forge and choose Diagnostics to copy a report or save a support bundle.";
    return text;
}

} // namespace TriAevum
