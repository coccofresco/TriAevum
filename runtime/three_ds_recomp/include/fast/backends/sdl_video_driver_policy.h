#pragma once

#include <SDL.h>

namespace Fast {

inline void ConfigureSdlVideoDriver(bool usesVulkan) {
#if defined(__linux__) && !defined(__ANDROID__) && SDL_VERSION_ATLEAST(2, 0, 22)
    // Native Wayland presentation still flashes at 30 Hz on the qualified
    // desktop. Prefer X11/XWayland without changing Vulkan or frame pacing.
    // Keep explicit user hints; Wayland remains available without X11.
    if (usesVulkan && SDL_GetHint(SDL_HINT_VIDEODRIVER) == nullptr) {
        SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "x11,wayland", SDL_HINT_DEFAULT);
    }
#else
    (void)usesVulkan;
#endif
}

} // namespace Fast
