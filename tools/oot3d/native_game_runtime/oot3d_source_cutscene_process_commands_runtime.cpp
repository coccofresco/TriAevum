#include "oot3d_source_cutscene_process_commands_runtime.h"

#include "oot3d_native_a32_process.h"
#include "oot3d_native_a32_vfp_ops.h"
#include "oot3d_native_owner_call_adapter.h"

#include "oot3d/cutscene_process_commands_owner.h"

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

constexpr uint32_t kGuestServiceReturn = 0x0BAD3000U;
constexpr uint32_t kOwnerCallFrameSize = 0x100U;
constexpr uint32_t kScratchAlignment = 4U;
constexpr uint32_t kFpscrRoundingModeMask = 3U << 22U;

constexpr uint32_t kBuildCameraVector = 0x0033CB90U;
constexpr uint32_t kCameraSetParam = 0x003521F0U;
constexpr uint32_t kGameplayCameraSetAtEye = 0x00367B14U;
constexpr uint32_t kSetCameraViewAngle = 0x00354220U;
constexpr uint32_t kCutsceneFrameLerp = 0x00361490U;
constexpr uint32_t kTitleCardInitPlaceName = 0x003471C8U;

thread_local SourceCutsceneProcessCommandsBridge* gActiveBridge = nullptr;

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

class ActiveBridgeScope {
  public:
    explicit ActiveBridgeScope(
        SourceCutsceneProcessCommandsBridge& bridge) noexcept
        : mPrevious(gActiveBridge) {
        gActiveBridge = &bridge;
    }

    ActiveBridgeScope(const ActiveBridgeScope&) = delete;
    ActiveBridgeScope& operator=(const ActiveBridgeScope&) = delete;

    ~ActiveBridgeScope() {
        gActiveBridge = mPrevious;
    }

  private:
    SourceCutsceneProcessCommandsBridge* mPrevious = nullptr;
};

} // namespace

class SourceCutsceneProcessCommandsBridge {
  public:
    explicit SourceCutsceneProcessCommandsBridge(NativeA32Process& process)
        : Process(process), Adapter(process) {
    }

    bool Execute(uint32_t pc, oot3d::recomp::a32::GuestState& state,
                 oot3d::recomp::a32::MemoryBus& memory,
                 oot3d::recomp::a32::ExecutionResult* result,
                 uint32_t* blocksConsumed) {
        if (pc != kSourceCutsceneProcessCommandsEntry) {
            return false;
        }
        if (result == nullptr || &memory != &Process.Memory()) {
            return false;
        }

        const uint32_t playAddress = state.r[0];
        const uint32_t cutsceneContextAddress = state.r[1];
        const uint32_t scriptAddress = state.r[2];
        if ((state.r[13] & 7U) != 0U ||
            state.r[13] < kOwnerCallFrameSize ||
            !Process.Memory().IsWritable(playAddress, 0x7C64U) ||
            !Process.Memory().IsWritable(
                cutsceneContextAddress, 0x280U) ||
            !Process.Memory().IsMapped(scriptAddress, 0x10U)) {
            return false;
        }

        ++RuntimeStats.OwnerCalls;
        Failed = false;
        FailureDetail = 0U;
        Error.clear();
        CallerState = state;
        LastReadAddress = 0U;
        LastReadSize = 0U;
        Literals = {};

        std::string adapterError;
        auto execution = Adapter.BeginExecution(
            kSourceCutsceneProcessCommandsEntry, state, &adapterError);
        if (!execution) {
            Fail("begin source owner: " + adapterError);
        }
        if (!Failed && !LoadLiterals()) {
            Fail("load Cutscene_ProcessCommands target literals");
        }

        if (!Failed) {
            ActiveBridgeScope bridgeScope(*this);
            try {
                Oot3dSourceCutsceneProcessCommands::
                    Cutscene_ProcessCommands(
                        playAddress, cutsceneContextAddress,
                        scriptAddress);
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
                FailureDetail != 0U
                    ? FailureDetail
                    : kSourceCutsceneProcessCommandsEntry,
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
            Fail("invalid Cutscene_ProcessCommands guest read", address);
            throw std::runtime_error(Error);
        }
        const uint8_t* source = Adapter.ResolveRead(address, size);
        if (source == nullptr) {
            Fail("Cutscene_ProcessCommands guest read failed at " +
                     std::to_string(address) + " size " +
                     std::to_string(size) + " after " +
                     std::to_string(LastReadAddress) + " size " +
                     std::to_string(LastReadSize),
                 address);
            throw std::runtime_error(Error);
        }
        std::memcpy(destination, source, size);
        LastReadAddress = address;
        LastReadSize = size;
        ++RuntimeStats.GuestReadCalls;
    }

    void Write(uint32_t address, const void* source, size_t size) {
        if (Failed || address == 0U || source == nullptr || size == 0U) {
            Fail("invalid Cutscene_ProcessCommands guest write", address);
            throw std::runtime_error(Error);
        }
        const auto bytes = std::span<const uint8_t>(
            static_cast<const uint8_t*>(source), size);
        if (!Process.Memory().WriteBytes(address, bytes)) {
            Fail("Cutscene_ProcessCommands guest write failed at " +
                     std::to_string(address),
                 address);
            throw std::runtime_error(Error);
        }
        ++RuntimeStats.GuestWriteCalls;
    }

    NativeA32OwnerGuestCallResult Invoke(
        uint32_t entryAddress,
        std::span<const uint32_t> arguments,
        std::span<const uint32_t> vfpArguments,
        std::span<const Oot3dSourceCutsceneProcessCommands::
            GuestScratchArgument> scratchArguments) {
        if (Failed) {
            throw std::runtime_error(Error);
        }
        if (arguments.size() > 16U || vfpArguments.size() > 16U) {
            Fail("Cutscene_ProcessCommands service argument overflow",
                 entryAddress);
            throw std::runtime_error(Error);
        }

        std::array<uint32_t, 16> words{};
        std::copy(arguments.begin(), arguments.end(), words.begin());
        const size_t coreCount = std::min<size_t>(arguments.size(), 4U);
        const size_t stackCount = arguments.size() - coreCount;
        const uint32_t stackBytes =
            static_cast<uint32_t>(stackCount * sizeof(uint32_t));
        uint32_t scratchOffset =
            AlignUp(stackBytes, kScratchAlignment);
        const uint32_t callStackPointer =
            CallerState.r[13] - kOwnerCallFrameSize;

        std::array<uint32_t, 8> scratchAddresses{};
        if (scratchArguments.size() > scratchAddresses.size()) {
            Fail("Cutscene_ProcessCommands scratch argument overflow",
                 entryAddress);
            throw std::runtime_error(Error);
        }
        for (size_t index = 0U; index < scratchArguments.size();
             ++index) {
            const auto& scratch = scratchArguments[index];
            if (scratch.CoreArgumentIndex >= coreCount ||
                scratch.HostData == nullptr || scratch.Size == 0U) {
                Fail("Cutscene_ProcessCommands invalid scratch argument",
                     entryAddress);
                throw std::runtime_error(Error);
            }
            scratchOffset = AlignUp(scratchOffset, kScratchAlignment);
            if (scratchOffset > kOwnerCallFrameSize ||
                scratch.Size >
                    static_cast<size_t>(
                        kOwnerCallFrameSize - scratchOffset)) {
                Fail("Cutscene_ProcessCommands scratch frame overflow",
                     entryAddress);
                throw std::runtime_error(Error);
            }
            const uint32_t scratchAddress =
                callStackPointer + scratchOffset;
            scratchAddresses[index] = scratchAddress;
            words[scratch.CoreArgumentIndex] = scratchAddress;
            if (scratch.CopyIn) {
                const auto bytes = std::span<const uint8_t>(
                    static_cast<const uint8_t*>(scratch.HostData),
                    scratch.Size);
                if (!Process.Memory().WriteBytes(
                        scratchAddress, bytes)) {
                    Fail("Cutscene_ProcessCommands scratch write failed",
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
        std::copy_n(words.begin(), coreCount, call.CoreArguments.begin());
        call.VfpArgumentCount = vfpArguments.size();
        std::copy(vfpArguments.begin(), vfpArguments.end(),
                  call.VfpArguments.begin());
        call.StackArguments = std::span<const uint32_t>(
            words.data() + coreCount, stackCount);

        NativeA32OwnerGuestCallResult result;
        std::string invokeError;
        if (!Adapter.Invoke(call, &result, &invokeError)) {
            Fail("Cutscene_ProcessCommands guest call " +
                     std::to_string(entryAddress) +
                     " failed: " + invokeError,
                 entryAddress);
            throw std::runtime_error(Error);
        }
        ++RuntimeStats.NestedGuestCalls;
        SetFpscr(result.State.fpscr);

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
                Fail("Cutscene_ProcessCommands scratch read failed",
                     scratchAddresses[index]);
                throw std::runtime_error(Error);
            }
        }
        return result;
    }

    uint32_t InvokeCore(
        uint32_t entryAddress,
        std::span<const uint32_t> arguments) {
        ++RuntimeStats.DirectDependencyCalls;
        return Invoke(entryAddress, arguments, {}, {}).State.r[0];
    }

    uint32_t InvokeHardFloat(
        uint32_t entryAddress,
        std::span<const uint32_t> coreArguments,
        std::span<const uint32_t> vfpArguments) {
        ++RuntimeStats.DirectDependencyCalls;
        ++RuntimeStats.HardFloatCalls;
        return Invoke(
                   entryAddress, coreArguments, vfpArguments, {})
            .State.r[0];
    }

    uint32_t InvokeCoreWithScratch(
        uint32_t entryAddress,
        std::span<const uint32_t> arguments,
        std::span<const Oot3dSourceCutsceneProcessCommands::
            GuestScratchArgument> scratchArguments) {
        ++RuntimeStats.DirectDependencyCalls;
        ++RuntimeStats.ScratchCalls;
        return Invoke(
                   entryAddress, arguments, {}, scratchArguments)
            .State.r[0];
    }

    float InvokeFloatResult(
        uint32_t entryAddress,
        std::span<const uint32_t> arguments) {
        ++RuntimeStats.DirectDependencyCalls;
        ++RuntimeStats.HardFloatCalls;
        return std::bit_cast<float>(
            Invoke(entryAddress, arguments, {}, {}).State.vfp[0]);
    }

    void InvokeDynamic(
        uint32_t entryAddress,
        std::span<const uint32_t> arguments) {
        ++RuntimeStats.DynamicCallbackCalls;
        Invoke(entryAddress, arguments, {}, {});
    }

    float ConvertSigned(
        int32_t value, uint32_t roundingMode) {
        const auto converted =
            oot3d::recomp::a32::VfpBinary32FromSigned(
                static_cast<uint32_t>(value),
                ConversionFpscr(roundingMode));
        ApplyFpscrFlags(converted.exception_flags);
        ++RuntimeStats.ConversionOperations;
        return std::bit_cast<float>(converted.value);
    }

    float ConvertUnsigned(
        uint32_t value, uint32_t roundingMode) {
        const auto converted =
            oot3d::recomp::a32::VfpBinary32FromUnsigned(
                value, ConversionFpscr(roundingMode));
        ApplyFpscrFlags(converted.exception_flags);
        ++RuntimeStats.ConversionOperations;
        return std::bit_cast<float>(converted.value);
    }

    uint32_t ConvertFloatToUnsigned(
        float value, uint32_t roundingMode) {
        const auto converted =
            oot3d::recomp::a32::VfpBinary32ToUnsigned(
                std::bit_cast<uint32_t>(value),
                ConversionFpscr(roundingMode));
        ApplyFpscrFlags(converted.exception_flags);
        ++RuntimeStats.ConversionOperations;
        return converted.value;
    }

    uint32_t ConversionFpscr(uint32_t roundingMode) const {
        return (CallerState.fpscr & ~kFpscrRoundingModeMask) |
               ((roundingMode & 3U) << 22U);
    }

    void ApplyFpscrFlags(uint32_t flags) {
        SetFpscr(CallerState.fpscr | flags);
    }

    void SetFpscr(uint32_t fpscr) {
        CallerState.fpscr = fpscr;
        Adapter.SetActiveFpscr(fpscr);
    }

    bool LoadLiterals() {
        for (size_t index = 0U;
             index < Literals.Words.size(); ++index) {
            if (!Process.Memory().ReadFast(
                    Oot3dSourceCutsceneProcessCommands::
                        kLiteralCellAddresses[index],
                    &Literals.Words[index])) {
                FailureDetail =
                    Oot3dSourceCutsceneProcessCommands::
                        kLiteralCellAddresses[index];
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
    Oot3dSourceCutsceneProcessCommands::
        CutsceneProcessCommandsLiterals Literals;
    SourceCutsceneProcessCommandsStats RuntimeStats;
    oot3d::recomp::a32::GuestState CallerState{};
    uint32_t LastReadAddress = 0U;
    size_t LastReadSize = 0U;
    uint32_t FailureDetail = 0U;
    bool Failed = false;
    std::string Error;
};

namespace {

SourceCutsceneProcessCommandsBridge& ActiveBridge() {
    if (gActiveBridge == nullptr) {
        throw std::runtime_error(
            "Cutscene_ProcessCommands source bridge is not active");
    }
    return *gActiveBridge;
}

} // namespace

SourceCutsceneProcessCommandsRuntime::
    SourceCutsceneProcessCommandsRuntime(NativeA32Process& process)
    : mBridge(std::make_unique<
              SourceCutsceneProcessCommandsBridge>(process)) {
}

SourceCutsceneProcessCommandsRuntime::
    ~SourceCutsceneProcessCommandsRuntime() = default;

bool SourceCutsceneProcessCommandsRuntime::Execute(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t* blocksConsumed) {
    return mBridge->Execute(
        pc, state, memory, result, blocksConsumed);
}

SourceCutsceneProcessCommandsStats
SourceCutsceneProcessCommandsRuntime::Stats() const noexcept {
    return mBridge->RuntimeStats;
}

void SourceCutsceneProcessCommandsRuntime::ResetStats() noexcept {
    mBridge->RuntimeStats = {};
}

const std::string&
SourceCutsceneProcessCommandsRuntime::LastError() const noexcept {
    return mBridge->Error;
}

} // namespace Oot3dNativeGame

namespace Oot3dSourceCutsceneProcessCommands {

const CutsceneProcessCommandsLiterals& ActiveLiterals() {
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

uint32_t InvokeGuestCoreWords(
    uint32_t entryAddress,
    std::span<const uint32_t> arguments) {
    return Oot3dNativeGame::ActiveBridge().InvokeCore(
        entryAddress, arguments);
}

uint32_t InvokeGuestHardFloatWords(
    uint32_t entryAddress,
    std::span<const uint32_t> coreArguments,
    std::span<const uint32_t> vfpArguments) {
    return Oot3dNativeGame::ActiveBridge().InvokeHardFloat(
        entryAddress, coreArguments, vfpArguments);
}

uint32_t InvokeGuestCoreWordsWithScratch(
    uint32_t entryAddress,
    std::span<const uint32_t> arguments,
    std::span<const GuestScratchArgument> scratchArguments) {
    return Oot3dNativeGame::ActiveBridge().InvokeCoreWithScratch(
        entryAddress, arguments, scratchArguments);
}

float VectorSignedToFloat(int32_t value, uint32_t mode) {
    return Oot3dNativeGame::ActiveBridge().ConvertSigned(value, mode);
}

float VectorUnsignedToFloat(uint32_t value, uint32_t mode) {
    return Oot3dNativeGame::ActiveBridge().ConvertUnsigned(value, mode);
}

uint32_t VectorFloatToUnsigned(float value, uint32_t mode) {
    return Oot3dNativeGame::ActiveBridge().ConvertFloatToUnsigned(
        value, mode);
}

void FUN_0033cb90(
    uint32_t* cameraTrackState, uint32_t value,
    uint32_t contextAddress) {
    const std::array<uint32_t, 3> arguments{
        0U, value, contextAddress};
    const GuestScratchArgument scratch{
        0U, cameraTrackState, sizeof(uint32_t) * 3U, true, false};
    InvokeGuestCoreWordsWithScratch(
        Oot3dNativeGame::kBuildCameraVector, arguments,
        std::span<const GuestScratchArgument>(&scratch, 1U));
}

uint32_t Camera_SetParam(
    uint32_t cameraAddress, uint32_t parameter, float* value) {
    const std::array<uint32_t, 3> arguments{
        cameraAddress, parameter, 0U};
    const GuestScratchArgument scratch{
        2U, value, sizeof(float), true, false};
    return InvokeGuestCoreWordsWithScratch(
        Oot3dNativeGame::kCameraSetParam, arguments,
        std::span<const GuestScratchArgument>(&scratch, 1U));
}

uint32_t Gameplay_CameraSetAtEye(
    uint32_t playAddress, int32_t cameraId,
    uint32_t atAddress, uint32_t* eyeVector) {
    const std::array<uint32_t, 4> arguments{
        playAddress, static_cast<uint32_t>(cameraId),
        atAddress, 0U};
    const GuestScratchArgument scratch{
        3U, eyeVector, sizeof(uint32_t) * 3U, true, false};
    return InvokeGuestCoreWordsWithScratch(
        Oot3dNativeGame::kGameplayCameraSetAtEye, arguments,
        std::span<const GuestScratchArgument>(&scratch, 1U));
}

uint32_t Gameplay_CameraSetAtEye(
    uint32_t playAddress, int32_t cameraId,
    uint32_t* atVector, uint32_t* eyeVector) {
    const std::array<uint32_t, 4> arguments{
        playAddress, static_cast<uint32_t>(cameraId), 0U, 0U};
    const std::array<GuestScratchArgument, 2> scratch{{
        {2U, atVector, sizeof(uint32_t) * 3U, true, false},
        {3U, eyeVector, sizeof(uint32_t) * 3U, true, false},
    }};
    return InvokeGuestCoreWordsWithScratch(
        Oot3dNativeGame::kGameplayCameraSetAtEye, arguments,
        scratch);
}

uint32_t FUN_00354220(
    uint32_t playAddress, int32_t cameraId, float viewAngle) {
    const std::array<uint32_t, 2> core{
        playAddress, static_cast<uint32_t>(cameraId)};
    const std::array<uint32_t, 1> vfp{
        std::bit_cast<uint32_t>(viewAngle)};
    return InvokeGuestHardFloatWords(
        Oot3dNativeGame::kSetCameraViewAngle, core, vfp);
}

float FUN_00361490(
    int32_t endFrame, int32_t startFrame, int32_t frame) {
    const std::array<uint32_t, 3> arguments{
        static_cast<uint32_t>(endFrame),
        static_cast<uint32_t>(startFrame),
        static_cast<uint32_t>(frame)};
    return Oot3dNativeGame::ActiveBridge().InvokeFloatResult(
        Oot3dNativeGame::kCutsceneFrameLerp, arguments);
}

void TitleCard_InitPlaceName(
    int32_t play, int32_t titleContext, int32_t texture,
    int32_t x, int32_t y, int32_t width, int32_t height,
    int32_t delay, int32_t unknown, float scale) {
    const std::array<uint32_t, 9> core{
        static_cast<uint32_t>(play),
        static_cast<uint32_t>(titleContext),
        static_cast<uint32_t>(texture), static_cast<uint32_t>(x),
        static_cast<uint32_t>(y), static_cast<uint32_t>(width),
        static_cast<uint32_t>(height), static_cast<uint32_t>(delay),
        static_cast<uint32_t>(unknown)};
    const std::array<uint32_t, 1> vfp{
        std::bit_cast<uint32_t>(scale)};
    InvokeGuestHardFloatWords(
        Oot3dNativeGame::kTitleCardInitPlaceName, core, vfp);
}

void InvokeDynamicCallback(
    GuestPtr<undefined4> callbackObject,
    GuestPtr<uint> command) {
    uint32_t vtableAddress = 0U;
    uint32_t entryAddress = 0U;
    ReadGuestMemory(
        callbackObject.Address(), &vtableAddress,
        sizeof(vtableAddress));
    if (vtableAddress == 0U) {
        throw std::runtime_error(
            "Cutscene_ProcessCommands callback vtable is zero");
    }
    ReadGuestMemory(
        vtableAddress, &entryAddress, sizeof(entryAddress));
    if (entryAddress == 0U) {
        throw std::runtime_error(
            "Cutscene_ProcessCommands callback entry is zero");
    }
    const std::array<uint32_t, 2> arguments{
        callbackObject.Address(), command.Address()};
    Oot3dNativeGame::ActiveBridge().InvokeDynamic(
        entryAddress, arguments);
}

} // namespace Oot3dSourceCutsceneProcessCommands
