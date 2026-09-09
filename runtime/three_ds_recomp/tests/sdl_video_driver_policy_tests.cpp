#define SDL_MAIN_HANDLED
#include "fast/backends/sdl_video_driver_policy.h"

#include <stdexcept>
#include <source_location>
#include <string>
#include <string_view>

int main() {
    const auto require = [](bool value, std::source_location where = std::source_location::current()) {
        if (!value) throw std::runtime_error("SDL video driver policy regression at line " + std::to_string(where.line()));
    };
#if defined(__linux__) && !defined(__ANDROID__) && SDL_VERSION_ATLEAST(2, 0, 22)
    // CTest supplies a clean driver environment before SDL can cache it.
    SDL_ClearHints();
    Fast::ConfigureSdlVideoDriver(false);
    require(SDL_GetHint(SDL_HINT_VIDEODRIVER) == nullptr);
    Fast::ConfigureSdlVideoDriver(true);
    require(std::string_view(SDL_GetHint(SDL_HINT_VIDEODRIVER)) == "wayland,x11");
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    Fast::ConfigureSdlVideoDriver(true);
    require(std::string_view(SDL_GetHint(SDL_HINT_VIDEODRIVER)) == "dummy");
#endif
    SDL_ClearHints();
    SDL_setenv("SDL_VIDEODRIVER", "", 1);
    // An explicit empty environment value requests SDL's automatic selection.
    Fast::ConfigureSdlVideoDriver(true);
    require(std::string_view(SDL_GetHint("SDL_VIDEODRIVER")).empty());
    SDL_setenv("SDL_VIDEODRIVER", "wayland", 1);
    Fast::ConfigureSdlVideoDriver(true);
    require(std::string_view(SDL_GetHint("SDL_VIDEODRIVER")) == "wayland");
}
