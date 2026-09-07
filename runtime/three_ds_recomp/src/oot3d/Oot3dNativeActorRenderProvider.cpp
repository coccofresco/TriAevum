#include "three_ds_recomp/oot3d/Oot3dNativeActorRenderProvider.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <exception>
#include <limits>

namespace ThreeDsRecomp::Oot3d {
namespace {

constexpr std::string_view kManifestResource =
    "oot3d/catalog/shards/oot3d-actors-native.json";
constexpr std::string_view kPlayerModelResourceProfile =
    "oot3d/characters/player_model_resource_profile.json";
constexpr std::string_view kEnKoRuntimeContractResource =
    "oot3d/catalog/contracts/oot3d-enko-native-runtime.json";
constexpr std::string_view kEnKoRuntimeContractFormat =
    "oot3d_enko_native_runtime_contract_v1";
constexpr std::string_view kAnimationTimeSourceContractFormat =
    "oot3d_character_animation_time_source_contract_v1";
constexpr std::string_view kAnimationSamplingContractFormat =
    "oot3d_skel_anime_sampling_contract_v1";
constexpr std::string_view kRootMotionOwnershipContractFormat =
    "oot3d_character_root_motion_ownership_contract_v1";

bool ReadJsonU32(const nlohmann::json& value, uint32_t& result) {
    if (value.is_number_unsigned()) {
        const uint64_t candidate = value.get<uint64_t>();
        if (candidate <= std::numeric_limits<uint32_t>::max()) {
            result = static_cast<uint32_t>(candidate);
            return true;
        }
    } else if (value.is_number_integer()) {
        const int64_t candidate = value.get<int64_t>();
        if (candidate >= 0 && candidate <= std::numeric_limits<uint32_t>::max()) {
            result = static_cast<uint32_t>(candidate);
            return true;
        }
    }
    return false;
}

const nlohmann::json* FindIndexedRecord(const nlohmann::json& records, uint32_t index) {
    if (!records.is_array()) {
        return nullptr;
    }
    const auto found = std::find_if(records.begin(), records.end(), [&](const auto& record) {
        uint32_t candidate = 0;
        return record.is_object() && record.contains("index") &&
               ReadJsonU32(record["index"], candidate) && candidate == index;
    });
    return found != records.end() ? &*found : nullptr;
}

bool ReadResourceIdByAge(const nlohmann::json& values, uint8_t ageIndex, uint32_t& resourceId) {
    if (!values.is_array() || ageIndex >= values.size()) {
        return false;
    }
    return ReadJsonU32(values[ageIndex], resourceId);
}

constexpr float kAnimationFrameEpsilon = 0.0001f;
constexpr float kBinangToRadians = 3.14159265358979323846f / 32768.0f;

Matrix4f MultiplyPoseMatrix(const Matrix4f& left, const Matrix4f& right) {
    Matrix4f out{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            for (size_t index = 0; index < 4; ++index) {
                out.M[row][column] += left.M[row][index] * right.M[index][column];
            }
        }
    }
    return out;
}

Matrix4f PoseRotationX(float angle) {
    Matrix4f out{};
    out.M[0][0] = 1.0f;
    out.M[3][3] = 1.0f;
    out.M[1][1] = out.M[2][2] = std::cos(angle);
    out.M[1][2] = -std::sin(angle);
    out.M[2][1] = std::sin(angle);
    return out;
}

Matrix4f PoseRotationZ(float angle) {
    Matrix4f out{};
    out.M[2][2] = 1.0f;
    out.M[3][3] = 1.0f;
    out.M[0][0] = out.M[1][1] = std::cos(angle);
    out.M[0][1] = -std::sin(angle);
    out.M[1][0] = std::sin(angle);
    return out;
}

float WrapAnimationFrame(float frame, float frameSpan) {
    if (!std::isfinite(frame) || !std::isfinite(frameSpan) || frameSpan <= 0.0f) {
        return 0.0f;
    }
    frame = std::fmod(frame, frameSpan);
    return frame < 0.0f ? frame + frameSpan : frame;
}

int ResolveSourceDirection(const NativeActorAnimationTimeInput& input) {
    if (std::isfinite(input.SourcePlaybackSpeed) &&
        std::abs(input.SourcePlaybackSpeed) > kAnimationFrameEpsilon) {
        return input.SourcePlaybackSpeed < 0.0f ? -1 : 1;
    }
    const float sourceRange = input.SourceEndFrame - input.SourceStartFrame;
    return sourceRange < -kAnimationFrameEpsilon ? -1 : 1;
}

float ResolveInitialNativeFrame(const CsabMetadata& animation,
                                const NativeActorAnimationTimeInput& input,
                                int sourceDirection) {
    const float nativeLastFrame = static_cast<float>(animation.FrameCount);
    const float nativeStartFrame = sourceDirection < 0 ? nativeLastFrame : 0.0f;
    const float sourceProgress = std::isfinite(input.SourceFrame) &&
                                         std::isfinite(input.SourceStartFrame)
                                     ? input.SourceFrame - input.SourceStartFrame
                                     : 0.0f;
    const float frame = nativeStartFrame + sourceProgress;
    if (input.PlaybackMode == NativeActorAnimationPlaybackMode::Loop) {
        return WrapAnimationFrame(frame, nativeLastFrame + 1.0f);
    }
    return std::clamp(frame, 0.0f, nativeLastFrame);
}

bool ReadJsonU8(const nlohmann::json& value, uint8_t& result) {
    uint32_t candidate = 0;
    if (!ReadJsonU32(value, candidate) || candidate > std::numeric_limits<uint8_t>::max()) {
        return false;
    }
    result = static_cast<uint8_t>(candidate);
    return true;
}

bool ReadJsonColor(const nlohmann::json& value, std::array<uint8_t, 4>& result) {
    if (!value.is_array() || value.size() != result.size()) {
        return false;
    }
    for (size_t index = 0; index < result.size(); ++index) {
        if (!ReadJsonU8(value[index], result[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

bool NativeActorArchiveSource::Ready() const {
    return Status == "ready" && Source != nullptr;
}

bool NativeActorRenderSource::Ready() const {
    return Status == "ready" && CatalogRecord != nullptr && Archive != nullptr &&
           Archive->Ready() && Model != nullptr;
}

bool NativeActorAnimationSample::Ready() const {
    return Status == "ready" && AnimationRecord != nullptr && Pose.Valid;
}

NativeActorAnimationTimeSourceBinding ResolveNativeActorAnimationTimeSourceBinding(
    const nlohmann::json& profile, std::string_view csabName) {
    NativeActorAnimationTimeSourceBinding result;
    if (!profile.is_object()) {
        result.Status = "actor_animation_time_source_profile_unavailable";
        result.Error = "character runtime profile is not an object";
        return result;
    }
    const auto contractIt = profile.find("animation_time_source_contract");
    if (contractIt == profile.end()) {
        result.Available = true;
        result.Status = "actor_animation_time_source_contract_absent";
        return result;
    }
    result.ContractPresent = true;
    const auto& contract = *contractIt;
    if (!contract.is_object() ||
        contract.value("format", "") != kAnimationTimeSourceContractFormat ||
        contract.value("default_source", "") != "skel_animation_clock") {
        result.Status = "actor_animation_time_source_contract_invalid";
        result.Error = "unsupported character animation time-source contract";
        return result;
    }
    const std::string contractStatus = contract.value("status", "");
    if (contractStatus == "not_provided") {
        result.Available = true;
        result.Status = "actor_animation_time_source_contract_not_provided";
        return result;
    }
    const auto bindingsIt = contract.find("bindings");
    if (contractStatus != "ready" || bindingsIt == contract.end() ||
        !bindingsIt->is_array()) {
        result.Status = "actor_animation_time_source_contract_invalid";
        result.Error = "animation time-source contract is not ready or has no binding array";
        return result;
    }
    for (const auto& binding : *bindingsIt) {
        if (!binding.is_object() || binding.value("csab_name", "") != csabName) {
            continue;
        }
        result.Matched = true;
        result.Source = binding.value("source", "");
        const std::string sampleMode = binding.value("sample_mode", "");
        const auto sourceFrameSpan = binding.find("source_frame_span");
        const auto sampleScale = binding.find("sample_scale");
        const auto sampleOffset = binding.find("sample_offset");
        const bool locomotionCycle =
            result.Source == "player_locomotion_cycle" &&
            sampleMode == "direct_scaled_frame";
        const bool playerTimeline =
            result.Source == "player_skel_animation_timeline" &&
            sampleMode == "direct_clamped_normalized_frame";
        if ((!locomotionCycle && !playerTimeline) ||
            sourceFrameSpan == binding.end() || !sourceFrameSpan->is_number() ||
            sampleScale == binding.end() || !sampleScale->is_number() ||
            sampleOffset == binding.end() || !sampleOffset->is_number()) {
            result.Status = "actor_animation_time_source_binding_invalid";
            result.Error = "animation time-source binding has unsupported or missing fields";
            return result;
        }
        result.SourceFrameSpan = sourceFrameSpan->get<float>();
        result.SampleScale = sampleScale->get<float>();
        result.SampleOffset = sampleOffset->get<float>();
        if (!std::isfinite(result.SourceFrameSpan) || result.SourceFrameSpan <= 0.0f ||
            !std::isfinite(result.SampleScale) || result.SampleScale <= 0.0f ||
            !std::isfinite(result.SampleOffset)) {
            result.Status = "actor_animation_time_source_binding_invalid";
            result.Error = "animation time-source binding contains a non-finite or non-positive scale";
            return result;
        }
        if (playerTimeline) {
            const auto playbackScale = binding.find("scaffold_playback_scale");
            const auto nativeFrameSpan = binding.find("native_frame_span");
            if (binding.value("playback_mode", "") != "once_full_span" ||
                playbackScale == binding.end() || !playbackScale->is_number() ||
                nativeFrameSpan == binding.end() || !nativeFrameSpan->is_number()) {
                result.Status = "actor_animation_time_source_binding_invalid";
                result.Error = "player timeline binding has no supported full-span playback contract";
                return result;
            }
            result.ScaffoldPlaybackScale = playbackScale->get<float>();
            result.NativeFrameSpan = nativeFrameSpan->get<float>();
            if (!std::isfinite(result.ScaffoldPlaybackScale) ||
                result.ScaffoldPlaybackScale <= 0.0f ||
                !std::isfinite(result.NativeFrameSpan) || result.NativeFrameSpan <= 1.0f) {
                result.Status = "actor_animation_time_source_binding_invalid";
                result.Error = "player timeline native frame span or scaffold playback scale is invalid";
                return result;
            }
            result.ClampSample = true;
            result.AdjustScaffoldPlayback = true;
        }
        result.DirectSample = true;
        result.Available = true;
        result.Status = "ready";
        return result;
    }
    result.Available = true;
    result.Status = "actor_animation_time_source_animation_not_bound";
    return result;
}

float ResolveNativeActorAnimationDirectSampleFrame(
    const NativeActorAnimationTimeSourceBinding& binding, float sourceFrame) {
    if (!binding.Available || !binding.DirectSample ||
        !std::isfinite(sourceFrame) || !std::isfinite(binding.SourceFrameSpan) ||
        binding.SourceFrameSpan <= 0.0f || !std::isfinite(binding.SampleScale) ||
        !std::isfinite(binding.SampleOffset)) {
        return 0.0f;
    }
    const float normalizedSourceFrame = binding.ClampSample
                                            ? std::clamp(sourceFrame, 0.0f,
                                                         binding.SourceFrameSpan - 1.0f)
                                            : WrapAnimationFrame(sourceFrame,
                                                                 binding.SourceFrameSpan);
    return normalizedSourceFrame * binding.SampleScale + binding.SampleOffset;
}

float ResolveNativeActorAnimationDirectSampleFrame(
    const NativeActorAnimationTimeSourceBinding& binding, float sourceFrame,
    float runtimeSourceStartFrame, float runtimeSourceEndFrame) {
    if (binding.Source != "player_skel_animation_timeline" ||
        !binding.ClampSample || !std::isfinite(binding.NativeFrameSpan) ||
        binding.NativeFrameSpan <= 1.0f || !std::isfinite(sourceFrame) ||
        !std::isfinite(runtimeSourceStartFrame) ||
        !std::isfinite(runtimeSourceEndFrame) ||
        runtimeSourceEndFrame <= runtimeSourceStartFrame) {
        return ResolveNativeActorAnimationDirectSampleFrame(binding, sourceFrame);
    }
    const float sourceWeight = std::clamp(
        (sourceFrame - runtimeSourceStartFrame) /
            (runtimeSourceEndFrame - runtimeSourceStartFrame),
        0.0f, 1.0f);
    return binding.SampleOffset + sourceWeight * (binding.NativeFrameSpan - 1.0f);
}

float ResolveNativeActorAnimationScaffoldPlaybackSpeed(
    const NativeActorAnimationTimeSourceBinding& binding, float playbackSpeed,
    float sourceStartFrame, float sourceEndFrame, bool playsOnce) {
    if (!binding.Available || !binding.AdjustScaffoldPlayback || !playsOnce ||
        !std::isfinite(playbackSpeed) || !std::isfinite(sourceStartFrame) ||
        !std::isfinite(sourceEndFrame) ||
        std::abs(sourceStartFrame) > 0.001f ||
        std::abs(sourceEndFrame - (binding.SourceFrameSpan - 1.0f)) > 0.001f) {
        return playbackSpeed;
    }
    return playbackSpeed * binding.ScaffoldPlaybackScale;
}

NativeActorResourceVisibility ResolveNativePlayerModelResourceVisibility(
    const nlohmann::json& profile, const CmbModel& model,
    const NativePlayerModelResourceState& state) {
    NativeActorResourceVisibility result;
    if (!profile.is_object() ||
        profile.value("format", "") != "oot3d_player_model_resource_profile_v1" ||
        !profile.contains("body_resource_ids_by_age") ||
        !profile.contains("model_groups") || !profile.contains("model_types")) {
        result.Status = "player_model_resource_profile_invalid";
        return result;
    }
    const auto& bodyByAge = profile["body_resource_ids_by_age"];
    if (!bodyByAge.is_array() || state.AgeIndex >= bodyByAge.size() ||
        !bodyByAge[state.AgeIndex].is_array()) {
        result.Status = "player_model_resource_age_out_of_range";
        return result;
    }
    const auto* modelGroup = FindIndexedRecord(profile["model_groups"], state.ModelGroup);
    if (modelGroup == nullptr || !modelGroup->contains("model_types") ||
        !(*modelGroup)["model_types"].is_array() || (*modelGroup)["model_types"].size() != 4) {
        result.Status = "player_model_group_unresolved";
        return result;
    }
    uint32_t waistType = 0;
    if (!ReadJsonU32((*modelGroup)["model_types"][3], waistType) ||
        waistType > std::numeric_limits<uint8_t>::max()) {
        result.Status = "player_model_group_waist_type_invalid";
        return result;
    }
    result.WaistType = static_cast<uint8_t>(waistType);

    uint8_t maxVisibilityId = 0;
    for (const auto& mesh : model.Meshes) {
        maxVisibilityId = std::max(maxVisibilityId, mesh.VisibilityId);
    }
    result.ResourceVisibility.assign(static_cast<size_t>(maxVisibilityId) + 1, 0);
    const uint32_t sentinel = profile.value("resource_id_sentinel", 0xFFFFFFFFu);
    const auto addResourceId = [&](uint32_t resourceId) -> bool {
        if (resourceId == sentinel) {
            return true;
        }
        if (resourceId >= result.ResourceVisibility.size()) {
            result.Status = "player_model_resource_id_out_of_model_range";
            result.Error = "resource " + std::to_string(resourceId) +
                           " exceeds CMB visibility range " + std::to_string(maxVisibilityId);
            return false;
        }
        if (result.ResourceVisibility[resourceId] == 0) {
            result.ResourceVisibility[resourceId] = 1;
            result.ActiveResourceIds.push_back(static_cast<uint8_t>(resourceId));
        }
        return true;
    };

    for (const auto& value : bodyByAge[state.AgeIndex]) {
        uint32_t resourceId = 0;
        if (!ReadJsonU32(value, resourceId) || !addResourceId(resourceId)) {
            if (result.Status.empty()) {
                result.Status = "player_body_resource_id_invalid";
            }
            return result;
        }
    }

    const std::array<uint8_t, 4> selectedTypes = {
        state.LeftHandType, state.RightHandType, state.SheathType, result.WaistType,
    };
    for (const uint8_t modelTypeIndex : selectedTypes) {
        const auto* modelType = FindIndexedRecord(profile["model_types"], modelTypeIndex);
        if (modelType == nullptr) {
            result.Status = "player_model_type_unresolved";
            result.Error = "model type " + std::to_string(modelTypeIndex);
            return result;
        }
        uint32_t resourceId = sentinel;
        const std::string selectionKind = modelType->value("selection_kind", "age");
        if (selectionKind == "shield_then_age") {
            if (!modelType->contains("shield_variant_resource_ids_by_age") ||
                !(*modelType)["shield_variant_resource_ids_by_age"].is_array() ||
                state.Shield >= (*modelType)["shield_variant_resource_ids_by_age"].size() ||
                !ReadResourceIdByAge(
                    (*modelType)["shield_variant_resource_ids_by_age"][state.Shield],
                    state.AgeIndex, resourceId)) {
                result.Status = "player_model_shield_variant_unresolved";
                return result;
            }
        } else if (selectionKind == "age") {
            if (!modelType->contains("resource_ids_by_age") ||
                !ReadResourceIdByAge((*modelType)["resource_ids_by_age"],
                                     state.AgeIndex, resourceId)) {
                result.Status = "player_model_age_resource_unresolved";
                return result;
            }
        } else {
            result.Status = "player_model_resource_selection_kind_unsupported";
            return result;
        }
        if (!addResourceId(resourceId)) {
            return result;
        }
    }
    std::sort(result.ActiveResourceIds.begin(), result.ActiveResourceIds.end());
    result.Available = true;
    result.Status = "ready";
    return result;
}

NativeActorAnimationBinding ResolveNativeActorAnimationBinding(
    const nlohmann::json& profile,
    const std::vector<NativeActorAnimationBindingIdentity>& identities) {
    NativeActorAnimationBinding result;
    if (!profile.is_object() ||
        profile.value("format", "") != "oot3d_character_runtime_profile_v1" ||
        !profile.contains("animation_lookup") || !profile["animation_lookup"].is_object() ||
        !profile.contains("native_resources") || !profile["native_resources"].is_object() ||
        !profile["native_resources"].contains("animations") ||
        !profile["native_resources"]["animations"].is_array()) {
        result.Status = "actor_animation_binding_profile_invalid";
        return result;
    }

    const auto& lookup = profile["animation_lookup"];
    const auto& animations = profile["native_resources"]["animations"];
    for (const auto& identity : identities) {
        if (identity.LookupName.empty() || identity.Value.empty() ||
            !lookup.contains(identity.LookupName) || !lookup[identity.LookupName].is_object()) {
            continue;
        }
        const auto& table = lookup[identity.LookupName];
        if (!table.contains(identity.Value)) {
            continue;
        }
        const auto& mapped = table[identity.Value];
        int64_t animationIndex = -1;
        if (mapped.is_number_integer()) {
            animationIndex = mapped.get<int64_t>();
        } else if (mapped.is_array()) {
            if (mapped.size() != 1 || !mapped.front().is_number_integer()) {
                result.Status = "actor_animation_binding_ambiguous";
                result.LookupName = identity.LookupName;
                result.LookupValue = identity.Value;
                return result;
            }
            animationIndex = mapped.front().get<int64_t>();
        } else {
            result.Status = "actor_animation_binding_index_invalid";
            result.LookupName = identity.LookupName;
            result.LookupValue = identity.Value;
            return result;
        }
        if (animationIndex < 0 || static_cast<size_t>(animationIndex) >= animations.size() ||
            !animations[static_cast<size_t>(animationIndex)].is_object()) {
            result.Status = "actor_animation_binding_index_out_of_range";
            result.LookupName = identity.LookupName;
            result.LookupValue = identity.Value;
            return result;
        }
        const auto& animation = animations[static_cast<size_t>(animationIndex)];
        result.CsabName = animation.value("csab_name", "");
        result.ResourcePath = animation.value("resource_path", "");
        result.LookupName = identity.LookupName;
        result.LookupValue = identity.Value;
        if (result.CsabName.empty()) {
            result.Status = "actor_animation_binding_csab_missing";
            return result;
        }
        result.Available = true;
        result.Status = "ready";
        return result;
    }
    result.Status = "actor_animation_binding_not_found";
    return result;
}

NativeActorAnimationControllerBinding ResolveNativeActorAnimationControllerBinding(
    const nlohmann::json& profile, std::string_view notificationSemantic,
    uint32_t animationTypeIndex) {
    NativeActorAnimationControllerBinding result;
    if (!profile.is_object() ||
        profile.value("format", "") != "oot3d_character_runtime_profile_v1" ||
        !profile.contains("animation_controller_contract") ||
        !profile["animation_controller_contract"].is_object()) {
        result.Status = "actor_animation_controller_profile_invalid";
        return result;
    }
    const auto& contract = profile["animation_controller_contract"];
    if (contract.value("format", "") != "oot3d_character_animation_controller_contract_v1" ||
        contract.value("status", "") != "ready" ||
        !contract.contains("controllers") || !contract["controllers"].is_array()) {
        result.Status = "actor_animation_controller_contract_unavailable";
        return result;
    }
    for (const auto& controller : contract["controllers"]) {
        if (!controller.is_object() ||
            controller.value("notification_semantic", "") != notificationSemantic) {
            continue;
        }
        result.ControllerId = controller.value("id", "");
        result.NotificationSemantic = controller.value("notification_semantic", "");
        result.PhaseSource = controller.value("phase_source", "");
        result.SourceFrameSpan = controller.value("source_frame_span", 0.0f);
        result.BlendSource = controller.value("blend_source", "");
        result.BlendThreshold = controller.value("blend_threshold", 0.0f);
        result.BlendScale = controller.value("blend_scale", 0.0f);
        result.WarmupSource = controller.value("warmup_source", "");
        if (result.ControllerId.empty() || result.PhaseSource.empty() ||
            result.BlendSource.empty() || result.WarmupSource.empty() ||
            !std::isfinite(result.SourceFrameSpan) || result.SourceFrameSpan <= 0.0f ||
            !std::isfinite(result.BlendThreshold) || !std::isfinite(result.BlendScale) ||
            !controller.contains("variants") || !controller["variants"].is_array()) {
            result.Status = "actor_animation_controller_invalid";
            return result;
        }
        for (const auto& variant : controller["variants"]) {
            if (!variant.is_object() ||
                variant.value("animation_type_index", UINT32_MAX) != animationTypeIndex) {
                continue;
            }
            result.WalkCsabName = variant.value("walk_csab_name", "");
            result.RunCsabName = variant.value("run_csab_name", "");
            if (result.WalkCsabName.empty() || result.RunCsabName.empty()) {
                result.Status = "actor_animation_controller_variant_invalid";
                return result;
            }
            result.Available = true;
            result.Status = "ready";
            return result;
        }
        result.Status = "actor_animation_controller_variant_unavailable";
        return result;
    }
    result.Status = "actor_animation_controller_not_found";
    return result;
}

float ResolveNativeActorAnimationFrame(const CsabMetadata& animation,
                                       const NativeActorAnimationTimeInput& input) {
    if (animation.FrameCount == 0 || !std::isfinite(input.SourceFrame)) {
        return 0.0f;
    }
    const float sourceStart = std::isfinite(input.SourceStartFrame)
                                  ? input.SourceStartFrame
                                  : 0.0f;
    const float sourceEnd = std::isfinite(input.SourceEndFrame)
                                ? input.SourceEndFrame
                                : sourceStart;
    const float sourceLength = sourceEnd - sourceStart;
    if (std::abs(sourceLength) <= 0.0001f) {
        return 0.0f;
    }
    float normalized = (input.SourceFrame - sourceStart) / sourceLength;
    if (input.PlaybackMode == NativeActorAnimationPlaybackMode::Loop) {
        normalized = std::fmod(normalized, 1.0f);
        if (normalized < 0.0f) {
            normalized += 1.0f;
        }
    } else {
        normalized = std::clamp(normalized, 0.0f, 1.0f);
    }
    return normalized * static_cast<float>(animation.FrameCount);
}

float AdvanceNativeActorAnimationClock(const CsabMetadata& animation,
                                       const NativeActorAnimationTimeInput& input,
                                       NativeActorAnimationClockState& state) {
    const int sourceDirection = ResolveSourceDirection(input);
    const bool bindingChanged = !state.Initialized || state.BindingKey != input.BindingKey ||
                                state.SourceDirection != sourceDirection;
    if (bindingChanged) {
        state.Initialized = true;
        state.BindingKey = input.BindingKey;
        state.PreviousSourceFrame = std::isfinite(input.SourceFrame) ? input.SourceFrame : 0.0f;
        state.PreviousSourceUpdateSerial = input.SourceUpdateSerial;
        state.NativeFrame = ResolveInitialNativeFrame(animation, input, sourceDirection);
        state.ObservedSourceStep = 0.0f;
        state.SourceDirection = sourceDirection;
        ++state.BindingResetCount;
        state.Status = "binding_reset";
        return state.NativeFrame;
    }

    uint64_t sourceUpdateSteps = 1;
    if (input.SourceUpdateSerialValid) {
        if (input.SourceUpdateSerial == state.PreviousSourceUpdateSerial) {
            ++state.RenderHoldCount;
            state.Status = "render_hold";
            return state.NativeFrame;
        }
        if (input.SourceUpdateSerial > state.PreviousSourceUpdateSerial) {
            sourceUpdateSteps = input.SourceUpdateSerial - state.PreviousSourceUpdateSerial;
        } else {
            ++state.SourceRestartCount;
            sourceUpdateSteps = 1;
        }
        state.PreviousSourceUpdateSerial = input.SourceUpdateSerial;
    }

    if (!std::isfinite(input.SourceFrame) &&
        (!input.SourceUpdateSerialValid || !std::isfinite(input.SourcePlaybackSpeed))) {
        state.Status = "source_time_non_finite";
        return state.NativeFrame;
    }

    float sourceDelta = std::isfinite(input.SourceFrame)
                            ? input.SourceFrame - state.PreviousSourceFrame
                            : 0.0f;
    const bool movedOpposite = sourceDelta * static_cast<float>(sourceDirection) < -kAnimationFrameEpsilon;
    if (movedOpposite) {
        const float sourceSpan = std::abs(input.SourceEndFrame - input.SourceStartFrame) + 1.0f;
        const float wrappedDelta = sourceDelta + static_cast<float>(sourceDirection) * sourceSpan;
        if (input.PlaybackMode == NativeActorAnimationPlaybackMode::Loop &&
            sourceSpan > 1.0f && std::abs(wrappedDelta) < std::abs(sourceDelta)) {
            sourceDelta = wrappedDelta;
            ++state.SourceLoopCount;
            state.Status = "source_loop";
        } else {
            state.NativeFrame = ResolveInitialNativeFrame(animation, input, sourceDirection);
            state.PreviousSourceFrame = input.SourceFrame;
            state.ObservedSourceStep = 0.0f;
            ++state.SourceRestartCount;
            state.Status = "source_restart";
            return state.NativeFrame;
        }
    } else if (std::abs(sourceDelta) <= kAnimationFrameEpsilon) {
        if (input.SourceUpdateSerialValid && std::isfinite(input.SourcePlaybackSpeed) &&
            std::abs(input.SourcePlaybackSpeed) > kAnimationFrameEpsilon) {
            sourceDelta = input.SourcePlaybackSpeed * static_cast<float>(sourceUpdateSteps);
            ++state.PlaybackSpeedAdvanceCount;
            state.Status = "advanced_from_playback_speed";
        } else {
            state.Status = "held";
        }
    } else {
        state.Status = "advanced";
    }

    state.ObservedSourceStep = std::abs(sourceDelta);
    if (std::isfinite(input.SourceFrame)) {
        state.PreviousSourceFrame = input.SourceFrame;
    }
    state.NativeFrame += sourceDelta;
    if (input.PlaybackMode == NativeActorAnimationPlaybackMode::Loop) {
        state.NativeFrame = WrapAnimationFrame(
            state.NativeFrame, static_cast<float>(animation.FrameSlotCount()));
    } else {
        state.NativeFrame = std::clamp(
            state.NativeFrame, 0.0f, static_cast<float>(animation.FrameCount));
    }
    return state.NativeFrame;
}

float AdvanceNativeActorAnimationMorphClock(
    const NativeActorAnimationMorphTimeInput& input,
    NativeActorAnimationMorphClockState& state) {
    const float sourceWeight = std::isfinite(input.SourceWeight)
                                   ? std::clamp(input.SourceWeight, 0.0f, 1.0f)
                                   : 0.0f;
    const bool bindingChanged = !state.Initialized || state.BindingKey != input.BindingKey;
    if (bindingChanged) {
        state.Initialized = true;
        state.BindingKey = input.BindingKey;
        state.PreviousSourceWeight = sourceWeight;
        state.PreviousSourceUpdateSerial = input.SourceUpdateSerial;
        state.NativeWeight = sourceWeight;
        ++state.BindingResetCount;
        state.Status = "binding_reset";
        return state.NativeWeight;
    }

    uint64_t sourceUpdateSteps = 1;
    if (input.SourceUpdateSerialValid) {
        if (input.SourceUpdateSerial == state.PreviousSourceUpdateSerial) {
            ++state.RenderHoldCount;
            state.Status = "render_hold";
            return state.NativeWeight;
        }
        if (input.SourceUpdateSerial > state.PreviousSourceUpdateSerial) {
            sourceUpdateSteps = input.SourceUpdateSerial - state.PreviousSourceUpdateSerial;
        } else {
            ++state.SourceRestartCount;
            sourceUpdateSteps = 1;
        }
        state.PreviousSourceUpdateSerial = input.SourceUpdateSerial;
    }

    const float sourceDelta = sourceWeight - state.PreviousSourceWeight;
    if (sourceWeight <= kAnimationFrameEpsilon) {
        state.NativeWeight = 0.0f;
        state.Status = "source_complete";
    } else if (sourceDelta > kAnimationFrameEpsilon) {
        state.NativeWeight = sourceWeight;
        ++state.SourceRestartCount;
        state.Status = "source_restart";
    } else if (std::abs(sourceDelta) > kAnimationFrameEpsilon) {
        state.NativeWeight = sourceWeight;
        ++state.SourceAdvanceCount;
        state.Status = "advanced_from_source";
    } else if (input.SourceUpdateSerialValid && std::isfinite(input.SourceRate) &&
               std::abs(input.SourceRate) > kAnimationFrameEpsilon) {
        state.NativeWeight = std::max(
            0.0f, state.NativeWeight -
                      std::abs(input.SourceRate) * static_cast<float>(sourceUpdateSteps));
        ++state.SourceHoldAdvanceCount;
        state.Status = "advanced_from_source_rate";
    } else {
        state.Status = "held";
    }
    state.PreviousSourceWeight = sourceWeight;
    return state.NativeWeight;
}

NativeEnKoRuntimeBinding ResolveNativeEnKoRuntimeBinding(
    const nlohmann::json& contract, uint32_t subtype,
    uint32_t semanticAnimationIndex) {
    NativeEnKoRuntimeBinding result;
    result.Subtype = subtype;
    result.SemanticAnimationIndex = semanticAnimationIndex;
    if (!contract.is_object() ||
        contract.value("format", "") != kEnKoRuntimeContractFormat ||
        contract.value("status", "") != "complete") {
        result.Status = "enko_runtime_contract_unavailable";
        result.Error = "native EnKo runtime contract is absent or unsupported";
        return result;
    }
    const auto subtypeIt = contract.find("subtypes");
    const auto animationIt = contract.find("animations");
    const auto callbackIt = contract.find("draw_callback");
    if (subtypeIt == contract.end() || animationIt == contract.end() ||
        callbackIt == contract.end() || !callbackIt->is_object()) {
        result.Status = "enko_runtime_contract_incomplete";
        result.Error = "native EnKo runtime contract has no subtype or animation table";
        return result;
    }
    const auto* subtypeRecord = FindIndexedRecord(*subtypeIt, subtype);
    const auto* animationRecord =
        FindIndexedRecord(*animationIt, semanticAnimationIndex);
    if (subtypeRecord == nullptr || animationRecord == nullptr) {
        result.Status = "enko_runtime_binding_index_unresolved";
        return result;
    }
    const auto modelClassIt = subtypeRecord->find("model_class_index");
    if (modelClassIt == subtypeRecord->end() ||
        !ReadJsonU32(*modelClassIt, result.ModelClassIndex)) {
        result.Status = "enko_runtime_subtype_invalid";
        result.Error = "native EnKo subtype has no model-class selector";
        return result;
    }
    result.ModelAssetId = subtypeRecord->value("model_asset_id", "");
    result.FaceModelAssetId = subtypeRecord->value("face_model_asset_id", "");
    const auto scaleIt = subtypeRecord->find("model_scale");
    const auto faceIt = subtypeRecord->find("face_animation_selector");
    const auto tunicIt = subtypeRecord->find("tunic_color");
    const auto bootsIt = subtypeRecord->find("boots_color");
    if (result.ModelAssetId.empty() || result.FaceModelAssetId.empty() ||
        scaleIt == subtypeRecord->end() ||
        !scaleIt->is_number() || faceIt == subtypeRecord->end() ||
        !ReadJsonU32(*faceIt, result.FaceAnimationSelector) ||
        tunicIt == subtypeRecord->end() ||
        !ReadJsonColor(*tunicIt, result.TunicColor) ||
        bootsIt == subtypeRecord->end() ||
        !ReadJsonColor(*bootsIt, result.BootsColor)) {
        result.Status = "enko_runtime_subtype_invalid";
        result.Error = "native EnKo subtype fields are missing or malformed";
        return result;
    }
    result.ModelScale = scaleIt->get<float>();
    if (!std::isfinite(result.ModelScale) || result.ModelScale <= 0.0f) {
        result.Status = "enko_runtime_subtype_invalid";
        result.Error = "native EnKo model scale is invalid";
        return result;
    }
    const auto torsoLimbIt = callbackIt->find("torso_limb_index");
    const auto headLimbIt = callbackIt->find("head_limb_index");
    if (torsoLimbIt == callbackIt->end() ||
        !ReadJsonU32(*torsoLimbIt, result.TorsoLimbIndex) ||
        headLimbIt == callbackIt->end() ||
        !ReadJsonU32(*headLimbIt, result.HeadLimbIndex)) {
        result.Status = "enko_runtime_draw_callback_invalid";
        result.Error = "native EnKo draw callback limb selectors are missing";
        return result;
    }
    const auto clearIt = subtypeRecord->find("resource_visibility_clear_ids");
    if (clearIt == subtypeRecord->end() || !clearIt->is_array()) {
        result.Status = "enko_runtime_subtype_invalid";
        result.Error = "native EnKo subtype has no resource-visibility selection";
        return result;
    }
    for (const auto& value : *clearIt) {
        uint8_t resourceId = 0;
        if (!ReadJsonU8(value, resourceId)) {
            result.Status = "enko_runtime_subtype_invalid";
            result.Error = "native EnKo visibility resource id is invalid";
            return result;
        }
        result.ResourceVisibilityClearIds.push_back(resourceId);
    }

    const auto bindingsIt = animationRecord->find("bindings_by_model_class");
    if (bindingsIt == animationRecord->end() || !bindingsIt->is_array()) {
        result.Status = "enko_runtime_animation_invalid";
        result.Error = "native EnKo animation has no model-class bindings";
        return result;
    }
    const nlohmann::json* animationBinding = nullptr;
    for (const auto& candidate : *bindingsIt) {
        if (!candidate.is_object()) {
            continue;
        }
        uint32_t modelClassIndex = 0;
        const auto classIt = candidate.find("model_class_index");
        if (classIt != candidate.end() &&
            ReadJsonU32(*classIt, modelClassIndex) &&
            modelClassIndex == result.ModelClassIndex) {
            animationBinding = &candidate;
            break;
        }
    }
    if (animationBinding == nullptr) {
        result.Status = "enko_runtime_animation_model_class_unresolved";
        return result;
    }
    result.AnimationAssetId = animationBinding->value("animation_asset_id", "");
    result.AnimationMember = animationBinding->value("csab_member", "");
    const auto speedIt = animationRecord->find("playback_speed");
    const auto startIt = animationRecord->find("start_frame");
    const auto endIt = animationRecord->find("end_frame");
    const auto modeIt = animationRecord->find("playback_mode");
    const auto morphIt = animationRecord->find("morph_frames");
    if (result.AnimationAssetId.empty() || result.AnimationMember.empty() ||
        speedIt == animationRecord->end() || !speedIt->is_number() ||
        startIt == animationRecord->end() || !startIt->is_number() ||
        endIt == animationRecord->end() || !endIt->is_number() ||
        modeIt == animationRecord->end() ||
        !ReadJsonU8(*modeIt, result.PlaybackMode) ||
        morphIt == animationRecord->end() || !morphIt->is_number()) {
        result.Status = "enko_runtime_animation_invalid";
        result.Error = "native EnKo animation fields are missing or malformed";
        return result;
    }
    result.PlaybackSpeed = speedIt->get<float>();
    result.StartFrame = startIt->get<float>();
    result.EndFrame = endIt->get<float>();
    result.MorphFrames = morphIt->get<float>();
    if (!std::isfinite(result.PlaybackSpeed) ||
        !std::isfinite(result.StartFrame) || !std::isfinite(result.EndFrame) ||
        !std::isfinite(result.MorphFrames)) {
        result.Status = "enko_runtime_animation_invalid";
        result.Error = "native EnKo animation contains non-finite values";
        return result;
    }
    result.Available = true;
    result.Status = "ready";
    return result;
}

NativeActorResourceVisibility ResolveNativeEnKoResourceVisibility(
    const NativeEnKoRuntimeBinding& binding, const CmbModel& model) {
    NativeActorResourceVisibility result;
    if (!binding.Available || model.Meshes.empty()) {
        result.Status = "enko_resource_visibility_model_unavailable";
        return result;
    }
    uint8_t maximumResourceId = 0;
    for (const auto& mesh : model.Meshes) {
        maximumResourceId = std::max(maximumResourceId, mesh.VisibilityId);
    }
    result.ResourceVisibility.assign(static_cast<size_t>(maximumResourceId) + 1, 1);
    for (const uint8_t resourceId : binding.ResourceVisibilityClearIds) {
        if (resourceId >= result.ResourceVisibility.size()) {
            result.Status = "enko_resource_visibility_contract_mismatch";
            result.Error = "native EnKo visibility id lies outside the selected CMB";
            return result;
        }
        result.ResourceVisibility[resourceId] = 0;
    }
    for (size_t resourceId = 0; resourceId < result.ResourceVisibility.size(); ++resourceId) {
        if (result.ResourceVisibility[resourceId] != 0) {
            result.ActiveResourceIds.push_back(static_cast<uint8_t>(resourceId));
        }
    }
    result.Available = true;
    result.Status = "native_enko_code_bin_resource_visibility";
    return result;
}

bool ApplyNativeActorPoseRotationOverrides(
    const CmbSkeleton& skeleton, CsabPose& pose,
    const std::vector<NativeActorPoseRotationOverride>& overrides) {
    if (!pose.Valid || pose.LocalTransforms.size() != skeleton.Bones.size()) {
        return false;
    }
    for (const auto& override : overrides) {
        if (override.BoneIndex >= pose.LocalTransforms.size()) {
            pose.Valid = false;
            return false;
        }
        auto& local = pose.LocalTransforms[override.BoneIndex];
        local = MultiplyPoseMatrix(
            MultiplyPoseMatrix(
                local, PoseRotationX(static_cast<float>(override.RotationY) * kBinangToRadians)),
            PoseRotationZ(static_cast<float>(override.RotationX) * kBinangToRadians));
    }
    return RecomposeCsabPoseWorldTransforms(skeleton, pose);
}

bool ApplyNativeActorPoseMatrixOverrides(
    const CmbSkeleton& skeleton, CsabPose& pose,
    const std::vector<NativeActorPoseMatrixOverride>& overrides) {
    if (!pose.Valid || pose.LocalTransforms.size() != skeleton.Bones.size()) {
        return false;
    }
    for (const auto& override : overrides) {
        if (override.BoneIndex >= pose.LocalTransforms.size()) {
            pose.Valid = false;
            return false;
        }
        pose.LocalTransforms[override.BoneIndex] = MultiplyPoseMatrix(
            pose.LocalTransforms[override.BoneIndex], override.LocalPostTransform);
    }
    return RecomposeCsabPoseWorldTransforms(skeleton, pose);
}

size_t ApplyNativeActorModelOpacity(Oot3dNativeRenderModel& model,
                                    uint8_t alpha,
                                    std::string_view source) {
    size_t updatedVertexCount = 0;
    if (alpha < 255) {
        for (auto& batch : model.Batches) {
            batch.Material.NativeRuntimeVertexAlphaBlend = true;
            batch.Material.NativeRenderStateDecoded = true;
            batch.Material.NativeBlendStateEnabled = true;
            batch.Material.NativeBlendStateSupported = true;
            batch.Material.NativeBlendFactorsSupported = true;
            batch.Material.NativeBlendEquationSupported = true;
            batch.Material.BlendMode = 1;
            batch.Material.BlendSrc = 0x0302;
            batch.Material.BlendDst = 0x0303;
            batch.Material.BlendEquation = 0x8006;
            batch.Material.ColorBlendSrc = 0x0302;
            batch.Material.ColorBlendDst = 0x0303;
            batch.Material.ColorBlendEquation = 0x8006;
            for (auto& vertex : batch.Vertices) {
                const uint32_t scaled =
                    static_cast<uint32_t>(vertex.Color.A) * alpha + 127;
                vertex.Color.A = static_cast<uint8_t>(scaled / 255);
                ++updatedVertexCount;
            }
        }
    }
    model.Diagnostics["native_actor_runtime_opacity"] = {
        { "alpha", alpha }, { "source", std::string(source) },
        { "updated_vertex_count", updatedVertexCount },
    };
    return updatedVertexCount;
}

NativeActorRenderProvider::NativeActorRenderProvider(const AssetCatalog& catalog,
                                                     NativeSourceProvider& sources)
    : mCatalog(catalog), mSources(sources) {
    try {
        const auto source = mSources.Load(kManifestResource);
        if (source == nullptr) {
            mManifestError = "native actor shard manifest is unavailable";
        } else {
            mManifest = nlohmann::json::parse(source->Bytes->begin(), source->Bytes->end());
            if (mManifest.value("format", "") != "oot3d_native_actor_shard_v1" ||
                mManifest.value("status", "") != "complete" ||
                !mManifest.contains("records") || !mManifest["records"].is_array()) {
                mManifest = {};
                mManifestError = "native actor shard manifest has an unsupported contract";
            }
        }
    } catch (const std::exception& error) {
        mManifest = {};
        mManifestError = error.what();
    }
    try {
        const auto source = mSources.Load(kEnKoRuntimeContractResource);
        if (source == nullptr) {
            mEnKoRuntimeContractError = "native EnKo runtime contract is unavailable";
        } else {
            mEnKoRuntimeContract =
                nlohmann::json::parse(source->Bytes->begin(), source->Bytes->end());
            if (mEnKoRuntimeContract.value("format", "") != kEnKoRuntimeContractFormat ||
                mEnKoRuntimeContract.value("status", "") != "complete") {
                mEnKoRuntimeContract = {};
                mEnKoRuntimeContractError =
                    "native EnKo runtime contract has an unsupported contract";
            }
        }
    } catch (const std::exception& error) {
        mEnKoRuntimeContract = {};
        mEnKoRuntimeContractError = error.what();
    }
}

std::shared_ptr<const NativeActorArchiveSource> NativeActorRenderProvider::ResolveArchive(
    std::string_view sourceContainer) {
    const std::string key(sourceContainer);
    {
        std::scoped_lock lock(mMutex);
        if (const auto found = mArchives.find(key); found != mArchives.end()) {
            return found->second;
        }
    }
    auto result = std::make_shared<NativeActorArchiveSource>();
    result->SourceContainer = key;
    try {
        if (!mManifest.is_object()) {
            throw std::runtime_error(mManifestError);
        }
        const nlohmann::json* manifestRecord = nullptr;
        for (const auto& candidate : mManifest.at("records")) {
            if (candidate.value("source_container", "") == key) {
                manifestRecord = &candidate;
                break;
            }
        }
        if (manifestRecord == nullptr) {
            result->Status = "actor_archive_not_packaged";
        } else {
            result->ResourcePath = manifestRecord->at("resource").get<std::string>();
            result->Source = mSources.Load(result->ResourcePath);
            if (result->Source == nullptr) {
                throw std::runtime_error("native actor ZAR resource could not be loaded");
            }
            result->Archive = ParseZarArchiveBytes(*result->Source->Bytes, result->ResourcePath);
            for (const auto& file : result->Archive.Files) {
                if (file.TypeName != "cmb" && !file.Name.ends_with(".cmb")) {
                    continue;
                }
                if (file.Offset > result->Source->Bytes->size() ||
                    file.Size > result->Source->Bytes->size() - file.Offset) {
                    throw std::runtime_error("native actor ZAR entry lies outside source bytes");
                }
                const auto bytes = std::span<const uint8_t>(
                    result->Source->Bytes->data() + file.Offset, file.Size);
                auto model = ParseCmbModelBytes(bytes, file.Name);
                auto renderModel = BuildOot3dNativeRenderModel(model);
                auto bindWorldTransforms = BuildCmbSkeletonWorldTransforms(model.Skeleton);
                result->Models.push_back({ file.Name, std::move(model), std::move(renderModel),
                                           std::move(bindWorldTransforms), false });
            }
            for (const auto& file : result->Archive.Files) {
                if (file.TypeName != "cmab" && !file.Name.ends_with(".cmab")) {
                    continue;
                }
                if (file.Offset > result->Source->Bytes->size() ||
                    file.Size > result->Source->Bytes->size() - file.Offset) {
                    throw std::runtime_error("native actor CMAB lies outside source bytes");
                }
                const auto bytes = std::span<const uint8_t>(
                    result->Source->Bytes->data() + file.Offset, file.Size);
                result->MaterialAnimations.push_back(
                    ParseCmabMaterialAnimationBytes(bytes, file.Name));
            }
            result->Status = result->Models.empty() ? "actor_archive_contains_no_cmb" : "ready";
        }
    } catch (const std::exception& error) {
        result->Status = "actor_archive_parse_failed";
        result->Error = error.what();
    }
    std::scoped_lock lock(mMutex);
    return mArchives.emplace(key, result).first->second;
}

std::shared_ptr<const NativeActorRenderSource> NativeActorRenderProvider::Resolve(
    std::string_view modelAssetId) {
    const std::string key(modelAssetId);
    {
        std::scoped_lock lock(mMutex);
        if (const auto found = mModels.find(key); found != mModels.end()) {
            return found->second;
        }
    }
    auto result = std::make_shared<NativeActorRenderSource>();
    result->CatalogRecord = mCatalog.Find(key);
    if (result->CatalogRecord == nullptr || result->CatalogRecord->Family != "actor_model") {
        result->Status = "actor_model_not_cataloged";
    } else if (result->CatalogRecord->SourceContainer.empty() ||
               result->CatalogRecord->SourceMember.empty()) {
        result->Status = "actor_model_source_identity_unresolved";
    } else {
        result->Archive = ResolveArchive(result->CatalogRecord->SourceContainer);
        if (!result->Archive->Ready()) {
            result->Status = "actor_archive_unavailable";
            result->Error = result->Archive->Status + ": " + result->Archive->Error;
        } else {
            for (const auto& candidate : result->Archive->Models) {
                if (candidate.MemberName == result->CatalogRecord->SourceMember) {
                    result->Model = &candidate;
                    break;
                }
            }
            for (const auto* dependent : mCatalog.FindDependents(key)) {
                if (dependent->Family == "skeletal_animation" &&
                    dependent->SourceContainer == result->CatalogRecord->SourceContainer) {
                    result->AnimationRecords.push_back(dependent);
                }
            }
            result->Status = result->Model == nullptr ? "actor_model_member_missing" : "ready";
        }
    }
    std::scoped_lock lock(mMutex);
    return mModels.emplace(key, result).first->second;
}

NativeActorAnimationSamplingBinding ResolveNativeActorAnimationSamplingBinding(
    const nlohmann::json& profile) {
    NativeActorAnimationSamplingBinding result;
    if (!profile.is_object()) {
        result.Status = "actor_animation_sampling_profile_unavailable";
        result.Error = "character runtime profile is not an object";
        return result;
    }
    const auto contractIt = profile.find("skel_anime_sampling_contract");
    if (contractIt == profile.end()) {
        result.Available = true;
        result.Status = "actor_animation_sampling_contract_absent";
        return result;
    }
    result.ContractPresent = true;
    const auto& contract = *contractIt;
    uint32_t frameDataPath = 0;
    uint32_t specialBone = 0;
    uint32_t defaultMask = 0;
    uint32_t specialMask = 0;
    if (!contract.is_object() ||
        contract.value("format", "") != kAnimationSamplingContractFormat ||
        !contract.contains("frame_data_path") ||
        !ReadJsonU32(contract["frame_data_path"], frameDataPath) ||
        frameDataPath != 1 ||
        !contract.contains("special_bone") ||
        !ReadJsonU32(contract["special_bone"], specialBone) ||
        specialBone > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        !contract.contains("default_channel_mask") ||
        !ReadJsonU32(contract["default_channel_mask"], defaultMask) ||
        !contract.contains("special_bone_channel_mask") ||
        !ReadJsonU32(contract["special_bone_channel_mask"], specialMask) ||
        defaultMask > 0x07 || specialMask > 0x07) {
        result.Status = "actor_animation_sampling_contract_invalid";
        result.Error = "unsupported native SkelAnime sampling contract";
        return result;
    }
    if (!contract.contains("base_translation") ||
        !contract["base_translation"].is_array() ||
        contract["base_translation"].size() != result.BaseTranslation.size()) {
        result.Status = "actor_animation_sampling_contract_invalid";
        result.Error = "native SkelAnime base translation is unavailable";
        return result;
    }
    for (size_t axis = 0; axis < result.BaseTranslation.size(); ++axis) {
        const auto& component = contract["base_translation"][axis];
        if (!component.is_number()) {
            result.Status = "actor_animation_sampling_contract_invalid";
            result.Error = "native SkelAnime base translation is invalid";
            return result;
        }
        result.BaseTranslation[axis] = component.get<float>();
        if (!std::isfinite(result.BaseTranslation[axis])) {
            result.Status = "actor_animation_sampling_contract_invalid";
            result.Error = "native SkelAnime base translation is non-finite";
            return result;
        }
    }
    result.Policy.DefaultChannelMask = static_cast<uint8_t>(defaultMask);
    result.Policy.SpecialBone = static_cast<int32_t>(specialBone);
    result.Policy.SpecialBoneChannelMask = static_cast<uint8_t>(specialMask);
    result.Available = true;
    result.Status = "ready";
    return result;
}

CsabMetadata NativeActorRenderProvider::ResolveAnimationMetadata(
    std::string_view cacheKey, std::span<const uint8_t> bytes) const {
    const std::string key(cacheKey);
    {
        std::scoped_lock lock(mMutex);
        const auto found = mAnimationCache.find(key);
        if (found != mAnimationCache.end() && found->second.MetadataReady) {
            return found->second.Metadata;
        }
    }
    auto metadata = ParseCsabMetadataBytes(bytes);
    std::scoped_lock lock(mMutex);
    auto& cached = mAnimationCache[key];
    if (!cached.MetadataReady) {
        cached.Metadata = metadata;
        cached.MetadataReady = true;
    }
    return cached.Metadata;
}

NativeActorAnimationSample NativeActorRenderProvider::SampleAnimation(
    const NativeActorRenderSource& actor, std::string_view animationAssetId,
    float frame) const {
    return SampleAnimation(actor, animationAssetId, frame, {});
}

NativeActorAnimationSample NativeActorRenderProvider::SampleAnimation(
    const NativeActorRenderSource& actor, std::string_view animationAssetId,
    float frame, const CsabPoseSamplingPolicy& policy) const {
    NativeActorAnimationSample result;
    if (!actor.Ready()) {
        result.Status = "actor_model_unavailable";
        return result;
    }
    result.AnimationRecord = mCatalog.Find(animationAssetId);
    if (result.AnimationRecord == nullptr ||
        result.AnimationRecord->Family != "skeletal_animation") {
        result.Status = "actor_animation_not_cataloged";
        return result;
    }
    const bool declaredDependency =
        std::find(actor.AnimationRecords.begin(), actor.AnimationRecords.end(),
                  result.AnimationRecord) != actor.AnimationRecords.end();
    if (!declaredDependency ||
        result.AnimationRecord->SourceContainer != actor.CatalogRecord->SourceContainer) {
        result.Status = "actor_animation_dependency_mismatch";
        return result;
    }
    const auto file = std::find_if(
        actor.Archive->Archive.Files.begin(), actor.Archive->Archive.Files.end(),
        [&](const ZarFileEntry& candidate) {
            return candidate.Name == result.AnimationRecord->SourceMember &&
                   (candidate.TypeName == "csab" || candidate.Name.ends_with(".csab"));
        });
    if (file == actor.Archive->Archive.Files.end()) {
        result.Status = "actor_animation_member_missing";
        return result;
    }
    if (file->Offset > actor.Archive->Source->Bytes->size() ||
        file->Size > actor.Archive->Source->Bytes->size() - file->Offset) {
        result.Status = "actor_animation_member_out_of_bounds";
        return result;
    }
    try {
        const auto bytes = std::span<const uint8_t>(
            actor.Archive->Source->Bytes->data() + file->Offset, file->Size);
        const std::string cacheKey = actor.CatalogRecord->AssetId + "|" +
                                     result.AnimationRecord->AssetId + "|mask=" +
                                     std::to_string(policy.DefaultChannelMask) + ":" +
                                     std::to_string(policy.SpecialBone) + ":" +
                                     std::to_string(policy.SpecialBoneChannelMask);
        const uint32_t sampledFrameBits = std::bit_cast<uint32_t>(frame);
        {
            std::scoped_lock lock(mMutex);
            const auto found = mAnimationCache.find(cacheKey);
            if (found != mAnimationCache.end() && found->second.PoseReady &&
                found->second.SampledFrameBits == sampledFrameBits) {
                result.Pose = found->second.Pose;
                result.FrameCount = found->second.Metadata.FrameCount;
                result.SampledFrame = frame;
                result.Status = result.Pose.Valid ? "ready" : "actor_animation_pose_invalid";
                return result;
            }
        }
        const auto metadata = ResolveAnimationMetadata(cacheKey, bytes);
        result.FrameCount = metadata.FrameCount;
        result.Pose = SampleCsabPoseFrameBytes(bytes, actor.Model->Model, metadata, frame, policy);
        result.SampledFrame = frame;
        result.Status = result.Pose.Valid ? "ready" : "actor_animation_pose_invalid";
        if (result.Pose.Valid) {
            std::scoped_lock lock(mMutex);
            auto& cached = mAnimationCache[cacheKey];
            cached.SampledFrameBits = sampledFrameBits;
            cached.Pose = result.Pose;
            cached.PoseReady = true;
        }
    } catch (const std::exception& error) {
        result.Status = "actor_animation_sample_failed";
        result.Error = error.what();
    }
    return result;
}

NativeActorAnimationSample NativeActorRenderProvider::SampleAnimation(
    const NativeActorRenderSource& actor, std::string_view animationAssetId,
    const NativeActorAnimationTimeInput& time) const {
    if (!actor.Ready()) {
        NativeActorAnimationSample result;
        result.Status = "actor_model_unavailable";
        return result;
    }
    const auto* animation = mCatalog.Find(animationAssetId);
    if (animation == nullptr) {
        NativeActorAnimationSample result;
        result.Status = "actor_animation_not_cataloged";
        return result;
    }
    const auto file = std::find_if(
        actor.Archive->Archive.Files.begin(), actor.Archive->Archive.Files.end(),
        [&](const ZarFileEntry& candidate) {
            return candidate.Name == animation->SourceMember &&
                   (candidate.TypeName == "csab" || candidate.Name.ends_with(".csab"));
        });
    if (file == actor.Archive->Archive.Files.end() ||
        file->Offset > actor.Archive->Source->Bytes->size() ||
        file->Size > actor.Archive->Source->Bytes->size() - file->Offset) {
        NativeActorAnimationSample result;
        result.AnimationRecord = animation;
        result.Status = "actor_animation_member_missing";
        return result;
    }
    const auto bytes = std::span<const uint8_t>(
        actor.Archive->Source->Bytes->data() + file->Offset, file->Size);
    try {
        const auto metadata = ResolveAnimationMetadata(
            actor.CatalogRecord->AssetId + "|" + animation->AssetId, bytes);
        auto result = SampleAnimation(actor, animationAssetId,
                                      ResolveNativeActorAnimationFrame(metadata, time));
        result.NativeClockStatus = "normalized_source_timeline";
        return result;
    } catch (const std::exception& error) {
        NativeActorAnimationSample result;
        result.AnimationRecord = animation;
        result.Status = "actor_animation_metadata_failed";
        result.Error = error.what();
        return result;
    }
}

NativeActorAnimationSample NativeActorRenderProvider::SampleAnimation(
    const NativeActorRenderSource& actor, std::string_view animationAssetId,
    const NativeActorAnimationTimeInput& time,
    NativeActorAnimationClockState& clock) const {
    return SampleAnimation(actor, animationAssetId, time, clock, {});
}

NativeActorAnimationSample NativeActorRenderProvider::SampleAnimation(
    const NativeActorRenderSource& actor, std::string_view animationAssetId,
    const NativeActorAnimationTimeInput& time,
    NativeActorAnimationClockState& clock,
    const CsabPoseSamplingPolicy& policy) const {
    if (!actor.Ready()) {
        NativeActorAnimationSample result;
        result.Status = "actor_model_unavailable";
        return result;
    }
    const auto* animation = mCatalog.Find(animationAssetId);
    if (animation == nullptr) {
        NativeActorAnimationSample result;
        result.Status = "actor_animation_not_cataloged";
        return result;
    }
    const auto file = std::find_if(
        actor.Archive->Archive.Files.begin(), actor.Archive->Archive.Files.end(),
        [&](const ZarFileEntry& candidate) {
            return candidate.Name == animation->SourceMember &&
                   (candidate.TypeName == "csab" || candidate.Name.ends_with(".csab"));
        });
    if (file == actor.Archive->Archive.Files.end() ||
        file->Offset > actor.Archive->Source->Bytes->size() ||
        file->Size > actor.Archive->Source->Bytes->size() - file->Offset) {
        NativeActorAnimationSample result;
        result.AnimationRecord = animation;
        result.Status = "actor_animation_member_missing";
        return result;
    }
    const auto bytes = std::span<const uint8_t>(
        actor.Archive->Source->Bytes->data() + file->Offset, file->Size);
    try {
        const auto metadata = ResolveAnimationMetadata(
            actor.CatalogRecord->AssetId + "|" + animation->AssetId, bytes);
        const float frame = AdvanceNativeActorAnimationClock(metadata, time, clock);
        auto result = SampleAnimation(actor, animationAssetId, frame, policy);
        result.SampledFrame = frame;
        result.NativeClockApplied = true;
        result.NativeClockStatus = clock.Status;
        return result;
    } catch (const std::exception& error) {
        NativeActorAnimationSample result;
        result.AnimationRecord = animation;
        result.Status = "actor_animation_metadata_failed";
        result.Error = error.what();
        return result;
    }
}

const AssetCatalogRecord* NativeActorRenderProvider::FindAnimation(
    const NativeActorRenderSource& actor, std::string_view memberName) const {
    const std::string requested(memberName);
    const size_t slash = requested.find_last_of("/\\");
    const std::string requestedBase =
        slash == std::string::npos ? requested : requested.substr(slash + 1);
    const AssetCatalogRecord* match = nullptr;
    for (const auto* candidate : actor.AnimationRecords) {
        const size_t candidateSlash = candidate->SourceMember.find_last_of("/\\");
        const std::string candidateBase = candidateSlash == std::string::npos
                                              ? candidate->SourceMember
                                              : candidate->SourceMember.substr(candidateSlash + 1);
        if (candidate->SourceMember != requested && candidateBase != requestedBase) {
            continue;
        }
        if (match != nullptr) {
            return nullptr;
        }
        match = candidate;
    }
    return match;
}

NativeActorFaceSample NativeActorRenderProvider::SampleFace(
    const NativeActorRenderSource& actor, const AssetCatalogRecord& animation,
    float frame) const {
    NativeActorFaceSample result;
    if (!actor.Ready() || animation.SourceContainer != actor.CatalogRecord->SourceContainer) {
        result.Status = "actor_face_animation_mismatch";
        return result;
    }
    const auto csab = std::find_if(
        actor.Archive->Archive.Files.begin(), actor.Archive->Archive.Files.end(),
        [&](const ZarFileEntry& candidate) {
            return candidate.Name == animation.SourceMember &&
                   (candidate.TypeName == "csab" || candidate.Name.ends_with(".csab"));
        });
    if (csab == actor.Archive->Archive.Files.end()) {
        result.Status = "actor_face_csab_member_missing";
        return result;
    }
    const auto faceb = std::find_if(
        actor.Archive->Archive.Files.begin(), actor.Archive->Archive.Files.end(),
        [&](const ZarFileEntry& candidate) {
            return candidate.TypeLocalIndex == csab->TypeLocalIndex &&
                   (candidate.TypeName == "faceb" || candidate.Name.ends_with(".faceb"));
        });
    if (faceb == actor.Archive->Archive.Files.end() ||
        faceb->Offset > actor.Archive->Source->Bytes->size() ||
        faceb->Size > actor.Archive->Source->Bytes->size() - faceb->Offset) {
        result.Status = "actor_face_parallel_member_missing";
        return result;
    }
    const auto bytes = std::span<const uint8_t>(
        actor.Archive->Source->Bytes->data() + faceb->Offset, faceb->Size);
    const auto track = ParseFacebMaterialFrameTrackBytes(bytes, faceb->Name);
    if (track.Events.empty()) {
        result.Status = "actor_face_track_empty";
        return result;
    }
    const auto selected = SampleFacebMaterialFrameTrack(track, frame);
    result.Available = true;
    result.EyeSelected = selected.EyeSelected;
    result.MouthSelected = selected.MouthSelected;
    result.EyeIndex = selected.EyeIndex;
    result.MouthIndex = selected.MouthIndex;
    result.HoldValue = selected.HoldValue;
    result.AnimationTypeLocalIndex = csab->TypeLocalIndex;
    result.SourceMember = faceb->Name;
    result.Status = "ready";
    return result;
}

NativeActorFaceSample ResolveNativeActorFaceState(const NativeActorFaceSample& sample,
                                                  NativeActorFaceRuntimeState& state) {
    NativeActorFaceSample resolved = sample;
    if (!state.Initialized) {
        state.Initialized = true;
        state.EyeIndex = 0;
        state.MouthIndex = 0;
    }
    if (sample.Available && sample.EyeSelected) {
        state.EyeIndex = sample.EyeIndex;
    }
    if (sample.Available && sample.MouthSelected) {
        state.MouthIndex = sample.MouthIndex;
    }
    resolved.Available = true;
    resolved.EyeIndex = state.EyeIndex;
    resolved.MouthIndex = state.MouthIndex;
    if (!sample.Available) {
        resolved.Status = "actor_face_state_held_without_track";
    } else if (!sample.EyeSelected && !sample.MouthSelected) {
        resolved.Status = "actor_face_state_held";
    } else {
        resolved.Status = "actor_face_state_updated";
    }
    return resolved;
}

NativeActorAnimationSpatialBinding ResolveNativeActorAnimationSpatialBinding(
    const nlohmann::json& profile, std::string_view csabName) {
    NativeActorAnimationSpatialBinding result;
    if (!profile.is_object()) {
        result.Status = "actor_spatial_profile_unavailable";
        result.Error = "character runtime profile is not an object";
        return result;
    }
    const auto contractIt = profile.find("segment_continuity_contract");
    if (contractIt == profile.end()) {
        result.Available = true;
        result.Status = "actor_spatial_segment_contract_absent";
        return result;
    }
    result.SegmentContractPresent = true;
    const auto& contract = *contractIt;
    if (!contract.is_object() ||
        contract.value("format", "") != "oot3d_character_segment_continuity_contract_v1" ||
        contract.value("status", "") != "ready" ||
        contract.value("normalization_policy", "") != "cumulative_segment_root_offset") {
        result.Status = "actor_spatial_segment_contract_invalid";
        result.Error = "unsupported character segment continuity contract";
        return result;
    }
    const auto rootMotionBone = contract.find("root_motion_bone");
    if (rootMotionBone == contract.end() || !rootMotionBone->is_number_integer() ||
        rootMotionBone->get<int64_t>() < 0 ||
        rootMotionBone->get<int64_t>() > std::numeric_limits<int32_t>::max()) {
        result.Status = "actor_spatial_root_motion_bone_invalid";
        result.Error = "segment continuity contract root motion bone is invalid";
        return result;
    }
    result.RootMotionBone = static_cast<int32_t>(rootMotionBone->get<int64_t>());
    const auto segments = contract.find("segments");
    if (segments == contract.end() || !segments->is_array()) {
        result.Status = "actor_spatial_segments_missing";
        result.Error = "segment continuity contract has no segment array";
        return result;
    }
    for (const auto& segment : *segments) {
        if (!segment.is_object() || segment.value("csab_name", "") != csabName) {
            continue;
        }
        const auto offset = segment.find("normalization_offset");
        if (offset == segment.end() || !offset->is_array() || offset->size() != 3) {
            result.Status = "actor_spatial_segment_offset_invalid";
            result.Error = "segment normalization offset is not a vec3";
            return result;
        }
        for (size_t component = 0; component < result.SegmentNormalizationOffset.size(); ++component) {
            if (!(*offset)[component].is_number()) {
                result.Status = "actor_spatial_segment_offset_invalid";
                result.Error = "segment normalization offset is not numeric";
                return result;
            }
            const float value = (*offset)[component].get<float>();
            if (!std::isfinite(value)) {
                result.Status = "actor_spatial_segment_offset_invalid";
                result.Error = "segment normalization offset is not finite";
                return result;
            }
            result.SegmentNormalizationOffset[component] = value;
        }
        result.SegmentMatched = true;
        result.Available = true;
        result.Status = "ready";
        return result;
    }
    result.Available = true;
    result.Status = "actor_spatial_animation_not_segmented";
    return result;
}

NativeActorRootMotionOwnership ResolveNativeActorRootMotionOwnership(
    const nlohmann::json& profile) {
    NativeActorRootMotionOwnership result;
    if (!profile.is_object()) {
        result.Status = "actor_root_motion_profile_unavailable";
        result.Error = "character runtime profile is not an object";
        return result;
    }
    const auto contractIt = profile.find("root_motion_ownership_contract");
    if (contractIt == profile.end()) {
        result.Status = "actor_root_motion_ownership_contract_absent";
        return result;
    }
    result.ContractPresent = true;
    const auto& contract = *contractIt;
    if (!contract.is_object() ||
        contract.value("format", "") != kRootMotionOwnershipContractFormat ||
        contract.value("status", "") != "ready" ||
        contract.value("translation_policy", "") !=
            "align_only_controller_consumed_axes" ||
        contract.value("xz_ownership", "") !=
            "controller_when_movement_enabled" ||
        contract.value("y_ownership", "") !=
            "controller_when_movement_enabled_and_update_y" ||
        contract.value("root_rotation_policy", "") !=
            "preserve_authored_oot3d") {
        result.Status = "actor_root_motion_ownership_contract_invalid";
        result.Error = "unsupported character root-motion ownership contract";
        return result;
    }
    const auto movementEnabledFlag = contract.find("movement_enabled_flag");
    const auto updateYFlag = contract.find("update_y_flag");
    uint32_t movementEnabledValue = 0;
    uint32_t updateYValue = 0;
    if (movementEnabledFlag == contract.end() ||
        updateYFlag == contract.end() ||
        !ReadJsonU32(*movementEnabledFlag, movementEnabledValue) ||
        !ReadJsonU32(*updateYFlag, updateYValue) ||
        movementEnabledValue == 0 || movementEnabledValue > 0xFF ||
        updateYValue == 0 || updateYValue > 0xFF) {
        result.Status = "actor_root_motion_ownership_flags_invalid";
        result.Error = "root-motion ownership flags are missing or out of range";
        return result;
    }
    result.MovementEnabledFlag = static_cast<uint8_t>(movementEnabledValue);
    result.UpdateYFlag = static_cast<uint8_t>(updateYValue);
    result.AlignXzWhenMovementEnabled = true;
    result.AlignYWhenMovementEnabledAndUpdateY = true;
    result.PreserveAuthoredRootRotation = true;
    result.Available = true;
    result.Status = "ready";
    return result;
}

NativeActorRootMotionAlignment ResolveNativeActorRootMotionAlignment(
    const NativeActorRootMotionOwnership& ownership, uint8_t movementFlags,
    std::array<float, 3> consumedRootBaseTranslation,
    std::array<float, 3> nativeRootTranslation) {
    NativeActorRootMotionAlignment result;
    const auto finiteVec3 = [](const std::array<float, 3>& value) {
        return std::all_of(value.begin(), value.end(), [](float component) {
            return std::isfinite(component);
        });
    };
    if (!ownership.Available || !finiteVec3(consumedRootBaseTranslation) ||
        !finiteVec3(nativeRootTranslation)) {
        result.Status = "actor_root_motion_alignment_input_invalid";
        return result;
    }
    const bool movementEnabled =
        (movementFlags & ownership.MovementEnabledFlag) != 0;
    result.ControllerOwnsXz =
        movementEnabled && ownership.AlignXzWhenMovementEnabled;
    result.ControllerOwnsY =
        movementEnabled && ownership.AlignYWhenMovementEnabledAndUpdateY &&
        (movementFlags & ownership.UpdateYFlag) != 0;
    if (result.ControllerOwnsXz) {
        result.Translation[0] =
            consumedRootBaseTranslation[0] - nativeRootTranslation[0];
        result.Translation[2] =
            consumedRootBaseTranslation[2] - nativeRootTranslation[2];
    }
    if (result.ControllerOwnsY) {
        result.Translation[1] =
            consumedRootBaseTranslation[1] - nativeRootTranslation[1];
    }
    result.Available = true;
    result.Status = result.ControllerOwnsXz || result.ControllerOwnsY
                        ? "controller_owned_axes_aligned"
                        : "authored_root_preserved";
    return result;
}

NativeActorResourceVisibility NativeActorRenderProvider::ResolvePlayerResourceVisibility(
    const NativeActorRenderSource& actor, const NativePlayerModelResourceState& state) {
    if (!actor.Ready()) {
        NativeActorResourceVisibility result;
        result.Status = "actor_model_unavailable";
        return result;
    }
    {
        std::scoped_lock lock(mMutex);
        if (!mPlayerModelResourceProfileAttempted) {
            mPlayerModelResourceProfileAttempted = true;
            try {
                const auto source = mSources.Load(kPlayerModelResourceProfile);
                if (source == nullptr) {
                    mPlayerModelResourceProfileError = "player model resource profile is unavailable";
                } else {
                    mPlayerModelResourceProfile =
                        nlohmann::json::parse(source->Bytes->begin(), source->Bytes->end());
                    if (mPlayerModelResourceProfile.value("format", "") !=
                        "oot3d_player_model_resource_profile_v1") {
                        mPlayerModelResourceProfile = {};
                        mPlayerModelResourceProfileError =
                            "player model resource profile has an unsupported contract";
                    }
                }
            } catch (const std::exception& error) {
                mPlayerModelResourceProfile = {};
                mPlayerModelResourceProfileError = error.what();
            }
        }
    }
    if (!mPlayerModelResourceProfile.is_object()) {
        NativeActorResourceVisibility result;
        result.Status = "player_model_resource_profile_unavailable";
        result.Error = mPlayerModelResourceProfileError;
        return result;
    }
    return ResolveNativePlayerModelResourceVisibility(
        mPlayerModelResourceProfile, actor.Model->Model, state);
}

NativeEnKoRuntimeBinding NativeActorRenderProvider::ResolveEnKoRuntimeBinding(
    uint32_t subtype, uint32_t semanticAnimationIndex) const {
    if (!mEnKoRuntimeContract.is_object()) {
        NativeEnKoRuntimeBinding result;
        result.Subtype = subtype;
        result.SemanticAnimationIndex = semanticAnimationIndex;
        result.Status = "enko_runtime_contract_unavailable";
        result.Error = mEnKoRuntimeContractError;
        return result;
    }
    return ResolveNativeEnKoRuntimeBinding(
        mEnKoRuntimeContract, subtype, semanticAnimationIndex);
}

Oot3dNativeRenderModel NativeActorRenderProvider::BuildPosedRenderModel(
    const NativeActorRenderSource& actor, const CsabPose& pose,
    const NativeActorFaceSample* face,
    const std::vector<uint8_t>* resourceVisibility,
    const NativeActorRenderSource* materialAnimationSource) const {
    if (!actor.Ready() || !pose.Valid) {
        return {};
    }
    Oot3dNativeRenderModel model;
    {
        std::scoped_lock lock(mMutex);
        if (!actor.Model->RenderModelTexturePayloadsStripped) {
            StripOot3dNativeRenderModelTexturePayloads(actor.Model->RenderModel);
            actor.Model->RenderModelTexturePayloadsStripped = true;
        }
        model = actor.Model->RenderModel;
    }
    if (resourceVisibility != nullptr) {
        ApplyOot3dNativeRenderModelResourceVisibility(model, *resourceVisibility);
    }
    if (!UpdatePosedRenderModel(model, actor, pose, face, materialAnimationSource)) {
        return {};
    }
    return model;
}

bool NativeActorRenderProvider::UpdatePosedRenderModel(
    Oot3dNativeRenderModel& model, const NativeActorRenderSource& actor,
    const CsabPose& pose, const NativeActorFaceSample* face,
    const NativeActorRenderSource* materialAnimationSource) const {
    if (!actor.Ready() || !pose.Valid || model.Batches.empty()) {
        return false;
    }
    const auto skinTransforms = BuildOot3dNativeDemoSkinTransforms(
        actor.Model->BindWorldTransforms, pose);
    ApplyOot3dNativeRenderModelPose(model, actor.Model->Model, &pose, &skinTransforms);
    const auto* faceArchive = materialAnimationSource != nullptr && materialAnimationSource->Ready()
                                  ? materialAnimationSource->Archive.get()
                                  : actor.Archive.get();
    if (face != nullptr && face->Available && faceArchive != nullptr &&
        !faceArchive->MaterialAnimations.empty()) {
        std::span<const CmabMaterialAnimation> materialAnimations =
            faceArchive->MaterialAnimations;
        if (face->MaterialAnimationSelectorAvailable) {
            if (face->MaterialAnimationTypeLocalIndex >= materialAnimations.size()) {
                return false;
            }
            materialAnimations = materialAnimations.subspan(
                face->MaterialAnimationTypeLocalIndex, 1);
        }
        ApplyOot3dNativeRenderModelMaterialAnimationFrames(
            model, actor.Model->Model, materialAnimations,
            { { "eye", static_cast<float>(face->EyeIndex) },
              { "mouth", static_cast<float>(face->MouthIndex) } },
            0.0f);
    }
    return true;
}

void NativeActorRenderProvider::Clear() {
    std::scoped_lock lock(mMutex);
    mModels.clear();
    mArchives.clear();
    mAnimationCache.clear();
}

} // namespace ThreeDsRecomp::Oot3d
