#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#include <spdlog/spdlog.h>

namespace Ship {
ConnectedPhysicalDeviceManager::ConnectedPhysicalDeviceManager() {
}

ConnectedPhysicalDeviceManager::~ConnectedPhysicalDeviceManager() {
    Shutdown();
}

bool ConnectedPhysicalDeviceManager::Initialize(const std::string& mappingDatabasePath) {
    if (mInitialized) {
        return true;
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        SPDLOG_ERROR("SDL controller initialization failed: {}", SDL_GetError());
        return false;
    }
    mInitialized = true;
    if (!mappingDatabasePath.empty()) {
        const int added = SDL_GameControllerAddMappingsFromFile(mappingDatabasePath.c_str());
        if (added < 0) {
            SPDLOG_WARN("Controller mappings unavailable at '{}': {}. SDL built-in mappings remain active.",
                        mappingDatabasePath, SDL_GetError());
        } else {
            SPDLOG_INFO("Loaded {} SDL controller mappings from '{}'", added, mappingDatabasePath);
        }
    }
    RefreshConnectedSDLGamepads();
    SPDLOG_INFO("SDL controllers initialized: {} joystick(s), {} mapped gamepad(s)",
                SDL_NumJoysticks(), mConnectedSDLGamepads.size());
    return true;
}

void ConnectedPhysicalDeviceManager::Shutdown() {
    // The window host can already have called SDL_Quit, which closes handles.
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0) {
        for (const auto& [id, controller] : mConnectedSDLGamepads) {
            SDL_GameControllerClose(controller);
        }
        if (mInitialized) {
            SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        }
    }
    mInitialized = false;
    mConnectedSDLGamepads.clear();
    mConnectedSDLGamepadNames.clear();
    mIgnoredInstanceIds.clear();
}

std::unordered_map<int32_t, SDL_GameController*>
ConnectedPhysicalDeviceManager::GetConnectedSDLGamepadsForPort(uint8_t portIndex) {
    std::unordered_map<int32_t, SDL_GameController*> result;

    for (const auto& [instanceId, gamepad] : mConnectedSDLGamepads) {
        if (!PortIsIgnoringInstanceId(portIndex, instanceId)) {
            result[instanceId] = gamepad;
        }
    }

    return result;
}

std::unordered_map<int32_t, std::string> ConnectedPhysicalDeviceManager::GetConnectedSDLGamepadNames() {
    return mConnectedSDLGamepadNames;
}

std::unordered_set<int32_t> ConnectedPhysicalDeviceManager::GetIgnoredInstanceIdsForPort(uint8_t portIndex) {
    return mIgnoredInstanceIds[portIndex];
}

bool ConnectedPhysicalDeviceManager::PortIsIgnoringInstanceId(uint8_t portIndex, int32_t instanceId) {
    return GetIgnoredInstanceIdsForPort(portIndex).contains(instanceId);
}

void ConnectedPhysicalDeviceManager::IgnoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId) {
    mIgnoredInstanceIds[portIndex].insert(instanceId);
}

void ConnectedPhysicalDeviceManager::UnignoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId) {
    mIgnoredInstanceIds[portIndex].erase(instanceId);
}

void ConnectedPhysicalDeviceManager::HandlePhysicalDeviceConnect(int32_t sdlDeviceIndex) {
    RefreshConnectedSDLGamepads();
}

void ConnectedPhysicalDeviceManager::HandlePhysicalDeviceDisconnect(int32_t sdlJoystickInstanceId) {
    RefreshConnectedSDLGamepads();
}

void ConnectedPhysicalDeviceManager::RefreshConnectedSDLGamepads() {
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
        return;
    }
    for (auto iterator = mConnectedSDLGamepads.begin(); iterator != mConnectedSDLGamepads.end();) {
        if (SDL_GameControllerGetAttached(iterator->second) == SDL_TRUE) {
            ++iterator;
            continue;
        }
        const auto id = iterator->first;
        SDL_GameControllerClose(iterator->second);
        mConnectedSDLGamepadNames.erase(id);
        for (auto& [port, ignored] : mIgnoredInstanceIds) {
            ignored.erase(id);
        }
        iterator = mConnectedSDLGamepads.erase(iterator);
    }
    static SDL_JoystickGUID sZeroGuid;

    for (int32_t i = 0; i < SDL_NumJoysticks(); i++) {

        SDL_JoystickGUID deviceGUID = SDL_JoystickGetDeviceGUID(i);
        if (SDL_memcmp(&deviceGUID, &sZeroGuid, sizeof(deviceGUID)) == 0) {
            SPDLOG_WARN(
                "Calling SDL JoystickGetDeviceGUID with index ({:d}) returned zero GUID. This is likely due to an "
                "invalid index. Refer to https://wiki.libsdl.org/SDL2/SDL_JoystickGetDeviceGUID for more information.",
                i);
            continue;
        }

        char deviceGuidCStr[33] = "";
        SDL_JoystickGetGUIDString(deviceGUID, deviceGuidCStr, sizeof(deviceGuidCStr));

        if (!SDL_IsGameController(i)) {
            SPDLOG_WARN("SDL Joystick (GUID: {}) not recognized as gamepad."
                        "This is likely due to a missing mapping string in gamecontrollerdb.txt."
                        "Refer to https://github.com/mdqinc/SDL_GameControllerDB for more information.",
                        deviceGuidCStr);
            continue;
        }

        const auto existingId = SDL_JoystickGetDeviceInstanceID(i);
        if (mConnectedSDLGamepads.contains(existingId)) {
            continue;
        }

        auto gamepad = SDL_GameControllerOpen(i);
        if (gamepad == nullptr) {
            SPDLOG_ERROR("SDL GameControllerOpen error (GUID: {}): {}", deviceGuidCStr, SDL_GetError());
            continue;
        }

        auto instanceId = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamepad));
        if (instanceId < 0) {
            SPDLOG_ERROR("SDL JoystickInstanceID error (GUID: {}): {}", deviceGuidCStr, SDL_GetError());
            SDL_GameControllerClose(gamepad);
            continue;
        }

        std::string gamepadName;
        auto name = SDL_GameControllerName(gamepad);
        if (name == nullptr) {
            gamepadName = deviceGuidCStr;
            SPDLOG_WARN("SDL_GameControllerName returned null. Setting name to GUID \"{}\" instead.", gamepadName);
        } else {
            gamepadName = name;
        }

        mConnectedSDLGamepads[instanceId] = gamepad;
        mConnectedSDLGamepadNames[instanceId] = gamepadName;

        for (uint8_t port = 1; port < 4; port++) {
            mIgnoredInstanceIds[port].insert(instanceId);
        }
    }
}
} // namespace Ship
