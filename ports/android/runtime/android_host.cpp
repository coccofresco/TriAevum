#include "android_host.h"

#include <SDL.h>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

void InitializeAndroidGameHost() {
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    const char* root = SDL_AndroidGetExternalStoragePath();
    if (!root || !*root) {
        throw std::runtime_error("Android application data directory is unavailable");
    }
    std::filesystem::current_path(root);
    std::filesystem::create_directories("logs");
    // Android does not preserve a console stream; retain diagnostics beside user data.
    if (!std::freopen("logs/native-stdout.log", "w", stdout) ||
        !std::freopen("logs/native-stderr.log", "w", stderr)) {
        throw std::runtime_error("Cannot open Android runtime logs");
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    SDL_Log("TriAevum game data: %s", root);
}
