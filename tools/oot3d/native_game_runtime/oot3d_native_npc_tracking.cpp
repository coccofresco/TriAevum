#include "oot3d_native_npc_tracking.h"

#include "oot3d_native_a32_execution.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

bool ReadU32(const nlohmann::json& value, uint32_t& output) {
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        return false;
    }
    const auto decoded = value.get<int64_t>();
    if (decoded < 0 || decoded > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    output = static_cast<uint32_t>(decoded);
    return true;
}

bool ReadU8(const nlohmann::json& value, uint8_t& output) {
    uint32_t decoded = 0;
    if (!ReadU32(value, decoded) || decoded > UINT8_MAX) {
        return false;
    }
    output = static_cast<uint8_t>(decoded);
    return true;
}

bool ReadS16(const nlohmann::json& value, int16_t& output) {
    if (!value.is_number_integer()) {
        return false;
    }
    const auto decoded = value.get<int64_t>();
    if (decoded < INT16_MIN || decoded > INT16_MAX) {
        return false;
    }
    output = static_cast<int16_t>(decoded);
    return true;
}

bool ReadSha256(const nlohmann::json& value, std::string& output) {
    if (!value.is_string()) {
        return false;
    }
    output = value.get<std::string>();
    return output.size() == 64 &&
           std::all_of(output.begin(), output.end(), [](unsigned char character) {
               return std::isxdigit(character) != 0;
           });
}

bool RangeWithin(uint32_t offset, uint32_t size, uint32_t extent) {
    return static_cast<uint64_t>(offset) + size <= extent;
}

const NativeNpcTrackingRoute* FindRoute(const NativeNpcTrackingContract& contract,
                                        uint32_t questState, uint32_t subtype) {
    const auto found = std::find_if(
        contract.InitialRoutes.begin(), contract.InitialRoutes.end(),
        [questState, subtype](const auto& route) {
            return route.QuestStateIndex == questState && route.Subtype == subtype;
        });
    return found == contract.InitialRoutes.end() ? nullptr : &*found;
}

const NativeNpcTrackingMode* FindMode(const NativeNpcTrackingContract& contract,
                                      uint8_t mode) {
    const auto found = std::find_if(
        contract.Modes.begin(), contract.Modes.end(),
        [mode](const auto& candidate) { return candidate.Mode == mode; });
    return found == contract.Modes.end() ? nullptr : &*found;
}

uint8_t ResolveForcedMode(const NativeNpcTrackingRoute& route,
                          const NativeNpcTrackingTickInput& input) {
    std::string_view policy = route.ModePolicy;
    uint8_t fixedMode = route.InitialForcedMode;
    if (policy == "talk_state_zero" && input.TalkState != 0) {
        policy = route.EngagedModePolicy;
        fixedMode = route.EngagedForcedMode;
    }
    if (policy == "fixed" || policy == "talk_state_zero") {
        return fixedMode;
    }
    if (policy == "facing_threshold") {
        return 0;
    }
    return 0;
}

} // namespace

NativeNpcTrackingContract ResolveNativeNpcTrackingContract(
    const nlohmann::json& enKoContract) {
    NativeNpcTrackingContract result;
    try {
    const auto runtimeIt = enKoContract.find("tracking_runtime");
    if (runtimeIt == enKoContract.end() || !runtimeIt->is_object() ||
        runtimeIt->value("status", "") !=
            "initial_child_start_routes_recovered") {
        result.Status = "native_npc_tracking_contract_unavailable";
        return result;
    }
    const auto& runtime = *runtimeIt;
    const auto serviceIt = runtime.find("service");
    const auto actorLayoutIt = runtime.find("actor_layout");
    const auto interactInfoIt = runtime.find("interact_info");
    const auto globalContextIt = runtime.find("global_context");
    const auto smoothingIt = runtime.find("smoothing");
    const auto selectorIt = runtime.find("selector");
    const auto presetsIt = runtime.find("presets");
    const auto modesIt = runtime.find("mode_semantics");
    const auto heightsIt = runtime.find(
        "target_heights_by_subtype_and_quest_state");
    const auto routesIt = runtime.find("initial_routes");
    if (serviceIt == runtime.end() || !serviceIt->is_object() ||
        actorLayoutIt == runtime.end() || !actorLayoutIt->is_object() ||
        interactInfoIt == runtime.end() || !interactInfoIt->is_object() ||
        globalContextIt == runtime.end() || !globalContextIt->is_object() ||
        smoothingIt == runtime.end() || !smoothingIt->is_object() ||
        selectorIt == runtime.end() || !selectorIt->is_object() ||
        presetsIt == runtime.end() || !presetsIt->is_array() ||
        modesIt == runtime.end() || !modesIt->is_array() ||
        heightsIt == runtime.end() || !heightsIt->is_array() ||
        routesIt == runtime.end() || !routesIt->is_array() ||
        selectorIt->value("forced_mode_path", "") !=
            "nonzero_argument_returned_unchanged" ||
        !ReadU32(serviceIt->at("address"), result.ServiceAddress) ||
        !ReadU32(serviceIt->at("size"), result.ServiceSize) ||
        !ReadSha256(serviceIt->at("sha256"), result.ServiceSha256) ||
        !ReadU32(selectorIt->at("address"), result.SelectorAddress) ||
        !ReadU32(selectorIt->at("size"), result.SelectorSize) ||
        !ReadSha256(selectorIt->at("sha256"), result.SelectorSha256) ||
        !ReadU32(selectorIt->at("random_state_address"),
                 result.RandomStateAddress) ||
        !ReadU32(actorLayoutIt->at("minimum_size"),
                 result.ActorMinimumSize) ||
        !ReadU32(actorLayoutIt->at("world_position_f32x3_offset"),
                 result.ActorWorldPositionOffset) ||
        !ReadU32(actorLayoutIt->at("shape_yaw_s16_offset"),
                 result.ActorShapeYawOffset) ||
        !ReadU32(interactInfoIt->at("size"), result.InteractInfoSize) ||
        !ReadU32(interactInfoIt->at("talk_state_s16_offset"),
                 result.InteractTalkStateOffset) ||
        !ReadU32(interactInfoIt->at("tracking_mode_offset"),
                 result.InteractTrackingModeOffset) ||
        !ReadU32(interactInfoIt->at("auto_turn_timer_s16_offset"),
                 result.InteractAutoTurnTimerOffset) ||
        !ReadU32(interactInfoIt->at("auto_turn_state_s16_offset"),
                 result.InteractAutoTurnStateOffset) ||
        !ReadU32(interactInfoIt->at("head_pitch_s16_offset"),
                 result.InteractHeadPitchOffset) ||
        !ReadU32(interactInfoIt->at("head_yaw_s16_offset"),
                 result.InteractHeadYawOffset) ||
        !ReadU32(interactInfoIt->at("torso_pitch_s16_offset"),
                 result.InteractTorsoPitchOffset) ||
        !ReadU32(interactInfoIt->at("torso_yaw_s16_offset"),
                 result.InteractTorsoYawOffset) ||
        !ReadU32(interactInfoIt->at("target_height_f32_offset"),
                 result.InteractTargetHeightOffset) ||
        !ReadU32(interactInfoIt->at("target_position_f32x3_offset"),
                 result.InteractTargetPositionOffset) ||
        !ReadU32(globalContextIt->at("pointer_address"),
                 result.GlobalContextPointerAddress) ||
        !ReadU32(globalContextIt->at("minimum_size"),
                 result.GlobalContextMinimumSize) ||
        !ReadU32(globalContextIt->at("update_rate_s16_offset"),
                 result.GlobalContextUpdateRateOffset) ||
        !ReadS16(globalContextIt->at("update_rate"),
                 result.GlobalContextUpdateRate) ||
        !ReadS16(smoothingIt->at("scale"), result.SmoothScale) ||
        !ReadS16(smoothingIt->at("max_step"), result.SmoothMaxStep) ||
        !ReadS16(smoothingIt->at("min_step"), result.SmoothMinStep) ||
        !ReadS16(selectorIt->at("facing_threshold"), result.FacingThreshold) ||
        result.ServiceAddress == 0 || result.ServiceSize == 0 ||
        result.SelectorAddress == 0 || result.SelectorSize == 0 ||
        result.RandomStateAddress == 0 ||
        !RangeWithin(result.ActorWorldPositionOffset, 12,
                     result.ActorMinimumSize) ||
        !RangeWithin(result.ActorShapeYawOffset, 2,
                     result.ActorMinimumSize) ||
        !RangeWithin(result.InteractTrackingModeOffset, 2,
                     result.InteractInfoSize) ||
        !RangeWithin(result.InteractTalkStateOffset, 2,
                     result.InteractInfoSize) ||
        !RangeWithin(result.InteractAutoTurnTimerOffset, 2,
                     result.InteractInfoSize) ||
        !RangeWithin(result.InteractAutoTurnStateOffset, 2,
                     result.InteractInfoSize) ||
        !RangeWithin(result.InteractHeadPitchOffset, 2,
                     result.InteractInfoSize) ||
        !RangeWithin(result.InteractHeadYawOffset, 2,
                     result.InteractInfoSize) ||
        !RangeWithin(result.InteractTorsoPitchOffset, 2,
                     result.InteractInfoSize) ||
        !RangeWithin(result.InteractTorsoYawOffset, 2,
                     result.InteractInfoSize) ||
        !RangeWithin(result.InteractTargetHeightOffset, 4,
                     result.InteractInfoSize) ||
        !RangeWithin(result.InteractTargetPositionOffset, 12,
                     result.InteractInfoSize) ||
        result.GlobalContextPointerAddress == 0 ||
        !RangeWithin(result.GlobalContextUpdateRateOffset, 2,
                     result.GlobalContextMinimumSize) ||
        result.GlobalContextUpdateRate <= 0 || result.SmoothScale <= 0 ||
        result.SmoothMaxStep <= 0 ||
        result.SmoothMinStep < 0 || result.FacingThreshold <= 0) {
        result.Status = "native_npc_tracking_contract_invalid";
        result.Error = "native smoothing or table fields are malformed";
        return result;
    }

    for (size_t index = 0; index < presetsIt->size(); ++index) {
        const auto& row = presetsIt->at(index);
        uint32_t rowIndex = 0;
        NativeNpcTrackingPreset preset;
        if (!row.is_object() || !ReadU32(row.at("index"), rowIndex) ||
            rowIndex != index ||
            !ReadS16(row.at("head_yaw_limit"), preset.HeadYawLimit) ||
            !ReadS16(row.at("head_pitch_min"), preset.HeadPitchMin) ||
            !ReadS16(row.at("head_pitch_max"), preset.HeadPitchMax) ||
            !ReadS16(row.at("torso_yaw_limit"), preset.TorsoYawLimit) ||
            !ReadS16(row.at("torso_pitch_min"), preset.TorsoPitchMin) ||
            !ReadS16(row.at("torso_pitch_max"), preset.TorsoPitchMax) ||
            !row.at("rotate_actor").is_boolean() ||
            !row.at("auto_turn_distance").is_number() ||
            !ReadS16(row.at("auto_turn_yaw_threshold"),
                     preset.AutoTurnYawThreshold)) {
            result.Status = "native_npc_tracking_contract_invalid";
            result.Error = "native preset row is malformed";
            return result;
        }
        preset.RotateActor = row.at("rotate_actor").get<bool>();
        preset.AutoTurnDistance = row.at("auto_turn_distance").get<float>();
        if (!std::isfinite(preset.AutoTurnDistance) ||
            preset.HeadYawLimit < 0 || preset.TorsoYawLimit < 0 ||
            preset.HeadPitchMin > preset.HeadPitchMax ||
            preset.TorsoPitchMin > preset.TorsoPitchMax) {
            result.Status = "native_npc_tracking_contract_invalid";
            result.Error = "native preset limits are inconsistent";
            return result;
        }
        result.Presets.push_back(preset);
    }

    std::set<uint8_t> modeIds;
    for (const auto& row : *modesIt) {
        NativeNpcTrackingMode mode;
        if (!row.is_object() || !ReadU8(row.at("mode"), mode.Mode) ||
            !row.at("head").is_boolean() || !row.at("torso").is_boolean() ||
            !row.at("rotate_actor").is_boolean() || mode.Mode == 0 ||
            !modeIds.insert(mode.Mode).second) {
            result.Status = "native_npc_tracking_contract_invalid";
            result.Error = "native tracking mode row is malformed";
            return result;
        }
        mode.Head = row.at("head").get<bool>();
        mode.Torso = row.at("torso").get<bool>();
        mode.RotateActor = row.at("rotate_actor").get<bool>();
        result.Modes.push_back(mode);
    }

    for (const auto& row : *heightsIt) {
        if (!row.is_array()) {
            result.Status = "native_npc_tracking_contract_invalid";
            result.Error = "native target-height row is malformed";
            return result;
        }
        std::vector<float> heights;
        for (const auto& value : row) {
            if (!value.is_number()) {
                result.Status = "native_npc_tracking_contract_invalid";
                result.Error = "native target height is malformed";
                return result;
            }
            const float height = value.get<float>();
            if (!std::isfinite(height)) {
                result.Status = "native_npc_tracking_contract_invalid";
                result.Error = "native target height is non-finite";
                return result;
            }
            heights.push_back(height);
        }
        result.TargetHeights.push_back(std::move(heights));
    }

    std::set<std::pair<uint32_t, uint32_t>> routeIds;
    for (const auto& row : *routesIt) {
        NativeNpcTrackingRoute route;
        if (!row.is_object() ||
            !ReadU32(row.at("quest_state_index"), route.QuestStateIndex) ||
            !ReadU32(row.at("subtype"), route.Subtype) ||
            !ReadU32(row.at("preset_index"), route.PresetIndex) ||
            !ReadU8(row.at("initial_forced_mode"), route.InitialForcedMode) ||
            !ReadU32(row.at("evidence_call_site"), route.EvidenceCallSite) ||
            !row.at("mode_policy").is_string() ||
            route.PresetIndex >= result.Presets.size() ||
            route.Subtype >= result.TargetHeights.size() ||
            route.QuestStateIndex >= result.TargetHeights[route.Subtype].size() ||
            !routeIds.emplace(route.QuestStateIndex, route.Subtype).second) {
            result.Status = "native_npc_tracking_contract_invalid";
            result.Error = "native initial route is malformed";
            return result;
        }
        route.ModePolicy = row.at("mode_policy").get<std::string>();
        if (row.contains("engaged_mode_policy") &&
            row.at("engaged_mode_policy").is_string()) {
            route.EngagedModePolicy =
                row.at("engaged_mode_policy").get<std::string>();
        }
        if (row.contains("engaged_forced_mode") &&
            row.at("engaged_forced_mode").is_number()) {
            if (!ReadU8(row.at("engaged_forced_mode"),
                        route.EngagedForcedMode)) {
                result.Status = "native_npc_tracking_contract_invalid";
                return result;
            }
        }
        if (row.contains("facing_mode") && row.at("facing_mode").is_number()) {
            if (!ReadU8(row.at("facing_mode"), route.FacingMode)) {
                result.Status = "native_npc_tracking_contract_invalid";
                return result;
            }
        }
        if (row.contains("not_facing_mode") &&
            row.at("not_facing_mode").is_number()) {
            if (!ReadU8(row.at("not_facing_mode"), route.NotFacingMode)) {
                result.Status = "native_npc_tracking_contract_invalid";
                return result;
            }
        }
        if (route.ModePolicy == "talk_state_zero" &&
            route.EngagedModePolicy.empty()) {
            result.Status = "native_npc_tracking_contract_invalid";
            result.Error = "native talk route has no engaged policy";
            return result;
        }
        result.InitialRoutes.push_back(std::move(route));
    }

    const auto modeExists = [&result](uint8_t mode) {
        return FindMode(result, mode) != nullptr;
    };
    for (const auto& route : result.InitialRoutes) {
        bool routeValid = false;
        if (route.ModePolicy == "fixed") {
            routeValid = route.InitialForcedMode != 0 &&
                         modeExists(route.InitialForcedMode);
        } else if (route.ModePolicy == "facing_threshold") {
            routeValid = modeExists(route.FacingMode) &&
                         modeExists(route.NotFacingMode);
        } else if (route.ModePolicy == "talk_state_zero") {
            routeValid = route.InitialForcedMode != 0 &&
                         modeExists(route.InitialForcedMode) &&
                         ((route.EngagedModePolicy == "fixed" &&
                           route.EngagedForcedMode != 0 &&
                           modeExists(route.EngagedForcedMode)) ||
                          (route.EngagedModePolicy == "facing_threshold" &&
                           modeExists(route.FacingMode) &&
                           modeExists(route.NotFacingMode)));
        }
        if (!routeValid) {
            result.Status = "native_npc_tracking_contract_invalid";
            result.Error = "native initial route mode policy is unsupported";
            return result;
        }
    }
    result.Available = !result.Presets.empty() && !result.Modes.empty() &&
                       !result.InitialRoutes.empty();
    result.Status = result.Available ? "ready" : "native_npc_tracking_contract_empty";
    return result;
    } catch (const nlohmann::json::exception& exception) {
        result.Available = false;
        result.Status = "native_npc_tracking_contract_invalid";
        result.Error = std::string("malformed native tracking JSON: ") +
                       exception.what();
        return result;
    }
}

bool UpdateNativeNpcTrackingTick(const NativeNpcTrackingContract& contract,
                                 const NativeNpcTrackingTickInput& input,
                                 NativeNpcTrackingState& state,
                                 uint32_t& randomState,
                                 NativeA32ExecutionRuntime& nativeExecution) {
    state.Error.clear();
    if (!contract.Available) {
        state.Status = "native_npc_tracking_contract_unavailable";
        return false;
    }
    const auto* route = FindRoute(contract, input.QuestStateIndex, input.Subtype);
    if (route == nullptr || route->PresetIndex >= contract.Presets.size() ||
        input.Subtype >= contract.TargetHeights.size() ||
        input.QuestStateIndex >= contract.TargetHeights[input.Subtype].size()) {
        state.Status = "native_npc_tracking_route_unavailable";
        return false;
    }
    const uint8_t forcedMode = ResolveForcedMode(*route, input);
    if (forcedMode != 0 && FindMode(contract, forcedMode) == nullptr) {
        state.Status = "native_npc_tracking_mode_unavailable";
        return false;
    }
    if (!std::isfinite(input.ActorX) || !std::isfinite(input.ActorY) ||
        !std::isfinite(input.ActorZ) || !std::isfinite(input.TargetX) ||
        !std::isfinite(input.TargetY) || !std::isfinite(input.TargetZ)) {
        state.Status = "native_npc_tracking_input_invalid";
        return false;
    }

    nativeExecution.ResetScratch();
    const auto globalContext = nativeExecution.AllocateScratch(
        contract.GlobalContextMinimumSize, 16);
    const auto actor =
        nativeExecution.AllocateScratch(contract.ActorMinimumSize, 16);
    const auto interact =
        nativeExecution.AllocateScratch(contract.InteractInfoSize, 16);
    if (!globalContext || !actor || !interact) {
        state.Status = "native_npc_tracking_scratch_unavailable";
        return false;
    }
    const auto writeFloat = [&nativeExecution](uint32_t address, float value) {
        return nativeExecution.Write32(address, std::bit_cast<uint32_t>(value));
    };
    const auto writeS16 = [&nativeExecution](uint32_t address, int16_t value) {
        return nativeExecution.Write16(address, static_cast<uint16_t>(value));
    };
    const float targetHeight =
        contract.TargetHeights[input.Subtype][input.QuestStateIndex];
    if (!nativeExecution.Write32(contract.GlobalContextPointerAddress,
                                 *globalContext) ||
        !nativeExecution.Write32(contract.RandomStateAddress, randomState) ||
        !writeS16(*globalContext + contract.GlobalContextUpdateRateOffset,
                  contract.GlobalContextUpdateRate) ||
        !writeFloat(*actor + contract.ActorWorldPositionOffset,
                    static_cast<float>(input.ActorX)) ||
        !writeFloat(*actor + contract.ActorWorldPositionOffset + 4,
                    static_cast<float>(input.ActorY)) ||
        !writeFloat(*actor + contract.ActorWorldPositionOffset + 8,
                    static_cast<float>(input.ActorZ)) ||
        !writeS16(*actor + contract.ActorShapeYawOffset, state.ShapeYaw) ||
        !writeS16(*interact + contract.InteractTalkStateOffset,
                  input.TalkState) ||
        !writeS16(*interact + contract.InteractTrackingModeOffset,
                  state.TrackingMode) ||
        !writeS16(*interact + contract.InteractAutoTurnTimerOffset,
                  state.AutoTurnTimer) ||
        !writeS16(*interact + contract.InteractAutoTurnStateOffset,
                  state.AutoTurnState) ||
        !writeS16(*interact + contract.InteractHeadPitchOffset,
                  state.HeadPitch) ||
        !writeS16(*interact + contract.InteractHeadYawOffset,
                  state.HeadYaw) ||
        !writeS16(*interact + contract.InteractTorsoPitchOffset,
                  state.TorsoPitch) ||
        !writeS16(*interact + contract.InteractTorsoYawOffset,
                  state.TorsoYaw) ||
        !writeFloat(*interact + contract.InteractTargetHeightOffset,
                    targetHeight) ||
        !writeFloat(*interact + contract.InteractTargetPositionOffset,
                    static_cast<float>(input.TargetX)) ||
        !writeFloat(*interact + contract.InteractTargetPositionOffset + 4,
                    static_cast<float>(input.TargetY)) ||
        !writeFloat(*interact + contract.InteractTargetPositionOffset + 8,
                    static_cast<float>(input.TargetZ))) {
        state.Status = "native_npc_tracking_guest_write_failed";
        return false;
    }

    oot3d::recomp::a32::GuestState guestState;
    guestState.r[0] = *actor;
    guestState.r[1] = *interact;
    guestState.r[2] = route->PresetIndex;
    guestState.r[3] = forcedMode;
    const auto call = nativeExecution.Call(contract.ServiceAddress, guestState);
    if (!call.Completed) {
        state.Status = "native_npc_tracking_service_call_failed";
        state.Error = call.Error + ":exit=" +
                      std::to_string(static_cast<uint32_t>(call.Exit.kind)) +
                      ":pc=" + std::to_string(call.Exit.pc) +
                      ":detail=" + std::to_string(call.Exit.detail);
        return false;
    }

    uint16_t trackingMode = 0;
    uint16_t autoTurnTimer = 0;
    uint16_t autoTurnState = 0;
    uint16_t headPitch = 0;
    uint16_t headYaw = 0;
    uint16_t torsoPitch = 0;
    uint16_t torsoYaw = 0;
    uint16_t shapeYaw = 0;
    uint32_t nextRandomState = 0;
    if (!nativeExecution.Read16(
            *interact + contract.InteractTrackingModeOffset, &trackingMode) ||
        !nativeExecution.Read16(*interact + contract.InteractAutoTurnTimerOffset,
                                &autoTurnTimer) ||
        !nativeExecution.Read16(*interact + contract.InteractAutoTurnStateOffset,
                                &autoTurnState) ||
        !nativeExecution.Read16(*interact + contract.InteractHeadPitchOffset,
                                &headPitch) ||
        !nativeExecution.Read16(*interact + contract.InteractHeadYawOffset,
                                &headYaw) ||
        !nativeExecution.Read16(*interact + contract.InteractTorsoPitchOffset,
                                &torsoPitch) ||
        !nativeExecution.Read16(*interact + contract.InteractTorsoYawOffset,
                                &torsoYaw) ||
        !nativeExecution.Read16(*actor + contract.ActorShapeYawOffset,
                                &shapeYaw) ||
        !nativeExecution.Read32(contract.RandomStateAddress,
                                &nextRandomState) ||
        trackingMode > UINT8_MAX ||
        FindMode(contract, static_cast<uint8_t>(trackingMode)) == nullptr) {
        state.Status = "native_npc_tracking_guest_read_failed";
        return false;
    }
    state.TrackingMode = static_cast<int16_t>(trackingMode);
    state.AutoTurnTimer = static_cast<int16_t>(autoTurnTimer);
    state.AutoTurnState = static_cast<int16_t>(autoTurnState);
    state.HeadPitch = static_cast<int16_t>(headPitch);
    state.HeadYaw = static_cast<int16_t>(headYaw);
    state.TorsoPitch = static_cast<int16_t>(torsoPitch);
    state.TorsoYaw = static_cast<int16_t>(torsoYaw);
    state.ShapeYaw = static_cast<int16_t>(shapeYaw);
    randomState = nextRandomState;
    ++state.UpdateCount;
    state.Status = "updated_by_native_npc_tracking_service";
    return true;
}

} // namespace Oot3dNativeGame
