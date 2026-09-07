#include "oot3d_source_overlay_cutscene_commands.h"

#include "oot3d/cutscene_process_commands_owner.h"
#include "oot3d_native_a32_vfp_ops.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace Oot3dSourceOverlay::CutsceneCommands {
namespace {

constexpr std::uint32_t kGuestServiceReturn = 0x0BAD3000U;
constexpr std::uint32_t kOwnerCallFrameSize = 0x100U;
constexpr std::uint32_t kScratchAlignment = 4U;
constexpr std::uint32_t kFpscrRoundingModeMask = 3U << 22U;
constexpr std::uint32_t kMinimumPlaySize = 0x7C64U;
constexpr std::uint32_t kMinimumCutsceneContextSize = 0x280U;

constexpr std::uint32_t kBuildCameraVector = 0x0033CB90U;
constexpr std::uint32_t kCameraSetParam = 0x003521F0U;
constexpr std::uint32_t kGameplayCameraSetAtEye = 0x00367B14U;
constexpr std::uint32_t kSetCameraViewAngle = 0x00354220U;
constexpr std::uint32_t kCutsceneFrameLerp = 0x00361490U;
constexpr std::uint32_t kTitleCardInitPlaceName = 0x003471C8U;

Stats gStats;

std::uint32_t AlignUp(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

bool RangeHas(const Oot3dSourceOverlayHostApi& host,
              std::uint32_t address, std::size_t size,
              std::uint32_t access) {
    return host.ProbeMemory != nullptr && address != 0U && size != 0U &&
           (host.ProbeMemory(host.Context, address, size) & access) != 0U;
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

struct GuestCallResult {
    Oot3dSourceOverlayGuestState State{};
};

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
        for (std::size_t index = 0U; index < Literals.Words.size(); ++index) {
            if (!RawRead(
                    Oot3dSourceCutsceneProcessCommands::
                        kLiteralCellAddresses[index],
                    &Literals.Words[index], sizeof(std::uint32_t))) {
                Fail("Cutscene_ProcessCommands target literal read failed",
                     Oot3dSourceCutsceneProcessCommands::
                         kLiteralCellAddresses[index]);
                return false;
            }
        }
        return true;
    }

    void Read(std::uint32_t address, void* destination, std::size_t size) {
        if (Failed || address == 0U || destination == nullptr || size == 0U) {
            Fail("invalid Cutscene_ProcessCommands guest read", address);
            throw std::runtime_error(Error);
        }
        if (!RawRead(address, destination, size)) {
            Fail("Cutscene_ProcessCommands guest read failed", address);
            throw std::runtime_error(Error);
        }
        ++gStats.GuestReadCalls;
    }

    void Write(std::uint32_t address, const void* source,
               std::size_t size) {
        if (Failed || address == 0U || source == nullptr || size == 0U) {
            Fail("invalid Cutscene_ProcessCommands guest write", address);
            throw std::runtime_error(Error);
        }
        if (!RawWrite(address, source, size)) {
            Fail("Cutscene_ProcessCommands guest write failed", address);
            throw std::runtime_error(Error);
        }
        ++gStats.GuestWriteCalls;
    }

    GuestCallResult Invoke(
        std::uint32_t entryAddress,
        std::span<const std::uint32_t> arguments,
        std::span<const std::uint32_t> vfpArguments,
        std::span<const Oot3dSourceCutsceneProcessCommands::
            GuestScratchArgument> scratchArguments) {
        if (Failed) {
            throw std::runtime_error(Error);
        }
        if (Host.CallGuest == nullptr || arguments.size() > 16U ||
            vfpArguments.size() > 16U ||
            scratchArguments.size() > 8U ||
            CallerState.Registers[13] < kOwnerCallFrameSize) {
            Fail("Cutscene_ProcessCommands invalid dependency call",
                 entryAddress);
            throw std::runtime_error(Error);
        }

        std::array<std::uint32_t, 16> words{};
        std::copy(arguments.begin(), arguments.end(), words.begin());
        const std::size_t coreCount =
            std::min<std::size_t>(arguments.size(), 4U);
        const std::size_t stackCount = arguments.size() - coreCount;
        const std::uint32_t stackBytes = static_cast<std::uint32_t>(
            stackCount * sizeof(std::uint32_t));
        std::uint32_t scratchOffset = AlignUp(stackBytes, kScratchAlignment);
        const std::uint32_t callStackPointer =
            CallerState.Registers[13] - kOwnerCallFrameSize;
        if ((callStackPointer & 7U) != 0U) {
            Fail("Cutscene_ProcessCommands dependency stack is not aligned",
                 entryAddress);
            throw std::runtime_error(Error);
        }

        std::array<std::uint32_t, 8> scratchAddresses{};
        for (std::size_t index = 0U; index < scratchArguments.size();
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
                scratch.Size > static_cast<std::size_t>(
                                   kOwnerCallFrameSize - scratchOffset)) {
                Fail("Cutscene_ProcessCommands scratch frame overflow",
                     entryAddress);
                throw std::runtime_error(Error);
            }
            const std::uint32_t scratchAddress =
                callStackPointer + scratchOffset;
            scratchAddresses[index] = scratchAddress;
            words[scratch.CoreArgumentIndex] = scratchAddress;
            if (scratch.CopyIn &&
                !RawWrite(scratchAddress, scratch.HostData, scratch.Size)) {
                Fail("Cutscene_ProcessCommands scratch write failed",
                     scratchAddress);
                throw std::runtime_error(Error);
            }
            scratchOffset += static_cast<std::uint32_t>(scratch.Size);
        }

        for (std::size_t index = 0U; index < stackCount; ++index) {
            if (!RawWrite(callStackPointer + static_cast<std::uint32_t>(
                                              index * sizeof(std::uint32_t)),
                          &words[coreCount + index],
                          sizeof(std::uint32_t))) {
                Fail("Cutscene_ProcessCommands stack argument write failed",
                     callStackPointer);
                throw std::runtime_error(Error);
            }
        }

        GuestCallResult output;
        output.State = CallerState;
        output.State.Registers[13] = callStackPointer;
        std::copy_n(words.begin(), coreCount, output.State.Registers);
        std::copy(vfpArguments.begin(), vfpArguments.end(),
                  output.State.Vfp);
        output.State.Registers[14] = kGuestServiceReturn;
        output.State.Registers[15] = entryAddress;

        Oot3dSourceOverlayExecutionResult callResult{sizeof(callResult)};
        if (Host.CallGuest(Host.Context, entryAddress, &output.State,
                           &callResult, BlockBudget) == 0 ||
            callResult.Kind != OOT3D_SOURCE_OVERLAY_BRANCH ||
            callResult.Pc != kGuestServiceReturn) {
            Fail("Cutscene_ProcessCommands dependency call failed",
                 entryAddress);
            throw std::runtime_error(Error);
        }
        ++gStats.NestedGuestCalls;
        SetFpscr(output.State.Fpscr);

        for (std::size_t index = 0U; index < scratchArguments.size();
             ++index) {
            const auto& scratch = scratchArguments[index];
            if (scratch.CopyOut &&
                !RawRead(scratchAddresses[index], scratch.HostData,
                         scratch.Size)) {
                Fail("Cutscene_ProcessCommands scratch read failed",
                     scratchAddresses[index]);
                throw std::runtime_error(Error);
            }
        }
        return output;
    }

    std::uint32_t InvokeCore(
        std::uint32_t entryAddress,
        std::span<const std::uint32_t> arguments) {
        ++gStats.DirectDependencyCalls;
        return Invoke(entryAddress, arguments, {}, {}).State.Registers[0];
    }

    std::uint32_t InvokeHardFloat(
        std::uint32_t entryAddress,
        std::span<const std::uint32_t> coreArguments,
        std::span<const std::uint32_t> vfpArguments) {
        ++gStats.DirectDependencyCalls;
        ++gStats.HardFloatCalls;
        return Invoke(entryAddress, coreArguments, vfpArguments, {})
            .State.Registers[0];
    }

    std::uint32_t InvokeCoreWithScratch(
        std::uint32_t entryAddress,
        std::span<const std::uint32_t> arguments,
        std::span<const Oot3dSourceCutsceneProcessCommands::
            GuestScratchArgument> scratchArguments) {
        ++gStats.DirectDependencyCalls;
        ++gStats.ScratchCalls;
        return Invoke(entryAddress, arguments, {}, scratchArguments)
            .State.Registers[0];
    }

    float InvokeFloatResult(
        std::uint32_t entryAddress,
        std::span<const std::uint32_t> arguments) {
        ++gStats.DirectDependencyCalls;
        ++gStats.HardFloatCalls;
        return std::bit_cast<float>(
            Invoke(entryAddress, arguments, {}, {}).State.Vfp[0]);
    }

    void InvokeDynamic(
        std::uint32_t entryAddress,
        std::span<const std::uint32_t> arguments) {
        ++gStats.DynamicCallbackCalls;
        Invoke(entryAddress, arguments, {}, {});
    }

    float ConvertSigned(std::int32_t value, std::uint32_t roundingMode) {
        const auto converted = oot3d::recomp::a32::VfpBinary32FromSigned(
            static_cast<std::uint32_t>(value),
            ConversionFpscr(roundingMode));
        ApplyFpscrFlags(converted.exception_flags);
        ++gStats.ConversionOperations;
        return std::bit_cast<float>(converted.value);
    }

    float ConvertUnsigned(std::uint32_t value,
                          std::uint32_t roundingMode) {
        const auto converted = oot3d::recomp::a32::VfpBinary32FromUnsigned(
            value, ConversionFpscr(roundingMode));
        ApplyFpscrFlags(converted.exception_flags);
        ++gStats.ConversionOperations;
        return std::bit_cast<float>(converted.value);
    }

    std::uint32_t ConvertFloatToUnsigned(
        float value, std::uint32_t roundingMode) {
        const auto converted = oot3d::recomp::a32::VfpBinary32ToUnsigned(
            std::bit_cast<std::uint32_t>(value),
            ConversionFpscr(roundingMode));
        ApplyFpscrFlags(converted.exception_flags);
        ++gStats.ConversionOperations;
        return converted.value;
    }

    std::uint32_t ConversionFpscr(std::uint32_t roundingMode) const {
        return (CallerState.Fpscr & ~kFpscrRoundingModeMask) |
               ((roundingMode & 3U) << 22U);
    }

    void ApplyFpscrFlags(std::uint32_t flags) {
        SetFpscr(CallerState.Fpscr | flags);
    }

    void SetFpscr(std::uint32_t fpscr) {
        CallerState.Fpscr = fpscr;
    }

    const Oot3dSourceCutsceneProcessCommands::
        CutsceneProcessCommandsLiterals&
    ActiveLiterals() const noexcept {
        return Literals;
    }

    std::uint32_t Fpscr() const noexcept {
        return CallerState.Fpscr;
    }

    bool HasFailed() const noexcept {
        return Failed;
    }

    std::uint32_t FailureAddress() const noexcept {
        return FailureDetail;
    }

    const std::string& FailureMessage() const noexcept {
        return Error;
    }

    void Fail(std::string message, std::uint32_t detail = 0U) {
        if (!Failed) {
            Failed = true;
            FailureDetail = detail;
            Error = std::move(message);
        }
    }

  private:
    bool RawRead(std::uint32_t address, void* destination,
                 std::size_t size) const {
        if (Host.ResolveRead != nullptr) {
            const void* source =
                Host.ResolveRead(Host.Context, address, size);
            if (source != nullptr) {
                std::memcpy(destination, source, size);
                return true;
            }
        }
        return Host.ReadMemory != nullptr &&
               Host.ReadMemory(Host.Context, address, destination, size) != 0;
    }

    bool RawWrite(std::uint32_t address, const void* source,
                  std::size_t size) const {
        return Host.WriteMemory != nullptr &&
               Host.WriteMemory(Host.Context, address, source, size) != 0;
    }

    const Oot3dSourceOverlayHostApi& Host;
    Oot3dSourceOverlayGuestState CallerState{};
    std::uint32_t BlockBudget = 1U;
    Oot3dSourceCutsceneProcessCommands::
        CutsceneProcessCommandsLiterals Literals;
    std::uint32_t FailureDetail = 0U;
    bool Failed = false;
    std::string Error;
};

Bridge& ActiveBridge() {
    if (gActiveBridge == nullptr) {
        throw std::runtime_error(
            "Cutscene_ProcessCommands source bridge is not active");
    }
    return *gActiveBridge;
}

} // namespace

int Execute(std::uint32_t entry, Oot3dSourceOverlayGuestState* state,
            Oot3dSourceOverlayExecutionResult* result,
            const Oot3dSourceOverlayHostApi* host,
            std::uint32_t blockBudget) {
    if (entry != kProcessEntry || state == nullptr || result == nullptr ||
        host == nullptr || host->StructSize < sizeof(*host)) {
        return 0;
    }

    ++gStats.OwnerCalls;
    const std::uint32_t playAddress = state->Registers[0];
    const std::uint32_t cutsceneContextAddress = state->Registers[1];
    const std::uint32_t scriptAddress = state->Registers[2];
    if ((state->Registers[13] & 7U) != 0U ||
        state->Registers[13] < kOwnerCallFrameSize ||
        !RangeHas(*host, playAddress, kMinimumPlaySize,
                  OOT3D_SOURCE_OVERLAY_MEMORY_WRITE) ||
        !RangeHas(*host, cutsceneContextAddress,
                  kMinimumCutsceneContextSize,
                  OOT3D_SOURCE_OVERLAY_MEMORY_WRITE) ||
        !RangeHas(*host, scriptAddress, 0x10U,
                  OOT3D_SOURCE_OVERLAY_MEMORY_READ)) {
        return 0;
    }

    Bridge bridge(*host, *state, blockBudget);
    if (!bridge.LoadLiterals()) {
        return 0;
    }

    try {
        ActiveBridgeScope scope(bridge);
        Oot3dSourceCutsceneProcessCommands::Cutscene_ProcessCommands(
            playAddress, cutsceneContextAddress, scriptAddress);
    } catch (const std::exception& exception) {
        bridge.Fail(std::string("Cutscene_ProcessCommands exception: ") +
                    exception.what());
    } catch (...) {
        bridge.Fail("Cutscene_ProcessCommands source owner exception");
    }

    if (bridge.HasFailed()) {
        ++gStats.Failures;
        if (host->Log != nullptr) {
            host->Log(host->Context, 2U,
                      bridge.FailureMessage().c_str());
        }
        SetResult(result, OOT3D_SOURCE_OVERLAY_MEMORY_FAULT,
                  kProcessEntry,
                  bridge.FailureAddress() != 0U
                      ? bridge.FailureAddress()
                      : kProcessEntry);
        return 1;
    }

    state->Fpscr = bridge.Fpscr();
    state->Registers[15] = state->Registers[14];
    SetResult(result, OOT3D_SOURCE_OVERLAY_BRANCH,
              state->Registers[15], kProcessEntry);
    ++gStats.OwnerHandled;
    return 1;
}

Stats GetStats() {
    return gStats;
}

} // namespace Oot3dSourceOverlay::CutsceneCommands

namespace Oot3dSourceCutsceneProcessCommands {

const CutsceneProcessCommandsLiterals& ActiveLiterals() {
    return Oot3dSourceOverlay::CutsceneCommands::ActiveBridge()
        .ActiveLiterals();
}

void ReadGuestMemory(
    std::uint32_t address, void* destination, std::size_t size) {
    Oot3dSourceOverlay::CutsceneCommands::ActiveBridge().Read(
        address, destination, size);
}

void WriteGuestMemory(
    std::uint32_t address, const void* source, std::size_t size) {
    Oot3dSourceOverlay::CutsceneCommands::ActiveBridge().Write(
        address, source, size);
}

std::uint32_t InvokeGuestCoreWords(
    std::uint32_t entryAddress,
    std::span<const std::uint32_t> arguments) {
    return Oot3dSourceOverlay::CutsceneCommands::ActiveBridge()
        .InvokeCore(entryAddress, arguments);
}

std::uint32_t InvokeGuestHardFloatWords(
    std::uint32_t entryAddress,
    std::span<const std::uint32_t> coreArguments,
    std::span<const std::uint32_t> vfpArguments) {
    return Oot3dSourceOverlay::CutsceneCommands::ActiveBridge()
        .InvokeHardFloat(entryAddress, coreArguments, vfpArguments);
}

std::uint32_t InvokeGuestCoreWordsWithScratch(
    std::uint32_t entryAddress,
    std::span<const std::uint32_t> arguments,
    std::span<const GuestScratchArgument> scratchArguments) {
    return Oot3dSourceOverlay::CutsceneCommands::ActiveBridge()
        .InvokeCoreWithScratch(entryAddress, arguments, scratchArguments);
}

float VectorSignedToFloat(std::int32_t value, std::uint32_t mode) {
    return Oot3dSourceOverlay::CutsceneCommands::ActiveBridge()
        .ConvertSigned(value, mode);
}

float VectorUnsignedToFloat(std::uint32_t value, std::uint32_t mode) {
    return Oot3dSourceOverlay::CutsceneCommands::ActiveBridge()
        .ConvertUnsigned(value, mode);
}

std::uint32_t VectorFloatToUnsigned(float value, std::uint32_t mode) {
    return Oot3dSourceOverlay::CutsceneCommands::ActiveBridge()
        .ConvertFloatToUnsigned(value, mode);
}

void FUN_0033cb90(
    std::uint32_t* cameraTrackState, std::uint32_t value,
    std::uint32_t contextAddress) {
    const std::array<std::uint32_t, 3> arguments{
        0U, value, contextAddress};
    const GuestScratchArgument scratch{
        0U, cameraTrackState, sizeof(std::uint32_t) * 3U, true, false};
    InvokeGuestCoreWordsWithScratch(
        Oot3dSourceOverlay::CutsceneCommands::kBuildCameraVector,
        arguments, std::span<const GuestScratchArgument>(&scratch, 1U));
}

std::uint32_t Camera_SetParam(
    std::uint32_t cameraAddress, std::uint32_t parameter, float* value) {
    const std::array<std::uint32_t, 3> arguments{
        cameraAddress, parameter, 0U};
    const GuestScratchArgument scratch{
        2U, value, sizeof(float), true, false};
    return InvokeGuestCoreWordsWithScratch(
        Oot3dSourceOverlay::CutsceneCommands::kCameraSetParam,
        arguments, std::span<const GuestScratchArgument>(&scratch, 1U));
}

std::uint32_t Gameplay_CameraSetAtEye(
    std::uint32_t playAddress, std::int32_t cameraId,
    std::uint32_t atAddress, std::uint32_t* eyeVector) {
    const std::array<std::uint32_t, 4> arguments{
        playAddress, static_cast<std::uint32_t>(cameraId), atAddress, 0U};
    const GuestScratchArgument scratch{
        3U, eyeVector, sizeof(std::uint32_t) * 3U, true, false};
    return InvokeGuestCoreWordsWithScratch(
        Oot3dSourceOverlay::CutsceneCommands::kGameplayCameraSetAtEye,
        arguments, std::span<const GuestScratchArgument>(&scratch, 1U));
}

std::uint32_t Gameplay_CameraSetAtEye(
    std::uint32_t playAddress, std::int32_t cameraId,
    std::uint32_t* atVector, std::uint32_t* eyeVector) {
    const std::array<std::uint32_t, 4> arguments{
        playAddress, static_cast<std::uint32_t>(cameraId), 0U, 0U};
    const std::array<GuestScratchArgument, 2> scratch{{
        {2U, atVector, sizeof(std::uint32_t) * 3U, true, false},
        {3U, eyeVector, sizeof(std::uint32_t) * 3U, true, false},
    }};
    return InvokeGuestCoreWordsWithScratch(
        Oot3dSourceOverlay::CutsceneCommands::kGameplayCameraSetAtEye,
        arguments, scratch);
}

std::uint32_t FUN_00354220(
    std::uint32_t playAddress, std::int32_t cameraId, float viewAngle) {
    const std::array<std::uint32_t, 2> core{
        playAddress, static_cast<std::uint32_t>(cameraId)};
    const std::array<std::uint32_t, 1> vfp{
        std::bit_cast<std::uint32_t>(viewAngle)};
    return InvokeGuestHardFloatWords(
        Oot3dSourceOverlay::CutsceneCommands::kSetCameraViewAngle,
        core, vfp);
}

float FUN_00361490(
    std::int32_t endFrame, std::int32_t startFrame, std::int32_t frame) {
    const std::array<std::uint32_t, 3> arguments{
        static_cast<std::uint32_t>(endFrame),
        static_cast<std::uint32_t>(startFrame),
        static_cast<std::uint32_t>(frame)};
    return Oot3dSourceOverlay::CutsceneCommands::ActiveBridge()
        .InvokeFloatResult(
            Oot3dSourceOverlay::CutsceneCommands::kCutsceneFrameLerp,
            arguments);
}

void TitleCard_InitPlaceName(
    std::int32_t play, std::int32_t titleContext, std::int32_t texture,
    std::int32_t x, std::int32_t y, std::int32_t width,
    std::int32_t height, std::int32_t delay, std::int32_t unknown,
    float scale) {
    const std::array<std::uint32_t, 9> core{
        static_cast<std::uint32_t>(play),
        static_cast<std::uint32_t>(titleContext),
        static_cast<std::uint32_t>(texture), static_cast<std::uint32_t>(x),
        static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height), static_cast<std::uint32_t>(delay),
        static_cast<std::uint32_t>(unknown)};
    const std::array<std::uint32_t, 1> vfp{
        std::bit_cast<std::uint32_t>(scale)};
    InvokeGuestHardFloatWords(
        Oot3dSourceOverlay::CutsceneCommands::kTitleCardInitPlaceName,
        core, vfp);
}

void InvokeDynamicCallback(
    GuestPtr<undefined4> callbackObject, GuestPtr<uint> command) {
    std::uint32_t vtableAddress = 0U;
    std::uint32_t entryAddress = 0U;
    ReadGuestMemory(callbackObject.Address(), &vtableAddress,
                    sizeof(vtableAddress));
    if (vtableAddress == 0U) {
        throw std::runtime_error(
            "Cutscene_ProcessCommands callback vtable is zero");
    }
    ReadGuestMemory(vtableAddress, &entryAddress, sizeof(entryAddress));
    if (entryAddress == 0U) {
        throw std::runtime_error(
            "Cutscene_ProcessCommands callback entry is zero");
    }
    const std::array<std::uint32_t, 2> arguments{
        callbackObject.Address(), command.Address()};
    Oot3dSourceOverlay::CutsceneCommands::ActiveBridge().InvokeDynamic(
        entryAddress, arguments);
}

} // namespace Oot3dSourceCutsceneProcessCommands
