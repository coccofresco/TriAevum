#pragma once

#include <SDL.h>

namespace Fast {

// Issue #123: SDL2 enables text input as soon as the video subsystem starts.
// Under Wayland that registers the window with the compositor's input method
// (text-input-v3), and a desktop input method may consume held letter keys
// (press-and-hold accent pickers, compose sequences): gameplay keys such as
// A/S/D then never reach the controller mapping, while X11 delivers them.
// Keep text input off during gameplay and enable it only while the menu has a
// focused text field (ImGui WantTextInput). Platform-neutral: on every desktop
// this matches what the game needs; mobile keeps its own keyboard helper.
class SdlTextInputGate {
  public:
    // Returns +1 when text input must be started, -1 when it must be stopped,
    // 0 when nothing changes. The first call always settles the state, since
    // SDL starts with text input enabled.
    int Decide(bool wantsTextInput) {
        if (mKnown && mActive == wantsTextInput) {
            return 0;
        }
        mKnown = true;
        mActive = wantsTextInput;
        return wantsTextInput ? 1 : -1;
    }

    void Apply(bool wantsTextInput) {
        const int change = Decide(wantsTextInput);
        if (change > 0) {
            SDL_StartTextInput();
        } else if (change < 0) {
            SDL_StopTextInput();
        }
    }

  private:
    bool mKnown = false;
    bool mActive = false;
};

inline SdlTextInputGate& DesktopSdlTextInputGate() {
    static SdlTextInputGate gate;
    return gate;
}

} // namespace Fast
