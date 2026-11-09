#define SDL_MAIN_HANDLED
#include "fast/backends/sdl_text_input_policy.h"

#include <cstdio>
#include <source_location>
#include <stdexcept>
#include <string>

// Issue #123: gameplay keys must never be routed through the platform input
// method. The gate is the real object Fast3dGui drives on every SDL event.
int main(int argc, char** argv) {
    const bool negative = argc > 1 && std::string(argv[1]) == "--negative";
    const auto require = [](bool value, std::source_location where = std::source_location::current()) {
        if (!value) throw std::runtime_error("SDL text input policy regression at line " + std::to_string(where.line()));
    };
    try {
        Fast::SdlTextInputGate pure;
        require(pure.Decide(false) == -1); // SDL starts enabled: first gameplay frame stops it
        require(pure.Decide(false) == 0);
        require(pure.Decide(true) == 1);   // a focused menu text field restores text entry
        require(pure.Decide(true) == 0);
        require(pure.Decide(false) == -1);

        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
        SDL_StartTextInput(); // the SDL2 default after video init
        require(SDL_IsTextInputActive() == SDL_TRUE);
        Fast::SdlTextInputGate gate;
        if (!negative) gate.Apply(false);
        require(SDL_IsTextInputActive() == SDL_FALSE);
        gate.Apply(true);
        require(SDL_IsTextInputActive() == SDL_TRUE);
        gate.Apply(false);
        require(SDL_IsTextInputActive() == SDL_FALSE);
        SDL_Quit();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        SDL_Quit();
        return 1;
    }
    std::puts("sdl_text_input_policy_tests: PASS");
    return 0;
}
