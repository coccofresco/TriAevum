#include "oot3d_source_player_update_runtime.h"

#include "oot3d_native_a32_process.h"
#include "oot3d_native_a32_vfp_ops.h"
#include "oot3d_native_owner_call_adapter.h"

#include "oot3d/player_update_owner.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kGuestServiceReturn = 0x0BAD3200U;
// Player_Update owns 0x60 bytes after its core/VFP saves and local
// allocation. Its 48-byte Input packet begins at owner SP + 0x10.
constexpr uint32_t kOwnerCallFrameSize = 0x60U;
constexpr uint32_t kInputScratchOffset = 0x10U;

constexpr uint32_t kObjectGetIndex = 0x00363C10U;
constexpr uint32_t kStaticInitGuardAcquire = 0x003679B4U;
constexpr uint32_t kCosIdx8 = 0x00338F60U;
constexpr uint32_t kSinIdx8 = 0x002CFCA0U;
constexpr uint32_t kActorSpawn = 0x003738D0U;
constexpr uint32_t kResetLinkedActor = 0x0036B02CU;
constexpr uint32_t kPlayerUpdateCommon = 0x00250AD0U;

thread_local SourcePlayerUpdateBridge* gActiveBridge = nullptr;

class ActiveBridgeScope {
  public:
    explicit ActiveBridgeScope(
        SourcePlayerUpdateBridge& bridge) noexcept
        : mPrevious(gActiveBridge) {
        gActiveBridge = &bridge;
    }

    ActiveBridgeScope(const ActiveBridgeScope&) = delete;
    ActiveBridgeScope& operator=(const ActiveBridgeScope&) = delete;

    ~ActiveBridgeScope() {
        gActiveBridge = mPrevious;
    }

  private:
    SourcePlayerUpdateBridge* mPrevious = nullptr;
};

} // namespace

class SourcePlayerUpdateBridge {
  public:
    explicit SourcePlayerUpdateBridge(NativeA32Process& process)
        : Process(process), Adapter(process) {
    }

    bool Execute(
        uint32_t pc, oot3d::recomp::a32::GuestState& state,
        oot3d::recomp::a32::MemoryBus& memory,
        oot3d::recomp::a32::ExecutionResult* result,
        uint32_t* blocksConsumed) {
        if (pc != kSourcePlayerUpdateEntry) {
            return false;
        }
        if (result == nullptr || &memory != &Process.Memory()) {
            return false;
        }

        const uint32_t player = state.r[0];
        const uint32_t play = state.r[1];
        if ((state.r[13] & 7U) != 0U ||
            state.r[13] < kOwnerCallFrameSize ||
            !Process.Memory().IsWritable(player, 0x29ECU) ||
            !Process.Memory().IsWritable(play, 0x3A60U)) {
            return false;
        }

        ++RuntimeStats.OwnerCalls;
        Failed = false;
        FailureDetail = 0U;
        Error.clear();
        CallerState = state;
        Literals = {};

        std::string adapterError;
        auto execution = Adapter.BeginExecution(
            kSourcePlayerUpdateEntry, state, &adapterError);
        if (!execution) {
            Fail("begin Player_Update source owner: " + adapterError);
        }
        if (!Failed && !LoadLiterals()) {
            Fail("load Player_Update target literals", FailureDetail);
        }

        if (!Failed) {
            ActiveBridgeScope bridgeScope(*this);
            try {
                Oot3dSourcePlayerUpdate::Player_Update(player, play);
            } catch (const std::exception& exception) {
                Fail(
                    std::string("Player_Update source owner exception: ") +
                    exception.what());
            } catch (...) {
                Fail("Player_Update source owner unknown exception");
            }
        }
        execution.Reset();

        if (Failed) {
            ++RuntimeStats.Failures;
            *result = {
                oot3d::recomp::a32::ExitKind::Unsupported,
                pc,
                oot3d::recomp::a32::FallbackReason::Unsupported,
                FailureDetail != 0U
                    ? FailureDetail
                    : kSourcePlayerUpdateEntry,
            };
            if (blocksConsumed != nullptr) {
                *blocksConsumed = 1U;
            }
            return true;
        }

        state.fpscr = CallerState.fpscr;
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

    void Read(uint32_t address, void* destination, size_t size) {
        if (Failed || address == 0U || destination == nullptr ||
            size == 0U) {
            Fail("invalid Player_Update guest read", address);
            throw std::runtime_error(Error);
        }
        const uint8_t* source = Adapter.ResolveRead(address, size);
        if (source == nullptr) {
            Fail(
                "Player_Update guest read failed at " +
                    std::to_string(address) + " size " +
                    std::to_string(size),
                address);
            throw std::runtime_error(Error);
        }
        std::memcpy(destination, source, size);
        ++RuntimeStats.GuestReadCalls;
    }

    void Write(uint32_t address, const void* source, size_t size) {
        if (Failed || address == 0U || source == nullptr ||
            size == 0U) {
            Fail("invalid Player_Update guest write", address);
            throw std::runtime_error(Error);
        }
        const auto bytes = std::span<const uint8_t>(
            static_cast<const uint8_t*>(source), size);
        if (!Process.Memory().WriteBytes(address, bytes)) {
            Fail(
                "Player_Update guest write failed at " +
                    std::to_string(address),
                address);
            throw std::runtime_error(Error);
        }
        ++RuntimeStats.GuestWriteCalls;
    }

    NativeA32OwnerGuestCallResult Invoke(
        uint32_t entryAddress,
        std::span<const uint32_t> coreArguments = {},
        std::span<const uint32_t> vfpArguments = {},
        std::span<const uint32_t> stackArguments = {}) {
        if (Failed) {
            throw std::runtime_error(Error);
        }
        NativeA32OwnerGuestCall call;
        call.EntryAddress = entryAddress;
        call.ReturnAddress = kGuestServiceReturn;
        call.CallerFrameSize = kOwnerCallFrameSize;
        call.CoreArgumentCount = coreArguments.size();
        call.VfpArgumentCount = vfpArguments.size();
        if (call.CoreArgumentCount > call.CoreArguments.size() ||
            call.VfpArgumentCount > call.VfpArguments.size()) {
            Fail(
                "Player_Update service argument overflow",
                entryAddress);
            throw std::runtime_error(Error);
        }
        std::copy(
            coreArguments.begin(), coreArguments.end(),
            call.CoreArguments.begin());
        std::copy(
            vfpArguments.begin(), vfpArguments.end(),
            call.VfpArguments.begin());
        call.StackArguments = stackArguments;

        NativeA32OwnerGuestCallResult guestResult;
        std::string invokeError;
        if (!Adapter.Invoke(call, &guestResult, &invokeError)) {
            Fail(
                "Player_Update guest call " +
                    std::to_string(entryAddress) +
                    " failed: " + invokeError,
                entryAddress);
            throw std::runtime_error(Error);
        }
        ++RuntimeStats.NestedGuestCalls;
        ++RuntimeStats.DirectDependencyCalls;
        if (!vfpArguments.empty() || entryAddress == kCosIdx8 ||
            entryAddress == kSinIdx8) {
            ++RuntimeStats.HardFloatCalls;
        }
        SetFpscr(guestResult.State.fpscr);
        return guestResult;
    }

    void UpdateCommon(
        uint32_t player, uint32_t play,
        const std::array<uint32_t, 12>& input) {
        const uint32_t scratchAddress =
            CallerState.r[13] - kOwnerCallFrameSize +
            kInputScratchOffset;
        Write(scratchAddress, input.data(), sizeof(input));
        const std::array<uint32_t, 3> core{
            player, play, scratchAddress};
        ++RuntimeStats.PlayerUpdateCommonCalls;
        ++RuntimeStats.ScratchCalls;
        Invoke(kPlayerUpdateCommon, core);
    }

    float ApplyVfpResult(
        const oot3d::recomp::a32::VfpBinary32Result& result) {
        SetFpscr(CallerState.fpscr | result.exception_flags);
        ++RuntimeStats.VfpOperations;
        return std::bit_cast<float>(result.value);
    }

    int32_t ConvertFloatToSigned(float value) {
        const auto result =
            oot3d::recomp::a32::VfpBinary32ToSigned(
                std::bit_cast<uint32_t>(value),
                CallerState.fpscr);
        SetFpscr(CallerState.fpscr | result.exception_flags);
        ++RuntimeStats.VfpOperations;
        ++RuntimeStats.FloatConversions;
        return static_cast<int32_t>(result.value);
    }

    void SetFpscr(uint32_t fpscr) {
        CallerState.fpscr = fpscr;
        Adapter.SetActiveFpscr(fpscr);
    }

    bool LoadLiterals() {
        for (size_t index = 0U; index < Literals.Words.size();
             ++index) {
            const uint32_t address =
                Oot3dSourcePlayerUpdate::
                    kLiteralCellAddresses[index];
            if (!Process.Memory().ReadFast(
                    address, &Literals.Words[index])) {
                FailureDetail = address;
                return false;
            }
        }
        return true;
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
    Oot3dSourcePlayerUpdate::PlayerUpdateLiterals Literals;
    SourcePlayerUpdateStats RuntimeStats;
    oot3d::recomp::a32::GuestState CallerState{};
    uint32_t FailureDetail = 0U;
    bool Failed = false;
    std::string Error;
};

namespace {

SourcePlayerUpdateBridge& ActiveBridge() {
    if (gActiveBridge == nullptr) {
        throw std::runtime_error(
            "Player_Update source bridge is not active");
    }
    return *gActiveBridge;
}

float FloatResult(const NativeA32OwnerGuestCallResult& result) {
    return std::bit_cast<float>(result.State.vfp[0]);
}

} // namespace

SourcePlayerUpdateRuntime::SourcePlayerUpdateRuntime(
    NativeA32Process& process)
    : mBridge(std::make_unique<SourcePlayerUpdateBridge>(process)) {
}

SourcePlayerUpdateRuntime::~SourcePlayerUpdateRuntime() = default;

bool SourcePlayerUpdateRuntime::Execute(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t* blocksConsumed) {
    return mBridge->Execute(
        pc, state, memory, result, blocksConsumed);
}

SourcePlayerUpdateStats
SourcePlayerUpdateRuntime::Stats() const noexcept {
    return mBridge->RuntimeStats;
}

void SourcePlayerUpdateRuntime::ResetStats() noexcept {
    mBridge->RuntimeStats = {};
}

const std::string&
SourcePlayerUpdateRuntime::LastError() const noexcept {
    return mBridge->Error;
}

} // namespace Oot3dNativeGame

namespace Oot3dSourcePlayerUpdate {

const PlayerUpdateLiterals& ActiveLiterals() {
    return Oot3dNativeGame::ActiveBridge().Literals;
}

void ReadGuestMemory(
    uint32_t address, void* destination, size_t size) {
    Oot3dNativeGame::ActiveBridge().Read(
        address, destination, size);
}

void WriteGuestMemory(
    uint32_t address, const void* source, size_t size) {
    Oot3dNativeGame::ActiveBridge().Write(
        address, source, size);
}

int32_t Object_GetIndex(
    uint32_t objectContext, uint32_t objectId) {
    const std::array<uint32_t, 2> core{
        objectContext, objectId};
    return static_cast<int32_t>(
        Oot3dNativeGame::ActiveBridge()
            .Invoke(Oot3dNativeGame::kObjectGetIndex, core)
            .State.r[0]);
}

int32_t oot3d_static_init_guard_acquire(
    uint32_t guardAddress) {
    const std::array<uint32_t, 1> core{guardAddress};
    return static_cast<int32_t>(
        Oot3dNativeGame::ActiveBridge()
            .Invoke(
                Oot3dNativeGame::kStaticInitGuardAcquire,
                core)
            .State.r[0]);
}

float oot3d_cos_idx8(int32_t angle) {
    const std::array<uint32_t, 1> core{
        static_cast<uint32_t>(angle)};
    return Oot3dNativeGame::FloatResult(
        Oot3dNativeGame::ActiveBridge().Invoke(
            Oot3dNativeGame::kCosIdx8, core));
}

float oot3d_sin_idx8(int32_t angle) {
    const std::array<uint32_t, 1> core{
        static_cast<uint32_t>(angle)};
    return Oot3dNativeGame::FloatResult(
        Oot3dNativeGame::ActiveBridge().Invoke(
            Oot3dNativeGame::kSinIdx8, core));
}

uint32_t Actor_Spawn(
    uint32_t actorContext, uint32_t play, int32_t actorId,
    float x, float y, float z, int32_t rotX, int32_t rotY,
    int32_t rotZ, int32_t params, int32_t initializeNow) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    const std::array<uint32_t, 4> core{
        actorContext, play, static_cast<uint32_t>(actorId),
        static_cast<uint32_t>(rotX)};
    const std::array<uint32_t, 3> vfp{
        std::bit_cast<uint32_t>(x),
        std::bit_cast<uint32_t>(y),
        std::bit_cast<uint32_t>(z)};
    const std::array<uint32_t, 4> stack{
        static_cast<uint32_t>(rotY),
        static_cast<uint32_t>(rotZ),
        static_cast<uint32_t>(params),
        static_cast<uint32_t>(initializeNow)};
    ++bridge.RuntimeStats.ActorSpawnCalls;
    return bridge
        .Invoke(
            Oot3dNativeGame::kActorSpawn, core, vfp, stack)
        .State.r[0];
}

void FUN_0036b02c(uint32_t play, uint32_t player) {
    const std::array<uint32_t, 2> core{play, player};
    Oot3dNativeGame::ActiveBridge().Invoke(
        Oot3dNativeGame::kResetLinkedActor, core);
}

void Player_UpdateCommon(
    uint32_t player, uint32_t play,
    const std::array<uint32_t, 12>& input) {
    Oot3dNativeGame::ActiveBridge().UpdateCommon(
        player, play, input);
}

float TargetAdd(float left, float right) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    return bridge.ApplyVfpResult(
        oot3d::recomp::a32::VfpBinary32Add(
            std::bit_cast<uint32_t>(left),
            std::bit_cast<uint32_t>(right),
            bridge.CallerState.fpscr));
}

float TargetMultiply(float left, float right) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    return bridge.ApplyVfpResult(
        oot3d::recomp::a32::VfpBinary32Multiply(
            std::bit_cast<uint32_t>(left),
            std::bit_cast<uint32_t>(right),
            bridge.CallerState.fpscr));
}

float TargetMultiplyAccumulate(
    float accumulator, float left, float right) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    return bridge.ApplyVfpResult(
        oot3d::recomp::a32::VfpBinary32MultiplyAccumulate(
            std::bit_cast<uint32_t>(accumulator),
            std::bit_cast<uint32_t>(left),
            std::bit_cast<uint32_t>(right),
            bridge.CallerState.fpscr));
}

float TargetMultiplySubtract(
    float accumulator, float left, float right) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    return bridge.ApplyVfpResult(
        oot3d::recomp::a32::VfpBinary32MultiplySubtract(
            std::bit_cast<uint32_t>(accumulator),
            std::bit_cast<uint32_t>(left),
            std::bit_cast<uint32_t>(right),
            bridge.CallerState.fpscr));
}

int32_t TargetFloatToSigned(float value) {
    return Oot3dNativeGame::ActiveBridge()
        .ConvertFloatToSigned(value);
}

} // namespace Oot3dSourcePlayerUpdate
