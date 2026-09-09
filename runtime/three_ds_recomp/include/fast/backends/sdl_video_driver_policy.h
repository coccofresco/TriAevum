#pragma once

#include <SDL.h>

namespace Fast {

inline void ConfigureSdlVideoDriver(bool usesVulkan) {
#if defined(__linux__) && !defined(__ANDROID__) && SDL_VERSION_ATLEAST(2, 0, 22)
    // Prefer native Wayland; the Vulkan surface policy selects ordered queue
    // presentation there. Preserve explicit hints and X11-only desktops.
    if (usesVulkan && SDL_GetHint(SDL_HINT_VIDEODRIVER) == nullptr) {
        SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "wayland,x11", SDL_HINT_DEFAULT);
    }
#else
    (void)usesVulkan;
#endif
}

} // namespace Fast
