#include "oot3d_native_a32_process.h"
#include "oot3d_source_actor_update_all_runtime.h"
#include "oot3d_a32_generated.h"

#include "oot3d/actor_spawn.h"
#include "oot3d/owner_closure.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

constexpr uint32_t kOwnerEntry = 0x00461344U;
constexpr uint32_t kOwnerReturn = 0x0BADF00CU;
constexpr uint32_t kInitContinuation = 0x004615E8U;
constexpr uint32_t kUpdateContinuation = 0x004617B4U;
constexpr uint32_t kInitCallback = 0x00600000U;
constexpr uint32_t kObjHanaInit = 0x001E1734U;
constexpr uint32_t kObjHanaTextBase = 0x001E1000U;
constexpr uint32_t kObjHanaModelRecords = 0x005350D4U;
constexpr uint32_t kObjHanaInitChain = 0x00535104U;
constexpr uint32_t kObjHanaCylinderInit = 0x0053509CU;
constexpr uint32_t kObjHanaCollisionInfo = 0x00535094U;
constexpr uint32_t kObjHanaSaveContext = 0x00588758U;
constexpr uint32_t kPlay = 0x10000000U;
constexpr uint32_t kPlayer = 0x10008000U;
constexpr uint32_t kActorContext = kPlay + 0x208CU;
constexpr uint32_t kActor = 0x1000C000U;
constexpr uint32_t kActorStride = 0x200U;
constexpr uint32_t kActorPoolSize = 0x3000U;
constexpr uint32_t kObjHanaActor = kActor + 0x2000U;
constexpr uint32_t kActorCategoryCount = 12U;
constexpr uint32_t kOverlay = 0x10020000U;
constexpr uint32_t kOverlayResource = 0x10021000U;
constexpr uint32_t kStack = 0x10010000U;
constexpr uint32_t kTls = 0x10015000U;
constexpr uint64_t kSystemTick = 0x0000000012345678ULL;
constexpr uint32_t kCallbackMarker = 0x3F400000U;

constexpr uint32_t kModelHandleLoadAll = 0x00372F38U;
constexpr uint32_t kActorProcessInitChain = 0x003510B0U;
constexpr uint32_t kActorSetScale = 0x0037572CU;
constexpr uint32_t kColliderInitCylinder = 0x00353DD0U;
constexpr uint32_t kColliderSetCylinder = 0x00353D24U;
constexpr uint32_t kColliderUpdateCylinder = 0x0037632CU;
constexpr uint32_t kCollisionCheckSetInfo = 0x00350D20U;
constexpr uint32_t kZarGetCmbByIndex = 0x00358EF8U;
constexpr uint32_t kModelFactoryCreate = kInitCallback + 0x40U;

constexpr uint32_t kAttentionState = 0x0050C8E8U;
constexpr uint32_t kRendererGuard = 0x0055A21CU;
constexpr uint32_t kRendererState = 0x005BE5B8U;
constexpr uint32_t kModelLoadStats = 0x0050C058U;
constexpr uint32_t kModelFactory = 0x005AF000U;
constexpr uint32_t kModelFactoryVtable = 0x005AF100U;
constexpr uint32_t kModelCmbBase = 0x005AF200U;
constexpr uint32_t kModelContext = 0x00C0FFEEU;
constexpr uint32_t kObjectReadyOffset = 0x00003A64U;
constexpr uint32_t kActorUpdateRecord = 0x005C1858U;

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void WriteWord(std::vector<uint8_t>& bytes, uint32_t base,
               uint32_t address, uint32_t value) {
    Expect(address >= base &&
               static_cast<uint64_t>(address - base) + sizeof(value) <=
                   bytes.size(),
           "literal lies outside mapped owner text");
    std::memcpy(bytes.data() + (address - base), &value, sizeof(value));
}

void WriteHalf(std::vector<uint8_t>& bytes, uint32_t base,
               uint32_t address, uint16_t value) {
    Expect(address >= base &&
               static_cast<uint64_t>(address - base) + sizeof(value) <=
                   bytes.size(),
           "halfword lies outside mapped fixture data");
    std::memcpy(bytes.data() + (address - base), &value, sizeof(value));
}

class OwnerHostServices final
    : public Oot3dNativeGame::NativeA32HostServices {
  public:
    Oot3dNativeGame::NativeA32HostResult HandleSvc(
        uint32_t immediate, oot3d::recomp::a32::GuestState& state,
        Oot3dNativeGame::NativeA32Memory&,
        Oot3dNativeGame::NativeA32HostContext&) override {
        if (immediate != 0x28U) {
            return {Oot3dNativeGame::NativeA32HostAction::Fault,
                    std::nullopt, immediate,
                    "unexpected Actor_UpdateAll SVC"};
        }
        ++SystemTickCalls;
        state.r[0] = static_cast<uint32_t>(kSystemTick);
        state.r[1] = static_cast<uint32_t>(kSystemTick >> 32U);
        return {Oot3dNativeGame::NativeA32HostAction::Resume};
    }

    uint32_t SystemTickCalls = 0U;
};

struct CallbackBoundaryCheckpoint {
    oot3d::recomp::a32::GuestState State;
    nlohmann::json ProcessState;
};

struct NativeCallbackProbe {
    Oot3dNativeGame::NativeA32Process* Process = nullptr;
    uint32_t ForcedFailurePc = 0U;
    uint32_t CallbackCalls = 0U;
    uint32_t InitCallbackCalls = 0U;
    uint32_t UpdateCallbackCalls = 0U;
    uint32_t DestroyCalls = 0U;
    uint32_t FreeCalls = 0U;
    uint32_t RegistryReleaseCalls = 0U;
    uint32_t CallbackRoundTrips = 0U;
    uint32_t ObjHanaModelLoadCalls = 0U;
    uint32_t ObjHanaZarCalls = 0U;
    uint32_t ObjHanaFactoryCalls = 0U;
    uint32_t ObjHanaInitChainCalls = 0U;
    uint32_t ObjHanaScaleCalls = 0U;
    uint32_t ObjHanaColliderInitCalls = 0U;
    uint32_t ObjHanaColliderSetCalls = 0U;
    uint32_t ObjHanaColliderUpdateCalls = 0U;
    uint32_t ObjHanaCollisionInfoCalls = 0U;
    uint32_t ObjHanaKillCalls = 0U;
    bool ObjHanaAbiMatched = true;
    bool InitContinuationRoundTripped = false;
    bool UpdateContinuationRoundTripped = false;
    std::vector<CallbackBoundaryCheckpoint> CallbackCheckpoints;
    std::vector<uint32_t> CallbackActors;
    bool CallbackArgumentsMatched = true;
    std::string Error;
};

uint32_t FixtureActorAddress(uint32_t category) {
    return category == 2U ? kPlayer : kActor + category * kActorStride;
}

bool IsFixtureActorAddress(uint32_t address) {
    if (address == kPlayer) {
        return true;
    }
    if (address < kActor) {
        return false;
    }
    const uint32_t offset = address - kActor;
    return offset % kActorStride == 0U &&
           offset / kActorStride < kActorCategoryCount &&
           offset / kActorStride != 2U;
}

bool GuestStatesEqual(
    const oot3d::recomp::a32::GuestState& left,
    const oot3d::recomp::a32::GuestState& right) {
    return left.r == right.r && left.cpsr == right.cpsr &&
           left.fpscr == right.fpscr &&
           left.thread_pointer == right.thread_pointer &&
           left.vfp == right.vfp &&
           left.exclusive_address == right.exclusive_address &&
           left.exclusive_token == right.exclusive_token &&
           left.exclusive_size == right.exclusive_size &&
           left.exclusive_valid == right.exclusive_valid;
}

bool CaptureCallbackBoundary(
    NativeCallbackProbe& probe,
    const oot3d::recomp::a32::GuestState& state) {
    bool* alreadyCaptured = nullptr;
    if (state.r[14] == kInitContinuation) {
        alreadyCaptured = &probe.InitContinuationRoundTripped;
    } else if (state.r[14] == kUpdateContinuation) {
        alreadyCaptured = &probe.UpdateContinuationRoundTripped;
    } else {
        probe.Error = "callback has an unknown owner continuation";
        return false;
    }
    if (*alreadyCaptured) {
        return true;
    }
    if (probe.Process == nullptr) {
        probe.Error = "callback checkpoint has no process";
        return false;
    }

    const auto schedulerState = probe.Process->PrimaryThreadState();
    probe.Process->PrimaryThreadState() = state;
    probe.CallbackCheckpoints.push_back(
        {state, probe.Process->CaptureState()});
    probe.Process->PrimaryThreadState() = schedulerState;
    *alreadyCaptured = true;
    return true;
}

void ReturnToLinkRegister(
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::ExecutionResult* result) {
    state.r[15] = state.r[14];
    *result = {
        oot3d::recomp::a32::ExitKind::Branch,
        state.r[15],
        oot3d::recomp::a32::FallbackReason::None,
        0U,
    };
}

bool HandleOwnerNativeFunction(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result, void* user) {
    if (result == nullptr || user == nullptr) {
        return false;
    }
    auto& probe = *static_cast<NativeCallbackProbe*>(user);
    if (pc == probe.ForcedFailurePc) {
        *result = {
            oot3d::recomp::a32::ExitKind::Unsupported,
            pc,
            oot3d::recomp::a32::FallbackReason::Unsupported,
            pc,
        };
        return true;
    }
    switch (pc) {
    case kModelHandleLoadAll: {
        uint32_t terminator = 0xFFFFFFFFU;
        uint16_t params = 0U;
        if (!memory.Read32(state.r[13], &terminator) ||
            !memory.Read16(state.r[0] + 0x1CU, &params)) {
            return false;
        }
        constexpr std::array<uint32_t, 3> modelIndices{13U, 5U, 10U};
        const uint32_t selector = params & 3U;
        probe.ObjHanaAbiMatched &=
            state.r[0] == kObjHanaActor &&
            state.r[1] == kPlay &&
            state.r[2] == kObjHanaActor + 0x1FCU &&
            selector < modelIndices.size() &&
            state.r[3] == modelIndices[selector] &&
            terminator == 0U;
        uint32_t modelContext = 0U;
        uint32_t modelsLoaded = 0U;
        uint32_t actorsWithModels = 0U;
        if (!memory.Read32(state.r[0] + 0x178U, &modelContext) ||
            !memory.Read32(kModelLoadStats, &modelsLoaded) ||
            !memory.Read32(
                kModelLoadStats + sizeof(uint32_t),
                &actorsWithModels)) {
            return false;
        }
        if (!probe.ObjHanaAbiMatched ||
            !memory.Write8(state.r[0] + 0x19AU, 1U) ||
            !memory.Write32(kModelFactory + 0x08U, modelContext) ||
            !memory.Write32(
                state.r[2], 0xA5000000U | state.r[3]) ||
            !memory.Write32(kModelLoadStats, modelsLoaded + 1U) ||
            !memory.Write32(kModelFactory + 0x08U, 0U) ||
            !memory.Write32(
                kModelLoadStats + sizeof(uint32_t),
                actorsWithModels + 1U)) {
            return false;
        }
        ++probe.ObjHanaModelLoadCalls;
        ReturnToLinkRegister(state, result);
        return true;
    }
    case kZarGetCmbByIndex: {
        uint16_t params = 0U;
        if (!memory.Read16(kObjHanaActor + 0x1CU, &params)) {
            return false;
        }
        constexpr std::array<uint32_t, 3> modelIndices{13U, 5U, 10U};
        const uint32_t selector = params & 3U;
        probe.ObjHanaAbiMatched &=
            selector < modelIndices.size() &&
            state.r[0] == kPlay + kObjectReadyOffset + 8U &&
            state.r[1] == modelIndices[selector];
        if (!probe.ObjHanaAbiMatched) {
            return false;
        }
        state.r[0] = kModelCmbBase + state.r[1] * sizeof(uint32_t);
        ++probe.ObjHanaZarCalls;
        ReturnToLinkRegister(state, result);
        return true;
    }
    case kModelFactoryCreate: {
        uint16_t params = 0U;
        uint32_t activeContext = 0U;
        if (!memory.Read16(kObjHanaActor + 0x1CU, &params) ||
            !memory.Read32(kModelFactory + 0x08U, &activeContext)) {
            return false;
        }
        constexpr std::array<uint32_t, 3> modelIndices{13U, 5U, 10U};
        const uint32_t selector = params & 3U;
        const uint32_t modelIndex =
            selector < modelIndices.size() ? modelIndices[selector] : 0U;
        probe.ObjHanaAbiMatched &=
            selector < modelIndices.size() &&
            state.r[0] == kModelFactory &&
            state.r[1] ==
                kModelCmbBase + modelIndex * sizeof(uint32_t) &&
            state.r[2] == 1U &&
            activeContext == kModelContext;
        if (!probe.ObjHanaAbiMatched) {
            return false;
        }
        state.r[0] = 0xA5000000U | modelIndex;
        ++probe.ObjHanaFactoryCalls;
        ReturnToLinkRegister(state, result);
        return true;
    }
    case kActorProcessInitChain:
        probe.ObjHanaAbiMatched &=
            state.r[0] == kObjHanaActor &&
            state.r[1] == kObjHanaInitChain;
        if (!probe.ObjHanaAbiMatched ||
            !memory.Write32(
                state.r[0] + 0xFCU,
                std::bit_cast<uint32_t>(900.0F)) ||
            !memory.Write32(
                state.r[0] + 0x100U,
                std::bit_cast<uint32_t>(60.0F)) ||
            !memory.Write32(
                state.r[0] + 0x104U,
                std::bit_cast<uint32_t>(800.0F))) {
            return false;
        }
        ++probe.ObjHanaInitChainCalls;
        ReturnToLinkRegister(state, result);
        return true;
    case kActorSetScale: {
        uint16_t params = 0U;
        if (!memory.Read16(state.r[0] + 0x1CU, &params)) {
            return false;
        }
        constexpr std::array<float, 3> scales{
            0.01F, 0.1F, 0.4F};
        const uint32_t selector = params & 3U;
        probe.ObjHanaAbiMatched &=
            state.r[0] == kObjHanaActor &&
            selector < scales.size() &&
            state.vfp[0] ==
                std::bit_cast<uint32_t>(scales[selector]);
        if (!probe.ObjHanaAbiMatched) {
            return false;
        }
        for (uint32_t offset = 0x54U;
             offset <= 0x5CU; offset += 4U) {
            if (!memory.Write32(
                    state.r[0] + offset, state.vfp[0])) {
                return false;
            }
        }
        ++probe.ObjHanaScaleCalls;
        ReturnToLinkRegister(state, result);
        return true;
    }
    case kColliderInitCylinder:
        probe.ObjHanaAbiMatched &=
            state.r[0] == kPlay &&
            state.r[1] == kObjHanaActor + 0x1A4U;
        if (!probe.ObjHanaAbiMatched) {
            return false;
        }
        for (uint32_t offset = 0U; offset < 0x58U; ++offset) {
            if (!memory.Write8(state.r[1] + offset, 0U)) {
                return false;
            }
        }
        ++probe.ObjHanaColliderInitCalls;
        ReturnToLinkRegister(state, result);
        return true;
    case kColliderSetCylinder:
        probe.ObjHanaAbiMatched &=
            state.r[0] == kPlay &&
            state.r[1] == kObjHanaActor + 0x1A4U &&
            state.r[2] == kObjHanaActor &&
            state.r[3] == kObjHanaCylinderInit;
        if (!probe.ObjHanaAbiMatched ||
            !memory.Write32(state.r[1], state.r[2]) ||
            !memory.Write32(state.r[1] + 4U, state.r[3])) {
            return false;
        }
        ++probe.ObjHanaColliderSetCalls;
        ReturnToLinkRegister(state, result);
        return true;
    case kColliderUpdateCylinder:
        probe.ObjHanaAbiMatched &=
            state.r[0] == kObjHanaActor &&
            state.r[1] == kObjHanaActor + 0x1A4U;
        if (!probe.ObjHanaAbiMatched) {
            return false;
        }
        for (uint32_t offset = 0U; offset < 12U; offset += 4U) {
            uint32_t value = 0U;
            if (!memory.Read32(state.r[0] + 0x28U + offset, &value) ||
                !memory.Write32(state.r[1] + 0x4CU + offset, value)) {
                return false;
            }
        }
        ++probe.ObjHanaColliderUpdateCalls;
        ReturnToLinkRegister(state, result);
        return true;
    case kCollisionCheckSetInfo:
        probe.ObjHanaAbiMatched &=
            state.r[0] == kObjHanaActor + 0xA0U &&
            state.r[1] == 0U &&
            state.r[2] == kObjHanaCollisionInfo;
        if (!probe.ObjHanaAbiMatched ||
            !memory.Write32(state.r[0], state.r[2])) {
            return false;
        }
        ++probe.ObjHanaCollisionInfoCalls;
        ReturnToLinkRegister(state, result);
        return true;
    case 0x00363A20U:
        if (!memory.Write16(state.r[2], 0U) ||
            !memory.Write16(state.r[3], 0U)) {
            return false;
        }
        ReturnToLinkRegister(state, result);
        return true;
    case 0x00368CC0U:
        if (!memory.Write32(state.r[2], 0U) ||
            !memory.Write32(state.r[2] + 4U, 0U) ||
            !memory.Write32(state.r[2] + 8U, 0U) ||
            !memory.Write32(
                state.r[3], std::bit_cast<uint32_t>(1.0F))) {
            return false;
        }
        ReturnToLinkRegister(state, result);
        return true;
    case 0x003723C0U: {
        uint32_t poly = 0U;
        uint32_t bgId = 0U;
        if (!memory.Read32(state.r[13], &poly) ||
            !memory.Read32(state.r[13] + 20U, &bgId) ||
            !memory.Write32(state.r[3], 0U) ||
            !memory.Write32(state.r[3] + 4U, 0U) ||
            !memory.Write32(state.r[3] + 8U, 0U) ||
            !memory.Write32(poly, 0U) ||
            !memory.Write32(bgId, 0U)) {
            return false;
        }
        state.r[0] = 0U;
        ReturnToLinkRegister(state, result);
        return true;
    }
    case 0x00374428U: {
        uint32_t flags = 0U;
        if (!memory.Read32(state.r[0] + 0x04U, &flags) ||
            !memory.Write32(state.r[0] + 0x04U, flags & ~1U) ||
            !memory.Write32(state.r[0] + 0x13CU, 0U) ||
            !memory.Write32(state.r[0] + 0x140U, 0U)) {
            return false;
        }
        if (state.r[0] == kObjHanaActor) {
            ++probe.ObjHanaKillCalls;
        }
        ReturnToLinkRegister(state, result);
        return true;
    }
    case 0x0047CCDCU:
        if (!memory.Write16(state.r[0] + 0x04U, 0U) ||
            !memory.Write16(state.r[0] + 0x0CU, 0U)) {
            return false;
        }
        ReturnToLinkRegister(state, result);
        return true;
    case 0x0030CB90U:
    case 0x00334354U:
    case 0x0036788CU:
    case 0x0037547CU:
    case 0x00452240U:
    case 0x00477E44U:
    case 0x0047955CU:
    case 0x0047976CU:
    case 0x0047AF24U:
    case 0x0047C938U:
        ReturnToLinkRegister(state, result);
        return true;
    case 0x002D644CU:
        ++probe.DestroyCalls;
        ReturnToLinkRegister(state, result);
        return true;
    case 0x00350EF4U:
        ++probe.FreeCalls;
        ReturnToLinkRegister(state, result);
        return true;
    case 0x0049FA78U:
        ++probe.RegistryReleaseCalls;
        ReturnToLinkRegister(state, result);
        return true;
    case 0x0036DF4CU:
        for (uint32_t offset = 0U; offset < 12U; offset += 4U) {
            uint32_t value = 0U;
            if (!memory.Read32(state.r[1] + offset, &value) ||
                !memory.Write32(state.r[0] + offset, value)) {
                return false;
            }
        }
        ReturnToLinkRegister(state, result);
        return true;
    case 0x003758B0U:
        state.r[0] = 0U;
        ReturnToLinkRegister(state, result);
        return true;
    case 0x00332284U:
    case 0x0035FE90U:
    case 0x003679B4U:
    case 0x0036C5BCU:
    case 0x003705A0U:
        state.r[0] = 0U;
        ReturnToLinkRegister(state, result);
        return true;
    case 0x0036A7A0U:
        state.r[0] = 1U;
        ReturnToLinkRegister(state, result);
        return true;
    case 0x00332754U:
        state.r[0] = 0U;
        state.r[1] = 0U;
        ReturnToLinkRegister(state, result);
        return true;
    case 0x00373074U:
        state.r[0] = state.r[1] == 0xFFFFFFFFU ? 0U : 1U;
        ReturnToLinkRegister(state, result);
        return true;
    case kInitCallback: {
        ++probe.CallbackCalls;
        probe.CallbackActors.push_back(state.r[0]);
        probe.CallbackArgumentsMatched &=
            IsFixtureActorAddress(state.r[0]) && state.r[1] == kPlay &&
            (state.r[14] == kInitContinuation ||
             state.r[14] == kUpdateContinuation);
        const uint32_t actor = state.r[0];
        if (!probe.CallbackArgumentsMatched ||
            !CaptureCallbackBoundary(probe, state)) {
            return false;
        }
        if (state.r[14] == kInitContinuation) {
            uint32_t flags = 0U;
            ++probe.InitCallbackCalls;
            if (!memory.Read32(actor + 0x04U, &flags) ||
                !memory.Write32(actor + 0x04U, flags | 0x50U) ||
                !memory.Write32(actor + 0x13CU, kInitCallback) ||
                !memory.Write32(actor + 0x1A0U, kCallbackMarker)) {
                return false;
            }
        } else {
            uint32_t marker = 0U;
            ++probe.UpdateCallbackCalls;
            if (!memory.Read32(actor + 0x1A0U, &marker) ||
                !memory.Write32(actor + 0x1A0U, marker + 1U)) {
                return false;
            }
        }
        ReturnToLinkRegister(state, result);
        return true;
    }
    default:
        return false;
    }
}

constexpr std::array<uint32_t, 38> kNativeFunctionPcs{
    0x002D644CU,
    0x0030CB90U,
    0x00332284U,
    0x00332754U,
    0x00334354U,
    0x00350EF4U,
    kCollisionCheckSetInfo,
    kActorProcessInitChain,
    kColliderSetCylinder,
    kColliderInitCylinder,
    kZarGetCmbByIndex,
    0x0035FE90U,
    0x00363A20U,
    0x0036788CU,
    0x003679B4U,
    0x00368CC0U,
    0x0036A7A0U,
    0x0036C5BCU,
    0x0036DF4CU,
    0x003705A0U,
    0x003723C0U,
    0x00373074U,
    0x00374428U,
    0x0037547CU,
    0x003758B0U,
    kActorSetScale,
    kColliderUpdateCylinder,
    kModelHandleLoadAll,
    0x00452240U,
    0x00477E44U,
    0x0047955CU,
    0x0047976CU,
    0x0047AF24U,
    0x0047C938U,
    0x0047CCDCU,
    0x0049FA78U,
    kInitCallback,
    kModelFactoryCreate,
};

void SeedAttentionState(Oot3dNativeGame::NativeA32Memory& memory) {
    const uint32_t floatMax =
        std::bit_cast<uint32_t>(std::numeric_limits<float>::max());
    Expect(memory.Write32(kAttentionState + 0xA4U, floatMax) &&
               memory.Write32(kAttentionState + 0xA8U, floatMax) &&
               memory.Write32(
                   kAttentionState + 0xACU,
                   static_cast<uint32_t>(
                       std::numeric_limits<int32_t>::max())),
           "seed attention search state");

    const uint32_t attention = kActorContext + 0x6CU;
    Expect(memory.Write32(attention + 0x40U,
                          std::bit_cast<uint32_t>(1.0F)) &&
               memory.Write8(attention + 0x4EU, 2U),
           "seed actor attention runtime");
}

void ConfigureProcess(Oot3dNativeGame::NativeA32Process& process,
                      bool withInitActor) {
    constexpr uint32_t lifecycleTextBase = 0x002DA000U;
    std::vector<uint8_t> lifecycleText(0x1000U);
    WriteWord(
        lifecycleText, lifecycleTextBase, 0x002DA298U, 0x000016F8U);
    WriteWord(
        lifecycleText, lifecycleTextBase, 0x002DA29CU, 0x00004C30U);

    constexpr uint32_t ownerTextBase = 0x00461000U;
    std::vector<uint8_t> ownerText(0x1000U);
    WriteWord(ownerText, ownerTextBase, 0x004617D8U, kRendererGuard);
    WriteWord(ownerText, ownerTextBase, 0x004617DCU, 0x005BE5B8U);
    WriteWord(ownerText, ownerTextBase, 0x004617E0U, 0x00100000U);
    WriteWord(ownerText, ownerTextBase, 0x004617E4U, 0x0048B210U);
    WriteWord(ownerText, ownerTextBase, 0x004617E8U,
              kActorUpdateRecord);
    WriteWord(ownerText, ownerTextBase, 0x004617ECU, 0x0050CCF4U);
    WriteWord(ownerText, ownerTextBase, 0x004617F0U, 0x00000116U);
    WriteWord(ownerText, ownerTextBase, 0x004617F4U, 0xC6C35000U);
    WriteWord(ownerText, ownerTextBase, 0x004617F8U, 0xBAD34AEEU);
    WriteWord(ownerText, ownerTextBase, 0x004617FCU, 0xC6C35000U);
    WriteWord(ownerText, ownerTextBase, 0x00461800U, 0x000F4240U);
    WriteWord(ownerText, ownerTextBase, 0x004618F8U, 0x0054AC24U);
    WriteWord(ownerText, ownerTextBase, 0x004618FCU, 0x0054AC20U);
    WriteWord(ownerText, ownerTextBase, 0x00461900U, 0x01000494U);

    std::vector<uint8_t> objHanaText(0x1000U);
    WriteWord(
        objHanaText, kObjHanaTextBase, 0x001E1818U,
        kObjHanaModelRecords);
    WriteWord(
        objHanaText, kObjHanaTextBase, 0x001E181CU,
        kObjHanaInitChain);
    WriteWord(
        objHanaText, kObjHanaTextBase, 0x001E1820U,
        kObjHanaCylinderInit);
    WriteWord(
        objHanaText, kObjHanaTextBase, 0x001E1824U,
        kObjHanaCollisionInfo);
    WriteWord(
        objHanaText, kObjHanaTextBase, 0x001E1828U,
        kObjHanaSaveContext);

    constexpr uint32_t modelLoaderTextBase = 0x00373000U;
    std::vector<uint8_t> modelLoaderText(0x1000U);
    WriteWord(
        modelLoaderText, modelLoaderTextBase, 0x0037305CU,
        kObjectReadyOffset);
    WriteWord(
        modelLoaderText, modelLoaderTextBase, 0x00373060U,
        kRendererGuard);
    WriteWord(
        modelLoaderText, modelLoaderTextBase, 0x00373064U,
        kRendererState);
    WriteWord(
        modelLoaderText, modelLoaderTextBase, 0x00373070U,
        kModelLoadStats);

    std::vector<uint8_t> objHanaData(0x1000U);
    WriteWord(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x00U, 13U);
    WriteWord(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x04U,
        std::bit_cast<uint32_t>(0.01F));
    WriteWord(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x08U, 0U);
    WriteHalf(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x0CU, 0xFFFFU);
    WriteHalf(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x0EU, 0U);
    WriteWord(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x10U, 5U);
    WriteWord(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x14U,
        std::bit_cast<uint32_t>(0.1F));
    WriteWord(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x18U,
        std::bit_cast<uint32_t>(58.0F));
    WriteHalf(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x1CU, 10U);
    WriteHalf(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x1EU, 18U);
    WriteWord(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x20U, 10U);
    WriteWord(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x24U,
        std::bit_cast<uint32_t>(0.4F));
    WriteWord(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x28U, 0U);
    WriteHalf(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x2CU, 12U);
    WriteHalf(
        objHanaData, 0x00535000U,
        kObjHanaModelRecords + 0x2EU, 44U);

    std::string error;
    const auto map = [&](const Oot3dNativeGame::NativeA32MemoryRegionConfig&
                             config) {
        Expect(process.MapRegion(config, &error),
               "map " + config.Name + ": " + error);
    };
    map({"actor_lifecycle_text", lifecycleTextBase, lifecycleText.size(),
         false, true, lifecycleText});
    map({"owner_text", ownerTextBase, ownerText.size(), false, true,
         ownerText});
    map({"obj_hana_text", kObjHanaTextBase, objHanaText.size(),
         false, true, objHanaText});
    map({"model_loader_text", modelLoaderTextBase,
         modelLoaderText.size(), false, true, modelLoaderText});
    map({"callback_text", kInitCallback, 0x1000U, false, true, {}});
    constexpr std::array<uint32_t, 26> serviceTextPages{
        0x002D6000U, 0x0030C000U, 0x00332000U, 0x00334000U,
        0x00350000U, 0x00351000U, 0x00353000U, 0x00358000U,
        0x0035F000U,
        0x00363000U, 0x00367000U,
        0x00368000U, 0x0036A000U, 0x0036C000U, 0x0036D000U,
        0x00370000U, 0x00372000U, 0x00374000U,
        0x00375000U, 0x00376000U, 0x00452000U, 0x00477000U,
        0x00479000U,
        0x0047A000U, 0x0047C000U, 0x0049F000U,
    };
    for (const uint32_t page : serviceTextPages) {
        map({"service_text_" + std::to_string(page),
             page, 0x1000U, false, true, {}});
    }
    map({"owner_globals_50c", 0x0050C000U, 0x1000U, true, false, {}});
    map({"owner_globals_51b", 0x0051B000U, 0x1000U, true, false, {}});
    map({"owner_globals_54a", 0x0054A000U, 0x1000U, true, false, {}});
    map({"owner_globals_55a", 0x0055A000U, 0x3000U, true, false, {}});
    map({"owner_globals_5af", 0x005AF000U, 0x3000U, true, false, {}});
    map({"owner_globals_5be", 0x005BE000U, 0x1000U, true, false, {}});
    map({"owner_globals_5c1", 0x005C1000U, 0x1000U, true, false, {}});
    map({"obj_hana_data", 0x00535000U, objHanaData.size(),
         false, false, objHanaData});
    map({"obj_hana_save", 0x00588000U, 0x1000U, true, false, {}});
    map({"play", kPlay, 0x7000U, true, false, {}});
    map({"player", kPlayer, 0x3000U, true, false, {}});
    map({"actor_pool", kActor, kActorPoolSize, true, false, {}});
    map({"overlay_resources", kOverlay, 0x2000U, true, false, {}});

    Expect(process.CreatePrimaryThread(
               {kOwnerEntry, kStack, 0x4000U, kTls, 0x1000U, 0U,
                0U, 0x10U, 0x03C00010U},
               &error),
           "create owner test thread: " + error);

    auto& memory = process.Memory();
    Expect(memory.Write32(kPlay + 0x20ACU, kPlayer) &&
               memory.Write8(kPlayer + 0x02U, 2U) &&
               memory.Write8(kPlayer + 0x1EU, 0U) &&
               memory.Write32(kPlayer + 0x13CU, kInitCallback) &&
               memory.Write8(kActorContext + 0x02U, 3U) &&
               memory.Write8(kActorContext + 0x08U, 1U) &&
               memory.Write32(kActorContext + 0x1CU, 1U) &&
               memory.Write32(kRendererGuard, 1U) &&
               memory.Write32(
                   kRendererState + 0x17CU, kModelFactory) &&
               memory.Write32(
                   kModelFactory, kModelFactoryVtable) &&
               memory.Write32(
                   kModelFactoryVtable + 0x08U,
                   kModelFactoryCreate) &&
               memory.Write32(kPlay + kObjectReadyOffset, 1U) &&
               memory.Write32(kModelLoadStats, 7U) &&
               memory.Write32(
                   kModelLoadStats + sizeof(uint32_t), 11U) &&
               memory.Write16(kActorUpdateRecord + 0x04U, 0x1122U) &&
               memory.Write16(kActorUpdateRecord + 0x0CU, 0x3344U),
           "seed owner fixture");
    SeedAttentionState(memory);

    if (withInitActor) {
        Expect(memory.Write8(
                   kActorContext + 0x08U,
                   static_cast<uint8_t>(kActorCategoryCount)),
               "seed actor-context total");
        for (uint32_t category = 0U;
             category < kActorCategoryCount; ++category) {
            const uint32_t actor = FixtureActorAddress(category);
            const uint32_t list = kActorContext + 0x0CU + category * 8U;
            Expect(memory.Write32(list, 1U) &&
                       memory.Write32(list + 4U, actor) &&
                       memory.Write8(
                           actor + 0x02U,
                           static_cast<uint8_t>(category)) &&
                       memory.Write8(actor + 0x1EU, 0U) &&
                       memory.Write32(actor + 0x24U, 0xFFFFFFFFU) &&
                       memory.Write32(
                           actor + 0x2CU,
                           std::bit_cast<uint32_t>(
                               category == 2U
                                   ? 0.0F
                                   : -26000.0F -
                                         static_cast<float>(category))) &&
                       memory.Write32(actor + 0x134U, kInitCallback),
                   "seed deferred-init actor category " +
                       std::to_string(category));
        }
    }
}

void SeedObjHanaScenario(
    Oot3dNativeGame::NativeA32Memory& memory,
    uint16_t selector, bool eventFlag) {
    constexpr uint32_t category = 6U;
    const uint32_t list =
        kActorContext + 0x0CU + category * 8U;
    Expect(
        selector < 3U &&
            memory.Write8(kActorContext + 0x08U, 2U) &&
            memory.Write32(list, 1U) &&
            memory.Write32(list + 4U, kObjHanaActor) &&
            memory.Write8(kObjHanaActor + 0x02U, category) &&
            memory.Write8(kObjHanaActor + 0x1EU, 0U) &&
            memory.Write16(kObjHanaActor + 0x1CU, selector) &&
            memory.Write32(kObjHanaActor + 0x04U, 1U) &&
            memory.Write32(kObjHanaActor + 0x24U, 0xFFFFFFFFU) &&
            memory.Write32(
                kObjHanaActor + 0x178U, kModelContext) &&
            memory.Write32(kObjHanaActor + 0x134U, kObjHanaInit) &&
            memory.Write32(
                kObjHanaActor + 0x28U,
                std::bit_cast<uint32_t>(11.0F)) &&
            memory.Write32(
                kObjHanaActor + 0x2CU,
                std::bit_cast<uint32_t>(22.0F)) &&
            memory.Write32(
                kObjHanaActor + 0x30U,
                std::bit_cast<uint32_t>(33.0F)) &&
            memory.Write16(
                kObjHanaSaveContext + 0xF4U,
                eventFlag ? 1U : 0U),
        "seed ObjHana deferred-init scenario");
}

struct ProcessFixture {
    ProcessFixture()
        : Process(oot3d::recomp::GetA32GeneratedRegistry(), Host),
          SourceRuntime(Process) {
    }

    OwnerHostServices Host;
    Oot3dNativeGame::NativeA32Process Process;
    Oot3dNativeGame::SourceActorUpdateAllRuntime SourceRuntime;
    NativeCallbackProbe Callback;
};

void ConfigureCallbacks(ProcessFixture& fixture) {
    fixture.Callback.Process = &fixture.Process;
    fixture.Process.SetNativeFunctionCallback(
        &HandleOwnerNativeFunction, &fixture.Callback,
        {kNativeFunctionPcs.begin(), kNativeFunctionPcs.end()});
}

void ValidateCallbackCheckpoints(ProcessFixture& fixture) {
    while (fixture.Callback.CallbackRoundTrips <
           fixture.Callback.CallbackCheckpoints.size()) {
        const auto& boundary =
            fixture.Callback.CallbackCheckpoints[
                fixture.Callback.CallbackRoundTrips];
        const nlohmann::json finalState = fixture.Process.CaptureState();
        std::string error;
        Expect(fixture.Process.RestoreState(boundary.ProcessState, &error),
               "restore callback boundary: " + error);
        Expect(GuestStatesEqual(
                   fixture.Process.PrimaryThreadState(), boundary.State),
               "callback boundary registers did not restore");
        Expect(fixture.Process.Memory().Write32(
                   boundary.State.r[0] + 0x1A0U, 0xDEADBEEFU),
               "mutate restored callback boundary");
        fixture.Process.PrimaryThreadState().r[0] ^= 0xFFFFFFFFU;
        Expect(fixture.Process.RestoreState(boundary.ProcessState, &error),
               "repeat callback boundary restore: " + error);
        Expect(GuestStatesEqual(
                   fixture.Process.PrimaryThreadState(), boundary.State),
               "callback boundary was not repeatably restorable");
        Expect(fixture.Process.RestoreState(finalState, &error),
               "resume final owner process state: " + error);
        ++fixture.Callback.CallbackRoundTrips;
    }
}

void CompareRange(const Oot3dNativeGame::NativeA32Memory& reference,
                  const Oot3dNativeGame::NativeA32Memory& source,
                  uint32_t address, size_t size, const char* name) {
    std::vector<uint8_t> referenceBytes(size);
    std::vector<uint8_t> sourceBytes(size);
    Expect(reference.ReadBytes(address, referenceBytes) &&
               source.ReadBytes(address, sourceBytes),
           std::string("read differential range ") + name);
    if (referenceBytes != sourceBytes) {
        size_t offset = 0U;
        while (offset < size &&
               referenceBytes[offset] == sourceBytes[offset]) {
            ++offset;
        }
        throw std::runtime_error(
            std::string(name) + " differs at guest address " +
            std::to_string(address + static_cast<uint32_t>(offset)) +
            " reference=" + std::to_string(referenceBytes[offset]) +
            " source=" + std::to_string(sourceBytes[offset]));
    }
}

void CompareOwnerMemory(const ProcessFixture& reference,
                        const ProcessFixture& source) {
    const auto& referenceMemory = reference.Process.Memory();
    const auto& sourceMemory = source.Process.Memory();
    CompareRange(referenceMemory, sourceMemory, 0x0050C000U, 0x1000U,
                 "owner globals 50c");
    CompareRange(referenceMemory, sourceMemory, 0x0051B000U, 0x1000U,
                 "owner globals 51b");
    CompareRange(referenceMemory, sourceMemory, 0x0054A000U, 0x1000U,
                 "owner globals 54a");
    CompareRange(referenceMemory, sourceMemory, 0x0055A000U, 0x3000U,
                 "owner globals 55a");
    CompareRange(referenceMemory, sourceMemory, 0x005AF000U, 0x3000U,
                 "owner globals 5af");
    CompareRange(referenceMemory, sourceMemory, 0x005BE000U, 0x1000U,
                 "owner globals 5be");
    CompareRange(referenceMemory, sourceMemory, 0x005C1000U, 0x1000U,
                 "owner globals 5c1");
    CompareRange(referenceMemory, sourceMemory, kPlay, 0x7000U, "play");
    CompareRange(referenceMemory, sourceMemory, kPlayer, 0x3000U,
                 "player");
    CompareRange(referenceMemory, sourceMemory, kActorContext, 0x1000U,
                 "actor context");
    CompareRange(referenceMemory, sourceMemory, kActor, kActorPoolSize,
                 "actor pool");
    CompareRange(referenceMemory, sourceMemory, kOverlay, 0x2000U,
                 "overlay resources");
}

void RunReferenceOwner(ProcessFixture& fixture) {
    auto state = fixture.Process.PrimaryThreadState();
    state.r[0] = kPlay;
    state.r[1] = kActorContext;
    std::string error;
    Expect(fixture.Process.InvokeFunctionWithState(
               kOwnerEntry, state, kOwnerReturn, &error),
           "original Actor_UpdateAll failed: " + error);
}

Oot3dNativeGame::SourceActorUpdateAllStats
RunSourceOwner(ProcessFixture& fixture) {
    auto state = fixture.Process.PrimaryThreadState();
    state.r[0] = kPlay;
    state.r[1] = kActorContext;
    state.r[14] = kOwnerReturn;
    oot3d::recomp::a32::ExecutionResult result;
    uint32_t blocksConsumed = 0U;
    fixture.SourceRuntime.ResetStats();
    Expect(fixture.SourceRuntime.Execute(
               kOwnerEntry, state, fixture.Process.Memory(),
               &result, &blocksConsumed),
           "source Actor_UpdateAll was not selected");
    Expect(result.kind == oot3d::recomp::a32::ExitKind::Branch &&
               result.pc == kOwnerReturn &&
               state.r[15] == kOwnerReturn &&
               blocksConsumed == 1U,
           "source Actor_UpdateAll did not return through guest LR: " +
               fixture.SourceRuntime.LastError());
    return fixture.SourceRuntime.Stats();
}

void RoundTripProcess(ProcessFixture& fixture) {
    const nlohmann::json checkpoint = fixture.Process.CaptureState();
    Expect(fixture.Process.Memory().Write32(
               kActorContext + 0x08U, 0xFFFFFFFFU) &&
               fixture.Process.Memory().Write32(
                   kActor + 0x1A0U, 0xFFFFFFFFU),
           "mutate owner checkpoint before restore");
    std::string error;
    Expect(fixture.Process.RestoreState(checkpoint, &error),
           "restore owner checkpoint: " + error);
}

void TestActorUpdateAllDifferential(bool withInitActor) {
    ProcessFixture reference;
    ProcessFixture source;
    ConfigureProcess(reference.Process, withInitActor);
    ConfigureProcess(source.Process, withInitActor);
    ConfigureCallbacks(reference);
    ConfigureCallbacks(source);

    uint32_t sourceTickCalls = 0U;
    uint32_t sourceCallbackCalls = 0U;
    uint32_t sourceInitCalls = 0U;
    uint32_t sourceUpdateCalls = 0U;
    for (uint32_t frame = 0U; frame < 3U; ++frame) {
        RunReferenceOwner(reference);
        ValidateCallbackCheckpoints(reference);
        const auto sourceBridge = RunSourceOwner(source);
        ValidateCallbackCheckpoints(source);
        sourceTickCalls +=
            static_cast<uint32_t>(sourceBridge.SvcCalls);
        sourceCallbackCalls += sourceBridge.CallbackCalls;
        sourceInitCalls += sourceBridge.InitCallbackCalls;
        sourceUpdateCalls += sourceBridge.UpdateCallbackCalls;
        Expect(source.SourceRuntime.LastError().empty(),
               "source owner callback bridge failed: " +
                   source.SourceRuntime.LastError());
        CompareOwnerMemory(reference, source);
        if (frame == 0U) {
            RoundTripProcess(reference);
            RoundTripProcess(source);
            CompareOwnerMemory(reference, source);
        }
    }

    const uint32_t expectedTicks =
        withInitActor ? 3U + kActorCategoryCount : 3U;
    Expect(reference.Host.SystemTickCalls == expectedTicks &&
               sourceTickCalls == expectedTicks,
           "source and original system-tick counts differ");
    const uint32_t expectedCallbacks =
        withInitActor ? 3U * kActorCategoryCount : 0U;
    std::vector<uint32_t> expectedCallbackActors;
    if (withInitActor) {
        for (uint32_t frame = 0U; frame < 3U; ++frame) {
            for (uint32_t category = 0U;
                 category < kActorCategoryCount; ++category) {
                expectedCallbackActors.push_back(
                    FixtureActorAddress(category));
            }
        }
    }
    Expect(reference.Callback.CallbackCalls == expectedCallbacks &&
               source.Callback.CallbackCalls == expectedCallbacks &&
               sourceCallbackCalls == expectedCallbacks &&
               reference.Callback.InitCallbackCalls ==
                   (withInitActor ? kActorCategoryCount : 0U) &&
               source.Callback.InitCallbackCalls ==
                   (withInitActor ? kActorCategoryCount : 0U) &&
               sourceInitCalls ==
                   (withInitActor ? kActorCategoryCount : 0U) &&
               reference.Callback.UpdateCallbackCalls ==
                   (withInitActor ? 2U * kActorCategoryCount : 0U) &&
               source.Callback.UpdateCallbackCalls ==
                   (withInitActor ? 2U * kActorCategoryCount : 0U) &&
               sourceUpdateCalls ==
                   (withInitActor ? 2U * kActorCategoryCount : 0U) &&
               reference.Callback.CallbackRoundTrips ==
                   (withInitActor ? 2U : 0U) &&
               source.Callback.CallbackRoundTrips ==
                   (withInitActor ? 2U : 0U) &&
               reference.Callback.CallbackActors ==
                   expectedCallbackActors &&
               source.Callback.CallbackActors ==
                   expectedCallbackActors &&
               reference.Callback.CallbackArgumentsMatched &&
               source.Callback.CallbackArgumentsMatched &&
               reference.Callback.Error.empty() &&
               source.Callback.Error.empty(),
           "source and original deferred-init callbacks differ");
}

enum class LifecycleScenario : uint8_t {
    KillNonresident,
    DestroyDrawn,
    DeleteAndRelease,
};

void SeedLifecycleScenario(Oot3dNativeGame::NativeA32Memory& memory,
                           LifecycleScenario scenario) {
    const uint32_t category =
        scenario == LifecycleScenario::DeleteAndRelease ? 5U : 3U;
    const uint32_t list = kActorContext + 0x0CU + category * 8U;
    Expect(memory.Write8(kActorContext + 0x08U, 2U) &&
               memory.Write32(list, 1U) &&
               memory.Write32(list + 4U, kActor) &&
               memory.Write8(
                   kActor + 0x02U, static_cast<uint8_t>(category)) &&
               memory.Write32(kActor + 0x04U, 1U),
           "seed lifecycle actor");

    switch (scenario) {
    case LifecycleScenario::KillNonresident:
        Expect(memory.Write8(kActor + 0x1EU, 0xFFU) &&
                   memory.Write32(kActor + 0x13CU, kInitCallback) &&
                   memory.Write32(kActor + 0x140U, kInitCallback),
               "seed nonresident actor");
        break;
    case LifecycleScenario::DestroyDrawn:
        Expect(memory.Write8(kActor + 0x1EU, 0U) &&
                   memory.Write8(kActor + 0x121U, 1U),
               "seed drawn destroyed actor");
        break;
    case LifecycleScenario::DeleteAndRelease:
        Expect(memory.Write8(kActor + 0x03U, 0U) &&
                   memory.Write8(kActor + 0x1EU, 0U) &&
                   memory.Write32(kActor + 0x144U, kOverlay) &&
                   memory.Write32(kOverlay + 0x08U, 1U) &&
                   memory.Write32(kOverlay + 0x10U, kOverlayResource) &&
                   memory.Write16(kOverlay + 0x1CU, 0U) &&
                   memory.Write8(kOverlay + 0x1EU, 1U),
               "seed deleted actor and overlay");
        break;
    }
}

void TestActorLifecycleDifferential(LifecycleScenario scenario) {
    ProcessFixture reference;
    ProcessFixture source;
    ConfigureProcess(reference.Process, false);
    ConfigureProcess(source.Process, false);
    ConfigureCallbacks(reference);
    ConfigureCallbacks(source);
    SeedLifecycleScenario(reference.Process.Memory(), scenario);
    SeedLifecycleScenario(source.Process.Memory(), scenario);

    RunReferenceOwner(reference);
    const auto sourceBridge = RunSourceOwner(source);
    CompareOwnerMemory(reference, source);
    Expect(source.SourceRuntime.LastError().empty(),
           "source lifecycle callback bridge failed: " +
               source.SourceRuntime.LastError());

    const uint32_t expectedDestroy =
        scenario == LifecycleScenario::KillNonresident ? 0U : 1U;
    const uint32_t expectedFree =
        scenario == LifecycleScenario::DeleteAndRelease ? 2U : 0U;
    const uint32_t expectedRegistryRelease =
        scenario == LifecycleScenario::DeleteAndRelease ? 1U : 0U;
    Expect(reference.Callback.DestroyCalls == expectedDestroy &&
               sourceBridge.DestroyCalls == expectedDestroy &&
               reference.Callback.FreeCalls == expectedFree &&
               sourceBridge.FreeCalls == expectedFree &&
               reference.Callback.RegistryReleaseCalls ==
                   expectedRegistryRelease &&
               sourceBridge.RegistryReleaseScans ==
                   expectedRegistryRelease,
           "source and original lifecycle service calls differ");
}

void TestDependencyFailureIsControlled() {
    constexpr uint32_t failingDependency = 0x0036A7A0U;
    ProcessFixture fixture;
    ConfigureProcess(fixture.Process, false);
    ConfigureCallbacks(fixture);
    fixture.Callback.ForcedFailurePc = failingDependency;

    auto state = fixture.Process.PrimaryThreadState();
    state.r[0] = kPlay;
    state.r[1] = kActorContext;
    state.r[14] = kOwnerReturn;
    oot3d::recomp::a32::ExecutionResult result;
    uint32_t blocksConsumed = 0U;
    Expect(fixture.SourceRuntime.Execute(
               kOwnerEntry, state, fixture.Process.Memory(),
               &result, &blocksConsumed),
           "source Actor_UpdateAll failure route was not selected");
    Expect(result.kind == oot3d::recomp::a32::ExitKind::Unsupported &&
               result.detail == failingDependency &&
               blocksConsumed == 1U,
           "dependency failure did not produce a controlled source-owner "
           "exit");
    Expect(fixture.SourceRuntime.Stats().Failures == 1U &&
               fixture.SourceRuntime.LastError().find(
                   std::to_string(failingDependency)) !=
                   std::string::npos,
           "dependency failure diagnostics were not retained");
}

void TestObjHanaInitCallbackDifferential(
    uint16_t selector, bool eventFlag) {
    ProcessFixture reference;
    ProcessFixture source;
    ConfigureProcess(reference.Process, false);
    ConfigureProcess(source.Process, false);
    ConfigureCallbacks(reference);
    ConfigureCallbacks(source);
    SeedObjHanaScenario(
        reference.Process.Memory(), selector, eventFlag);
    SeedObjHanaScenario(
        source.Process.Memory(), selector, eventFlag);

    RunReferenceOwner(reference);
    const auto sourceBridge = RunSourceOwner(source);
    CompareOwnerMemory(reference, source);
    CompareRange(
        reference.Process.Memory(), source.Process.Memory(),
        0x00535000U, 0x1000U, "ObjHana static data");
    CompareRange(
        reference.Process.Memory(), source.Process.Memory(),
        0x00588000U, 0x1000U, "ObjHana save context");

    const uint32_t expectedColliderCalls = selector == 0U ? 0U : 1U;
    const uint32_t expectedKillCalls =
        selector == 2U && eventFlag ? 1U : 0U;
    const auto validateSharedProbe =
        [&](const NativeCallbackProbe& probe, const char* name) {
            Expect(
                probe.ObjHanaAbiMatched &&
                    probe.ObjHanaInitChainCalls == 1U &&
                    probe.ObjHanaScaleCalls == 1U &&
                    probe.ObjHanaColliderInitCalls ==
                        expectedColliderCalls &&
                    probe.ObjHanaColliderSetCalls ==
                        expectedColliderCalls &&
                    probe.ObjHanaColliderUpdateCalls ==
                        expectedColliderCalls &&
                    probe.ObjHanaCollisionInfoCalls ==
                        expectedColliderCalls &&
                    probe.ObjHanaKillCalls == expectedKillCalls,
                std::string(name) +
                    " ObjHana dependency trace differs");
        };
    validateSharedProbe(reference.Callback, "reference");
    validateSharedProbe(source.Callback, "source");
    Expect(
        reference.Callback.ObjHanaModelLoadCalls == 1U &&
            reference.Callback.ObjHanaZarCalls == 0U &&
            reference.Callback.ObjHanaFactoryCalls == 0U,
        "reference ObjHana model loader did not use the A32 boundary");
    Expect(
        source.Callback.ObjHanaModelLoadCalls == 0U &&
            source.Callback.ObjHanaZarCalls == 1U &&
            source.Callback.ObjHanaFactoryCalls == 1U,
        "source ObjHana model loader did not use ZAR and factory "
        "boundaries");
    Expect(
        sourceBridge.SourceCallbackCalls == 1U &&
            sourceBridge.SourceObjHanaInitCalls == 1U &&
            sourceBridge.SourceCallbackFailures == 0U &&
            source.SourceRuntime.LastError().empty(),
        "ObjHana source callback was not completed cleanly");
}

void TestObjHanaDependencyFailureIsControlled() {
    ProcessFixture fixture;
    ConfigureProcess(fixture.Process, false);
    ConfigureCallbacks(fixture);
    SeedObjHanaScenario(fixture.Process.Memory(), 1U, false);
    fixture.Callback.ForcedFailurePc = kActorSetScale;

    auto state = fixture.Process.PrimaryThreadState();
    state.r[0] = kPlay;
    state.r[1] = kActorContext;
    state.r[14] = kOwnerReturn;
    oot3d::recomp::a32::ExecutionResult result;
    uint32_t blocksConsumed = 0U;
    Expect(
        fixture.SourceRuntime.Execute(
            kOwnerEntry, state, fixture.Process.Memory(),
            &result, &blocksConsumed),
        "ObjHana source callback failure route was not selected");
    const auto stats = fixture.SourceRuntime.Stats();
    Expect(
        result.kind == oot3d::recomp::a32::ExitKind::Unsupported &&
            result.detail == kActorSetScale &&
            blocksConsumed == 1U &&
            stats.Failures == 1U &&
            stats.SourceCallbackFailures == 1U &&
            stats.SourceCallbackCalls == 0U &&
            fixture.SourceRuntime.LastError().find(
                std::to_string(kActorSetScale)) !=
                std::string::npos,
        "ObjHana dependency failure did not remain controlled: kind=" +
            std::to_string(static_cast<uint32_t>(result.kind)) +
            " detail=" + std::to_string(result.detail) +
            " blocks=" + std::to_string(blocksConsumed) +
            " owner_failures=" + std::to_string(stats.Failures) +
            " callback_failures=" +
            std::to_string(stats.SourceCallbackFailures) +
            " callback_calls=" +
            std::to_string(stats.SourceCallbackCalls) +
            " error=" + fixture.SourceRuntime.LastError());
}

} // namespace

int main() {
    try {
        TestActorUpdateAllDifferential(false);
        TestActorUpdateAllDifferential(true);
        TestActorLifecycleDifferential(
            LifecycleScenario::KillNonresident);
        TestActorLifecycleDifferential(
            LifecycleScenario::DestroyDrawn);
        TestActorLifecycleDifferential(
            LifecycleScenario::DeleteAndRelease);
        TestDependencyFailureIsControlled();
        TestObjHanaInitCallbackDifferential(0U, false);
        TestObjHanaInitCallbackDifferential(1U, false);
        TestObjHanaInitCallbackDifferential(2U, true);
        TestObjHanaDependencyFailureIsControlled();
        std::cout << "oot3d_source_actor_update_all_tests: ok\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_source_actor_update_all_tests: "
                  << exception.what() << '\n';
        return 1;
    }
}
