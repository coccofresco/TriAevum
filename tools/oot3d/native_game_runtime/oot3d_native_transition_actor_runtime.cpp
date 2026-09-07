#include "oot3d_native_transition_actor_runtime.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

bool ReadU32(const nlohmann::json& object, const char* field, uint32_t& value) {
    const auto found = object.find(field);
    if (found == object.end() ||
        (!found->is_number_unsigned() && !found->is_number_integer())) {
        return false;
    }
    const auto candidate = found->get<int64_t>();
    if (candidate < 0 || candidate > UINT32_MAX) {
        return false;
    }
    value = static_cast<uint32_t>(candidate);
    return true;
}

bool ReadFinite(const nlohmann::json& object, const char* field, double& value) {
    const auto found = object.find(field);
    if (found == object.end() || !found->is_number()) {
        return false;
    }
    value = found->get<double>();
    return std::isfinite(value);
}

bool ContainsMode(const NativeEnHollTriggerContract& contract, uint8_t mode) {
    return std::find(contract.SupportedModes.begin(), contract.SupportedModes.end(), mode) !=
           contract.SupportedModes.end();
}

} // namespace

NativeEnHollTriggerContract ResolveNativeEnHollTriggerContract(
    const nlohmann::json& contract) {
    NativeEnHollTriggerContract result;
    if (!contract.is_object() ||
        contract.value("format", "") != "oot3d_enholl_native_runtime_contract_v1" ||
        contract.value("status", "") != "room_request_modes_4_6_complete") {
        result.Status = "native_enholl_contract_unavailable";
        return result;
    }
    const auto profile = contract.find("actor_profile");
    const auto parameters = contract.find("parameter_layout");
    const auto actions = contract.find("action_table");
    const auto trigger = contract.find("room_request_trigger");
    if (profile == contract.end() || !profile->is_object() ||
        parameters == contract.end() || !parameters->is_object() ||
        actions == contract.end() || !actions->is_object() ||
        trigger == contract.end() || !trigger->is_object()) {
        result.Status = "native_enholl_contract_invalid";
        return result;
    }

    uint32_t actorId = 0;
    uint32_t category = 0;
    uint32_t actionShift = 0;
    uint32_t actionMask = 0;
    uint32_t transitionShift = 0;
    uint32_t narrowMode = 0;
    uint32_t entryCount = 0;
    if (!ReadU32(*profile, "actor_id", actorId) || actorId > UINT16_MAX ||
        !ReadU32(*profile, "category", category) || category > UINT8_MAX ||
        !ReadU32(*profile, "instance_size", result.InstanceSize) ||
        !ReadU32(*profile, "init_address", result.InitAddress) ||
        !ReadU32(*profile, "destroy_address", result.DestroyAddress) ||
        !ReadU32(*profile, "update_address", result.UpdateAddress) ||
        !ReadU32(*profile, "draw_address", result.DrawAddress) ||
        !ReadU32(*parameters, "action_selector_shift", actionShift) ||
        actionShift > 15 || !ReadU32(*parameters, "action_selector_mask", actionMask) ||
        actionMask > UINT8_MAX ||
        !ReadU32(*parameters, "transition_index_shift", transitionShift) ||
        transitionShift > 15 || !ReadU32(*actions, "entry_count", entryCount) ||
        !actions->contains("handlers") || !actions->at("handlers").is_array() ||
        actions->at("handlers").size() != entryCount ||
        !ReadU32(*trigger, "handler_address", result.HandlerAddress) ||
        !ReadU32(*trigger, "next_action_address", result.NextActionAddress) ||
        !ReadU32(*trigger, "narrow_mode", narrowMode) || narrowMode > UINT8_MAX ||
        !trigger->contains("supported_modes") ||
        !trigger->at("supported_modes").is_array() ||
        !trigger->contains("bounds") || !trigger->at("bounds").is_object()) {
        result.Status = "native_enholl_contract_invalid";
        return result;
    }
    result.ActorId = static_cast<uint16_t>(actorId);
    result.Category = static_cast<uint8_t>(category);
    result.ActionSelectorShift = static_cast<uint8_t>(actionShift);
    result.ActionSelectorMask = static_cast<uint8_t>(actionMask);
    result.TransitionIndexShift = static_cast<uint8_t>(transitionShift);
    result.NarrowMode = static_cast<uint8_t>(narrowMode);

    for (const auto& value : actions->at("handlers")) {
        uint32_t address = 0;
        if ((!value.is_number_unsigned() && !value.is_number_integer()) ||
            value.get<int64_t>() <= 0 || value.get<int64_t>() > UINT32_MAX) {
            result.Status = "native_enholl_contract_invalid";
            return result;
        }
        address = value.get<uint32_t>();
        result.ActionHandlers.push_back(address);
    }
    for (const auto& value : trigger->at("supported_modes")) {
        if ((!value.is_number_unsigned() && !value.is_number_integer()) ||
            value.get<int64_t>() < 0 || value.get<int64_t>() > UINT8_MAX) {
            result.Status = "native_enholl_contract_invalid";
            return result;
        }
        result.SupportedModes.push_back(value.get<uint8_t>());
    }
    const auto& bounds = trigger->at("bounds");
    if (!ReadFinite(bounds, "vertical_min", result.VerticalMin) ||
        !ReadFinite(bounds, "vertical_max", result.VerticalMax) ||
        !ReadFinite(bounds, "absolute_depth_min", result.AbsoluteDepthMin) ||
        !ReadFinite(bounds, "absolute_depth_max", result.AbsoluteDepthMax) ||
        !ReadFinite(bounds, "default_half_width", result.DefaultHalfWidth) ||
        !ReadFinite(bounds, "narrow_half_width", result.NarrowHalfWidth) ||
        !bounds.contains("strict_comparisons") ||
        !bounds.at("strict_comparisons").is_boolean() ||
        result.VerticalMin >= result.VerticalMax ||
        result.AbsoluteDepthMin >= result.AbsoluteDepthMax ||
        result.DefaultHalfWidth <= 0.0 || result.NarrowHalfWidth <= 0.0) {
        result.Status = "native_enholl_contract_invalid";
        return result;
    }
    result.StrictComparisons = bounds.at("strict_comparisons").get<bool>();
    if (!result.StrictComparisons || result.ActionHandlers.empty() ||
        result.SupportedModes.empty() ||
        std::any_of(result.SupportedModes.begin(), result.SupportedModes.end(),
                    [&](uint8_t mode) {
                        return mode >= result.ActionHandlers.size() ||
                               result.ActionHandlers[mode] != result.HandlerAddress;
                    })) {
        result.Status = "native_enholl_contract_invalid";
        return result;
    }
    result.Available = true;
    result.Status = "native_enholl_room_request_contract_ready";
    return result;
}

uint8_t ResolveNativeEnHollActionMode(const NativeEnHollTriggerContract& contract,
                                      int16_t params) {
    const uint16_t raw = static_cast<uint16_t>(params);
    return static_cast<uint8_t>((raw >> contract.ActionSelectorShift) &
                                contract.ActionSelectorMask);
}

NativeEnHollTriggerEvaluation EvaluateNativeEnHollRoomRequestTrigger(
    const NativeEnHollTriggerContract& contract,
    const NativeEnHollTriggerInput& input) {
    NativeEnHollTriggerEvaluation result;
    if (!contract.Available || !ContainsMode(contract, input.Mode)) {
        result.Status = "native_enholl_action_mode_not_implemented";
        return result;
    }
    result.Supported = true;
    result.HalfWidth = input.Mode == contract.NarrowMode
                           ? contract.NarrowHalfWidth
                           : contract.DefaultHalfWidth;

    constexpr double kS16AngleToRadians = 3.14159265358979323846 / 32768.0;
    const double yaw = static_cast<double>(input.ActorYaw) * kS16AngleToRadians;
    const double cosine = std::cos(yaw);
    const double sine = std::sin(yaw);
    const double deltaX = input.FocusPosition.X - input.ActorPosition.X;
    const double deltaZ = input.FocusPosition.Z - input.ActorPosition.Z;
    result.LocalPosition = {
        deltaX * cosine - deltaZ * sine,
        input.FocusPosition.Y - input.ActorPosition.Y,
        deltaX * sine + deltaZ * cosine,
    };
    const double absoluteX = std::abs(result.LocalPosition.X);
    const double absoluteZ = std::abs(result.LocalPosition.Z);
    result.InsideTrigger =
        input.SpecialBypass ||
        (result.LocalPosition.Y > contract.VerticalMin &&
         result.LocalPosition.Y < contract.VerticalMax &&
         absoluteX < result.HalfWidth &&
         absoluteZ > contract.AbsoluteDepthMin &&
         absoluteZ < contract.AbsoluteDepthMax);
    result.Side = result.LocalPosition.Z < 0.0 ? 0 : 1;
    result.TargetRoom = result.Side == 0 ? input.FrontRoom : input.BackRoom;
    result.RoomRequestRequired = result.InsideTrigger && result.TargetRoom >= 0 &&
                                 result.TargetRoom != input.CurrentRoom;
    result.Status = result.RoomRequestRequired
                        ? "native_enholl_room_request_required"
                        : result.InsideTrigger
                              ? "native_enholl_target_room_already_current"
                              : "native_enholl_outside_trigger";
    return result;
}

} // namespace Oot3dNativeGame
