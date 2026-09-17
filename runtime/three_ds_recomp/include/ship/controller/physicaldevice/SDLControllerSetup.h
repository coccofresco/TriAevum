#pragma once

#include <SDL2/SDL.h>

namespace Ship {

// Azahar input_common/sdl/sdl_impl.cpp enables extended Sony reports to expose
// motion. Apply before opening devices, but respect user/Steam environment
// overrides. Do not create a competing input backend or force virtual pads off.
inline void ConfigureSDLControllerCapabilities() {
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI, "1", SDL_HINT_DEFAULT);
#ifdef SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1", SDL_HINT_DEFAULT);
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1", SDL_HINT_DEFAULT);
#endif
#ifdef SDL_HINT_ACCELEROMETER_AS_JOYSTICK
    SDL_SetHintWithPriority(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0", SDL_HINT_DEFAULT);
#endif
}

} // namespace Ship
