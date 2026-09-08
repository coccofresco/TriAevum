#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
    }
}

int AttachController() {
    SDL_VirtualJoystickDesc descriptor{};
    descriptor.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    descriptor.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    descriptor.naxes = SDL_CONTROLLER_AXIS_MAX;
    descriptor.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    descriptor.axis_mask = (1U << SDL_CONTROLLER_AXIS_MAX) - 1;
    descriptor.button_mask = (1U << SDL_CONTROLLER_BUTTON_MAX) - 1;
    descriptor.name = "TriAevum virtual controller regression test";
    const int index = SDL_JoystickAttachVirtualEx(&descriptor);
    Require(index >= 0, "attach virtual gamepad");
    return index;
}

void CheckInput(SDL_GameController* controller) {
    auto* joystick = SDL_GameControllerGetJoystick(controller);
    Require(SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_A, 1) == 0,
            "press A");
    Require(SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_START, 1) == 0,
            "press Start");
    Require(SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, 24000) == 0,
            "move left stick");
    Require(SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_RIGHTY, -18000) == 0,
            "move right stick");
    SDL_GameControllerUpdate();
    Require(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) == 1, "read A");
    Require(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START) == 1, "read Start");
    Require(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) == 24000, "read left stick");
    Require(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY) == -18000, "read right stick");
    SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_A, 0);
    SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_START, 0);
    SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, 0);
    SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_RIGHTY, 0);
    SDL_GameControllerUpdate();
    Require(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) == 0, "release A");
    Require(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START) == 0, "release Start");
    Require(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) == 0, "center stick");
}
} // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    try {
        Require(argc == 2, "supply packaged controller database path");
        Require(SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0, "start without controller subsystem");
        Ship::ConnectedPhysicalDeviceManager manager;
        Require(manager.Initialize(argv[1]), "initialize manager");
        Require(SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0, "manager owns initialization");
        Require(SDL_GameControllerNumMappings() > 0, "controller mappings loaded");
        const int index = AttachController();
        manager.HandlePhysicalDeviceConnect(index);
        const auto id = SDL_JoystickGetDeviceInstanceID(index);
        auto controllers = manager.GetConnectedSDLGamepadsForPort(0);
        Require(controllers.contains(id), "new device appears on native port");
        auto* controller = controllers.at(id);
        CheckInput(controller);
        manager.IgnoreInstanceIdForPort(0, id);
        Require(manager.GetConnectedSDLGamepadsForPort(0).count(id) == 0, "ignore port");
        manager.RefreshConnectedSDLGamepads();
        Require(manager.PortIsIgnoringInstanceId(0, id), "refresh preserves ignore settings");
        manager.UnignoreInstanceIdForPort(0, id);
        for (int refresh = 0; refresh < 100; ++refresh) {
            manager.RefreshConnectedSDLGamepads();
            Require(manager.GetConnectedSDLGamepadsForPort(0).at(id) == controller,
                    "refresh retains open controller handle");
        }
        Require(manager.Initialize(argv[1]), "initialization is idempotent");
        Require(SDL_JoystickDetachVirtual(index) == 0, "unplug controller");
        manager.HandlePhysicalDeviceDisconnect(id);
        Require(manager.GetConnectedSDLGamepadsForPort(0).count(id) == 0, "remove detached device");
        Require(SDL_GameControllerFromInstanceID(id) == nullptr, "no leaked open controller reference");
        Require(!manager.PortIsIgnoringInstanceId(1, id), "remove stale port ignores");

        const int reconnected = AttachController();
        manager.HandlePhysicalDeviceConnect(reconnected);
        const auto newId = SDL_JoystickGetDeviceInstanceID(reconnected);
        Require(newId != id, "reconnection uses fresh instance id");
        CheckInput(manager.GetConnectedSDLGamepadsForPort(0).at(newId));
        manager.Shutdown();
        Require(SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0, "balanced subsystem ownership");
        Require(manager.GetConnectedSDLGamepadsForPort(0).empty(), "shutdown clears handles");
        manager.Shutdown();

        // A controller can predate the manager and another host may own SDL too.
        Require(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0, "external subsystem owner");
        const int preexisting = AttachController();
        const auto preexistingId = SDL_JoystickGetDeviceInstanceID(preexisting);
        Require(manager.Initialize(""), "initialize alongside external owner");
        Require(manager.GetConnectedSDLGamepadsForPort(0).contains(preexistingId), "discover preconnected device");
        manager.Shutdown();
        Require(SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0, "preserve external subsystem reference");
        Require(SDL_GameControllerFromInstanceID(preexistingId) == nullptr, "close only owned handle");
        Require(manager.Initialize("missing-controller-db.txt"), "built-in mappings survive missing optional database");
        CheckInput(manager.GetConnectedSDLGamepadsForPort(0).at(preexistingId));
        SDL_Quit();
        manager.Shutdown();
        std::cout << "PASS: initialization, mappings, buttons, sticks, refresh, hotplug and ownership\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
