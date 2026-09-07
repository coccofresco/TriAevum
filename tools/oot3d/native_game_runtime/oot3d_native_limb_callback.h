#pragma once

#include "oot3d_native_a32_execution.h"

#include <array>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {

struct NativeEnKoLimbCallbackContract {
    bool Available = false;
    uint32_t CallbackAddress = 0;
    uint32_t CallbackSize = 0;
    std::string CallbackSha256;
    uint32_t ActorStateSize = 0;
    uint32_t TorsoLimbIndex = 0;
    uint32_t HeadLimbIndex = 0;
    uint32_t TorsoRotationOffset = 0;
    uint32_t HeadRotationOffset = 0;
    std::string Status;
    std::string Error;
};

struct NativeEnKoLimbCallbackInput {
    int16_t HeadPitch = 0;
    int16_t HeadYaw = 0;
    int16_t TorsoPitch = 0;
    int16_t TorsoYaw = 0;
};

struct NativeEnKoLimbCallbackState {
    std::array<float, 12> TorsoTransform = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    std::array<float, 12> HeadTransform = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    uint64_t UpdateCount = 0;
    std::string Status = "not_evaluated";
    std::string Error;
};

NativeEnKoLimbCallbackContract ResolveNativeEnKoLimbCallbackContract(
    const nlohmann::json& document);

bool UpdateNativeEnKoLimbCallback(
    const NativeEnKoLimbCallbackContract& contract,
    const NativeEnKoLimbCallbackInput& input,
    NativeEnKoLimbCallbackState& state,
    NativeA32ExecutionRuntime& nativeExecution);

} // namespace Oot3dNativeGame
