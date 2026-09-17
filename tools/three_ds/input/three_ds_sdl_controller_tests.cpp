#define SDL_MAIN_HANDLED
#include "three_ds_sdl_controller.h"
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() try {
    using namespace ThreeDsRecomp::Input;
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    Require(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0, "SDL initialization");
    const int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
        SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    Require(index >= 0, "virtual controller attachment");
    auto* controller = SDL_GameControllerOpen(index);
    Require(controller != nullptr, "virtual controller open");
    auto* joystick = SDL_GameControllerGetJoystick(controller);
    const auto id = SDL_JoystickInstanceID(joystick);
    const std::unordered_map<std::int32_t, SDL_GameController*> connected{{id, controller}};
    auto selection = ResolveSdlController(connected, "", "", -1);
    Require(selection.InstanceId == id && selection.Controller == controller &&
            selection.Devices.size() == 1 && selection.Devices[0].Selected,
            "shared SDL selection/descriptor mismatch");
    for (int button = 0; button <= static_cast<int>(GamepadButton::DpadRight); ++button) {
        SDL_JoystickSetVirtualButton(joystick, button, 1);
        SDL_GameControllerUpdate();
        Require(IsSdlControllerButtonHeld(controller, static_cast<GamepadButton>(button), 6000),
                "normalized SDL button mapping");
        SDL_JoystickSetVirtualButton(joystick, button, 0);
        SDL_GameControllerUpdate();
        Require(!IsSdlControllerButtonHeld(controller, static_cast<GamepadButton>(button), 6000),
                "released SDL button remains pressed");
    }
    SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, 24000);
    SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_RIGHTY, -32768);
    SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767);
    SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    SDL_GameControllerUpdate();
    PhysicalInputState state;
    SampleSdlController(controller, state);
    Require(state.LeftStickX == 24000 && state.RightStickY == 32767,
            "axes changed magnitude or overflowed on inversion");
    Require(IsSdlControllerButtonHeld(controller, GamepadButton::LeftTrigger, 6000) &&
            !IsSdlControllerButtonHeld(controller, GamepadButton::RightTrigger, 6000),
            "trigger normalization/threshold");
    SampleSdlController(controller, state, false);
    Require(state.LeftStickX == 0 && state.RightStickY == 0,
            "UI-owned analog axes leak to gameplay");
    Require(!state.ControllerMotion.GyroscopeValid && !state.ControllerMotion.AccelerometerValid,
            "unavailable sensors synthesized valid samples");
    Require(SDL_JoystickDetachVirtual(index) == 0, "virtual disconnect");
    Require(ResolveSdlController(connected, "", "", id).Controller == nullptr,
            "detached handle remains selected");
    state.LeftStickX = 20000;
    state.ControllerMotion.GyroscopeValid = true;
    SampleSdlController(controller, state);
    Require(state.LeftStickX == 0 && !state.ControllerMotion.GyroscopeValid &&
            !IsSdlControllerButtonHeld(controller, GamepadButton::A, 0),
            "disconnect leaves stuck physical input");
    SDL_GameControllerClose(controller);
    SDL_Quit();
    std::cout << "PASS: SDL selection, buttons, triggers, axes, UI capture, absent sensors, disconnect\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << ": " << SDL_GetError() << '\n';
    SDL_Quit();
    return 1;
}
