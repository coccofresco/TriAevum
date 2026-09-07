#include "oot3d_native_camera_config.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

double NativeCameraPercent(double value) {
    return value / 100.0;
}

void MarkNativeCameraTableFallback(NativeCameraConfig& config, const std::string& field) {
    config.NativeCameraTableCompiledFallbackUsed = true;
    if (std::find(config.NativeCameraTableMissingFields.begin(), config.NativeCameraTableMissingFields.end(),
                  field) == config.NativeCameraTableMissingFields.end()) {
        config.NativeCameraTableMissingFields.push_back(field);
    }
}

const nlohmann::json* NativeCameraJsonObject(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_object()) {
        return nullptr;
    }
    return &object.at(key);
}

const nlohmann::json* NativeCameraJsonArray(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_array()) {
        return nullptr;
    }
    return &object.at(key);
}

bool NativeCameraReadDoubleField(const nlohmann::json& object, const char* key, double& target,
                                 NativeCameraConfig& config, const std::string& path) {
    if (object.is_object() && object.contains(key) && object.at(key).is_number()) {
        target = object.at(key).get<double>();
        return true;
    }
    MarkNativeCameraTableFallback(config, path);
    return false;
}

bool NativeCameraReadIntField(const nlohmann::json& object, const char* key, int& target,
                              NativeCameraConfig& config, const std::string& path) {
    if (object.is_object() && object.contains(key) && object.at(key).is_number_integer()) {
        target = object.at(key).get<int>();
        return true;
    }
    MarkNativeCameraTableFallback(config, path);
    return false;
}

bool NativeCameraReadStringField(const nlohmann::json& object, const char* key, std::string& target,
                                 NativeCameraConfig& config, const std::string& path) {
    if (object.is_object() && object.contains(key) && object.at(key).is_string()) {
        target = object.at(key).get<std::string>();
        return true;
    }
    MarkNativeCameraTableFallback(config, path);
    return false;
}

bool NativeCameraReadIntStringKey(const nlohmann::json& object, const std::string& key, int& target,
                                  NativeCameraConfig& config, const std::string& path) {
    if (object.is_object() && object.contains(key) && object.at(key).is_number_integer()) {
        target = object.at(key).get<int>();
        return true;
    }
    MarkNativeCameraTableFallback(config, path);
    return false;
}

bool NativeCameraReadIntArrayField(const nlohmann::json& object, const char* key, std::vector<int>& target,
                                   NativeCameraConfig& config, const std::string& path) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_array()) {
        MarkNativeCameraTableFallback(config, path);
        return false;
    }

    std::vector<int> values;
    for (const auto& item : object.at(key)) {
        if (!item.is_number_integer()) {
            MarkNativeCameraTableFallback(config, path);
            return false;
        }
        values.push_back(item.get<int>());
    }
    target = std::move(values);
    return true;
}

void InstallCompiledNativeCameraFallbackTables(NativeCameraConfig& config) {
    constexpr int kFunctionSettings[] = {
        kNativeCameraSetNormal0,        kNativeCameraSetDungeon0,    kNativeCameraSetNormal3,
        kNativeCameraSetTowerClimb,     kNativeCameraSetPivotCrawlspace,
        kNativeCameraSetPivotInFront,   kNativeCameraSetPrerendFixed,
        kNativeCameraSetPrerendPivot,   kNativeCameraSetCrawlspace,
        kNativeCameraSetStart1,
    };
    config.FunctionBySetting.clear();
    for (int setting : kFunctionSettings) {
        config.FunctionBySetting[setting] = NativeCameraCompiledFunctionForSetting(setting);
    }

    constexpr int kNormalSettings[] = {
        kNativeCameraSetNormal0,
        kNativeCameraSetDungeon0,
        kNativeCameraSetNormal3,
        kNativeCameraSetTowerClimb,
    };
    config.ModeProfiles.clear();
    for (int setting : kNormalSettings) {
        config.ModeProfiles[setting] = NativeCameraCompiledModeProfileForSetting(setting);
    }
}

void ApplyNativeCameraNormal0Profile(NativeCameraConfig& config, const NativeCameraModeProfile& profile) {
    config.Normal0.Set = profile.Set;
    config.Normal0.Mode = profile.Mode;
    config.Normal0.Function = profile.Function;
    config.Normal0.DataYOffset = profile.DataYOffset;
    config.Normal0.DataEyeDistanceMin = profile.DataEyeDistanceMin;
    config.Normal0.DataEyeDistanceMax = profile.DataEyeDistanceMax;
    config.Normal0.DataPitchTargetDegrees = profile.DataPitchTargetDegrees;
    config.Normal0.DataYawUpdateRateTarget = profile.DataYawUpdateRateTarget;
    config.Normal0.DataMaxYawUpdate = profile.DataMaxYawUpdate;
    config.Normal0.DataFovDegrees = profile.DataFovDegrees;
    config.Normal0.DataAtLerpStepScale = profile.DataAtLerpStepScale;
}

void ApplyNativeCameraTableProfile(const nlohmann::json& profileJson, NativeCameraConfig& config) {
    int set = kNativeCameraSetNormal0;
    if (!NativeCameraReadIntField(profileJson, "set", set, config, "normal_mode_profiles[].set")) {
        return;
    }

    NativeCameraModeProfile profile = NativeCameraCompiledModeProfileForSetting(set);
    profile.Set = set;
    NativeCameraReadIntField(profileJson, "mode", profile.Mode, config,
                             "normal_mode_profiles[" + std::to_string(set) + "].mode");
    NativeCameraReadIntField(profileJson, "function", profile.Function, config,
                             "normal_mode_profiles[" + std::to_string(set) + "].function");
    NativeCameraReadStringField(profileJson, "behavior", profile.Behavior, config,
                                "normal_mode_profiles[" + std::to_string(set) + "].behavior");
    NativeCameraReadDoubleField(profileJson, "data_y_offset", profile.DataYOffset, config,
                                "normal_mode_profiles[" + std::to_string(set) + "].data_y_offset");
    NativeCameraReadDoubleField(profileJson, "data_eye_distance_min", profile.DataEyeDistanceMin, config,
                                "normal_mode_profiles[" + std::to_string(set) + "].data_eye_distance_min");
    NativeCameraReadDoubleField(profileJson, "data_eye_distance_max", profile.DataEyeDistanceMax, config,
                                "normal_mode_profiles[" + std::to_string(set) + "].data_eye_distance_max");
    NativeCameraReadDoubleField(profileJson, "data_pitch_target_degrees", profile.DataPitchTargetDegrees, config,
                                "normal_mode_profiles[" + std::to_string(set) + "].data_pitch_target_degrees");
    NativeCameraReadDoubleField(profileJson, "data_yaw_update_rate_target", profile.DataYawUpdateRateTarget,
                                config,
                                "normal_mode_profiles[" + std::to_string(set) + "].data_yaw_update_rate_target");
    NativeCameraReadDoubleField(profileJson, "data_max_yaw_update", profile.DataMaxYawUpdate, config,
                                "normal_mode_profiles[" + std::to_string(set) + "].data_max_yaw_update");
    NativeCameraReadDoubleField(profileJson, "data_fov_degrees", profile.DataFovDegrees, config,
                                "normal_mode_profiles[" + std::to_string(set) + "].data_fov_degrees");
    NativeCameraReadDoubleField(profileJson, "data_at_lerp_step_scale", profile.DataAtLerpStepScale, config,
                                "normal_mode_profiles[" + std::to_string(set) + "].data_at_lerp_step_scale");
    config.ModeProfiles[set] = profile;
    if (set == kNativeCameraSetNormal0) {
        ApplyNativeCameraNormal0Profile(config, profile);
    }
}

void ApplyNativeCameraTable(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, NativeCameraConfig& config) {
    config.NativeCameraTableAvailable = scene.NativeCameraTableAvailable;
    config.NativeCameraTableRepoRootDefaultUsed = scene.NativeCameraTableRepoRootDefaultUsed;
    config.NativeCameraTablePath = scene.NativeCameraTablePath.string();
    config.NativeCameraTableFormat = scene.NativeCameraTableFormat;
    config.NativeCameraTableSourceKind = scene.NativeCameraTableSourceKind;

    if (!scene.NativeCameraTableAvailable || !scene.NativeCameraTable.is_object()) {
        MarkNativeCameraTableFallback(config, "native_camera_table");
        return;
    }

    config.Source = "oot3d_native_camera_table_and_scene_collision_bgcam";
    config.NativeCameraTableCompiledFallbackUsed = false;
    config.NativeCameraTableMissingFields.clear();

    const auto& table = scene.NativeCameraTable;
    if (config.NativeCameraTableFormat.empty()) {
        MarkNativeCameraTableFallback(config, "format");
    }
    if (config.NativeCameraTableSourceKind.empty()) {
        MarkNativeCameraTableFallback(config, "source_kind");
    }

    if (const auto* initial = NativeCameraJsonObject(table, "initial_player_settings")) {
        NativeCameraReadDoubleField(*initial, "distance", config.Normal0.InitialDistance, config,
                                    "initial_player_settings.distance");
        NativeCameraReadIntField(*initial, "pitch_s16", config.Normal0.InitialPitchS16, config,
                                 "initial_player_settings.pitch_s16");
    } else {
        MarkNativeCameraTableFallback(config, "initial_player_settings");
    }
    NativeCameraReadDoubleField(table, "reference_player_height", config.Normal0.ReferencePlayerHeight,
                                config, "reference_player_height");

    if (const auto* oreg = NativeCameraJsonObject(table, "oreg")) {
        NativeCameraReadIntStringKey(*oreg, "2", config.Normal0.Oreg2XzOffsetUpdatePercent, config, "oreg.2");
        NativeCameraReadIntStringKey(*oreg, "3", config.Normal0.Oreg3YOffsetUpdatePercent, config, "oreg.3");
        NativeCameraReadIntStringKey(*oreg, "4", config.Normal0.Oreg4FovUpdatePercent, config, "oreg.4");
        NativeCameraReadIntStringKey(*oreg, "5", config.Normal0.Oreg5MaxPitchS16, config, "oreg.5");
        NativeCameraReadIntStringKey(*oreg, "6", config.Normal0.Oreg6RUpdateRateInv, config, "oreg.6");
        NativeCameraReadIntStringKey(*oreg, "7", config.Normal0.Oreg7DefaultPitchUpdateRate, config, "oreg.7");
        NativeCameraReadIntStringKey(*oreg, "8", config.Normal0.Oreg8SpeedRatioPercent, config, "oreg.8");
        NativeCameraReadIntStringKey(*oreg, "25", config.Normal0.Oreg25YawRateLerpPercent, config, "oreg.25");
        NativeCameraReadIntStringKey(*oreg, "26", config.Normal0.Oreg26PitchRateLerpPercent, config, "oreg.26");
        NativeCameraReadIntStringKey(*oreg, "41", config.Normal0.Oreg41AtLerpMinPercent, config, "oreg.41");
        NativeCameraReadIntStringKey(*oreg, "42", config.Normal0.Oreg42AtLerpScalePercent, config, "oreg.42");
        NativeCameraReadIntStringKey(*oreg, "46", config.Normal0.Oreg46YOffsetNorm, config, "oreg.46");
        NativeCameraReadIntStringKey(*oreg, "48", config.Normal0.Oreg48IdleYawCurvePercent, config, "oreg.48");
        NativeCameraReadIntStringKey(*oreg, "49", config.Normal0.Oreg49YawAccelScalePercent, config, "oreg.49");
        NativeCameraReadIntStringKey(*oreg, "50", config.Normal0.Oreg50StartSwingHoldTicks, config, "oreg.50");
        NativeCameraReadIntStringKey(*oreg, "51", config.Normal0.Oreg51StartSwingApproachTicks, config, "oreg.51");
    } else {
        MarkNativeCameraTableFallback(config, "oreg");
    }

    if (const auto* collision = NativeCameraJsonObject(table, "collision_line_test")) {
        NativeCameraReadDoubleField(*collision, "ray_extend", config.CollisionRayExtend, config,
                                    "collision_line_test.ray_extend");
        NativeCameraReadDoubleField(*collision, "surface_push", config.CollisionSurfacePush, config,
                                    "collision_line_test.surface_push");
    } else {
        MarkNativeCameraTableFallback(config, "collision_line_test");
    }

    if (const auto* behaviors = NativeCameraJsonObject(table, "bgcam_behaviors")) {
        NativeCameraReadDoubleField(*behaviors, "prerend_fixed_target_radius", config.PrerendFixedTargetRadius,
                                    config, "bgcam_behaviors.prerend_fixed_target_radius");
        NativeCameraReadDoubleField(*behaviors, "prerend_pivot_yaw_lerp", config.PrerendPivotYawLerp, config,
                                    "bgcam_behaviors.prerend_pivot_yaw_lerp");
        NativeCameraReadDoubleField(*behaviors, "prerend_pivot_max_yaw_step_s16",
                                    config.PrerendPivotMaxYawStepS16, config,
                                    "bgcam_behaviors.prerend_pivot_max_yaw_step_s16");
        NativeCameraReadDoubleField(*behaviors, "unique0_exit_distance", config.Unique0ExitDistance, config,
                                    "bgcam_behaviors.unique0_exit_distance");
        if (const auto* floorNone = NativeCameraJsonObject(*behaviors, "floor_none_default_selector")) {
            NativeCameraReadStringField(*floorNone, "selector", config.FloorNoneDefaultCameraSelector, config,
                                        "bgcam_behaviors.floor_none_default_selector.selector");
            NativeCameraReadIntArrayField(*floorNone, "preferred_settings",
                                          config.FloorNoneDefaultCameraSettings, config,
                                          "bgcam_behaviors.floor_none_default_selector.preferred_settings");
            NativeCameraReadIntArrayField(*floorNone, "start_settings_that_yield",
                                          config.FloorNoneDefaultStartSettingsThatYield, config,
                                          "bgcam_behaviors.floor_none_default_selector.start_settings_that_yield");
            config.FloorNoneDefaultCameraSelectorSupported =
                config.FloorNoneDefaultCameraSelector ==
                    "first_active_camera_with_preferred_setting_when_floor_setting_is_none" &&
                !config.FloorNoneDefaultCameraSettings.empty();
        } else {
            MarkNativeCameraTableFallback(config, "bgcam_behaviors.floor_none_default_selector");
        }
    } else {
        MarkNativeCameraTableFallback(config, "bgcam_behaviors");
    }

    if (const auto* functions = NativeCameraJsonObject(table, "function_by_setting")) {
        for (auto& [setting, function] : config.FunctionBySetting) {
            NativeCameraReadIntStringKey(*functions, std::to_string(setting), function, config,
                                         "function_by_setting." + std::to_string(setting));
        }
    } else {
        MarkNativeCameraTableFallback(config, "function_by_setting");
    }

    const auto compiledModeProfiles = config.ModeProfiles;
    if (const auto* profiles = NativeCameraJsonArray(table, "normal_mode_profiles")) {
        config.ModeProfiles.clear();
        for (const auto& profile : *profiles) {
            if (profile.is_object()) {
                ApplyNativeCameraTableProfile(profile, config);
            }
        }
        constexpr int kRequiredNormalProfiles[] = {
            kNativeCameraSetNormal0,
            kNativeCameraSetDungeon0,
            kNativeCameraSetNormal3,
            kNativeCameraSetTowerClimb,
        };
        for (int setting : kRequiredNormalProfiles) {
            if (config.ModeProfiles.find(setting) == config.ModeProfiles.end()) {
                MarkNativeCameraTableFallback(config, "normal_mode_profiles." + std::to_string(setting));
                const auto compiled = compiledModeProfiles.find(setting);
                config.ModeProfiles[setting] =
                    compiled == compiledModeProfiles.end() ? NativeCameraCompiledModeProfileForSetting(setting)
                                                           : compiled->second;
            }
        }
        ApplyNativeCameraNormal0Profile(config, config.ModeProfiles[kNativeCameraSetNormal0]);
    } else {
        MarkNativeCameraTableFallback(config, "normal_mode_profiles");
        config.ModeProfiles = compiledModeProfiles;
    }
}

} // namespace

NativeCameraModeProfile NativeCameraCompiledModeProfileForSetting(int setting) {
    NativeCameraModeProfile profile;
    profile.Set = setting;
    profile.Mode = kNativeCameraModeNormal;
    switch (setting) {
        case kNativeCameraSetDungeon0:
            profile.Behavior = "normal_follow_dungeon0_norm1";
            profile.DataYOffset = -10.0;
            profile.DataEyeDistanceMin = 150.0;
            profile.DataEyeDistanceMax = 250.0;
            profile.DataPitchTargetDegrees = 5.0;
            profile.DataYawUpdateRateTarget = 10.0;
            profile.DataMaxYawUpdate = 30.0;
            break;
        case kNativeCameraSetNormal3:
            profile.Function = kNativeCameraFuncJump3;
            profile.Behavior = "normal3_jump3";
            profile.DataYOffset = -20.0;
            profile.DataEyeDistanceMin = 280.0;
            profile.DataEyeDistanceMax = 300.0;
            profile.DataPitchTargetDegrees = 20.0;
            profile.DataYawUpdateRateTarget = 15.0;
            profile.DataMaxYawUpdate = 40.0;
            profile.DataAtLerpStepScale = 1.0;
            break;
        case kNativeCameraSetTowerClimb:
            profile.Function = kNativeCameraFuncNorm2;
            profile.Behavior = "tower_climb_norm2";
            profile.DataYOffset = 0.0;
            profile.DataEyeDistanceMin = 120.0;
            profile.DataEyeDistanceMax = 280.0;
            profile.DataPitchTargetDegrees = 12.0;
            profile.DataYawUpdateRateTarget = 8.0;
            profile.DataMaxYawUpdate = 40.0;
            profile.DataAtLerpStepScale = 0.5;
            break;
        case kNativeCameraSetNormal0:
        default:
            profile.Behavior = "normal_follow";
            break;
    }
    return profile;
}

int NativeCameraCompiledFunctionForSetting(int setting) {
    switch (setting) {
        case kNativeCameraSetDungeon0:
            return kNativeCameraFuncNorm1;
        case kNativeCameraSetNormal3:
            return kNativeCameraFuncJump3;
        case kNativeCameraSetTowerClimb:
            return kNativeCameraFuncNorm2;
        case kNativeCameraSetPivotCrawlspace:
            return kNativeCameraFuncFixed2;
        case kNativeCameraSetPivotInFront:
            return kNativeCameraFuncFixed4;
        case kNativeCameraSetPrerendFixed:
            return kNativeCameraFuncFixed3;
        case kNativeCameraSetPrerendPivot:
            return kNativeCameraFuncUnique7;
        case kNativeCameraSetCrawlspace:
            return kNativeCameraFuncSubj4;
        case kNativeCameraSetStart1:
            return kNativeCameraFuncUnique0;
        case kNativeCameraSetNormal0:
        default:
            return kNativeCameraFuncNorm1;
    }
}

bool NativeCameraSettingIsNormalFollowFamily(int setting) {
    return setting == kNativeCameraSetNormal0 || setting == kNativeCameraSetDungeon0 ||
           setting == kNativeCameraSetNormal3 || setting == kNativeCameraSetTowerClimb;
}

NativeCameraModeProfile NativeCameraModeProfileForSetting(const NativeCameraConfig& config, int setting) {
    const auto found = config.ModeProfiles.find(setting);
    if (found != config.ModeProfiles.end()) {
        return found->second;
    }
    return NativeCameraCompiledModeProfileForSetting(setting);
}

int NativeCameraFunctionForSetting(const NativeCameraConfig& config, int setting) {
    const auto found = config.FunctionBySetting.find(setting);
    if (found != config.FunctionBySetting.end()) {
        return found->second;
    }
    return NativeCameraCompiledFunctionForSetting(setting);
}

double NativeCameraScaledDistance(const NativeCameraConfig& config, double cameraDataValue) {
    return cameraDataValue * config.Normal0.CameraUnit;
}

NativeCameraConfig BuildNativeCameraConfig(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene) {
    NativeCameraConfig config;
    InstallCompiledNativeCameraFallbackTables(config);
    ApplyNativeCameraTable(scene, config);
    config.Normal0.PlayerHeight =
        scene.LinkTargetHeight > 0.000001 ? scene.LinkTargetHeight : config.Normal0.ReferencePlayerHeight;
    const double yOffsetNorm = NativeCameraPercent(config.Normal0.Oreg46YOffsetNorm);
    config.Normal0.HeightNorm =
        (1.0 + yOffsetNorm) -
        (yOffsetNorm * (config.Normal0.ReferencePlayerHeight / config.Normal0.PlayerHeight));
    config.Normal0.CameraUnit = config.Normal0.HeightNorm * (config.Normal0.PlayerHeight / 100.0);
    config.Normal0.FocusYOffset = config.Normal0.DataYOffset * config.Normal0.CameraUnit;
    config.Normal0.InitialEyeDistance = config.Normal0.InitialDistance * config.Normal0.CameraUnit;
    config.Normal0.EyeDistanceMin = config.Normal0.DataEyeDistanceMin * config.Normal0.CameraUnit;
    config.Normal0.EyeDistanceMax = config.Normal0.DataEyeDistanceMax * config.Normal0.CameraUnit;
    config.CollisionCameraDataSupported = scene.Collision.Valid && !scene.Collision.SurfaceTypes.empty() &&
                                          !scene.Collision.BgCameras.empty();
    config.CollisionLineTestSupported = scene.Collision.Valid && !scene.Collision.Vertices.empty() &&
                                        !scene.Collision.Polygons.empty();
    config.CollisionPolygonFlagsSupported = config.CollisionLineTestSupported;
    config.StartCameraDataIndex = scene.PlayerStart.Valid ? scene.PlayerStart.CameraDataIndex : -1;
    config.StartCameraDataSupported =
        scene.PlayerStart.Valid &&
        config.StartCameraDataIndex != kNativeCameraIgnoreSentinel &&
        config.StartCameraDataIndex >= 0 &&
        static_cast<size_t>(config.StartCameraDataIndex) < scene.Collision.BgCameras.size() &&
        scene.Collision.BgCameras[static_cast<size_t>(config.StartCameraDataIndex)].Setting != kNativeCameraSetNone;
    return config;
}

bool NativeCameraConfigSupported(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                 const NativeCameraConfig& config) {
    return config.NativeCameraTableAvailable &&
           !config.NativeCameraTableCompiledFallbackUsed &&
           config.Normal0.Set == kNativeCameraSetNormal0 &&
           config.Normal0.Mode == kNativeCameraModeNormal &&
           config.Normal0.Function == kNativeCameraFuncNormal1 &&
           config.Normal0.PlayerHeight > 0.0 &&
           config.Normal0.EyeDistanceMax >= config.Normal0.EyeDistanceMin &&
           config.CollisionLineTestSupported == (scene.Collision.Valid && !scene.Collision.Vertices.empty() &&
                                                 !scene.Collision.Polygons.empty()) &&
           config.CollisionPolygonFlagsSupported == config.CollisionLineTestSupported &&
           config.CollisionCameraDataSupported == (scene.Collision.Valid && !scene.Collision.SurfaceTypes.empty() &&
                                                   !scene.Collision.BgCameras.empty()) &&
           config.StartCameraDataIndex == (scene.PlayerStart.Valid ? scene.PlayerStart.CameraDataIndex : -1);
}
