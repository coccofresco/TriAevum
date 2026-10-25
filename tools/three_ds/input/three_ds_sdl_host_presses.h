#pragma once

#include "three_ds_pointer_press_latch.h"
#include <SDL2/SDL.h>

namespace ThreeDsRecomp::Input {

// Press events that polling of the held state misses, latched while this
// object lives: the primary pointer (mouse, and touches SDL turns into mouse
// events) and the Android Back gesture, which SDL reports as a key the host
// keyboard state does not map. Event watches run on the thread that queues
// the event.
class SdlHostPressWatch {
  public:
    SdlHostPressWatch() { SDL_AddEventWatch(&Watch, this); }
    ~SdlHostPressWatch() { SDL_DelEventWatch(&Watch, this); }
    SdlHostPressWatch(const SdlHostPressWatch&) = delete;
    SdlHostPressWatch& operator=(const SdlHostPressWatch&) = delete;

    // The 3DS touch screen and the host's own menus read the pointer
    // separately, each consuming its own presses.
    PointerPressLatch NativeTouch;
    PointerPressLatch HostMenu;
    PointerPressLatch Back;

  private:
    static int SDLCALL Watch(void* userdata, SDL_Event* event) {
        auto* self = static_cast<SdlHostPressWatch*>(userdata);
        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
            self->NativeTouch.ObservePress();
            self->HostMenu.ObservePress();
        } else if (event->type == SDL_KEYDOWN && !event->key.repeat &&
                   event->key.keysym.scancode == SDL_SCANCODE_AC_BACK) {
            self->Back.ObservePress();
        }
        return 1;
    }
};

} // namespace ThreeDsRecomp::Input
