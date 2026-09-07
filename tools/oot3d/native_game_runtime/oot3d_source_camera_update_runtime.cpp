#include "oot3d_source_camera_update_runtime.h"

#include "oot3d_native_a32_process.h"
#include "oot3d_native_a32_vfp_ops.h"
#include "oot3d_native_owner_call_adapter.h"

#include "oot3d/camera_update_owner.h"

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

constexpr uint32_t kGuestServiceReturn = 0x0BAD3100U;
constexpr uint32_t kOwnerCallFrameSize = 0x100U;
constexpr uint32_t kScratchAlignment = 4U;

thread_local SourceCameraUpdateBridge* gActiveBridge = nullptr;

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

bool UsesHardFloat(uint32_t entryAddress) {
    switch (entryAddress) {
    case 0x002CFCA0U:
    case 0x002D052CU:
    case 0x00316C18U:
    case 0x00338AC8U:
    case 0x00338F60U:
    case 0x00343840U:
    case 0x00343858U:
    case 0x00355804U:
    case 0x00367E60U:
    case 0x00367EF0U:
    case 0x00372448U:
    case 0x004710F8U:
    case 0x00479718U:
    case 0x0047CC14U:
        return true;
    default:
        return false;
    }
}

class ActiveBridgeScope {
  public:
    explicit ActiveBridgeScope(SourceCameraUpdateBridge& bridge) noexcept
        : mPrevious(gActiveBridge) {
        gActiveBridge = &bridge;
    }

    ActiveBridgeScope(const ActiveBridgeScope&) = delete;
    ActiveBridgeScope& operator=(const ActiveBridgeScope&) = delete;

    ~ActiveBridgeScope() {
        gActiveBridge = mPrevious;
    }

  private:
    SourceCameraUpdateBridge* mPrevious = nullptr;
};

class TargetRoundingScope {
  public:
    explicit TargetRoundingScope(uint32_t fpscr)
        : mPrevious(std::fegetround()) {
        if (mPrevious == -1 ||
            std::fesetround(HostRoundingMode(fpscr)) != 0) {
            throw std::runtime_error(
                "cannot install Camera_Update FPSCR rounding mode");
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
                "invalid Camera_Update FPSCR rounding mode");
        }
    }

    int mPrevious = FE_TONEAREST;
    bool mActive = false;
};

} // namespace

class SourceCameraUpdateBridge {
  public:
    explicit SourceCameraUpdateBridge(NativeA32Process& process)
        : Process(process), Adapter(process) {
    }

    bool Execute(uint32_t pc, oot3d::recomp::a32::GuestState& state,
                 oot3d::recomp::a32::MemoryBus& memory,
                 oot3d::recomp::a32::ExecutionResult* result,
                 uint32_t* blocksConsumed) {
        if (pc != kSourceCameraUpdateEntry) {
            return false;
        }
        if (result == nullptr || &memory != &Process.Memory()) {
            return false;
        }

        const uint32_t resultAddress = state.r[0];
        const uint32_t cameraAddress = state.r[1];
        if ((state.r[13] & 7U) != 0U ||
            state.r[13] < kOwnerCallFrameSize ||
            !Process.Memory().IsWritable(
                resultAddress,
                sizeof(Oot3dSourceCameraUpdate::Oot3dVec3s)) ||
            !Process.Memory().IsWritable(cameraAddress, 0x1B8U)) {
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
            kSourceCameraUpdateEntry, state, &adapterError);
        if (!execution) {
            Fail("begin Camera_Update source owner: " + adapterError);
        }
        if (!Failed && !LoadLiterals()) {
            Fail("load Camera_Update target literals", FailureDetail);
        }

        if (!Failed) {
            ActiveBridgeScope bridgeScope(*this);
            try {
                TargetRoundingScope roundingScope(CallerState.fpscr);
                Oot3dSourceCameraUpdate::Camera_Update(
                    resultAddress, cameraAddress);
            } catch (const std::exception& exception) {
                Fail(std::string("Camera_Update source owner exception: ") +
                     exception.what());
            } catch (...) {
                Fail("Camera_Update source owner unknown exception");
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
                    : kSourceCameraUpdateEntry,
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
            Fail("invalid Camera_Update guest read", address);
            throw std::runtime_error(Error);
        }
        const uint8_t* source = Adapter.ResolveRead(address, size);
        if (source == nullptr) {
            Fail("Camera_Update guest read failed at " +
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
            Fail("invalid Camera_Update guest write", address);
            throw std::runtime_error(Error);
        }
        const auto bytes = std::span<const uint8_t>(
            static_cast<const uint8_t*>(source), size);
        if (!Process.Memory().WriteBytes(address, bytes)) {
            Fail("Camera_Update guest write failed at " +
                     std::to_string(address),
                 address);
            throw std::runtime_error(Error);
        }
        ++RuntimeStats.GuestWriteCalls;
    }

    Oot3dSourceCameraUpdate::GuestCallResult Invoke(
        uint32_t entryAddress,
        std::span<const uint32_t> arguments,
        std::span<const uint32_t> vfpArguments,
        std::span<const Oot3dSourceCameraUpdate::GuestScratchArgument>
            scratchArguments,
        bool dynamicCall) {
        if (Failed) {
            throw std::runtime_error(Error);
        }
        if (entryAddress == 0U || arguments.size() > 16U ||
            vfpArguments.size() > 16U) {
            Fail("Camera_Update service argument overflow", entryAddress);
            throw std::runtime_error(Error);
        }

        std::array<uint32_t, 16> words{};
        std::copy(arguments.begin(), arguments.end(), words.begin());
        const size_t coreCount = std::min<size_t>(arguments.size(), 4U);
        const size_t stackCount = arguments.size() - coreCount;
        const uint32_t stackBytes =
            static_cast<uint32_t>(stackCount * sizeof(uint32_t));
        uint32_t scratchOffset = AlignUp(stackBytes, kScratchAlignment);
        const uint32_t callStackPointer =
            CallerState.r[13] - kOwnerCallFrameSize;

        std::array<uint32_t, 8> scratchAddresses{};
        if (scratchArguments.size() > scratchAddresses.size()) {
            Fail("Camera_Update scratch argument overflow", entryAddress);
            throw std::runtime_error(Error);
        }
        for (size_t index = 0U; index < scratchArguments.size();
             ++index) {
            const auto& scratch = scratchArguments[index];
            if (scratch.ArgumentIndex >= arguments.size() ||
                scratch.HostData == nullptr || scratch.Size == 0U) {
                Fail("Camera_Update invalid scratch argument",
                     entryAddress);
                throw std::runtime_error(Error);
            }
            scratchOffset = AlignUp(scratchOffset, kScratchAlignment);
            if (scratchOffset > kOwnerCallFrameSize ||
                scratch.Size >
                    static_cast<size_t>(
                        kOwnerCallFrameSize - scratchOffset)) {
                Fail("Camera_Update scratch frame overflow", entryAddress);
                throw std::runtime_error(Error);
            }
            const uint32_t scratchAddress =
                callStackPointer + scratchOffset;
            scratchAddresses[index] = scratchAddress;
            words[scratch.ArgumentIndex] = scratchAddress;
            if (scratch.CopyIn) {
                const auto bytes = std::span<const uint8_t>(
                    static_cast<const uint8_t*>(scratch.HostData),
                    scratch.Size);
                if (!Process.Memory().WriteBytes(scratchAddress, bytes)) {
                    Fail("Camera_Update scratch write failed",
                         scratchAddress);
                    throw std::runtime_error(Error);
                }
            }
            scratchOffset += static_cast<uint32_t>(scratch.Size);
        }

        NativeA32OwnerGuestCall call;
        call.EntryAddress = entryAddress;
        call.ReturnAddress = kGuestServiceReturn;
        call.CallerFrameSize = kOwnerCallFrameSize;
        call.CoreArgumentCount = coreCount;
        std::copy_n(
            words.begin(), coreCount, call.CoreArguments.begin());
        call.VfpArgumentCount = vfpArguments.size();
        std::copy(
            vfpArguments.begin(), vfpArguments.end(),
            call.VfpArguments.begin());
        call.StackArguments = std::span<const uint32_t>(
            words.data() + coreCount, stackCount);

        NativeA32OwnerGuestCallResult guestResult;
        std::string invokeError;
        if (!Adapter.Invoke(call, &guestResult, &invokeError)) {
            Fail("Camera_Update guest call " +
                     std::to_string(entryAddress) +
                     " failed: " + invokeError,
                 entryAddress);
            throw std::runtime_error(Error);
        }
        ++RuntimeStats.NestedGuestCalls;
        if (dynamicCall) {
            ++RuntimeStats.DynamicCameraFunctionCalls;
        } else {
            ++RuntimeStats.DirectDependencyCalls;
        }
        if (UsesHardFloat(entryAddress) || !vfpArguments.empty()) {
            ++RuntimeStats.HardFloatCalls;
        }
        if (!scratchArguments.empty()) {
            ++RuntimeStats.ScratchCalls;
        }
        SetFpscr(guestResult.State.fpscr);

        for (size_t index = 0U; index < scratchArguments.size();
             ++index) {
            const auto& scratch = scratchArguments[index];
            if (!scratch.CopyOut) {
                continue;
            }
            auto bytes = std::span<uint8_t>(
                static_cast<uint8_t*>(scratch.HostData),
                scratch.Size);
            if (!Process.Memory().ReadBytes(
                    scratchAddresses[index], bytes)) {
                Fail("Camera_Update scratch read failed",
                     scratchAddresses[index]);
                throw std::runtime_error(Error);
            }
        }

        Oot3dSourceCameraUpdate::GuestCallResult result;
        result.CoreResult = guestResult.State.r[0];
        std::copy_n(
            guestResult.State.vfp.begin(), result.VfpWords.size(),
            result.VfpWords.begin());
        return result;
    }

    float SquareRoot(float value) {
        const auto converted =
            oot3d::recomp::a32::VfpBinary32SquareRoot(
                std::bit_cast<uint32_t>(value), CallerState.fpscr);
        SetFpscr(CallerState.fpscr | converted.exception_flags);
        ++RuntimeStats.SquareRootOperations;
        return std::bit_cast<float>(converted.value);
    }

    void SetFpscr(uint32_t fpscr) {
        CallerState.fpscr = fpscr;
        Adapter.SetActiveFpscr(fpscr);
    }

    bool LoadLiterals() {
        for (size_t index = 0U; index < Literals.Words.size(); ++index) {
            const uint32_t address =
                Oot3dSourceCameraUpdate::kLiteralCellAddresses[index];
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
    Oot3dSourceCameraUpdate::CameraUpdateLiterals Literals;
    SourceCameraUpdateStats RuntimeStats;
    oot3d::recomp::a32::GuestState CallerState{};
    uint32_t FailureDetail = 0U;
    bool Failed = false;
    std::string Error;
};

namespace {

SourceCameraUpdateBridge& ActiveBridge() {
    if (gActiveBridge == nullptr) {
        throw std::runtime_error(
            "Camera_Update source bridge is not active");
    }
    return *gActiveBridge;
}

} // namespace

SourceCameraUpdateRuntime::SourceCameraUpdateRuntime(
    NativeA32Process& process)
    : mBridge(std::make_unique<SourceCameraUpdateBridge>(process)) {
}

SourceCameraUpdateRuntime::~SourceCameraUpdateRuntime() = default;

bool SourceCameraUpdateRuntime::Execute(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t* blocksConsumed) {
    return mBridge->Execute(
        pc, state, memory, result, blocksConsumed);
}

SourceCameraUpdateStats
SourceCameraUpdateRuntime::Stats() const noexcept {
    return mBridge->RuntimeStats;
}

void SourceCameraUpdateRuntime::ResetStats() noexcept {
    mBridge->RuntimeStats = {};
}

const std::string&
SourceCameraUpdateRuntime::LastError() const noexcept {
    return mBridge->Error;
}

} // namespace Oot3dNativeGame

namespace Oot3dSourceCameraUpdate {

const CameraUpdateLiterals& ActiveLiterals() {
    return Oot3dNativeGame::ActiveBridge().Literals;
}

void ReadGuestMemory(
    uint32_t address, void* destination, size_t size) {
    Oot3dNativeGame::ActiveBridge().Read(
        address, destination, size);
}

void WriteGuestMemory(
    uint32_t address, const void* source, size_t size) {
    Oot3dNativeGame::ActiveBridge().Write(address, source, size);
}

GuestCallResult InvokeGuestWords(
    uint32_t entryAddress,
    std::span<const uint32_t> arguments,
    std::span<const uint32_t> vfpArguments,
    std::span<const GuestScratchArgument> scratchArguments) {
    return Oot3dNativeGame::ActiveBridge().Invoke(
        entryAddress, arguments, vfpArguments, scratchArguments,
        false);
}

void InvokeDynamicCameraFunction(
    uint32_t entryAddress, uint32_t cameraAddress) {
    const std::array<uint32_t, 1> arguments{cameraAddress};
    Oot3dNativeGame::ActiveBridge().Invoke(
        entryAddress, arguments, {}, {}, true);
}

float TargetSqrt(float value) {
    return Oot3dNativeGame::ActiveBridge().SquareRoot(value);
}

} // namespace Oot3dSourceCameraUpdate
