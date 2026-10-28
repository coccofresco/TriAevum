#define SDL_MAIN_HANDLED
#include "three_ds_sdl_controller.h"
#include "three_ds_sdl_key_trace.h"
#include "../../../runtime/three_ds_recomp/include/ship/controller/physicaldevice/SDLControllerSetup.h"
#include <cstdlib>
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
    // Steam Input delivers pads to Linux/SteamOS sessions as its own virtual
    // gamepad, which SDL drops unless the process opts in. SDL3 reads the
    // environment directly, so that is what has to be set - and an explicit
    // Steam or user value must survive.
    const auto steamVirtualPadHint = [] {
        const char* value = SDL_getenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD");
        return value != nullptr ? std::string_view(value) : std::string_view{};
    };
    Require(steamVirtualPadHint() == "1", "Steam Input virtual gamepad opt-in is not applied");
    SDL_setenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "0", 1);
    Ship::ConfigureSDLControllerCapabilities();
    Require(steamVirtualPadHint() == "0",
            "controller setup overwrote an explicit virtual gamepad preference");
    SDL_setenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1", 1);
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

    // Button prompts from what SDL reports: two virtual pads of known
    // vendors and one SDL cannot place, selection, disconnect and reconnect.
    Require(ControllerTypeFromSdl(SDL_CONTROLLER_TYPE_PS5) == ControllerType::PS5 &&
                ControllerTypeFromSdl(SDL_CONTROLLER_TYPE_MAX - 1) == ControllerType::NintendoJoyConPair &&
                ControllerTypeFromSdl(SDL_CONTROLLER_TYPE_MAX) == ControllerType::Unknown &&
                ControllerTypeFromSdl(-1) == ControllerType::Unknown,
            "ControllerType mirrors SDL_GameControllerType");
    const auto attach = [](Uint16 vendor, Uint16 product, const char* name) {
        SDL_VirtualJoystickDesc desc{};
        desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        desc.naxes = SDL_CONTROLLER_AXIS_MAX;
        desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
        desc.vendor_id = vendor;
        desc.product_id = product;
        desc.name = name;
        const int deviceIndex = SDL_JoystickAttachVirtualEx(&desc);
        Require(deviceIndex >= 0, "virtual pad attachment");
        auto* pad = SDL_GameControllerOpen(deviceIndex);
        Require(pad != nullptr, "virtual pad open");
        return pad;
    };
    const auto detach = [](SDL_GameController* pad) {
        const auto instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
        for (int deviceIndex = 0; deviceIndex < SDL_NumJoysticks(); ++deviceIndex) {
            if (SDL_JoystickGetDeviceInstanceID(deviceIndex) == instance) {
                Require(SDL_JoystickDetachVirtual(deviceIndex) == 0, "virtual pad detach");
                return;
            }
        }
        throw std::runtime_error("virtual pad not found");
    };
    const auto instanceOf = [](SDL_GameController* pad) {
        return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
    };
    auto* xboxPad = attach(kUsbVendorMicrosoft, 0x02EA, "Xbox One Controller");
    auto* dualSense = attach(kUsbVendorSony, 0x0CE6, "DualSense Wireless Controller");
    auto* oddPad = attach(0x1234, 0x5678, "Unknown Arcade Stick");
    std::unordered_map<std::int32_t, SDL_GameController*> pads{
        {instanceOf(xboxPad), xboxPad}, {instanceOf(dualSense), dualSense}, {instanceOf(oddPad), oddPad}};
    const auto prompts = [&](std::string_view guid, std::int32_t previous, PromptDeviceTracker& tracker) {
        auto chosen = ResolveSdlController(pads, guid, "", previous);
        const DeviceDescriptor* pad = nullptr;
        for (const auto& device : chosen.Devices)
            if (device.Selected) pad = &device;
        return std::pair{ResolvePromptSet(ButtonPrompts::Automatic, tracker.LastUsed(), pad, PromptPlatform::Desktop),
                         chosen};
    };
    auto all = ResolveSdlController(pads, "", "", -1);
    Require(all.Devices.size() == 3, "three virtual pads");
    std::string dualSenseGuid;
    for (const auto& device : all.Devices) {
        const auto expected = device.InstanceId == instanceOf(xboxPad)    ? PromptSet::Xbox
                              : device.InstanceId == instanceOf(dualSense) ? PromptSet::PlayStation
                                                                            : PromptSet::Generic;
        Require(device.Prompts == expected, "each pad's prompt family from SDL's type and vendor");
        if (device.InstanceId == instanceOf(dualSense)) dualSenseGuid = device.Guid;
    }
    PromptDeviceTracker tracker;
    // Before any input, the automatic selection (the first pad) shows its family.
    Require(prompts("", -1, tracker).first == PromptSet::Xbox, "a connected pad before any input");
    tracker.Observe(InputActivity::Keyboard, 1.0);
    Require(prompts("", -1, tracker).first == PromptSet::Keyboard, "the keyboard used");
    // The pad's press is what the host poll reports as controller activity.
    SDL_JoystickSetVirtualButton(SDL_GameControllerGetJoystick(dualSense), SDL_CONTROLLER_BUTTON_A, 1);
    SDL_GameControllerUpdate();
    Require(IsSdlControllerActive(dualSense) && !IsSdlControllerActive(xboxPad), "the pressed pad is the active one");
    tracker.Observe(InputActivity::Controller, 2.0);
    SDL_JoystickSetVirtualButton(SDL_GameControllerGetJoystick(dualSense), SDL_CONTROLLER_BUTTON_A, 0);
    SDL_JoystickSetVirtualAxis(SDL_GameControllerGetJoystick(dualSense), SDL_CONTROLLER_AXIS_LEFTX, 30000);
    SDL_GameControllerUpdate();
    Require(IsSdlControllerActive(dualSense), "a pushed stick is activity");
    SDL_JoystickSetVirtualAxis(SDL_GameControllerGetJoystick(dualSense), SDL_CONTROLLER_AXIS_LEFTX, 0);
    SDL_GameControllerUpdate();
    Require(!IsSdlControllerActive(dualSense), "a resting pad is no activity");
    // Two pads: the preferred (selected) one gives the family.
    auto [preferred, chosenPad] = prompts(dualSenseGuid, -1, tracker);
    Require(preferred == PromptSet::PlayStation && chosenPad.InstanceId == instanceOf(dualSense),
            "the selected pad of two");
    // Disconnect: automatic selection moves to the other pad, then none.
    detach(dualSense);
    Require(prompts("", chosenPad.InstanceId, tracker).first == PromptSet::Xbox, "the remaining pad after a disconnect");
    detach(xboxPad);
    detach(oddPad);
    Require(prompts("", -1, tracker).first == PromptSet::Keyboard, "no pad: the keyboard's prompts");
    // Reconnect: the pad's family again, without a new press.
    auto* again = attach(kUsbVendorSony, 0x0CE6, "DualSense Wireless Controller");
    pads[instanceOf(again)] = again;
    Require(prompts("", -1, tracker).first == PromptSet::PlayStation, "the pad reconnected");
    detach(again);
    for (auto* pad : {xboxPad, dualSense, oddPad, again}) SDL_GameControllerClose(pad);
    {
        // The device log of real SDL descriptors (a second pad, no serial).
        auto* logPad = attach(kUsbVendorMicrosoft, 0x0B13, "Xbox Series X Controller");
        const std::unordered_map<std::int32_t, SDL_GameController*> logPads{
            {SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(logPad)), logPad}};
        const auto described = ResolveSdlController(logPads, "", "", -1);
        const auto lines = DescribeInputDevices("startup", described.Devices, KeyboardDescription{true, "dummy", false});
        Require(lines.size() == 3 && lines[1].find("type=xbox_one") != std::string::npos &&
                    lines[1].find("serial=none") != std::string::npos,
                "device log of an SDL pad");
        for (const auto& line : lines) std::cout << line << '\n';
        detach(logPad);
        SDL_GameControllerClose(logPad);
    }
    {
        // TRIAEVUM_INPUT_TRACE=1: each SDL key event as SDL delivered it.
        // The CRT's environment (what std::getenv reads), not SDL's copy.
        const auto setTrace = [](const char* value) {
#ifdef _WIN32
            _putenv_s("TRIAEVUM_INPUT_TRACE", value);
#else
            setenv("TRIAEVUM_INPUT_TRACE", value, 1);
#endif
        };
        setTrace("0");
        Require(!SdlKeyTrace::Requested(), "key trace without the switch");
        setTrace("1");
        Require(SdlKeyTrace::Requested(), "key trace switch");
        SdlKeyTrace trace;
        SDL_Event key{};
        key.type = SDL_KEYDOWN;
        key.key.keysym.scancode = SDL_SCANCODE_A;
        key.key.keysym.sym = SDLK_a;
        key.key.windowID = 7;
        Require(SDL_PushEvent(&key) == 1, "push key event");
        key.type = SDL_KEYUP;
        Require(SDL_PushEvent(&key) == 1, "push key release");
        const auto lines = trace.Drain();
        Require(lines.size() == 2 &&
                    lines[0].find("sdl_key down scancode=4 (A) keycode=0x61 (A) repeat=0") != std::string::npos &&
                    lines[0].find("window=7") != std::string::npos &&
                    lines[1].find("sdl_key up scancode=4") != std::string::npos,
                "key trace lines");
        Require(trace.Drain().empty(), "key trace drained twice");
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    }
    SDL_Quit();
    std::cout << "PASS: SDL selection, buttons, triggers, axes, UI capture, absent sensors, disconnect, "
                 "prompt families, two pads, reconnect, device log, key trace\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << ": " << SDL_GetError() << '\n';
    SDL_Quit();
    return 1;
}
