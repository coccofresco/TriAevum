#include "oot3d_native_limb_callback.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string_view>

namespace Oot3dNativeGame {
namespace {

bool ReadU32(const nlohmann::json& value, uint32_t& result) {
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

bool ReadSha256(const nlohmann::json& value, std::string& result) {
    if (!value.is_string()) {
        return false;
    }
    result = value.get<std::string>();
    return result.size() == 64 &&
           std::all_of(result.begin(), result.end(), [](unsigned char value) {
               return (value >= '0' && value <= '9') ||
                      (value >= 'a' && value <= 'f');
           });
}

bool RangeWithin(uint32_t offset, uint32_t size, uint32_t extent) {
    return static_cast<uint64_t>(offset) + size <= extent;
}

bool MatchesParameterTypes(const nlohmann::json& value) {
    static constexpr std::array<std::string_view, 4> expected = {
        "Oot3dPlayState*", "s32", "Oot3dMtx3x4*", "void*",
    };
    if (!value.is_array() || value.size() != expected.size()) {
        return false;
    }
    for (size_t index = 0; index < expected.size(); ++index) {
        if (!value[index].is_string() ||
            value[index].get<std::string>() != expected[index]) {
            return false;
        }
    }
    return true;
}

bool WriteS16(NativeA32ExecutionRuntime& nativeExecution, uint32_t address,
              int16_t value) {
    return nativeExecution.Write16(address, static_cast<uint16_t>(value));
}

bool WriteMatrix(NativeA32ExecutionRuntime& nativeExecution, uint32_t address,
                 const std::array<float, 12>& matrix) {
    for (size_t index = 0; index < matrix.size(); ++index) {
        if (!nativeExecution.Write32(
                address + static_cast<uint32_t>(index * sizeof(float)),
                std::bit_cast<uint32_t>(matrix[index]))) {
            return false;
        }
    }
    return true;
}

bool ReadMatrix(NativeA32ExecutionRuntime& nativeExecution, uint32_t address,
                std::array<float, 12>& matrix) {
    for (size_t index = 0; index < matrix.size(); ++index) {
        uint32_t bits = 0;
        if (!nativeExecution.Read32(
                address + static_cast<uint32_t>(index * sizeof(float)), &bits)) {
            return false;
        }
        matrix[index] = std::bit_cast<float>(bits);
        if (!std::isfinite(matrix[index])) {
            return false;
        }
    }
    return true;
}

bool ExecuteLimb(const NativeEnKoLimbCallbackContract& contract,
                 uint32_t actorAddress, uint32_t playAddress,
                 uint32_t matrixAddress, uint32_t limbIndex,
                 std::array<float, 12>& matrix,
                 NativeA32ExecutionRuntime& nativeExecution,
                 std::string& error) {
    static constexpr std::array<float, 12> identity = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    if (!WriteMatrix(nativeExecution, matrixAddress, identity)) {
        error = "could not initialize native limb matrix";
        return false;
    }
    oot3d::recomp::a32::GuestState guestState{};
    guestState.r[0] = playAddress;
    guestState.r[1] = limbIndex;
    guestState.r[2] = matrixAddress;
    guestState.r[3] = actorAddress;
    const auto call = nativeExecution.Call(contract.CallbackAddress, guestState);
    if (!call.Completed) {
        error = call.Error;
        return false;
    }
    if (guestState.r[0] != 0) {
        error = "native limb callback returned a nonzero result";
        return false;
    }
    if (!ReadMatrix(nativeExecution, matrixAddress, matrix)) {
        error = "could not read native limb matrix";
        return false;
    }
    return true;
}

} // namespace

NativeEnKoLimbCallbackContract ResolveNativeEnKoLimbCallbackContract(
    const nlohmann::json& document) {
    NativeEnKoLimbCallbackContract result;
    const auto drawIt = document.find("draw_callback");
    const auto actorIt = document.find("actor_profile");
    if (drawIt == document.end() || !drawIt->is_object() ||
        actorIt == document.end() || !actorIt->is_object()) {
        result.Status = "native_enko_limb_callback_contract_invalid";
        return result;
    }
    const auto abiIt = drawIt->find("native_abi");
    if (abiIt == drawIt->end() || !abiIt->is_object() ||
        abiIt->value("name", "") != "EnKo_OverrideLimbDraw" ||
        abiIt->value("closure_kind", "") != "maintained_abi" ||
        abiIt->value("return_type", "") != "s32" ||
        !abiIt->contains("parameter_types") ||
        !MatchesParameterTypes(abiIt->at("parameter_types")) ||
        !drawIt->contains("address") || !drawIt->contains("size") ||
        !drawIt->contains("sha256") ||
        !drawIt->contains("torso_limb_index") ||
        !drawIt->contains("head_limb_index") ||
        !drawIt->contains("torso_rotation_u16x3_offset") ||
        !drawIt->contains("head_rotation_u16x3_offset") ||
        !actorIt->contains("instance_size") ||
        !ReadU32(drawIt->at("address"), result.CallbackAddress) ||
        !ReadU32(drawIt->at("size"), result.CallbackSize) ||
        !ReadSha256(drawIt->at("sha256"), result.CallbackSha256) ||
        !ReadU32(actorIt->at("instance_size"), result.ActorStateSize) ||
        !ReadU32(drawIt->at("torso_limb_index"), result.TorsoLimbIndex) ||
        !ReadU32(drawIt->at("head_limb_index"), result.HeadLimbIndex) ||
        !ReadU32(drawIt->at("torso_rotation_u16x3_offset"),
                 result.TorsoRotationOffset) ||
        !ReadU32(drawIt->at("head_rotation_u16x3_offset"),
                 result.HeadRotationOffset) ||
        result.CallbackAddress == 0 || result.CallbackSize == 0 ||
        result.TorsoLimbIndex == result.HeadLimbIndex ||
        !RangeWithin(result.TorsoRotationOffset, 6, result.ActorStateSize) ||
        !RangeWithin(result.HeadRotationOffset, 6, result.ActorStateSize)) {
        result.Status = "native_enko_limb_callback_contract_invalid";
        return result;
    }
    uint32_t abiAddress = 0;
    if (!abiIt->contains("address") ||
        !ReadU32(abiIt->at("address"), abiAddress) ||
        abiAddress != result.CallbackAddress) {
        result.Status = "native_enko_limb_callback_contract_invalid";
        return result;
    }
    result.Available = true;
    result.Status = "native_enko_limb_callback_contract_ready";
    return result;
}

bool UpdateNativeEnKoLimbCallback(
    const NativeEnKoLimbCallbackContract& contract,
    const NativeEnKoLimbCallbackInput& input,
    NativeEnKoLimbCallbackState& state,
    NativeA32ExecutionRuntime& nativeExecution) {
    state.Error.clear();
    if (!contract.Available) {
        state.Status = "native_enko_limb_callback_unavailable";
        return false;
    }
    nativeExecution.ResetScratch();
    const auto actor = nativeExecution.AllocateScratch(contract.ActorStateSize);
    const auto play = nativeExecution.AllocateScratch(4);
    const auto matrix = nativeExecution.AllocateScratch(sizeof(float) * 12);
    if (!actor || !play || !matrix) {
        state.Status = "native_enko_limb_callback_scratch_unavailable";
        return false;
    }
    if (!WriteS16(nativeExecution, *actor + contract.HeadRotationOffset,
                  input.HeadPitch) ||
        !WriteS16(nativeExecution, *actor + contract.HeadRotationOffset + 2,
                  input.HeadYaw) ||
        !WriteS16(nativeExecution, *actor + contract.HeadRotationOffset + 4, 0) ||
        !WriteS16(nativeExecution, *actor + contract.TorsoRotationOffset,
                  input.TorsoPitch) ||
        !WriteS16(nativeExecution, *actor + contract.TorsoRotationOffset + 2,
                  input.TorsoYaw) ||
        !WriteS16(nativeExecution, *actor + contract.TorsoRotationOffset + 4, 0)) {
        state.Status = "native_enko_limb_callback_state_write_failed";
        return false;
    }

    std::array<float, 12> torso{};
    std::array<float, 12> head{};
    if (!ExecuteLimb(contract, *actor, *play, *matrix,
                     contract.TorsoLimbIndex, torso, nativeExecution,
                     state.Error) ||
        !ExecuteLimb(contract, *actor, *play, *matrix,
                     contract.HeadLimbIndex, head, nativeExecution,
                     state.Error)) {
        state.Status = "native_enko_limb_callback_call_failed";
        return false;
    }
    state.TorsoTransform = torso;
    state.HeadTransform = head;
    ++state.UpdateCount;
    state.Status = "updated_by_native_enko_limb_callback";
    return true;
}

} // namespace Oot3dNativeGame
