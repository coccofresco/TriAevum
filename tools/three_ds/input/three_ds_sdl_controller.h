#pragma once

#include "three_ds_input.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace ThreeDsRecomp::Input {

// Borrow handles from the host's sole SDL device owner. No event pump, open,
// close or second controller database belongs in a title input consumer.
struct SdlControllerSelection {
    SDL_GameController* Controller = nullptr;
    std::int32_t InstanceId = -1;
    std::vector<DeviceDescriptor> Devices;
};

inline SdlControllerSelection ResolveSdlController(
    const std::unordered_map<std::int32_t, SDL_GameController*>& connected,
    std::string_view guid, std::string_view serial, std::int32_t previousInstance) {
    SdlControllerSelection result;
    for (const auto& [id, controller] : connected) {
        if (!controller || SDL_GameControllerGetAttached(controller) != SDL_TRUE) continue;
        char text[33]{};
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(controller)),
                                 text, sizeof(text));
        DeviceDescriptor device;
        device.InstanceId = id;
        device.Guid = text;
        const char* name = SDL_GameControllerName(controller);
        device.Name = name ? name : text;
#if SDL_VERSION_ATLEAST(2, 0, 14)
        const char* deviceSerial = SDL_GameControllerGetSerial(controller);
        if (deviceSerial) device.Serial = NormalizeControllerSerial(deviceSerial);
        device.HasGyroscope = SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) == SDL_TRUE;
        device.HasAccelerometer = SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL) == SDL_TRUE;
        device.TouchpadCount = std::max(0, SDL_GameControllerGetNumTouchpads(controller));
#endif
        result.Devices.push_back(std::move(device));
    }
    std::sort(result.Devices.begin(), result.Devices.end(), [](const auto& a, const auto& b) {
        return a.InstanceId < b.InstanceId;
    });
    result.InstanceId = SelectControllerDevice(result.Devices, guid, serial, previousInstance);
    for (auto& device : result.Devices) device.Selected = device.InstanceId == result.InstanceId;
    if (result.InstanceId >= 0) result.Controller = connected.at(result.InstanceId);
    return result;
}

inline bool IsSdlControllerButtonHeld(SDL_GameController* controller,
                                      GamepadButton button, std::int16_t triggerThreshold) {
    if (!controller || SDL_GameControllerGetAttached(controller) != SDL_TRUE) return false;
    if (button == GamepadButton::LeftTrigger || button == GamepadButton::RightTrigger) {
        return SDL_GameControllerGetAxis(controller, button == GamepadButton::LeftTrigger
            ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > triggerThreshold;
    }
    constexpr std::array buttons{
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    };
    const auto index = static_cast<int>(button);
    return index >= 0 && index < static_cast<int>(buttons.size()) &&
        SDL_GameControllerGetButton(controller, buttons[index]) != 0;
}

// Convert SDL's down-positive axes, rad/s and m/s^2 once at the shared 3DS
// boundary. UI capture can mute axes while retaining sensor calibration.
inline void SampleSdlController(SDL_GameController* controller,
                                PhysicalInputState& state, bool readAxes = true,
                                std::int32_t touchpadIndex = 0,
                                std::int16_t triggerThreshold = 8192) {
    state.LeftStickX = state.LeftStickY = state.RightStickX = state.RightStickY = 0;
    state.ControllerMotion = {};
    state.ControllerTouch = {};
    state.ControllerButtons = 0;
    state.ControllerPressed = 0;
    if (!controller || SDL_GameControllerGetAttached(controller) != SDL_TRUE) return;
    if (readAxes) {
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX && i < 32; ++i)
            if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(i)))
                state.ControllerButtons |= std::uint32_t{1} << i;
        if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > triggerThreshold)
            state.ControllerButtons |= std::uint32_t{1} << 30;
        if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > triggerThreshold)
            state.ControllerButtons |= std::uint32_t{1} << 31;
        const auto invert = [](std::int16_t value) {
            return static_cast<std::int16_t>(std::clamp(-static_cast<std::int32_t>(value), -32767, 32767));
        };
        state.LeftStickX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
        state.LeftStickY = invert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
        state.RightStickX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
        state.RightStickY = invert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY));
    }
#if SDL_VERSION_ATLEAST(2, 0, 14)
    const auto sample = [controller](SDL_SensorType type, std::array<float, 3>& data, std::uint64_t& timestamp) {
        if (SDL_GameControllerHasSensor(controller, type) != SDL_TRUE) return false;
        if (SDL_GameControllerIsSensorEnabled(controller, type) != SDL_TRUE &&
            SDL_GameControllerSetSensorEnabled(controller, type, SDL_TRUE) != 0) return false;
#if SDL_VERSION_ATLEAST(2, 26, 0)
        Uint64 nativeTimestamp = 0;
        const bool ok = SDL_GameControllerGetSensorDataWithTimestamp(
            controller, type, &nativeTimestamp, data.data(), static_cast<int>(data.size())) == 0;
        timestamp = nativeTimestamp;
        return ok;
#else
        timestamp = 0;
        return SDL_GameControllerGetSensorData(controller, type, data.data(), static_cast<int>(data.size())) == 0;
#endif
    };
    std::array<float, 3> accel{}, gyro{};
    std::uint64_t accelTimestamp = 0, gyroTimestamp = 0;
    const bool accelValid = sample(SDL_SENSOR_ACCEL, accel, accelTimestamp);
    const bool gyroValid = sample(SDL_SENSOR_GYRO, gyro, gyroTimestamp);
    state.ControllerMotion = ConvertSdlMotion(accel, accelValid, gyro, gyroValid);
    state.ControllerMotion.AccelerometerTimestampMicroseconds = accelTimestamp;
    state.ControllerMotion.GyroscopeTimestampMicroseconds = gyroTimestamp;
    if (readAxes && touchpadIndex >= 0 && touchpadIndex < SDL_GameControllerGetNumTouchpads(controller)) {
        for (int finger = 0; finger < SDL_GameControllerGetNumTouchpadFingers(controller, touchpadIndex); ++finger) {
            Uint8 pressed = 0;
            float x = 0, y = 0, pressure = 0;
            if (SDL_GameControllerGetTouchpadFinger(controller, touchpadIndex, finger, &pressed, &x, &y, &pressure) == 0 && pressed) {
                const NormalizedTouch touch{x, y, true};
                if (MapNormalizedTouch(touch).Pressed) { state.ControllerTouch = touch; break; }
            }
        }
    }
#endif
}

} // namespace ThreeDsRecomp::Input
