#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"

namespace Oot3dNativeGame {

struct NativeEnHollTriggerContract {
    bool Available = false;
    uint16_t ActorId = 0;
    uint8_t Category = 0;
    uint32_t InstanceSize = 0;
    uint32_t InitAddress = 0;
    uint32_t DestroyAddress = 0;
    uint32_t UpdateAddress = 0;
    uint32_t DrawAddress = 0;
    uint8_t ActionSelectorShift = 0;
    uint8_t ActionSelectorMask = 0;
    uint8_t TransitionIndexShift = 0;
    uint8_t NarrowMode = 0;
    uint32_t HandlerAddress = 0;
    uint32_t NextActionAddress = 0;
    std::vector<uint32_t> ActionHandlers;
    std::vector<uint8_t> SupportedModes;
    double VerticalMin = 0.0;
    double VerticalMax = 0.0;
    double AbsoluteDepthMin = 0.0;
    double AbsoluteDepthMax = 0.0;
    double DefaultHalfWidth = 0.0;
    double NarrowHalfWidth = 0.0;
    bool StrictComparisons = false;
    std::string Status;
    std::string Error;
};

struct NativeEnHollTriggerInput {
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 ActorPosition;
    int16_t ActorYaw = 0;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 FocusPosition;
    int32_t FrontRoom = -1;
    int32_t BackRoom = -1;
    int32_t CurrentRoom = -1;
    uint8_t Mode = 0;
    bool SpecialBypass = false;
};

struct NativeEnHollTriggerEvaluation {
    bool Supported = false;
    bool InsideTrigger = false;
    bool RoomRequestRequired = false;
    uint8_t Side = 0;
    int32_t TargetRoom = -1;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 LocalPosition;
    double HalfWidth = 0.0;
    std::string Status;
};

NativeEnHollTriggerContract ResolveNativeEnHollTriggerContract(
    const nlohmann::json& contract);
uint8_t ResolveNativeEnHollActionMode(const NativeEnHollTriggerContract& contract,
                                      int16_t params);
NativeEnHollTriggerEvaluation EvaluateNativeEnHollRoomRequestTrigger(
    const NativeEnHollTriggerContract& contract,
    const NativeEnHollTriggerInput& input);

} // namespace Oot3dNativeGame
