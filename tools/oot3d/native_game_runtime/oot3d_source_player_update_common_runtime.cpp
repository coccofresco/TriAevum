#include "oot3d_source_player_update_common_runtime.h"

#include "oot3d_native_a32_process.h"
#include "oot3d_native_a32_vfp_ops.h"
#include "oot3d_native_owner_call_adapter.h"

#include "oot3d/player_update_common_owner.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
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

constexpr uint32_t kGuestServiceReturn = 0x0BAD3300U;
// Target prologue: 36-byte core save, 56-byte VFP save and 0x44 local
// allocation. Nested callees therefore observe caller SP - 0xA0.
constexpr uint32_t kOwnerCallFrameSize = 0xA0U;

thread_local SourcePlayerUpdateCommonBridge* gActiveBridge = nullptr;

class ActiveBridgeScope {
  public:
    explicit ActiveBridgeScope(
        SourcePlayerUpdateCommonBridge& bridge) noexcept
        : mPrevious(gActiveBridge) {
        gActiveBridge = &bridge;
    }

    ActiveBridgeScope(const ActiveBridgeScope&) = delete;
    ActiveBridgeScope& operator=(const ActiveBridgeScope&) = delete;

    ~ActiveBridgeScope() {
        gActiveBridge = mPrevious;
    }

  private:
    SourcePlayerUpdateCommonBridge* mPrevious = nullptr;
};

class TargetRoundingScope {
  public:
    explicit TargetRoundingScope(uint32_t fpscr)
        : mPrevious(std::fegetround()) {
        if (mPrevious == -1 ||
            std::fesetround(HostRoundingMode(fpscr)) != 0) {
            throw std::runtime_error(
                "cannot install Player_UpdateCommon FPSCR rounding mode");
        }
        mActive = true;
    }

    TargetRoundingScope(const TargetRoundingScope&) = delete;
    TargetRoundingScope& operator=(const TargetRoundingScope&) = delete;

    ~TargetRoundingScope() {
        if (mActive) {
            std::fesetround(mPrevious);
        }
    }

  private:
    static int HostRoundingMode(uint32_t fpscr) {
        switch ((fpscr >> 22U) & 3U) {
        case 0U:
            return FE_TONEAREST;
        case 1U:
            return FE_UPWARD;
        case 2U:
            return FE_DOWNWARD;
        case 3U:
            return FE_TOWARDZERO;
        default:
            throw std::runtime_error(
                "invalid Player_UpdateCommon FPSCR rounding mode");
        }
    }

    int mPrevious = FE_TONEAREST;
    bool mActive = false;
};

} // namespace

class SourcePlayerUpdateCommonBridge {
  public:
    explicit SourcePlayerUpdateCommonBridge(NativeA32Process& process)
        : Process(process), Adapter(process) {
    }

    bool Execute(
        uint32_t pc, oot3d::recomp::a32::GuestState& state,
        oot3d::recomp::a32::MemoryBus& memory,
        oot3d::recomp::a32::ExecutionResult* result,
        uint32_t* blocksConsumed) {
        if (pc != kSourcePlayerUpdateCommonEntry) {
            return false;
        }
        if (result == nullptr || &memory != &Process.Memory()) {
            return false;
        }

        const uint32_t player = state.r[0];
        const uint32_t play = state.r[1];
        const uint32_t input = state.r[2];
        if ((state.r[13] & 7U) != 0U ||
            state.r[13] < kOwnerCallFrameSize ||
            !Process.Memory().IsWritable(player, 0x29ECU) ||
            !Process.Memory().IsWritable(play, 0x3A60U) ||
            !Process.Memory().IsMapped(input, 48U)) {
            return false;
        }

        ++RuntimeStats.OwnerCalls;
        Failed = false;
        FailureDetail = 0U;
        Error.clear();
        CallerState = state;
        Literals = {};
        RecentReads = {};
        RecentReadSizes = {};
        RecentReadCursor = 0U;
        TotalReadAttempts = 0U;

        std::string adapterError;
        auto execution = Adapter.BeginExecution(
            kSourcePlayerUpdateCommonEntry, state, &adapterError);
        if (!execution) {
            Fail(
                "begin Player_UpdateCommon source owner: " +
                adapterError);
        }
        if (!Failed && !LoadLiterals()) {
            Fail(
                "load Player_UpdateCommon target literals",
                FailureDetail);
        }

        if (!Failed) {
            ActiveBridgeScope bridgeScope(*this);
            try {
                TargetRoundingScope roundingScope(CallerState.fpscr);
                Oot3dSourcePlayerUpdateCommon::Player_UpdateCommon(
                    player, play, input);
            } catch (const std::exception& exception) {
                Fail(
                    std::string(
                        "Player_UpdateCommon source owner exception: ") +
                    exception.what());
            } catch (...) {
                Fail(
                    "Player_UpdateCommon source owner unknown exception");
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
                    : kSourcePlayerUpdateCommonEntry,
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
        const size_t slot = RecentReadCursor % RecentReads.size();
        RecentReads[slot] = address;
        RecentReadSizes[slot] = static_cast<uint32_t>(size);
        ++RecentReadCursor;
        ++TotalReadAttempts;
        if (Failed || address == 0U || destination == nullptr ||
            size == 0U) {
            Fail(
                "invalid Player_UpdateCommon guest read", address);
            throw std::runtime_error(Error);
        }
        const uint8_t* source = Adapter.ResolveRead(address, size);
        if (source == nullptr) {
            Fail(
                "Player_UpdateCommon guest read failed at " +
                    std::to_string(address) + " size " +
                    std::to_string(size) + " after read " +
                    std::to_string(TotalReadAttempts) +
                    "; recent reads: " + FormatRecentReads(),
                address);
            throw std::runtime_error(Error);
        }
        std::memcpy(destination, source, size);
        ++RuntimeStats.GuestReadCalls;
    }

    void Write(uint32_t address, const void* source, size_t size) {
        if (Failed || address == 0U || source == nullptr ||
            size == 0U) {
            Fail(
                "invalid Player_UpdateCommon guest write", address);
            throw std::runtime_error(Error);
        }
        const auto bytes = std::span<const uint8_t>(
            static_cast<const uint8_t*>(source), size);
        if (!Process.Memory().WriteBytes(address, bytes)) {
            Fail(
                "Player_UpdateCommon guest write failed at " +
                    std::to_string(address),
                address);
            throw std::runtime_error(Error);
        }
        ++RuntimeStats.GuestWriteCalls;
    }

    Oot3dSourcePlayerUpdateCommon::GuestCallResult Invoke(
        uint32_t entryAddress,
        std::span<const uint32_t> coreArguments,
        std::span<const uint32_t> vfpArguments,
        std::span<const uint32_t> stackArguments,
        bool dynamicCall) {
        if (Failed) {
            throw std::runtime_error(Error);
        }
        if (entryAddress == 0U ||
            coreArguments.size() > 4U ||
            vfpArguments.size() > 16U ||
            stackArguments.size() * sizeof(uint32_t) >
                kOwnerCallFrameSize) {
            Fail(
                "Player_UpdateCommon service argument overflow",
                entryAddress);
            throw std::runtime_error(Error);
        }

        NativeA32OwnerGuestCall call;
        call.EntryAddress = entryAddress;
        call.ReturnAddress = kGuestServiceReturn;
        call.CallerFrameSize = kOwnerCallFrameSize;
        call.CoreArgumentCount = coreArguments.size();
        std::copy(
            coreArguments.begin(), coreArguments.end(),
            call.CoreArguments.begin());
        call.VfpArgumentCount = vfpArguments.size();
        std::copy(
            vfpArguments.begin(), vfpArguments.end(),
            call.VfpArguments.begin());
        call.StackArguments = stackArguments;

        NativeA32OwnerGuestCallResult guestResult;
        std::string invokeError;
        if (!Adapter.Invoke(call, &guestResult, &invokeError)) {
            Fail(
                "Player_UpdateCommon guest call " +
                    std::to_string(entryAddress) +
                    " failed: " + invokeError,
                entryAddress);
            throw std::runtime_error(Error);
        }
        ++RuntimeStats.NestedGuestCalls;
        if (dynamicCall) {
            ++RuntimeStats.DynamicPlayerActionCalls;
        } else {
            ++RuntimeStats.DirectDependencyCalls;
        }
        if (!vfpArguments.empty()) {
            ++RuntimeStats.HardFloatCalls;
        }
        SetFpscr(guestResult.State.fpscr);

        Oot3dSourcePlayerUpdateCommon::GuestCallResult result;
        result.CoreResult = guestResult.State.r[0];
        std::copy_n(
            guestResult.State.vfp.begin(), result.VfpWords.size(),
            result.VfpWords.begin());
        return result;
    }

    uint32_t LocalAddress(uint32_t offset) {
        if (offset >= 0x44U) {
            Fail(
                "Player_UpdateCommon local offset exceeds target frame",
                offset);
            throw std::runtime_error(Error);
        }
        return CallerState.r[13] - kOwnerCallFrameSize + offset;
    }

    float ConvertSigned(int32_t value) {
        return ApplyVfpResult(
            oot3d::recomp::a32::VfpBinary32FromSigned(
                static_cast<uint32_t>(value), CallerState.fpscr),
            true, false);
    }

    float ConvertUnsigned(uint32_t value) {
        return ApplyVfpResult(
            oot3d::recomp::a32::VfpBinary32FromUnsigned(
                value, CallerState.fpscr),
            true, false);
    }

    float SquareRoot(float value) {
        return ApplyVfpResult(
            oot3d::recomp::a32::VfpBinary32SquareRoot(
                std::bit_cast<uint32_t>(value), CallerState.fpscr),
            false, true);
    }

    float ApplyVfpResult(
        const oot3d::recomp::a32::VfpBinary32Result& result,
        bool conversion, bool squareRoot) {
        SetFpscr(CallerState.fpscr | result.exception_flags);
        ++RuntimeStats.VfpOperations;
        if (conversion) {
            ++RuntimeStats.FloatConversions;
        }
        if (squareRoot) {
            ++RuntimeStats.SquareRootOperations;
        }
        return std::bit_cast<float>(result.value);
    }

    void SetFpscr(uint32_t fpscr) {
        CallerState.fpscr = fpscr;
        Adapter.SetActiveFpscr(fpscr);
    }

    bool LoadLiterals() {
        for (size_t index = 0U; index < Literals.Words.size();
             ++index) {
            const uint32_t address =
                Oot3dSourcePlayerUpdateCommon::
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

    std::string FormatRecentReads() const {
        std::string trace;
        const size_t count =
            std::min(RecentReadCursor, RecentReads.size());
        const size_t first =
            RecentReadCursor > RecentReads.size()
                ? RecentReadCursor - RecentReads.size()
                : 0U;
        for (size_t index = 0U; index < count; ++index) {
            const size_t slot =
                (first + index) % RecentReads.size();
            if (!trace.empty()) {
                trace += ", ";
            }
            trace += std::to_string(RecentReads[slot]);
            trace += "/";
            trace += std::to_string(RecentReadSizes[slot]);
        }
        return trace;
    }

    NativeA32Process& Process;
    NativeA32OwnerCallAdapter Adapter;
    Oot3dSourcePlayerUpdateCommon::PlayerUpdateCommonLiterals
        Literals;
    SourcePlayerUpdateCommonStats RuntimeStats;
    oot3d::recomp::a32::GuestState CallerState{};
    uint32_t FailureDetail = 0U;
    bool Failed = false;
    std::string Error;
    std::array<uint32_t, 16> RecentReads{};
    std::array<uint32_t, 16> RecentReadSizes{};
    size_t RecentReadCursor = 0U;
    uint64_t TotalReadAttempts = 0U;
};

namespace {

SourcePlayerUpdateCommonBridge& ActiveBridge() {
    if (gActiveBridge == nullptr) {
        throw std::runtime_error(
            "Player_UpdateCommon source bridge is not active");
    }
    return *gActiveBridge;
}

} // namespace

SourcePlayerUpdateCommonRuntime::SourcePlayerUpdateCommonRuntime(
    NativeA32Process& process)
    : mBridge(
          std::make_unique<SourcePlayerUpdateCommonBridge>(process)) {
}

SourcePlayerUpdateCommonRuntime::~SourcePlayerUpdateCommonRuntime() =
    default;

bool SourcePlayerUpdateCommonRuntime::Execute(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t* blocksConsumed) {
    return mBridge->Execute(
        pc, state, memory, result, blocksConsumed);
}

SourcePlayerUpdateCommonStats
SourcePlayerUpdateCommonRuntime::Stats() const noexcept {
    return mBridge->RuntimeStats;
}

void SourcePlayerUpdateCommonRuntime::ResetStats() noexcept {
    mBridge->RuntimeStats = {};
}

const std::string&
SourcePlayerUpdateCommonRuntime::LastError() const noexcept {
    return mBridge->Error;
}

} // namespace Oot3dNativeGame

namespace Oot3dSourcePlayerUpdateCommon {

const PlayerUpdateCommonLiterals& ActiveLiterals() {
    return Oot3dNativeGame::ActiveBridge().Literals;
}

std::uint32_t OwnerLocalAddress(std::uint32_t offset) {
    return Oot3dNativeGame::ActiveBridge().LocalAddress(offset);
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

GuestCallResult InvokeGuestWords(
    uint32_t entryAddress,
    std::span<const uint32_t> coreArguments,
    std::span<const uint32_t> vfpArguments,
    std::span<const uint32_t> stackArguments) {
    return Oot3dNativeGame::ActiveBridge().Invoke(
        entryAddress, coreArguments, vfpArguments, stackArguments,
        false);
}

void InvokeDynamicPlayerAction(
    uint32_t entryAddress, uint32_t player, uint32_t play) {
    const std::array<uint32_t, 2> core{player, play};
    Oot3dNativeGame::ActiveBridge().Invoke(
        entryAddress, core, {}, {}, true);
}

float TargetSignedToFloat(int32_t value, int) {
    return Oot3dNativeGame::ActiveBridge().ConvertSigned(value);
}

float TargetUnsignedToFloat(uint32_t value, int) {
    return Oot3dNativeGame::ActiveBridge().ConvertUnsigned(value);
}

float TargetSqrt(float value) {
    return Oot3dNativeGame::ActiveBridge().SquareRoot(value);
}

} // namespace Oot3dSourcePlayerUpdateCommon
