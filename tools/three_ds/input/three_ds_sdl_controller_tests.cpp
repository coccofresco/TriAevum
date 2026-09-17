#define SDL_MAIN_HANDLED
#include "three_ds_sdl_controller.h"
#include "../../../runtime/three_ds_recomp/include/ship/controller/physicaldevice/SDLControllerSetup.h"
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) try {
    using namespace ThreeDsRecomp::Input;
    SDL_SetMainReady();
    Ship::ConfigureSDLControllerCapabilities();
    if (argc == 2 && std::string_view(argv[1]) == "--probe-physical") {
        Require(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0, "SDL initialization");
        unsigned opened = 0;
        for (int index = 0; index < SDL_NumJoysticks(); ++index) {
            if (!SDL_IsGameController(index)) continue;
            auto* pad = SDL_GameControllerOpen(index);
            if (!pad) continue;
            ++opened;
            unsigned gyroSamples = 0, accelSamples = 0, gyroFresh = 0, accelFresh = 0;
            std::uint64_t lastGyro = 0, lastAccel = 0;
            PhysicalInputState state;
            for (int i = 0; i < 100; ++i) {
                SDL_PumpEvents();
                SDL_GameControllerUpdate();
                SampleSdlController(pad, state);
                const auto& motion = state.ControllerMotion;
                gyroSamples += motion.GyroscopeValid;
                accelSamples += motion.AccelerometerValid;
                gyroFresh += motion.GyroscopeValid && motion.GyroscopeTimestampMicroseconds > lastGyro;
                accelFresh += motion.AccelerometerValid && motion.AccelerometerTimestampMicroseconds > lastAccel;
                lastGyro = motion.GyroscopeTimestampMicroseconds;
                lastAccel = motion.AccelerometerTimestampMicroseconds;
                SDL_Delay(10);
            }
            std::cout << "Controller: " << SDL_GameControllerName(pad)
                      << " gyro_polls=" << gyroSamples << " accel_polls=" << accelSamples
                      << " fresh_gyro=" << gyroFresh << " fresh_accel=" << accelFresh
                      << " touchpads=" << SDL_GameControllerGetNumTouchpads(pad) << '\n';
            std::cout << "Last native sample: gyro=";
            for (const auto value : state.ControllerMotion.GyroscopeDegreesPerSecond) std::cout << value << ' ';
            std::cout << " accel=";
            for (const auto value : state.ControllerMotion.Accelerometer) std::cout << value << ' ';
            std::cout << '\n';
            SDL_GameControllerClose(pad);
        }
        std::cout << "Physical probe: " << opened << " controllers opened (not a gameplay qualification)\n";
        SDL_Quit();
        return 0;
    }
    Require(argc == 1, "usage: [--probe-physical]");
    Require(SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, SDL_FALSE) &&
            SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, SDL_FALSE),
            "extended PlayStation sensor reports not enabled");
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "0", SDL_HINT_OVERRIDE);
    Ship::ConfigureSDLControllerCapabilities();
    Require(!SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, SDL_TRUE),
            "controller setup overwrote explicit user preference");
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
        PhysicalInputState intent;
        SampleSdlController(controller, intent);
        Require((intent.ControllerButtons & (std::uint32_t{1} << button)) != 0,
                "pressed button missing from device ownership input");
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
    Require(state.LeftStickX == 0 && state.RightStickY == 0 && state.ControllerButtons == 0,
            "UI-owned analog axes leak to gameplay");
    Require(!state.ControllerMotion.GyroscopeValid && !state.ControllerMotion.AccelerometerValid,
            "unavailable sensors synthesized valid samples");
    Require(!state.ControllerTouch.Pressed, "unavailable touchpad synthesized a touch");
    Require(SDL_JoystickDetachVirtual(index) == 0, "virtual disconnect");
    Require(ResolveSdlController(connected, "", "", id).Controller == nullptr,
            "detached handle remains selected");
    state.LeftStickX = 20000;
    state.ControllerMotion.GyroscopeValid = true;
    state.ControllerTouch = {0.5F, 0.5F, true};
    SampleSdlController(controller, state);
    Require(state.LeftStickX == 0 && !state.ControllerMotion.GyroscopeValid && !state.ControllerTouch.Pressed &&
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
