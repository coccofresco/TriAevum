#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Oot3dNativeGame {

class NativeA32ExecutionRuntime;

struct NativeNpcTrackingPreset {
    int16_t HeadYawLimit = 0;
    int16_t HeadPitchMin = 0;
    int16_t HeadPitchMax = 0;
    int16_t TorsoYawLimit = 0;
    int16_t TorsoPitchMin = 0;
    int16_t TorsoPitchMax = 0;
    bool RotateActor = false;
    float AutoTurnDistance = 0.0f;
    int16_t AutoTurnYawThreshold = 0;
};

struct NativeNpcTrackingMode {
    uint8_t Mode = 0;
    bool Head = false;
    bool Torso = false;
    bool RotateActor = false;
};

struct NativeNpcTrackingRoute {
    uint32_t QuestStateIndex = 0;
    uint32_t Subtype = 0;
    uint32_t PresetIndex = 0;
    std::string ModePolicy;
    uint8_t InitialForcedMode = 0;
    std::string EngagedModePolicy;
    uint8_t EngagedForcedMode = 0;
    uint8_t FacingMode = 0;
    uint8_t NotFacingMode = 0;
    uint32_t EvidenceCallSite = 0;
};

struct NativeNpcTrackingContract {
    bool Available = false;
    uint32_t ServiceAddress = 0;
    uint32_t ServiceSize = 0;
    std::string ServiceSha256;
    uint32_t SelectorAddress = 0;
    uint32_t SelectorSize = 0;
    std::string SelectorSha256;
    uint32_t RandomStateAddress = 0;
    uint32_t ActorMinimumSize = 0;
    uint32_t ActorWorldPositionOffset = 0;
    uint32_t ActorShapeYawOffset = 0;
    uint32_t InteractInfoSize = 0;
    uint32_t InteractTalkStateOffset = 0;
    uint32_t InteractTrackingModeOffset = 0;
    uint32_t InteractAutoTurnTimerOffset = 0;
    uint32_t InteractAutoTurnStateOffset = 0;
    uint32_t InteractHeadPitchOffset = 0;
    uint32_t InteractHeadYawOffset = 0;
    uint32_t InteractTorsoPitchOffset = 0;
    uint32_t InteractTorsoYawOffset = 0;
    uint32_t InteractTargetHeightOffset = 0;
    uint32_t InteractTargetPositionOffset = 0;
    uint32_t GlobalContextPointerAddress = 0;
    uint32_t GlobalContextMinimumSize = 0;
    uint32_t GlobalContextUpdateRateOffset = 0;
    int16_t GlobalContextUpdateRate = 0;
    int16_t SmoothScale = 0;
    int16_t SmoothMaxStep = 0;
    int16_t SmoothMinStep = 0;
    int16_t FacingThreshold = 0;
    std::vector<NativeNpcTrackingPreset> Presets;
    std::vector<NativeNpcTrackingMode> Modes;
    std::vector<std::vector<float>> TargetHeights;
    std::vector<NativeNpcTrackingRoute> InitialRoutes;
    std::string Status;
    std::string Error;
};

struct NativeNpcTrackingState {
    int16_t TrackingMode = 0;
    int16_t HeadPitch = 0;
    int16_t HeadYaw = 0;
    int16_t TorsoPitch = 0;
    int16_t TorsoYaw = 0;
    int16_t ShapeYaw = 0;
    int16_t AutoTurnTimer = 0;
    int16_t AutoTurnState = 0;
    double PendingTicks = 0.0;
    uint64_t UpdateCount = 0;
    std::string Status;
    std::string Error;
};

struct NativeNpcTrackingTickInput {
    uint32_t QuestStateIndex = 0;
    uint32_t Subtype = 0;
    int16_t TalkState = 0;
    double ActorX = 0.0;
    double ActorY = 0.0;
    double ActorZ = 0.0;
    double TargetX = 0.0;
    double TargetY = 0.0;
    double TargetZ = 0.0;
};

NativeNpcTrackingContract ResolveNativeNpcTrackingContract(
    const nlohmann::json& enKoContract);
bool UpdateNativeNpcTrackingTick(const NativeNpcTrackingContract& contract,
                                 const NativeNpcTrackingTickInput& input,
                                 NativeNpcTrackingState& state,
                                 uint32_t& randomState,
                                 NativeA32ExecutionRuntime& nativeExecution);

} // namespace Oot3dNativeGame
