#include "oot3d_native_player_collision_action_contract.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

const nlohmann::json& RequireObject(const nlohmann::json& parent, const char* key) {
    if (!parent.contains(key) || !parent.at(key).is_object()) {
        throw std::runtime_error(std::string("player collision/action contract has no ") + key);
    }
    return parent.at(key);
}

double ReadFinite(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || !object.at(key).is_number()) {
        throw std::runtime_error(std::string("player collision/action contract has invalid ") + key);
    }
    const double value = object.at(key).get<double>();
    if (!std::isfinite(value)) {
        throw std::runtime_error(std::string("player collision/action contract has non-finite ") + key);
    }
    return value;
}

uint32_t ReadU32(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) ||
        (!object.at(key).is_number_unsigned() && !object.at(key).is_number_integer())) {
        throw std::runtime_error(std::string("player collision/action contract has invalid ") + key);
    }
    const int64_t value = object.at(key).get<int64_t>();
    if (value < 0 || static_cast<uint64_t>(value) > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string("player collision/action contract overflows ") + key);
    }
    return static_cast<uint32_t>(value);
}

int ReadPositiveInt(const nlohmann::json& object, const char* key) {
    const uint32_t value = ReadU32(object, key);
    if (value == 0 || value > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error(std::string("player collision/action contract has invalid ") + key);
    }
    return static_cast<int>(value);
}

int ReadInt(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || !object.at(key).is_number_integer()) {
        throw std::runtime_error(std::string("player collision/action contract has invalid ") + key);
    }
    const int64_t value = object.at(key).get<int64_t>();
    if (value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        throw std::runtime_error(std::string("player collision/action contract overflows ") + key);
    }
    return static_cast<int>(value);
}

double ReadLiteralValue(const nlohmann::json& literals, const char* key) {
    return ReadFinite(RequireObject(literals, key), "value");
}

int ReadImmediateValue(const nlohmann::json& values, const char* key) {
    return ReadPositiveInt(RequireObject(values, key), "value");
}

const nlohmann::json& FindAgeRecord(const nlohmann::json& ageProperties,
                                    std::string_view age) {
    if (!ageProperties.contains("records") || !ageProperties.at("records").is_array()) {
        throw std::runtime_error("player collision/action contract has no age records");
    }
    const nlohmann::json* found = nullptr;
    for (const auto& record : ageProperties.at("records")) {
        if (record.is_object() && record.value("age", "") == age) {
            if (found != nullptr) {
                throw std::runtime_error("player collision/action contract duplicates an age record");
            }
            found = &record;
        }
    }
    if (found == nullptr) {
        throw std::runtime_error("player collision/action contract has no child age record");
    }
    return *found;
}

double ReadAgeField(const nlohmann::json& record, const char* key) {
    const auto& fields = RequireObject(record, "fields");
    return ReadLiteralValue(fields, key);
}

void RequirePositiveFinite(double value, std::string_view label) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::runtime_error("player collision/action contract has invalid " + std::string(label));
    }
}

} // namespace

PlayerCollisionActionContract PlayerCollisionActionContract::LoadFile(
    const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("could not open player collision/action contract: " + path.string());
    }
    nlohmann::json document;
    stream >> document;
    if (document.value("format", "") !=
            "oot3d_player_collision_action_native_contract_v2" ||
        document.value("status", "") != "ready") {
        throw std::runtime_error("unsupported or incomplete player collision/action contract");
    }

    PlayerCollisionActionContract result;
    const auto& source = RequireObject(document, "source");
    if (!source.contains("code_bin") || !source.at("code_bin").is_string() ||
        !source.contains("code_sha256") || !source.at("code_sha256").is_string()) {
        throw std::runtime_error("player collision/action contract has invalid source provenance");
    }
    result.mCodeBinPath = source.at("code_bin").get<std::string>();
    result.mCodeBinSha256 = source.at("code_sha256").get<std::string>();
    if (!std::filesystem::is_regular_file(result.mCodeBinPath) ||
        result.mCodeBinSha256.size() != 64) {
        throw std::runtime_error("player collision/action contract source is unavailable");
    }

    const auto& actionState = RequireObject(document, "player_action_state_machine");
    if (ReadU32(actionState, "player_init_function_address") != 0x00191844) {
        throw std::runtime_error("player collision/action contract Player_Init route changed");
    }
    auto& actionConfig = result.mActionConfig;
    const auto& startModeSelector = RequireObject(actionState, "start_mode_selector");
    actionConfig.StartModeMask = ReadU32(
        RequireObject(startModeSelector, "params_mask"), "value");
    actionConfig.StartModeShift = ReadU32(
        RequireObject(startModeSelector, "params_shift"), "value");
    if (ReadU32(RequireObject(startModeSelector, "table_index"), "value") != 4) {
        throw std::runtime_error("player collision/action contract start-mode index changed");
    }
    const auto& startModeTable = RequireObject(actionState, "start_mode_table");
    if (ReadU32(startModeTable, "pointer_literal_address") != 0x001924D0 ||
        ReadU32(startModeTable, "runtime_address") != 0x0053C15C ||
        ReadU32(startModeTable, "entry_count") != 16) {
        throw std::runtime_error("player collision/action contract start-mode table changed");
    }

    const auto& sceneEntrance = RequireObject(actionState, "scene_entrance");
    if (ReadU32(sceneEntrance, "setup_function_address") != 0x0033EA74 ||
        ReadU32(sceneEntrance, "action_pointer_literal_address") != 0x0033EBF8 ||
        ReadU32(sceneEntrance, "action_function_address") != 0x00496458) {
        throw std::runtime_error("player collision/action contract scene-entrance route changed");
    }
    const auto& startModes = RequireObject(sceneEntrance, "start_modes");
    const auto& idleMode = RequireObject(startModes, "idle");
    const auto& slowMode = RequireObject(startModes, "move_forward_slow");
    const auto& forwardMode = RequireObject(startModes, "move_forward");
    actionConfig.SceneEntranceIdleStartMode = ReadInt(idleMode, "index");
    actionConfig.SceneEntranceSlowStartMode = ReadInt(slowMode, "index");
    actionConfig.SceneEntranceForwardStartMode = ReadInt(forwardMode, "index");
    if (ReadU32(idleMode, "function_address") != 0x00276344 ||
        ReadU32(slowMode, "function_address") != 0x0025D3F0 ||
        ReadU32(forwardMode, "function_address") != 0x001D0364 ||
        idleMode.value("initial_speed_source", "") !=
            "incoming_runtime_linear_velocity") {
        throw std::runtime_error("player collision/action contract start-mode functions changed");
    }
    actionConfig.SceneEntranceIdleTargetDistance = ReadFinite(
        RequireObject(idleMode, "target_distance"), "value");
    actionConfig.SceneEntranceIdleTimer = ReadInt(
        RequireObject(idleMode, "timer"), "value");
    actionConfig.SceneEntranceSlowSpeedUnitsPerTick = ReadFinite(
        RequireObject(slowMode, "initial_speed"), "value");
    actionConfig.SceneEntranceSlowTargetDistance = ReadFinite(
        RequireObject(slowMode, "target_distance"), "value");
    actionConfig.SceneEntranceSlowTimer = ReadInt(
        RequireObject(slowMode, "timer"), "value");
    actionConfig.SceneEntranceForwardMinimumSpeedUnitsPerTick = ReadFinite(
        RequireObject(forwardMode, "minimum_initial_speed"), "value");
    actionConfig.SceneEntranceForwardTargetDistance = ReadFinite(
        RequireObject(forwardMode, "target_distance"), "value");
    actionConfig.SceneEntranceForwardTimerNumerator = ReadFinite(
        RequireObject(forwardMode, "timer_numerator"), "value");
    actionConfig.SceneEntranceForwardMinimumTimer = ReadInt(
        RequireObject(forwardMode, "minimum_timer"), "value");

    const auto& entranceFloats = RequireObject(sceneEntrance, "action_float_literals");
    actionConfig.SceneEntranceInitialLinearSpeedUnitsPerTick = ReadLiteralValue(
        entranceFloats, "action_initial_linear_speed");
    actionConfig.SceneEntranceDefaultTargetSpeedUnitsPerTick = ReadLiteralValue(
        entranceFloats, "action_default_target_speed");
    actionConfig.SceneEntranceFloorProbeYOffset = ReadLiteralValue(
        entranceFloats, "action_floor_probe_y_offset");
    const auto& entranceImmediates = RequireObject(
        sceneEntrance, "action_immediate_values");
    actionConfig.SceneEntranceTargetCaptureDistance = ReadPositiveInt(
        RequireObject(entranceImmediates, "action_target_capture_distance"), "value");
    actionConfig.SceneEntranceCompletionDistance = ReadPositiveInt(
        RequireObject(entranceImmediates, "action_completion_distance"), "value");

    if (actionConfig.StartModeMask != 0x0F00 || actionConfig.StartModeShift != 8 ||
        actionConfig.SceneEntranceIdleStartMode != 13 ||
        actionConfig.SceneEntranceSlowStartMode != 14 ||
        actionConfig.SceneEntranceForwardStartMode != 15 ||
        actionConfig.SceneEntranceIdleTimer >= 0 ||
        actionConfig.SceneEntranceSlowTimer >= 0 ||
        actionConfig.SceneEntranceForwardTimerNumerator >= 0.0 ||
        actionConfig.SceneEntranceForwardMinimumTimer >= 0 ||
        actionConfig.SceneEntranceTargetCaptureDistance <=
            actionConfig.SceneEntranceCompletionDistance) {
        throw std::runtime_error("player collision/action contract entrance policy is inconsistent");
    }
    for (const auto& [value, label] : {
             std::pair{ actionConfig.SceneEntranceIdleTargetDistance,
                        "scene_entrance_idle_target_distance" },
             std::pair{ actionConfig.SceneEntranceSlowSpeedUnitsPerTick,
                        "scene_entrance_slow_speed" },
             std::pair{ actionConfig.SceneEntranceSlowTargetDistance,
                        "scene_entrance_slow_target_distance" },
             std::pair{ actionConfig.SceneEntranceForwardMinimumSpeedUnitsPerTick,
                        "scene_entrance_forward_minimum_speed" },
             std::pair{ actionConfig.SceneEntranceForwardTargetDistance,
                        "scene_entrance_forward_target_distance" },
             std::pair{ actionConfig.SceneEntranceInitialLinearSpeedUnitsPerTick,
                        "scene_entrance_initial_linear_speed" },
             std::pair{ actionConfig.SceneEntranceDefaultTargetSpeedUnitsPerTick,
                        "scene_entrance_default_target_speed" },
             std::pair{ actionConfig.SceneEntranceFloorProbeYOffset,
                        "scene_entrance_floor_probe_y_offset" },
         }) {
        RequirePositiveFinite(value, label);
    }

    const auto& ageProperties = RequireObject(document, "age_properties");
    if (ReadU32(ageProperties, "pointer_literal_address") != 0x00250AA8 ||
        ReadU32(ageProperties, "record_stride") != 0x134) {
        throw std::runtime_error("player collision/action contract age table layout changed");
    }
    const auto& child = FindAgeRecord(ageProperties, "child");
    auto& config = result.mConfig;
    config.WallCheckRadius = ReadAgeField(child, "wall_check_radius");
    config.LedgeFloorProbeHeight = ReadAgeField(child, "ledge_floor_probe_height");
    config.LedgeType2MinimumY = ReadAgeField(child, "ledge_type_2_minimum_y");
    config.LedgeType3MinimumY = ReadAgeField(child, "ledge_type_3_minimum_y");
    config.LedgeType4MinimumY = ReadAgeField(child, "ledge_type_4_minimum_y");
    config.FallingGrabMinimumLedgeAboveFloor = ReadAgeField(child, "unk_34");

    const auto& detector = RequireObject(document, "ledge_detector");
    const auto& floats = RequireObject(detector, "float_literals");
    const auto& integers = RequireObject(detector, "integer_literals");
    const auto& immediates = RequireObject(detector, "immediate_values");
    const auto& derived = RequireObject(detector, "derived");
    config.StandingWallCheckHeight = ReadLiteralValue(floats, "standing_wall_check_height");
    config.LedgeWallProbeHeight = ReadLiteralValue(floats, "ledge_wall_probe_height");
    config.WallProbeForwardAddend = ReadLiteralValue(floats, "wall_probe_forward_addend");
    config.MinimumLedgeY = ReadLiteralValue(floats, "minimum_ledge_y");
    config.InvalidLedgeY = ReadLiteralValue(floats, "invalid_ledge_y");
    config.CeilingClearanceAboveLedge =
        ReadLiteralValue(floats, "ceiling_clearance_above_ledge");
    config.UpperWallProbeAboveLedge =
        ReadLiteralValue(floats, "upper_wall_probe_above_ledge");
    config.WallYawSpeedScale = ReadLiteralValue(floats, "wall_yaw_speed_scale");
    config.MinimumWallSpeed = ReadLiteralValue(floats, "minimum_wall_speed");
    config.ShapeYawToWallMaximumS16 =
        ReadImmediateValue(immediates, "shape_yaw_to_wall_maximum_s16");
    config.UpperWallYawDifferenceMaximumS16 =
        ReadImmediateValue(immediates, "upper_wall_yaw_difference_maximum_s16");
    config.WallNormalYAbsMaximumExclusive =
        ReadPositiveInt(derived, "wall_normal_y_abs_maximum_exclusive");
    config.FloorNormalYAbsMinimumExclusive =
        ReadPositiveInt(derived, "floor_normal_y_abs_minimum_exclusive");
    config.CurrentWallRejectFlagMask = ReadU32(derived, "current_wall_reject_flag_mask");
    config.UpperWallClearanceFlagMask = ReadU32(derived, "upper_wall_clearance_flag_mask");
    config.MinimumClimbTypeForFallingGrab =
        ReadPositiveInt(derived, "minimum_climb_type_for_falling_grab");
    if (ReadU32(RequireObject(integers, "wall_normal_y_range_constant"), "value") != 1198 ||
        ReadU32(RequireObject(integers, "floor_normal_y_range_constant"), "value") != 56000) {
        throw std::runtime_error("player collision/action contract normal tests are inconsistent");
    }

    const auto& surfaceFlags = RequireObject(document, "surface_wall_flags");
    config.SurfaceBehaviorIndexShift = ReadU32(surfaceFlags, "behavior_index_shift");
    config.SurfaceBehaviorIndexMask = ReadU32(surfaceFlags, "behavior_index_mask");
    if (!surfaceFlags.contains("values") || !surfaceFlags.at("values").is_array() ||
        surfaceFlags.at("values").size() != config.SurfaceWallFlags.size()) {
        throw std::runtime_error("player collision/action contract wall-flags table has wrong size");
    }
    for (size_t index = 0; index < config.SurfaceWallFlags.size(); ++index) {
        if (!surfaceFlags.at("values")[index].is_number_unsigned() &&
            !surfaceFlags.at("values")[index].is_number_integer()) {
            throw std::runtime_error("player collision/action contract has an invalid wall flag");
        }
        const int64_t value = surfaceFlags.at("values")[index].get<int64_t>();
        if (value < 0 || static_cast<uint64_t>(value) > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("player collision/action contract wall flag overflows uint32");
        }
        config.SurfaceWallFlags[index] = static_cast<uint32_t>(value);
    }

    const auto& transition = RequireObject(document, "action_transition_policy");
    if (!transition.contains("falling_grab_requires_descending") ||
        !transition.at("falling_grab_requires_descending").is_boolean() ||
        !transition.contains("falling_grab_requires_forward_speed") ||
        !transition.at("falling_grab_requires_forward_speed").is_boolean()) {
        throw std::runtime_error("player collision/action contract has invalid transition policy");
    }
    config.FallingGrabRequiresDescending =
        transition.at("falling_grab_requires_descending").get<bool>();
    config.FallingGrabRequiresForwardSpeed =
        transition.at("falling_grab_requires_forward_speed").get<bool>();
    config.FallingGrabMaximumFallDistance =
        ReadFinite(transition, "falling_grab_maximum_fall_distance");
    if (transition.value("falling_grab_minimum_ledge_above_floor_age_field", "") !=
        "unk_34") {
        throw std::runtime_error(
            "player collision/action contract has unknown ledge floor-clearance field");
    }
    config.HangWallPlaneOffset = ReadFinite(transition, "hang_wall_plane_offset");
    config.ClimbInputMinimumWallwardDotExclusive =
        ReadFinite(transition, "climb_input_minimum_wallward_dot_exclusive");

    const auto& surfaceClimb = RequireObject(document, "surface_climb");
    if (ReadU32(surfaceClimb, "selector_function_address") != 0x001CF9AC ||
        ReadU32(surfaceClimb, "action_pointer_literal_address") != 0x004C11EC ||
        ReadU32(surfaceClimb, "action_function_address") != 0x004BE20C ||
        surfaceClimb.value("child_top_reach_age_field", "") != "unk_40") {
        throw std::runtime_error("player collision/action contract surface-climb route changed");
    }
    const auto& surfaceClimbFloats = RequireObject(surfaceClimb, "float_literals");
    const auto& surfaceClimbImmediates = RequireObject(surfaceClimb, "immediate_values");
    config.FreeClimbWallFlagMask = ReadU32(
        RequireObject(surfaceClimbImmediates, "free_climb_wall_flag_mask"), "value");
    config.SurfaceClimbHorizontalInputPlaySpeedScale = ReadLiteralValue(
        surfaceClimbFloats, "horizontal_input_play_speed_scale");
    config.SurfaceClimbVerticalInputPlaySpeedScale = ReadLiteralValue(
        surfaceClimbFloats, "vertical_input_play_speed_scale");
    config.SurfaceClimbMinimumPlaySpeed = ReadLiteralValue(
        surfaceClimbFloats, "minimum_play_speed");
    config.SurfaceClimbMaximumPlaySpeed = ReadLiteralValue(
        surfaceClimbFloats, "maximum_play_speed");
    config.SurfaceClimbTopFloorProbeForwardDistance = ReadLiteralValue(
        surfaceClimbFloats, "top_floor_probe_forward_distance");
    config.SurfaceClimbDismountPlaySpeed = ReadLiteralValue(
        surfaceClimbFloats, "dismount_play_speed");
    config.SurfaceClimbBottomDismountFloorDelta = ReadLiteralValue(
        surfaceClimbFloats, "bottom_dismount_floor_delta");
    config.SurfaceClimbTopReachHeight = ReadAgeField(child, "unk_40");

    const auto& groundClimb = RequireObject(document, "ground_climb_entry");
    if (ReadU32(groundClimb, "function_address") != 0x0035150C ||
        ReadU32(groundClimb, "action_wrapper_pointer_literal_address") != 0x00351868 ||
        ReadU32(groundClimb, "action_wrapper_function_address") != 0x004C11A0) {
        throw std::runtime_error("player collision/action contract ground-climb route changed");
    }
    const auto& groundClimbFloats = RequireObject(groundClimb, "float_literals");
    const auto& groundClimbImmediates = RequireObject(groundClimb, "immediate_values");
    config.GroundClimbMinimumYDistanceToLedge = ReadLiteralValue(
        groundClimbFloats, "minimum_y_distance_to_ledge");
    config.GroundClimbRungInterval = ReadLiteralValue(
        groundClimbFloats, "rung_interval");
    config.GroundClimbWallPlaneInset = ReadLiteralValue(
        groundClimbFloats, "wall_plane_inset");
    config.GroundClimbLateralAlignmentMaximumExclusive = static_cast<double>(ReadImmediateValue(
        groundClimbImmediates, "lateral_alignment_maximum_exclusive"));
    config.RegularLadderWallFlagMask = ReadU32(
        RequireObject(groundClimbImmediates, "regular_ladder_wall_flag_mask"), "value");
    config.FreeClimbModeValue = ReadImmediateValue(
        groundClimbImmediates, "free_climb_mode_value");
    config.RegularLadderModeValue = ReadInt(groundClimb, "regular_ladder_mode_value");
    config.GroundClimbInitialPhase = ReadInt(groundClimb, "initial_phase");
    if (ReadU32(RequireObject(groundClimbImmediates, "free_climb_wall_flag_mask"), "value") !=
            config.FreeClimbWallFlagMask ||
        ReadPositiveInt(RequireObject(
            groundClimbImmediates, "regular_climb_start_front_age_field_offset"), "value") !=
            0x104 ||
        ReadPositiveInt(RequireObject(groundClimbImmediates, "initial_phase_magnitude"), "value") !=
            -config.GroundClimbInitialPhase) {
        throw std::runtime_error("player collision/action contract climb entry is inconsistent");
    }

    for (const auto& [value, label] : {
             std::pair{ config.WallCheckRadius, "wall_check_radius" },
             std::pair{ config.StandingWallCheckHeight, "standing_wall_check_height" },
             std::pair{ config.LedgeWallProbeHeight, "ledge_wall_probe_height" },
             std::pair{ config.WallProbeForwardAddend, "wall_probe_forward_addend" },
             std::pair{ config.LedgeFloorProbeHeight, "ledge_floor_probe_height" },
             std::pair{ config.MinimumLedgeY, "minimum_ledge_y" },
             std::pair{ config.InvalidLedgeY, "invalid_ledge_y" },
             std::pair{ config.CeilingClearanceAboveLedge, "ceiling_clearance_above_ledge" },
             std::pair{ config.UpperWallProbeAboveLedge, "upper_wall_probe_above_ledge" },
             std::pair{ config.LedgeType2MinimumY, "ledge_type_2_minimum_y" },
             std::pair{ config.LedgeType3MinimumY, "ledge_type_3_minimum_y" },
             std::pair{ config.LedgeType4MinimumY, "ledge_type_4_minimum_y" },
             std::pair{ config.FallingGrabMaximumFallDistance, "falling_grab_maximum_fall_distance" },
             std::pair{ config.FallingGrabMinimumLedgeAboveFloor,
                        "falling_grab_minimum_ledge_above_floor" },
             std::pair{ config.HangWallPlaneOffset, "hang_wall_plane_offset" },
             std::pair{ config.WallYawSpeedScale, "wall_yaw_speed_scale" },
             std::pair{ config.MinimumWallSpeed, "minimum_wall_speed" },
             std::pair{ config.SurfaceClimbHorizontalInputPlaySpeedScale,
                        "surface_climb_horizontal_input_play_speed_scale" },
             std::pair{ config.SurfaceClimbVerticalInputPlaySpeedScale,
                        "surface_climb_vertical_input_play_speed_scale" },
             std::pair{ config.SurfaceClimbMinimumPlaySpeed,
                        "surface_climb_minimum_play_speed" },
             std::pair{ config.SurfaceClimbMaximumPlaySpeed,
                        "surface_climb_maximum_play_speed" },
             std::pair{ config.SurfaceClimbBottomDismountFloorDelta,
                        "surface_climb_bottom_dismount_floor_delta" },
             std::pair{ config.SurfaceClimbTopReachHeight,
                        "surface_climb_top_reach_height" },
             std::pair{ config.SurfaceClimbTopFloorProbeForwardDistance,
                        "surface_climb_top_floor_probe_forward_distance" },
             std::pair{ config.SurfaceClimbDismountPlaySpeed,
                        "surface_climb_dismount_play_speed" },
             std::pair{ config.GroundClimbMinimumYDistanceToLedge,
                        "ground_climb_minimum_y_distance_to_ledge" },
             std::pair{ config.GroundClimbLateralAlignmentMaximumExclusive,
                        "ground_climb_lateral_alignment_maximum_exclusive" },
             std::pair{ config.GroundClimbRungInterval,
                        "ground_climb_rung_interval" },
             std::pair{ config.GroundClimbWallPlaneInset,
                        "ground_climb_wall_plane_inset" },
         }) {
        RequirePositiveFinite(value, label);
    }
    if (!(config.LedgeType2MinimumY < config.LedgeType3MinimumY &&
          config.LedgeType3MinimumY < config.LedgeType4MinimumY) ||
        config.SurfaceBehaviorIndexShift >= 32 || config.SurfaceBehaviorIndexMask == 0 ||
        config.FreeClimbWallFlagMask == 0 || config.RegularLadderWallFlagMask == 0 ||
        config.RegularLadderModeValue != static_cast<int>(LinkNativeClimbMode::RegularLadder) ||
        config.FreeClimbModeValue != static_cast<int>(LinkNativeClimbMode::FreeSurface) ||
        config.GroundClimbInitialPhase != -2 ||
        config.SurfaceClimbMinimumPlaySpeed > config.SurfaceClimbMaximumPlaySpeed) {
        throw std::runtime_error("player collision/action contract has inconsistent thresholds");
    }
    return result;
}

const std::filesystem::path& PlayerCollisionActionContract::CodeBinPath() const {
    return mCodeBinPath;
}

const std::string& PlayerCollisionActionContract::CodeBinSha256() const {
    return mCodeBinSha256;
}

const LinkNativeCollisionActionConfig& PlayerCollisionActionContract::Config() const {
    return mConfig;
}

const LinkNativePlayerActionConfig& PlayerCollisionActionContract::ActionConfig() const {
    return mActionConfig;
}

} // namespace Oot3dNativeGame
