#include "oot3d_source_cutscene_update_frame_runtime.h"

#include "oot3d_native_a32_process.h"
#include "oot3d_native_owner_call_adapter.h"
#include "oot3d_native_a32_vfp_ops.h"

#include "oot3d/cutscene_update_frame_owner.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kGuestServiceReturn = 0x0BAD2000U;
constexpr uint32_t kOwnerCallFrameSize = 0x18U;
constexpr uint32_t kQueryBackendClock = 0x002C2D78U;
constexpr uint32_t kCommitBackendClock = 0x0048B198U;
constexpr uint32_t kCutsceneProcessCommands = 0x002C5BA0U;

thread_local SourceCutsceneUpdateFrameBridge* gActiveBridge = nullptr;

class ActiveBridgeScope {
  public:
    explicit ActiveBridgeScope(
        SourceCutsceneUpdateFrameBridge& bridge) noexcept
        : mPrevious(gActiveBridge) {
        gActiveBridge = &bridge;
    }

    ActiveBridgeScope(const ActiveBridgeScope&) = delete;
    ActiveBridgeScope& operator=(const ActiveBridgeScope&) = delete;

    ~ActiveBridgeScope() {
        gActiveBridge = mPrevious;
    }

  private:
    SourceCutsceneUpdateFrameBridge* mPrevious = nullptr;
};

} // namespace

class SourceCutsceneUpdateFrameBridge {
  public:
    explicit SourceCutsceneUpdateFrameBridge(NativeA32Process& process)
        : Process(process), Adapter(process) {
    }

    bool Execute(uint32_t pc, oot3d::recomp::a32::GuestState& state,
                 oot3d::recomp::a32::MemoryBus& memory,
                 oot3d::recomp::a32::ExecutionResult* result,
                 uint32_t* blocksConsumed) {
        if (pc != kSourceCutsceneUpdateFrameEntry) {
            return false;
        }
        if (result == nullptr || &memory != &Process.Memory()) {
            return false;
        }

        const uint32_t playAddress = state.r[0];
        const uint32_t cutsceneContextAddress = state.r[1];
        if ((state.r[13] & 7U) != 0U ||
            !Process.Memory().IsWritable(playAddress, 0x7C64U) ||
            !Process.Memory().IsWritable(
                cutsceneContextAddress, 0x27CU)) {
            return false;
        }

        ++RuntimeStats.OwnerCalls;
        Failed = false;
        Error.clear();
        CallerState = state;
        Literals = {};

        std::string adapterError;
        auto execution = Adapter.BeginExecution(
            kSourceCutsceneUpdateFrameEntry, state, &adapterError);
        if (!execution) {
            Fail("begin source owner: " + adapterError);
        }
        if (!Failed && !LoadLiterals()) {
            Fail("load Cutscene_UpdateFrame target literals");
        }

        if (!Failed) {
            ActiveBridgeScope bridgeScope(*this);
            try {
                Oot3dSourceCutsceneUpdateFrame::Cutscene_UpdateFrame(
                    playAddress, cutsceneContextAddress);
            } catch (const std::exception& exception) {
                Fail(std::string("source owner exception: ") +
                     exception.what());
            } catch (...) {
                Fail("source owner unknown exception");
            }
        }
        execution.Reset();

        if (Failed) {
            ++RuntimeStats.Failures;
            *result = {
                oot3d::recomp::a32::ExitKind::Unsupported,
                pc,
                oot3d::recomp::a32::FallbackReason::Unsupported,
                kSourceCutsceneUpdateFrameEntry,
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

    void* Resolve(uint32_t address, size_t size) {
        if (Failed || address == 0U || size == 0U) {
            throw std::runtime_error(
                "Cutscene_UpdateFrame invalid guest pointer");
        }
        auto* pointer = Adapter.ResolveWrite(address, size);
        if (pointer == nullptr) {
            Fail(
                "Cutscene_UpdateFrame guest pointer resolution failed at " +
                std::to_string(address));
            throw std::runtime_error(Error);
        }
        return pointer;
    }

    uint32_t InvokeCore(
        uint32_t entryAddress, std::span<const uint32_t> arguments) {
        if (arguments.size() > 4U) {
            Fail("Cutscene_UpdateFrame service argument overflow");
            throw std::runtime_error(Error);
        }
        NativeA32OwnerGuestCall call;
        call.EntryAddress = entryAddress;
        call.ReturnAddress = kGuestServiceReturn;
        call.CallerFrameSize = kOwnerCallFrameSize;
        call.CoreArgumentCount = arguments.size();
        std::copy(
            arguments.begin(), arguments.end(),
            call.CoreArguments.begin());

        NativeA32OwnerGuestCallResult result;
        std::string invokeError;
        if (!Adapter.Invoke(call, &result, &invokeError)) {
            Fail(
                "Cutscene_UpdateFrame guest call " +
                std::to_string(entryAddress) +
                " failed: " + invokeError);
            throw std::runtime_error(Error);
        }
        ++RuntimeStats.NestedGuestCalls;
        SetFpscr(result.State.fpscr);
        return result.State.r[0];
    }

    int32_t ConvertBackendTicksToFrame(
        int32_t ticks, uint32_t tickScaleBits,
        uint32_t frameRateBits) {
        const auto converted =
            oot3d::recomp::a32::VfpBinary32FromSigned(
                static_cast<uint32_t>(ticks), CallerState.fpscr);
        ApplyFpscrFlags(converted.exception_flags);
        const auto scaled =
            oot3d::recomp::a32::VfpBinary32Multiply(
                converted.value, tickScaleBits, CallerState.fpscr);
        ApplyFpscrFlags(scaled.exception_flags);
        const auto framed =
            oot3d::recomp::a32::VfpBinary32Multiply(
                scaled.value, frameRateBits, CallerState.fpscr);
        ApplyFpscrFlags(framed.exception_flags);
        const auto target =
            oot3d::recomp::a32::VfpBinary32ToSigned(
                framed.value, CallerState.fpscr);
        ApplyFpscrFlags(target.exception_flags);
        return static_cast<int32_t>(target.value);
    }

    void ApplyFpscrFlags(uint32_t flags) {
        SetFpscr(CallerState.fpscr | flags);
    }

    void SetFpscr(uint32_t fpscr) {
        CallerState.fpscr = fpscr;
        Adapter.SetActiveFpscr(fpscr);
    }

    void Fail(std::string message) {
        if (!Failed) {
            Failed = true;
            Error = std::move(message);
        }
    }

    bool LoadLiterals() {
        return LoadLiteral(
                   0x00322074U, Literals.SchedulerStateAddress) &&
               LoadLiteral(
                   0x00322078U, Literals.SchedulerThreshold) &&
               LoadLiteral(
                   0x0032207CU, Literals.BackendClockTicksOffset) &&
               LoadLiteral(0x00322080U, Literals.TickScaleBits) &&
               LoadLiteral(0x00322084U, Literals.FrameRateBits);
    }

    bool LoadLiteral(uint32_t cellAddress, uint32_t& value) {
        return Process.Memory().ReadFast(cellAddress, &value);
    }

    NativeA32Process& Process;
    NativeA32OwnerCallAdapter Adapter;
    Oot3dSourceCutsceneUpdateFrame::CutsceneUpdateFrameLiterals Literals;
    SourceCutsceneUpdateFrameStats RuntimeStats;
    oot3d::recomp::a32::GuestState CallerState{};
    bool Failed = false;
    std::string Error;
};

namespace {

SourceCutsceneUpdateFrameBridge& ActiveBridge() {
    if (gActiveBridge == nullptr) {
        throw std::runtime_error(
            "Cutscene_UpdateFrame source bridge is not active");
    }
    return *gActiveBridge;
}

} // namespace

SourceCutsceneUpdateFrameRuntime::SourceCutsceneUpdateFrameRuntime(
    NativeA32Process& process)
    : mBridge(
          std::make_unique<SourceCutsceneUpdateFrameBridge>(process)) {
}

SourceCutsceneUpdateFrameRuntime::~SourceCutsceneUpdateFrameRuntime() =
    default;

bool SourceCutsceneUpdateFrameRuntime::Execute(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t* blocksConsumed) {
    return mBridge->Execute(
        pc, state, memory, result, blocksConsumed);
}

SourceCutsceneUpdateFrameStats
SourceCutsceneUpdateFrameRuntime::Stats() const noexcept {
    return mBridge->RuntimeStats;
}

void SourceCutsceneUpdateFrameRuntime::ResetStats() noexcept {
    mBridge->RuntimeStats = {};
}

const std::string&
SourceCutsceneUpdateFrameRuntime::LastError() const noexcept {
    return mBridge->Error;
}

} // namespace Oot3dNativeGame

namespace Oot3dSourceCutsceneUpdateFrame {

const CutsceneUpdateFrameLiterals& ActiveLiterals() {
    return Oot3dNativeGame::ActiveBridge().Literals;
}

void* ResolveGuestMemory(uint32_t address, size_t size) {
    return Oot3dNativeGame::ActiveBridge().Resolve(address, size);
}

int32_t QueryBackendClock() {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.ClockQueryCalls;
    return static_cast<int32_t>(
        bridge.InvokeCore(Oot3dNativeGame::kQueryBackendClock, {}));
}

void CommitBackendClock(uint32_t clockStateAddress) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.ClockCommitCalls;
    const std::array arguments{clockStateAddress};
    bridge.InvokeCore(
        Oot3dNativeGame::kCommitBackendClock, arguments);
}

int32_t ConvertBackendTicksToFrame(
    int32_t ticks, uint32_t tickScaleBits, uint32_t frameRateBits) {
    return Oot3dNativeGame::ActiveBridge().ConvertBackendTicksToFrame(
        ticks, tickScaleBits, frameRateBits);
}

void Cutscene_ProcessCommands(
    uint32_t playAddress, uint32_t cutsceneContextAddress,
    uint32_t cutsceneDataAddress) {
    auto& bridge = Oot3dNativeGame::ActiveBridge();
    ++bridge.RuntimeStats.ProcessCommandCalls;
    const std::array arguments{
        playAddress, cutsceneContextAddress, cutsceneDataAddress};
    bridge.InvokeCore(
        Oot3dNativeGame::kCutsceneProcessCommands, arguments);
}

} // namespace Oot3dSourceCutsceneUpdateFrame
