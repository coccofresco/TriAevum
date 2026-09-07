#include "oot3d_source_actor_update_all_runtime.h"

#include "oot3d_native_a32_process.h"
#include "oot3d_native_owner_call_adapter.h"
#include "oot3d_source_actor_callback_runtime.h"

extern "C" {
#include "oot3d/audio.h"
#include "oot3d/game_math.h"
#include "oot3d/gameplay_leaf.h"
#include "oot3d/model_render.h"
#include "oot3d/owner_actor_runtime.h"
#include "oot3d/owner_camera_mode_runtime.h"
#include "oot3d/owner_certified_player_runtime.h"
#include "oot3d/owner_closure.h"
#include "oot3d/owner_projection_runtime.h"
#include "oot3d/runtime_helpers.h"
#include "oot3d/runtime_mass_recovery.h"
#include "oot3d/runtime_structural_families.h"
#include "oot3d/static_init.h"
}

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kInitContinuation = 0x004615E8U;
constexpr uint32_t kUpdateContinuation = 0x004617B4U;
constexpr uint32_t kOwnerGuardLiteral = 0x004617D8U;
constexpr uint32_t kOwnerRendererStateLiteral = 0x004617DCU;
constexpr uint32_t kGuestServiceReturn = 0x0BAD0000U;
constexpr uint32_t kOwnerFrameSize = 0x68U;
constexpr uint32_t kSmallScratchFrameSize = 0x20U;
constexpr uint32_t kCollisionScratchFrameSize = 0x40U;

constexpr uint32_t kActorDestroy = 0x002D644CU;
constexpr uint32_t kRegistryReset = 0x0030CB90U;
constexpr uint32_t kCameraChangeMode = 0x00332284U;
constexpr uint32_t kPlayerReleaseFocus = 0x00334354U;
constexpr uint32_t kZeldaArenaFree = 0x00350EF4U;
constexpr uint32_t kCollisionPolyAcceptsSlot = 0x0035FE90U;
constexpr uint32_t kActorGetScreenPos = 0x00363A20U;
constexpr uint32_t kRendererGlobalStateInit = 0x0036788CU;
constexpr uint32_t kStaticInitGuardAcquire = 0x003679B4U;
constexpr uint32_t kProjectWorldPosition = 0x00368CC0U;
constexpr uint32_t kPlayerInCsMode = 0x0036A7A0U;
constexpr uint32_t kGameplayGetCamera = 0x0036C5BCU;
constexpr uint32_t kMathStepToF = 0x003705A0U;
constexpr uint32_t kCollisionLineTest = 0x003723C0U;
constexpr uint32_t kObjectSlotPositive = 0x00373074U;
constexpr uint32_t kActorKill = 0x00374428U;
constexpr uint32_t kAudioPlaySoundGeneral = 0x0037547CU;
constexpr uint32_t kMathAtan2S = 0x003758B0U;
constexpr uint32_t kActorSpawnSceneEntries = 0x00452240U;
constexpr uint32_t kCopyDynaInteraction = 0x00477E44U;
constexpr uint32_t kPauseUiUpdateDualAlpha = 0x0047955CU;
constexpr uint32_t kDynaResetActorFlags = 0x0047AF24U;
constexpr uint32_t kActorUpdateDefaults = 0x0047C938U;
constexpr uint32_t kActorUpdateHalfwords = 0x0047CCDCU;

thread_local SourceActorUpdateAllBridge* gActiveBridge = nullptr;

class ActiveBridgeScope {
  public:
    explicit ActiveBridgeScope(SourceActorUpdateAllBridge& bridge) noexcept
        : mPrevious(gActiveBridge) {
        gActiveBridge = &bridge;
    }

    ActiveBridgeScope(const ActiveBridgeScope&) = delete;
    ActiveBridgeScope& operator=(const ActiveBridgeScope&) = delete;

    ~ActiveBridgeScope() {
        gActiveBridge = mPrevious;
    }

  private:
    SourceActorUpdateAllBridge* mPrevious = nullptr;
};

class SourceActorUpdateAllAbort final : public std::exception {
  public:
    const char* what() const noexcept override {
        return "source Actor_UpdateAll bridge aborted";
    }
};

} // namespace

class SourceActorUpdateAllBridge final
    : public SourceActorCallbackHost {
  public:
    explicit SourceActorUpdateAllBridge(NativeA32Process& process)
        : Process(process), Adapter(process) {
    }

    bool Execute(uint32_t pc, oot3d::recomp::a32::GuestState& state,
                 oot3d::recomp::a32::MemoryBus& memory,
                 oot3d::recomp::a32::ExecutionResult* result,
                 uint32_t* blocksConsumed) {
        if (pc != kSourceActorUpdateAllEntry) {
            return false;
        }
        if (result == nullptr || &memory != &Process.Memory()) {
            return false;
        }
        const uint32_t playAddress = state.r[0];
        const uint32_t actorContextAddress = state.r[1];
        if ((state.r[13] & 7U) != 0U ||
            !Process.Memory().IsWritable(playAddress, 0x5C78U) ||
            !Process.Memory().IsWritable(
                actorContextAddress, sizeof(Oot3dActorContext))) {
            return false;
        }

        ++RuntimeStats.OwnerCalls;
        Failed = false;
        FailureDetail = 0U;
        Error.clear();
        CallerState = state;
        CallbackAddress = 0U;
        GuardAddress = 0U;
        RendererStateAddress = 0U;

        std::string adapterError;
        auto execution = Adapter.BeginExecution(
            kSourceActorUpdateAllEntry, state, &adapterError);
        if (!execution) {
            Fail("begin source owner: " + adapterError);
        }
        if (!Failed &&
            (!Process.Memory().ReadFast(
                 kOwnerGuardLiteral, &GuardAddress) ||
             !Process.Memory().ReadFast(
                 kOwnerRendererStateLiteral, &RendererStateAddress) ||
             !Process.Memory().ReadFast(
                 GuardAddress, &gOot3dRendererInitGuard))) {
            Fail("resolve source owner renderer globals");
        }

        if (!Failed) {
            auto* play = reinterpret_cast<Oot3dPlayState*>(
                Adapter.ResolveWrite(playAddress, 0x5C78U));
            auto* actorContext = reinterpret_cast<Oot3dActorContext*>(
                Adapter.ResolveWrite(
                    actorContextAddress, sizeof(Oot3dActorContext)));
            if (play == nullptr || actorContext == nullptr) {
                Fail("resolve source owner arguments");
            } else {
                ActiveBridgeScope bridgeScope(*this);
                try {
                    FUN_00461344(play, actorContext);
                } catch (const std::exception& exception) {
                    Fail(std::string("source owner exception: ") +
                         exception.what());
                } catch (...) {
                    Fail("source owner unknown exception");
                }
            }
        }
        execution.Reset();

        if (Failed) {
            ++RuntimeStats.Failures;
            *result = {
                oot3d::recomp::a32::ExitKind::Unsupported,
                pc,
                oot3d::recomp::a32::FallbackReason::Unsupported,
                FailureDetail != 0U ? FailureDetail
                                    : kSourceActorUpdateAllEntry,
            };
            if (blocksConsumed != nullptr) {
                *blocksConsumed = 1U;
            }
            return true;
        }

        state.r[15] = state.r[14];
        *result = {
            oot3d::recomp::a32::ExitKind::Branch,
            state.r[15],
            oot3d::recomp::a32::FallbackReason::None,
            pc,
        };
        if (blocksConsumed != nullptr) {
            *blocksConsumed = 1U;
        }
        return true;
    }

    void* Resolve(uint32_t address, size_t size = 1U) {
        if (Failed) {
            throw SourceActorUpdateAllAbort{};
        }
        if (address == 0U || size == 0U) {
            Fail("guest pointer resolution received an invalid range",
                 address);
            throw SourceActorUpdateAllAbort{};
        }
        auto* pointer = Adapter.ResolveWrite(address, size);
        if (pointer == nullptr) {
            Fail("guest pointer resolution failed at " +
                     std::to_string(address),
                 address);
            throw SourceActorUpdateAllAbort{};
        }
        return pointer;
    }

    const uint8_t* ResolveGuestRead(
        uint32_t address, size_t size,
        std::string* error) override {
        if (Failed || address == 0U || size == 0U) {
            if (error != nullptr) {
                *error = "invalid guest read range";
            }
            return nullptr;
        }
        const auto* pointer = Adapter.ResolveRead(address, size);
        if (pointer == nullptr && error != nullptr) {
            *error = "guest read range is not mapped";
        }
        return pointer;
    }

    uint8_t* ResolveGuestWrite(
        uint32_t address, size_t size,
        std::string* error) override {
        if (Failed || address == 0U || size == 0U) {
            if (error != nullptr) {
                *error = "invalid guest write range";
            }
            return nullptr;
        }
        auto* pointer = Adapter.ResolveWrite(address, size);
        if (pointer == nullptr && error != nullptr) {
            *error = "guest write range is not mapped";
        }
        return pointer;
    }

    uint32_t Encode(const void* pointer, size_t size = 1U) {
        if (pointer == nullptr) {
            return 0U;
        }
        const auto address = Adapter.ResolveGuestAddress(pointer, size);
        if (!address.has_value()) {
            Fail("host pointer is outside guest memory");
            return 0U;
        }
        return *address;
    }

    bool Invoke(uint32_t entryAddress, std::span<const uint32_t> core,
                std::span<const uint32_t> vfp,
                std::span<const uint32_t> stack, uint32_t frameSize,
                uint32_t returnAddress,
                NativeA32OwnerGuestCallResult* result) {
        if (Failed) {
            return false;
        }
        NativeA32OwnerGuestCall call;
        call.EntryAddress = entryAddress;
        call.ReturnAddress = returnAddress;
        call.CallerFrameSize = frameSize;
        call.CoreArgumentCount =
            std::min(core.size(), call.CoreArguments.size());
        call.VfpArgumentCount =
            std::min(vfp.size(), call.VfpArguments.size());
        if (call.CoreArgumentCount != core.size() ||
            call.VfpArgumentCount != vfp.size()) {
            Fail("source owner service argument overflow");
            return false;
        }
        std::copy(core.begin(), core.end(), call.CoreArguments.begin());
        std::copy(vfp.begin(), vfp.end(), call.VfpArguments.begin());
        call.StackArguments = stack;

        std::string invokeError;
        if (!Adapter.Invoke(call, result, &invokeError)) {
            Fail("guest service " + std::to_string(entryAddress) +
                     " failed: " + invokeError,
                 entryAddress);
            return false;
        }
        ++RuntimeStats.NestedGuestCalls;
        return true;
    }

    bool InvokeGuest(
        uint32_t entryAddress, std::span<const uint32_t> core,
        std::span<const uint32_t> vfp,
        std::span<const uint32_t> stack, uint32_t frameSize,
        uint32_t* coreResult, std::string* error) override {
        NativeA32OwnerGuestCallResult result;
        if (!Invoke(
                entryAddress, core, vfp, stack, frameSize,
                kGuestServiceReturn, &result)) {
            if (error != nullptr) {
                *error = Error;
            }
            return false;
        }
        if (coreResult != nullptr) {
            *coreResult = result.State.r[0];
        }
        return true;
    }

    uint32_t InvokeCore(
        uint32_t entryAddress,
        std::initializer_list<uint32_t> coreArguments) {
        const std::span<const uint32_t> core{
            coreArguments.begin(), coreArguments.size()};
        NativeA32OwnerGuestCallResult result;
        if (!Invoke(entryAddress, core, {}, {}, 0U,
                    kGuestServiceReturn, &result)) {
            return 0U;
        }
        return result.State.r[0];
    }

    uint64_t SystemTick() {
        if (Failed) {
            return 0U;
        }
        NativeA32OwnerGuestCallResult result;
        std::string invokeError;
        if (!Adapter.InvokeSvc(0x28U, &result, &invokeError)) {
            Fail("source owner SVC 0x28 failed: " + invokeError);
            return 0U;
        }
        ++RuntimeStats.SvcCalls;
        return (static_cast<uint64_t>(result.State.r[1]) << 32U) |
               result.State.r[0];
    }

    uint32_t ScratchAddress(uint32_t frameSize, uint32_t offset,
                            size_t size) {
        if (frameSize > CallerState.r[13] ||
            offset > frameSize ||
            size > static_cast<size_t>(frameSize - offset)) {
            Fail("source owner scratch frame overflow");
            return 0U;
        }
        const uint32_t address =
            CallerState.r[13] - frameSize + offset;
        if (Adapter.ResolveWrite(address, size) == nullptr) {
            Fail("source owner scratch memory is not writable");
            return 0U;
        }
        return address;
    }

    void DispatchCallback(void* actorPointer, void* playPointer) {
        const uint32_t actorAddress = Encode(actorPointer);
        const uint32_t playAddress = Encode(playPointer);
        if (Failed || CallbackAddress == 0U) {
            if (!Failed) {
                Fail("source owner callback address is zero");
            }
            return;
        }
        auto* actor = static_cast<Oot3dActor*>(actorPointer);
        const bool isInit = actor->init != 0U;
        const SourceActorCallbackInvocation sourceInvocation{
            CallbackAddress, actorAddress, playAddress,
            kOwnerFrameSize};
        const auto sourceResult =
            SourceCallbacks.Dispatch(sourceInvocation, *this);
        if (sourceResult !=
            SourceActorCallbackDispatchResult::NotHandled) {
            ++RuntimeStats.CallbackCalls;
            if (isInit) {
                ++RuntimeStats.InitCallbackCalls;
            } else {
                ++RuntimeStats.UpdateCallbackCalls;
            }
            if (sourceResult ==
                SourceActorCallbackDispatchResult::Completed) {
                ++RuntimeStats.SourceCallbackCalls;
                if (CallbackAddress ==
                    kSourceObjHanaInitCallbackEntry) {
                    ++RuntimeStats.SourceObjHanaInitCalls;
                }
            } else {
                ++RuntimeStats.SourceCallbackFailures;
                Fail(
                    "source actor callback " +
                        std::to_string(CallbackAddress) +
                        " failed: " +
                        SourceCallbacks.LastError(),
                    CallbackAddress);
            }
            return;
        }
        const std::array<uint32_t, 2> core{
            actorAddress, playAddress};
        NativeA32OwnerGuestCallResult result;
        if (!Invoke(
                CallbackAddress, core, {}, {}, kOwnerFrameSize,
                isInit ? kInitContinuation : kUpdateContinuation,
                &result)) {
            return;
        }
        ++RuntimeStats.CallbackCalls;
        if (isInit) {
            ++RuntimeStats.InitCallbackCalls;
        } else {
            ++RuntimeStats.UpdateCallbackCalls;
        }
    }

    void DispatchRegistryReset(void* record, uint32_t value) {
        InvokeCore(kRegistryReset, {Encode(record), value});
    }

    void Fail(std::string message, uint32_t detail = 0U) {
        if (!Failed) {
            Failed = true;
            FailureDetail = detail;
            Error = std::move(message);
        }
    }

    NativeA32Process& Process;
    NativeA32OwnerCallAdapter Adapter;
    SourceActorCallbackRuntime SourceCallbacks;
    SourceActorUpdateAllStats RuntimeStats;
    oot3d::recomp::a32::GuestState CallerState{};
    uint32_t CallbackAddress = 0U;
    uint32_t GuardAddress = 0U;
    uint32_t RendererStateAddress = 0U;
    uint32_t FailureDetail = 0U;
    bool Failed = false;
    std::string Error;
};

namespace {

SourceActorUpdateAllBridge* ActiveBridge() noexcept {
    return gActiveBridge;
}

uint32_t GuestAddress(const void* pointer, size_t size = 1U) {
    auto* bridge = ActiveBridge();
    return bridge != nullptr ? bridge->Encode(pointer, size) : 0U;
}

void* GuestPointer(uint32_t address, size_t size = 1U) {
    auto* bridge = ActiveBridge();
    return bridge != nullptr ? bridge->Resolve(address, size) : nullptr;
}

uint32_t ReadResult(
    uint32_t entryAddress, std::span<const uint32_t> core = {},
    std::span<const uint32_t> vfp = {},
    std::span<const uint32_t> stack = {}, uint32_t frameSize = 0U) {
    auto* bridge = ActiveBridge();
    if (bridge == nullptr) {
        return 0U;
    }
    NativeA32OwnerGuestCallResult result;
    if (!bridge->Invoke(
            entryAddress, core, vfp, stack, frameSize,
            kGuestServiceReturn, &result)) {
        return 0U;
    }
    return result.State.r[0];
}

void SourceActorCallback(Oot3dActor* actor, Oot3dPlayState* play) {
    if (auto* bridge = ActiveBridge(); bridge != nullptr) {
        bridge->DispatchCallback(actor, play);
    }
}

void SourceRegistryReset(void* record, uint32_t value) {
    if (auto* bridge = ActiveBridge(); bridge != nullptr) {
        bridge->DispatchRegistryReset(record, value);
    }
}

} // namespace

SourceActorUpdateAllRuntime::SourceActorUpdateAllRuntime(
    NativeA32Process& process)
    : mBridge(std::make_unique<SourceActorUpdateAllBridge>(process)) {
}

SourceActorUpdateAllRuntime::~SourceActorUpdateAllRuntime() = default;

bool SourceActorUpdateAllRuntime::Execute(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t* blocksConsumed) {
    return mBridge->Execute(pc, state, memory, result, blocksConsumed);
}

SourceActorUpdateAllStats
SourceActorUpdateAllRuntime::Stats() const noexcept {
    return mBridge->RuntimeStats;
}

void SourceActorUpdateAllRuntime::ResetStats() noexcept {
    mBridge->RuntimeStats = {};
    mBridge->SourceCallbacks.ResetStats();
}

const std::string& SourceActorUpdateAllRuntime::LastError() const noexcept {
    return mBridge->Error;
}

} // namespace Oot3dNativeGame

extern "C" {

uint32_t gOot3dRendererInitGuard = 0U;
uint8_t gOot3dRendererGlobalState[1] = {};

void* oot3d_host_owner_actor_global_resolve(uint32_t address) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge == nullptr) {
        return nullptr;
    }
    if (address == 0x0054ABB4U) {
        ++bridge->RuntimeStats.RegistryReleaseScans;
    }
    return bridge->Resolve(address);
}

uint64_t oot3d_host_owner_actor_system_tick() {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    return bridge != nullptr ? bridge->SystemTick() : 0U;
}

void* oot3d_host_actor_spawn_ptr32_resolve(uint32_t address) {
    return address == 0U ? nullptr
                         : Oot3dNativeGame::GuestPointer(address);
}

uint32_t oot3d_host_actor_spawn_ptr32_encode(const void* pointer) {
    return pointer == nullptr ? 0U
                              : Oot3dNativeGame::GuestAddress(pointer);
}

Oot3dActorCallback
oot3d_host_actor_spawn_callback_resolve(uint32_t address) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge == nullptr) {
        return nullptr;
    }
    bridge->CallbackAddress = address;
    return &Oot3dNativeGame::SourceActorCallback;
}

Oot3dOwnerRegistryResetCallback
oot3d_host_owner_actor_registry_callback_resolve() {
    return &Oot3dNativeGame::SourceRegistryReset;
}

void Actor_Destroy(Oot3dActor* actor, Oot3dPlayState* play) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge == nullptr) {
        return;
    }
    ++bridge->RuntimeStats.DestroyCalls;
    bridge->InvokeCore(
        Oot3dNativeGame::kActorDestroy,
        {bridge->Encode(actor), bridge->Encode(play)});
}

void Actor_Kill(Oot3dActor* actor) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge != nullptr) {
        bridge->InvokeCore(
            Oot3dNativeGame::kActorKill, {bridge->Encode(actor)});
    }
}

void Actor_SpawnSceneEntries(Oot3dPlayState* play) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge != nullptr) {
        bridge->InvokeCore(
            Oot3dNativeGame::kActorSpawnSceneEntries,
            {bridge->Encode(play)});
    }
}

void ZeldaArena_Free(void* pointer) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge == nullptr) {
        return;
    }
    ++bridge->RuntimeStats.FreeCalls;
    bridge->InvokeCore(
        Oot3dNativeGame::kZeldaArenaFree,
        {bridge->Encode(pointer)});
}

void FUN_00334354(Oot3dPlayer* player) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge != nullptr) {
        bridge->InvokeCore(
            Oot3dNativeGame::kPlayerReleaseFocus,
            {bridge->Encode(player)});
    }
}

void FUN_00477e44(Oot3dPlayState* play, void* dynaContext) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge != nullptr) {
        bridge->InvokeCore(
            Oot3dNativeGame::kCopyDynaInteraction,
            {bridge->Encode(play), bridge->Encode(dynaContext)});
    }
}

void FUN_0047955c(
    uint32_t unused, Oot3dPauseUiDualAlphaState* state) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge != nullptr) {
        bridge->InvokeCore(
            Oot3dNativeGame::kPauseUiUpdateDualAlpha,
            {unused, bridge->Encode(state)});
    }
}

void FUN_0047af24(
    Oot3dPlayState* play, void* dynaContext, Oot3dActor* actor) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge != nullptr) {
        bridge->InvokeCore(
            Oot3dNativeGame::kDynaResetActorFlags,
            {bridge->Encode(play), bridge->Encode(dynaContext),
             bridge->Encode(actor)});
    }
}

void FUN_0047c938(Oot3dActorUpdateDefaultsRecord* record) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge != nullptr) {
        bridge->InvokeCore(
            Oot3dNativeGame::kActorUpdateDefaults,
            {bridge->Encode(record)});
    }
}

void FUN_0047ccdc(Oot3dActorUpdateHalfwordRecord* record) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge != nullptr) {
        bridge->InvokeCore(
            Oot3dNativeGame::kActorUpdateHalfwords,
            {bridge->Encode(record)});
    }
}

uint32_t Camera_ChangeMode(void* camera, uint32_t mode) {
    const std::array<uint32_t, 2> core{
        Oot3dNativeGame::GuestAddress(camera), mode};
    return Oot3dNativeGame::ReadResult(
        Oot3dNativeGame::kCameraChangeMode, core);
}

void* Gameplay_GetCamera(Oot3dPlayState* play, int32_t cameraId) {
    const std::array<uint32_t, 2> core{
        Oot3dNativeGame::GuestAddress(play),
        static_cast<uint32_t>(cameraId)};
    const uint32_t result = Oot3dNativeGame::ReadResult(
        Oot3dNativeGame::kGameplayGetCamera, core);
    return result == 0U ? nullptr
                        : Oot3dNativeGame::GuestPointer(result);
}

void Actor_GetScreenPos(
    const void* play, const void* actor, int16_t* x, int16_t* y) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge == nullptr || x == nullptr || y == nullptr) {
        return;
    }
    const uint32_t scratch = bridge->ScratchAddress(
        Oot3dNativeGame::kSmallScratchFrameSize, 0U, 4U);
    const std::array<uint32_t, 4> core{
        bridge->Encode(play), bridge->Encode(actor),
        scratch, scratch + 2U};
    Oot3dNativeGame::ReadResult(
        Oot3dNativeGame::kActorGetScreenPos, core, {}, {},
        Oot3dNativeGame::kSmallScratchFrameSize);
    uint16_t rawX = 0U;
    uint16_t rawY = 0U;
    if (!bridge->Process.Memory().ReadFast(scratch, &rawX) ||
        !bridge->Process.Memory().ReadFast(scratch + 2U, &rawY)) {
        bridge->Fail("read Actor_GetScreenPos scratch result");
        return;
    }
    *x = static_cast<int16_t>(rawX);
    *y = static_cast<int16_t>(rawY);
}

void FUN_00368cc0(
    const void* play, const Oot3dActorVec3f* world,
    Oot3dActorVec3f* projected, float* inverseW) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge == nullptr || projected == nullptr || inverseW == nullptr) {
        return;
    }
    const uint32_t scratch = bridge->ScratchAddress(
        Oot3dNativeGame::kSmallScratchFrameSize, 0U, 16U);
    const std::array<uint32_t, 4> core{
        bridge->Encode(play), bridge->Encode(world),
        scratch, scratch + 12U};
    Oot3dNativeGame::ReadResult(
        Oot3dNativeGame::kProjectWorldPosition, core, {}, {},
        Oot3dNativeGame::kSmallScratchFrameSize);
    std::array<uint8_t, 16> values{};
    if (!bridge->Process.Memory().ReadBytes(scratch, values)) {
        bridge->Fail("read projection scratch result");
        return;
    }
    std::memcpy(projected, values.data(), sizeof(*projected));
    std::memcpy(inverseW, values.data() + 12U, sizeof(*inverseW));
}

int32_t FUN_003723c0(
    void* manager, const Oot3dActorVec3f* from,
    const Oot3dActorVec3f* to, Oot3dActorVec3f* intersection,
    uint32_t* poly, int32_t checkOne, int32_t checkTwo,
    int32_t checkThree, int32_t checkFour, int32_t* bgId) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge == nullptr || intersection == nullptr ||
        poly == nullptr || bgId == nullptr) {
        return 0;
    }
    constexpr uint32_t intersectionOffset = 24U;
    constexpr uint32_t polyOffset = 36U;
    constexpr uint32_t bgIdOffset = 40U;
    const uint32_t frame = bridge->ScratchAddress(
        Oot3dNativeGame::kCollisionScratchFrameSize, 0U,
        Oot3dNativeGame::kCollisionScratchFrameSize);
    const std::array<uint32_t, 4> core{
        bridge->Encode(manager), bridge->Encode(from),
        bridge->Encode(to), frame + intersectionOffset};
    const std::array<uint32_t, 6> stack{
        frame + polyOffset, static_cast<uint32_t>(checkOne),
        static_cast<uint32_t>(checkTwo),
        static_cast<uint32_t>(checkThree),
        static_cast<uint32_t>(checkFour), frame + bgIdOffset};
    const uint32_t result = Oot3dNativeGame::ReadResult(
        Oot3dNativeGame::kCollisionLineTest, core, {}, stack,
        Oot3dNativeGame::kCollisionScratchFrameSize);
    std::array<uint8_t, 20> values{};
    if (!bridge->Process.Memory().ReadBytes(
            frame + intersectionOffset, values)) {
        bridge->Fail("read collision scratch result");
        return 0;
    }
    std::memcpy(intersection, values.data(), sizeof(*intersection));
    std::memcpy(poly, values.data() + 12U, sizeof(*poly));
    std::memcpy(bgId, values.data() + 16U, sizeof(*bgId));
    return static_cast<int32_t>(result);
}

uint32_t FUN_0035fe90(
    const void* manager, const void* flags, uint32_t slot) {
    const std::array<uint32_t, 3> core{
        Oot3dNativeGame::GuestAddress(manager),
        Oot3dNativeGame::GuestAddress(flags), slot};
    return Oot3dNativeGame::ReadResult(
        Oot3dNativeGame::kCollisionPolyAcceptsSlot, core);
}

int32_t Player_InCsMode(void* play) {
    const std::array<uint32_t, 1> core{
        Oot3dNativeGame::GuestAddress(play)};
    return static_cast<int32_t>(Oot3dNativeGame::ReadResult(
        Oot3dNativeGame::kPlayerInCsMode, core));
}

int32_t Math_StepToF(float* value, float target, float step) {
    const std::array<uint32_t, 1> core{
        Oot3dNativeGame::GuestAddress(value, sizeof(*value))};
    const std::array<uint32_t, 2> vfp{
        std::bit_cast<uint32_t>(target),
        std::bit_cast<uint32_t>(step)};
    return static_cast<int32_t>(Oot3dNativeGame::ReadResult(
        Oot3dNativeGame::kMathStepToF, core, vfp));
}

int16_t Math_Atan2S(float y, float x) {
    const std::array<uint32_t, 2> vfp{
        std::bit_cast<uint32_t>(y),
        std::bit_cast<uint32_t>(x)};
    return static_cast<int16_t>(Oot3dNativeGame::ReadResult(
        Oot3dNativeGame::kMathAtan2S, {}, vfp));
}

uint32_t oot3d_field_80x4_positive(
    const void* object, int32_t index) {
    const std::array<uint32_t, 2> core{
        Oot3dNativeGame::GuestAddress(object),
        static_cast<uint32_t>(index)};
    return Oot3dNativeGame::ReadResult(
        Oot3dNativeGame::kObjectSlotPositive, core);
}

int32_t oot3d_static_init_guard_acquire(uint32_t*) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge == nullptr) {
        return 0;
    }
    const std::array<uint32_t, 1> core{bridge->GuardAddress};
    const int32_t result = static_cast<int32_t>(
        Oot3dNativeGame::ReadResult(
            Oot3dNativeGame::kStaticInitGuardAcquire, core));
    if (!bridge->Process.Memory().ReadFast(
            bridge->GuardAddress, &gOot3dRendererInitGuard)) {
        bridge->Fail("read static initialization guard");
    }
    return result;
}

void RendererGlobalState_Init(void*) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge != nullptr) {
        bridge->InvokeCore(
            Oot3dNativeGame::kRendererGlobalStateInit,
            {bridge->RendererStateAddress});
    }
}

void Audio_PlaySoundGeneral(
    uint32_t sfxId, Oot3dAudioVec3f* position, uint8_t token,
    float* frequencyScale, float* volumeScale, int8_t* reverb) {
    auto* bridge = Oot3dNativeGame::ActiveBridge();
    if (bridge == nullptr) {
        return;
    }
    const std::array<uint32_t, 4> core{
        sfxId, bridge->Encode(position), token,
        bridge->Encode(frequencyScale, sizeof(*frequencyScale))};
    const std::array<uint32_t, 2> stack{
        bridge->Encode(volumeScale, sizeof(*volumeScale)),
        bridge->Encode(reverb, sizeof(*reverb))};
    Oot3dNativeGame::ReadResult(
        Oot3dNativeGame::kAudioPlaySoundGeneral, core, {}, stack,
        8U);
}

} // extern "C"
