#include "android_host.h"

#include <SDL.h>
#include <cstdio>
#include <filesystem>
#include <jni.h>
#include <mutex>
#include <stdexcept>

#include "ship/Context.h"

namespace {

std::mutex installerPageMutex;
std::string installerPage;

} // namespace

bool RequestAndroidInstallerPage(std::string_view page, std::string* error) {
    if (page != "saves" && page != "diagnostics") {
        if (error != nullptr) *error = "unknown installer page";
        return false;
    }
    std::lock_guard lock(installerPageMutex);
    installerPage = page;
    std::fprintf(stderr, "TRIAEVUM_INSTALLER_PAGE %s requested; quitting the game\n", installerPage.c_str());
    return true;
}

extern "C" {

// org.triaevum.android.NativeHostBridge.takeInstallerPage(): the page the
// menu asked for, once, or null. Called by the activity as the game ends.
JNIEXPORT jstring JNICALL Java_org_triaevum_android_NativeHostBridge_takeInstallerPage(JNIEnv* env, jobject) {
    std::string page;
    {
        std::lock_guard lock(installerPageMutex);
        page.swap(installerPage);
    }
    return page.empty() ? nullptr : env->NewStringUTF(page.c_str());
}

} // extern "C"

void InitializeAndroidGameHost() {
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    const char* root = SDL_AndroidGetExternalStoragePath();
    if (!root || !*root) {
        throw std::runtime_error("Android application data directory is unavailable");
    }
    std::filesystem::current_path(root);
    // The managed data tree's logs (UX plan WP-05C): D/logs, D = <app
    // storage>/data, established here before any argument is parsed. Android
    // does not preserve a console stream; stdout and stderr are kept there
    // too. A launch profile's --log-directory may still route the runtime log.
    const auto logs = std::filesystem::path(root) / "data" / "logs";
    std::filesystem::create_directories(logs);
    Ship::Context::SetLogDirectory(logs.string());
    if (!std::freopen((logs / "native-stdout.log").c_str(), "w", stdout) ||
        !std::freopen((logs / "native-stderr.log").c_str(), "w", stderr)) {
        throw std::runtime_error("Cannot open Android runtime logs");
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    SDL_Log("TriAevum game data: %s", root);
}
