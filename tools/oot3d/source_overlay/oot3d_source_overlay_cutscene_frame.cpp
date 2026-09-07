#include "oot3d_source_overlay_cutscene_frame.h"

#include "oot3d_source_overlay_cutscene_commands.h"

#include "oot3d/cutscene_update_frame_owner.h"
#include "oot3d_native_a32_vfp_ops.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace Oot3dSourceOverlay::CutsceneFrame {
namespace {

constexpr std::uint32_t kGuestServiceReturn = 0x0BAD2000U;
constexpr std::uint32_t kOwnerCallFrameSize = 0x18U;
constexpr std::uint32_t kQueryBackendClock = 0x002C2D78U;
constexpr std::uint32_t kCommitBackendClock = 0x0048B198U;
constexpr std::uint32_t kCutsceneProcessCommands = 0x002C5BA0U;
constexpr std::uint32_t kMinimumPlaySize = 0x7C64U;
constexpr std::uint32_t kMinimumCutsceneContextSize = 0x27CU;

Stats gStats;

class Bridge;
thread_local Bridge* gActiveBridge = nullptr;

class ActiveBridgeScope final {
  public:
    explicit ActiveBridgeScope(Bridge& bridge) noexcept
        : mPrevious(gActiveBridge) {
        gActiveBridge = &bridge;
    }

    ~ActiveBridgeScope() {
        gActiveBridge = mPrevious;
    }

    ActiveBridgeScope(const ActiveBridgeScope&) = delete;
    ActiveBridgeScope& operator=(const ActiveBridgeScope&) = delete;

  private:
    Bridge* mPrevious = nullptr;
};

class Bridge final {
  public:
    Bridge(const Oot3dSourceOverlayHostApi& host,
           const Oot3dSourceOverlayGuestState& callerState,
           std::uint32_t blockBudget)
        : Host(host), CallerState(callerState),
          BlockBudget(std::max(blockBudget, 1U)) {
    }

    bool LoadLiterals() {
        return LoadLiteral(0x00322074U, Literals.SchedulerStateAddress) &&
               LoadLiteral(0x00322078U, Literals.SchedulerThreshold) &&
               LoadLiteral(0x0032207CU, Literals.BackendClockTicksOffset) &&
               LoadLiteral(0x00322080U, Literals.TickScaleBits) &&
               LoadLiteral(0x00322084U, Literals.FrameRateBits);
    }

    void* Resolve(std::uint32_t address, std::size_t size) {
        if (Failed || address == 0U || size == 0U ||
            Host.ResolveWrite == nullptr) {
            Fail("Cutscene_UpdateFrame invalid guest pointer");
            throw std::runtime_error(Error);
        }
        void* pointer = Host.ResolveWrite(Host.Context, address, size);
        if (pointer == nullptr) {
            Fail("Cutscene_UpdateFrame guest pointer resolution failed");
            throw std::runtime_error(Error);
        }
        return pointer;
    }

    std::uint32_t InvokeCore(std::uint32_t entryAddress,
                             std::span<const std::uint32_t> arguments) {
        if (arguments.size() > 4U ||
            CallerState.Registers[13] < kOwnerCallFrameSize ||
            Host.CallGuest == nullptr) {
            Fail("Cutscene_UpdateFrame invalid nested guest call");
            throw std::runtime_error(Error);
        }

        Oot3dSourceOverlayGuestState callState = CallerState;
        callState.Registers[13] -= kOwnerCallFrameSize;
        if ((callState.Registers[13] & 7U) != 0U) {
            Fail("Cutscene_UpdateFrame nested stack is not aligned");
            throw std::runtime_error(Error);
        }
        std::copy(arguments.begin(), arguments.end(),
                  callState.Registers);
        callState.Registers[14] = kGuestServiceReturn;
        callState.Registers[15] = entryAddress;

        Oot3dSourceOverlayExecutionResult callResult{sizeof(callResult)};
        const int executed =
            entryAddress == CutsceneCommands::kProcessEntry
                ? CutsceneCommands::Execute(
                      entryAddress, &callState, &callResult, &Host,
                      BlockBudget)
                : Host.CallGuest(Host.Context, entryAddress, &callState,
                                 &callResult, BlockBudget);
        if (executed == 0 ||
            callResult.Kind != OOT3D_SOURCE_OVERLAY_BRANCH ||
            callResult.Pc != kGuestServiceReturn) {
            Fail("Cutscene_UpdateFrame nested guest call failed");
            throw std::runtime_error(Error);
        }
        if (entryAddress == CutsceneCommands::kProcessEntry) {
            ++gStats.SourceDependencyCalls;
        } else {
            ++gStats.NestedGuestCalls;
        }
        SetFpscr(callState.Fpscr);
        return callState.Registers[0];
    }

    std::int32_t ConvertBackendTicksToFrame(
        std::int32_t ticks, std::uint32_t tickScaleBits,
        std::uint32_t frameRateBits) {
        const auto converted =
            oot3d::recomp::a32::VfpBinary32FromSigned(
                static_cast<std::uint32_t>(ticks), CallerState.Fpscr);
        ApplyFpscrFlags(converted.exception_flags);
        const auto scaled = oot3d::recomp::a32::VfpBinary32Multiply(
            converted.value, tickScaleBits, CallerState.Fpscr);
        ApplyFpscrFlags(scaled.exception_flags);
        const auto framed = oot3d::recomp::a32::VfpBinary32Multiply(
            scaled.value, frameRateBits, CallerState.Fpscr);
        ApplyFpscrFlags(framed.exception_flags);
        const auto target = oot3d::recomp::a32::VfpBinary32ToSigned(
            framed.value, CallerState.Fpscr);
        ApplyFpscrFlags(target.exception_flags);
        return static_cast<std::int32_t>(target.value);
    }

    void ApplyFpscrFlags(std::uint32_t flags) {
        SetFpscr(CallerState.Fpscr | flags);
    }

    void SetFpscr(std::uint32_t fpscr) {
        CallerState.Fpscr = fpscr;
    }

    void Fail(std::string message) {
        if (!Failed) {
            Failed = true;
            Error = std::move(message);
        }
    }

    const Oot3dSourceCutsceneUpdateFrame::CutsceneUpdateFrameLiterals&
    ActiveLiterals() const noexcept {
        return Literals;
    }

    std::uint32_t Fpscr() const noexcept {
        return CallerState.Fpscr;
    }

    bool HasFailed() const noexcept {
        return Failed;
    }

  private:
    bool LoadLiteral(std::uint32_t address, std::uint32_t& value) {
        if (Host.ReadMemory == nullptr ||
            Host.ReadMemory(Host.Context, address, &value,
                            sizeof(value)) == 0) {
            Fail("Cutscene_UpdateFrame target literal read failed");
            return false;
        }
        return true;
    }

    const Oot3dSourceOverlayHostApi& Host;
    Oot3dSourceOverlayGuestState CallerState{};
    std::uint32_t BlockBudget = 1U;
    Oot3dSourceCutsceneUpdateFrame::CutsceneUpdateFrameLiterals Literals;
    bool Failed = false;
    std::string Error;
};

Bridge& ActiveBridge() {
    if (gActiveBridge == nullptr) {
        throw std::runtime_error(
            "Cutscene_UpdateFrame source bridge is not active");
    }
    return *gActiveBridge;
}

bool WritableRange(const Oot3dSourceOverlayHostApi& host,
                   std::uint32_t address, std::size_t size) {
    return host.ProbeMemory != nullptr &&
           (host.ProbeMemory(host.Context, address, size) &
            OOT3D_SOURCE_OVERLAY_MEMORY_WRITE) != 0U;
}

void SetResult(Oot3dSourceOverlayExecutionResult* result,
               std::uint32_t kind, std::uint32_t pc,
               std::uint32_t detail) {
    result->StructSize = sizeof(*result);
    result->Kind = kind;
    result->Pc = pc;
    result->Detail = detail;
    result->BlocksConsumed = 1U;
}

} // namespace

int Execute(std::uint32_t entry, Oot3dSourceOverlayGuestState* state,
            Oot3dSourceOverlayExecutionResult* result,
            const Oot3dSourceOverlayHostApi* host,
            std::uint32_t blockBudget) {
    if (entry != kUpdateEntry || state == nullptr || result == nullptr ||
        host == nullptr || host->StructSize < sizeof(*host)) {
        return 0;
    }

    ++gStats.OwnerCalls;
    const std::uint32_t playAddress = state->Registers[0];
    const std::uint32_t cutsceneContextAddress = state->Registers[1];
    if ((state->Registers[13] & 7U) != 0U ||
        !WritableRange(*host, playAddress, kMinimumPlaySize) ||
        !WritableRange(*host, cutsceneContextAddress,
                       kMinimumCutsceneContextSize)) {
        return 0;
    }

    Bridge bridge(*host, *state, blockBudget);
    if (!bridge.LoadLiterals()) {
        return 0;
    }

    try {
        ActiveBridgeScope scope(bridge);
        Oot3dSourceCutsceneUpdateFrame::Cutscene_UpdateFrame(
            playAddress, cutsceneContextAddress);
    } catch (...) {
        bridge.Fail("Cutscene_UpdateFrame source owner exception");
    }

    if (bridge.HasFailed()) {
        ++gStats.Failures;
        SetResult(result, OOT3D_SOURCE_OVERLAY_MEMORY_FAULT,
                  kUpdateEntry, kUpdateEntry);
        return 1;
    }

    state->Fpscr = bridge.Fpscr();
    state->Registers[15] = state->Registers[14];
    SetResult(result, OOT3D_SOURCE_OVERLAY_BRANCH,
              state->Registers[15], kUpdateEntry);
    ++gStats.OwnerHandled;
    return 1;
}

Stats GetStats() {
    return gStats;
}

} // namespace Oot3dSourceOverlay::CutsceneFrame

namespace Oot3dSourceCutsceneUpdateFrame {

const CutsceneUpdateFrameLiterals& ActiveLiterals() {
    return Oot3dSourceOverlay::CutsceneFrame::ActiveBridge()
        .ActiveLiterals();
}

void* ResolveGuestMemory(std::uint32_t address, std::size_t size) {
    return Oot3dSourceOverlay::CutsceneFrame::ActiveBridge().Resolve(
        address, size);
}

std::int32_t QueryBackendClock() {
    ++Oot3dSourceOverlay::CutsceneFrame::gStats.ClockQueryCalls;
    return static_cast<std::int32_t>(
        Oot3dSourceOverlay::CutsceneFrame::ActiveBridge().InvokeCore(
            Oot3dSourceOverlay::CutsceneFrame::kQueryBackendClock, {}));
}

void CommitBackendClock(std::uint32_t clockStateAddress) {
    ++Oot3dSourceOverlay::CutsceneFrame::gStats.ClockCommitCalls;
    const std::array arguments{clockStateAddress};
    Oot3dSourceOverlay::CutsceneFrame::ActiveBridge().InvokeCore(
        Oot3dSourceOverlay::CutsceneFrame::kCommitBackendClock,
        arguments);
}

std::int32_t ConvertBackendTicksToFrame(
    std::int32_t ticks, std::uint32_t tickScaleBits,
    std::uint32_t frameRateBits) {
    return Oot3dSourceOverlay::CutsceneFrame::ActiveBridge()
        .ConvertBackendTicksToFrame(ticks, tickScaleBits,
                                    frameRateBits);
}

void Cutscene_ProcessCommands(std::uint32_t playAddress,
                              std::uint32_t cutsceneContextAddress,
                              std::uint32_t cutsceneDataAddress) {
    ++Oot3dSourceOverlay::CutsceneFrame::gStats.ProcessCommandCalls;
    const std::array arguments{playAddress, cutsceneContextAddress,
                               cutsceneDataAddress};
    Oot3dSourceOverlay::CutsceneFrame::ActiveBridge().InvokeCore(
        Oot3dSourceOverlay::CutsceneFrame::kCutsceneProcessCommands,
        arguments);
}

} // namespace Oot3dSourceCutsceneUpdateFrame
