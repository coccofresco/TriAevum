#include "oot3d_native_a32_process.h"
#include "oot3d_source_cutscene_process_commands_runtime.h"
#include "oot3d_a32_generated.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kOwnerEntry = 0x002C5BA0U;
constexpr uint32_t kOwnerReturn = 0x0BADF20CU;
constexpr uint32_t kOwnerTextBase = 0x002C4000U;
constexpr uint32_t kOwnerTextSize = 0x00005000U;
constexpr uint32_t kDependencyTextBase = 0x00300000U;
constexpr uint32_t kDependencyTextSize = 0x00080000U;
constexpr uint32_t kMemcpyTextBase = 0x00470000U;
constexpr uint32_t kMemcpyTextSize = 0x00010000U;
constexpr uint32_t kExternalDependencyTextBase = 0x00490000U;
constexpr uint32_t kExternalDependencyTextSize = 0x00010000U;
constexpr uint32_t kRam = 0x10000000U;
constexpr uint32_t kRamSize = 0x00020000U;
constexpr uint32_t kPlay = kRam;
constexpr uint32_t kCutsceneContext = kRam + 0x10000U;
constexpr uint32_t kScript = kRam + 0x11000U;
constexpr uint32_t kProbeState = kRam + 0x12000U;
constexpr uint32_t kCameraGlobal = kRam + 0x12100U;
constexpr uint32_t kCamera = kRam + 0x13000U;
constexpr uint32_t kAtRecord = kRam + 0x14000U;
constexpr uint32_t kRendererGuard = kRam + 0x15000U;
constexpr uint32_t kFrameScaleSlot = kRam + 0x15010U;
constexpr uint32_t kFrameScaleObject = kRam + 0x15100U;
constexpr uint32_t kTitleGlobal = kRam + 0x16000U;
constexpr uint32_t kTexture = kRam + 0x17000U;
constexpr uint32_t kCallbackObject = kRam + 0x18000U;
constexpr uint32_t kCallbackVtable = kRam + 0x18100U;
constexpr uint32_t kDynamicWrite = kRam + 0x19000U;
constexpr uint32_t kStack = 0x20000000U;
constexpr uint32_t kTls = 0x20010000U;

constexpr uint32_t kGetCutsceneGate = 0x00307650U;
constexpr uint32_t kGameplayCameraChangeSetting = 0x002C41C4U;
constexpr uint32_t kGameplayChangeCameraStatus = 0x00320D7CU;
constexpr uint32_t kTitleCardInitPlaceName = 0x003471C8U;
constexpr uint32_t kCameraSetParam = 0x003521F0U;
constexpr uint32_t kSetCameraViewAngle = 0x00354220U;
constexpr uint32_t kCutsceneFrameLerp = 0x00361490U;
constexpr uint32_t kSetEnvironmentRange = 0x003665FCU;
constexpr uint32_t kGameplayCameraSetAtEye = 0x00367B14U;
constexpr uint32_t kGameplayGetCamera = 0x0036C5BCU;
constexpr uint32_t kQuakeSetCountdown = 0x0036F628U;
constexpr uint32_t kQuakeSetQuakeValues = 0x0036F6B0U;
constexpr uint32_t kQuakeSetSpeed = 0x0036F7C0U;
constexpr uint32_t kQuakeAdd = 0x0036F848U;
constexpr uint32_t kZarGetCtxbByIndex = 0x00372C90U;
constexpr uint32_t kAudioPlaySoundGeneral = 0x0037547CU;
constexpr uint32_t kDynamicCallback = 0x00350000U;
constexpr uint32_t kMemcpy = 0x00470778U;
constexpr uint32_t kSetGameplayMode = 0x00490E1CU;
constexpr uint32_t kInitialFpscr = 0x03C00010U;
constexpr uint32_t kDynamicWriteValue = 0xD1A60092U;

enum class ScenarioKind {
    Empty,
    GenericStride,
    ServiceMatrix,
    EnvironmentFlagAbi,
    CameraScratch,
    DynamicCallback,
};

struct Scenario {
    const char* Name;
    ScenarioKind Kind;
    uint16_t Frame;
    uint32_t ExpectedMemcpyCalls;
    uint64_t ExpectedNestedCalls;
    uint64_t ExpectedDirectCalls;
    uint64_t ExpectedDynamicCalls;
    uint64_t ExpectedScratchCalls;
    uint64_t ExpectedHardFloatCalls;
    uint64_t ExpectedConversions;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T>
void Store(std::span<uint8_t> bytes, size_t offset, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    Expect(offset <= bytes.size() &&
               sizeof(T) <= bytes.size() - offset,
           "test payload write is out of range");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void WriteMappedWord(
    std::vector<uint8_t>& bytes, uint32_t base,
    uint32_t address, uint32_t value) {
    Expect(address >= base &&
               static_cast<uint64_t>(address - base) +
                       sizeof(value) <=
                   bytes.size(),
           "literal lies outside mapped owner text");
    std::memcpy(
        bytes.data() + (address - base), &value, sizeof(value));
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

struct ObservedCall {
    uint32_t Entry = 0U;
    std::vector<uint32_t> Values;

    bool operator==(const ObservedCall&) const = default;
};

struct DependencyProbe {
    std::vector<ObservedCall> Calls;
    uint32_t MemcpyCalls = 0U;
    std::string Error;
};

bool ReadWords(
    Oot3dNativeGame::NativeA32Memory& memory,
    uint32_t address, size_t count, std::vector<uint32_t>* values) {
    if (values == nullptr) {
        return false;
    }
    for (size_t index = 0U; index < count; ++index) {
        uint32_t value = 0U;
        if (!memory.Read32(
                address + static_cast<uint32_t>(
                              index * sizeof(uint32_t)),
                &value)) {
            return false;
        }
        values->push_back(value);
    }
    return true;
}

void RecordCore(
    DependencyProbe& probe, uint32_t entry,
    const oot3d::recomp::a32::GuestState& state, size_t count) {
    ObservedCall call;
    call.Entry = entry;
    call.Values.insert(
        call.Values.end(), state.r.begin(),
        state.r.begin() + static_cast<std::ptrdiff_t>(count));
    probe.Calls.push_back(std::move(call));
}

bool HandleNativeFunction(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result, void* user) {
    if (result == nullptr || user == nullptr) {
        return false;
    }
    auto& probe = *static_cast<DependencyProbe*>(user);
    auto& processMemory =
        static_cast<Oot3dNativeGame::NativeA32Memory&>(memory);

    switch (pc) {
    case kMemcpy: {
        ++probe.MemcpyCalls;
        const uint32_t destination = state.r[0];
        const uint32_t source = state.r[1];
        const uint32_t size = state.r[2];
        std::vector<uint8_t> bytes(size);
        if (!processMemory.ReadBytes(source, bytes) ||
            !processMemory.WriteBytes(destination, bytes)) {
            probe.Error = "guest memcpy failed";
            return false;
        }
        state.r[0] = destination;
        ReturnToLinkRegister(state, result);
        return true;
    }
    case kGetCutsceneGate:
        RecordCore(probe, pc, state, 0U);
        state.r[0] = kProbeState;
        break;
    case kCutsceneFrameLerp:
        RecordCore(probe, pc, state, 3U);
        state.vfp[0] = std::bit_cast<uint32_t>(0.25F);
        break;
    case kSetEnvironmentRange:
        RecordCore(probe, pc, state, 3U);
        break;
    case kSetGameplayMode:
        RecordCore(probe, pc, state, 2U);
        break;
    case kQuakeAdd:
        RecordCore(probe, pc, state, 2U);
        state.r[0] = 7U;
        break;
    case kQuakeSetSpeed:
    case kQuakeSetCountdown:
    case kGameplayGetCamera:
        RecordCore(probe, pc, state, 2U);
        if (pc == kGameplayGetCamera) {
            state.r[0] = kCamera;
        }
        break;
    case kGameplayChangeCameraStatus:
    case kGameplayCameraChangeSetting:
        RecordCore(probe, pc, state, 3U);
        break;
    case kQuakeSetQuakeValues: {
        ObservedCall call{pc, {state.r[0], state.r[1],
                               state.r[2], state.r[3]}};
        if (!ReadWords(
                processMemory, state.r[13], 1U, &call.Values)) {
            probe.Error = "read Quake_SetQuakeValues stack";
            return false;
        }
        probe.Calls.push_back(std::move(call));
        break;
    }
    case kAudioPlaySoundGeneral: {
        ObservedCall call{pc, {state.r[0], state.r[1],
                               state.r[2], state.r[3]}};
        if (!ReadWords(
                processMemory, state.r[13], 2U, &call.Values)) {
            probe.Error = "read Audio_PlaySoundGeneral stack";
            return false;
        }
        probe.Calls.push_back(std::move(call));
        break;
    }
    case kGameplayCameraSetAtEye: {
        ObservedCall call{pc, {state.r[0], state.r[1]}};
        if (!ReadWords(
                processMemory, state.r[2], 3U, &call.Values) ||
            !ReadWords(
                processMemory, state.r[3], 3U, &call.Values)) {
            probe.Error = "read Gameplay_CameraSetAtEye vectors";
            return false;
        }
        probe.Calls.push_back(std::move(call));
        state.r[0] = 1U;
        break;
    }
    case kCameraSetParam: {
        ObservedCall call{pc, {state.r[0], state.r[1]}};
        if (!ReadWords(
                processMemory, state.r[2], 1U, &call.Values)) {
            probe.Error = "read Camera_SetParam value";
            return false;
        }
        probe.Calls.push_back(std::move(call));
        state.r[0] = 1U;
        break;
    }
    case kSetCameraViewAngle:
        probe.Calls.push_back(
            {pc, {state.r[0], state.r[1], state.vfp[0]}});
        state.r[0] = 1U;
        break;
    case kZarGetCtxbByIndex:
        RecordCore(probe, pc, state, 1U);
        state.r[0] = kTexture;
        break;
    case kTitleCardInitPlaceName: {
        ObservedCall call{pc, {state.r[0], state.r[1],
                               state.r[2], state.r[3]}};
        if (!ReadWords(
                processMemory, state.r[13], 5U, &call.Values)) {
            probe.Error = "read TitleCard_InitPlaceName stack";
            return false;
        }
        call.Values.push_back(state.vfp[0]);
        probe.Calls.push_back(std::move(call));
        break;
    }
    case kDynamicCallback:
        RecordCore(probe, pc, state, 2U);
        if (!processMemory.Write32(
                kDynamicWrite, kDynamicWriteValue)) {
            probe.Error = "dynamic callback write failed";
            return false;
        }
        break;
    default:
        probe.Error =
            "unhandled guest dependency " + std::to_string(pc);
        return false;
    }

    ReturnToLinkRegister(state, result);
    return true;
}

class TestHostServices final
    : public Oot3dNativeGame::NativeA32HostServices {
  public:
    Oot3dNativeGame::NativeA32HostResult HandleSvc(
        uint32_t immediate, oot3d::recomp::a32::GuestState&,
        Oot3dNativeGame::NativeA32Memory&,
        Oot3dNativeGame::NativeA32HostContext&) override {
        return {
            Oot3dNativeGame::NativeA32HostAction::Fault,
            std::nullopt,
            immediate,
            "unexpected Cutscene_ProcessCommands SVC",
        };
    }
};

struct ProcessFixture {
    ProcessFixture()
        : Process(oot3d::recomp::GetA32GeneratedRegistry(), Host),
          SourceRuntime(Process) {
    }

    TestHostServices Host;
    Oot3dNativeGame::NativeA32Process Process;
    Oot3dNativeGame::SourceCutsceneProcessCommandsRuntime SourceRuntime;
    DependencyProbe Probe;
    oot3d::recomp::a32::GuestState FinalState{};
};

std::vector<uint8_t> BuildScript(const Scenario& scenario) {
    std::vector<uint8_t> script(0x100U);
    const uint32_t commandCount =
        scenario.Kind == ScenarioKind::Empty
            ? 0U
            : scenario.Kind == ScenarioKind::GenericStride ? 2U : 1U;
    Store<uint32_t>(script, 8U, commandCount);
    Store<int32_t>(script, 12U, 100);

    if (scenario.Kind == ScenarioKind::GenericStride) {
        Store<uint32_t>(script, 16U, 0x0DU);
        Store<uint32_t>(script, 20U, 1U);
        Store<uint32_t>(script, 72U, 0U);
    } else if (scenario.Kind == ScenarioKind::ServiceMatrix) {
        Store<uint32_t>(script, 16U, 3U);
        Store<uint32_t>(script, 20U, 4U);
        constexpr std::array<uint16_t, 4> eventTypes{
            0x10U, 0x0DU, 0x26U, 0x0FU};
        for (size_t index = 0U; index < eventTypes.size(); ++index) {
            const size_t offset = 24U + index * 48U;
            Store<uint16_t>(script, offset, eventTypes[index]);
            Store<uint16_t>(script, offset + 2U, scenario.Frame);
            Store<uint16_t>(
                script, offset + 4U,
                static_cast<uint16_t>(scenario.Frame + 5U));
        }
    } else if (scenario.Kind == ScenarioKind::EnvironmentFlagAbi) {
        Store<uint32_t>(script, 16U, 3U);
        Store<uint32_t>(script, 20U, 1U);
        Store<uint16_t>(script, 24U, 2U);
        Store<uint16_t>(script, 26U, scenario.Frame);
        Store<uint16_t>(script, 28U, scenario.Frame);
    } else if (scenario.Kind == ScenarioKind::CameraScratch) {
        Store<uint32_t>(script, 16U, 7U);
        Store<uint16_t>(script, 22U, 1U);
        Store<uint16_t>(script, 24U, 10U);
        Store<float>(script, 32U, 60.0F);
        Store<int16_t>(script, 36U, 4);
        Store<int16_t>(script, 38U, 5);
        Store<int16_t>(script, 40U, 6);
    } else if (scenario.Kind == ScenarioKind::DynamicCallback) {
        Store<uint32_t>(script, 16U, 0x92U);
        Store<uint32_t>(script, 20U, 1U);
        Store<uint16_t>(script, 26U, scenario.Frame);
    }
    return script;
}

void ConfigureProcess(
    ProcessFixture& fixture, const Scenario& scenario) {
    std::vector<uint8_t> ownerText(kOwnerTextSize);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C5D60U, 0x002C82ACU);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C5D6CU, 0x002C6080U);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C5D7CU, 0x002C76BCU);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C5D94U, 0x002C8288U);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C6104U, 0x002C61D4U);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C6130U, 0x002C63E8U);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C6138U, 0x002C6448U);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C613CU, 0x002C6598U);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C6194U, 0x002C6AECU);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C5FF4U,
        std::bit_cast<uint32_t>(300.0F));
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C6564U,
        kFrameScaleSlot);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C654CU,
        kProbeState + 0x20U);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C656CU,
        std::bit_cast<uint32_t>(0.0F));
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C6580U,
        kRendererGuard);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C6590U,
        kTitleGlobal);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C6594U,
        std::bit_cast<uint32_t>(1.25F));
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C6CB4U,
        std::bit_cast<uint32_t>(1.0F));
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C6CB8U,
        std::bit_cast<uint32_t>(2.0F));
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C6CBCU,
        std::bit_cast<uint32_t>(3.0F));
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C6CC0U,
        kCameraGlobal);
    WriteMappedWord(
        ownerText, kOwnerTextBase, 0x002C7A5CU,
        std::bit_cast<uint32_t>(1.40625F));

    std::string error;
    const auto map = [&](const Oot3dNativeGame::NativeA32MemoryRegionConfig&
                             config) {
        Expect(fixture.Process.MapRegion(config, &error),
               "map " + config.Name + ": " + error);
    };
    map({"cutscene_process_text", kOwnerTextBase, ownerText.size(),
         false, true, ownerText});
    map({"cutscene_dependency_text", kDependencyTextBase,
         kDependencyTextSize, false, true, {}});
    map({"cutscene_memcpy_text", kMemcpyTextBase, kMemcpyTextSize,
         false, true, {}});
    map({"cutscene_external_dependency_text",
         kExternalDependencyTextBase, kExternalDependencyTextSize,
         false, true, {}});
    map({"cutscene_ram", kRam, kRamSize, true, false, {}});
    Expect(fixture.Process.CreatePrimaryThread(
               {kOwnerEntry, kStack, 0x8000U, kTls, 0x1000U, 0U,
                0U, 0x10U, kInitialFpscr},
               &error),
           "create Cutscene_ProcessCommands test thread: " + error);

    const auto script = BuildScript(scenario);
    const std::array<uint32_t, 3> cameraAt{
        std::bit_cast<uint32_t>(10.0F),
        std::bit_cast<uint32_t>(20.0F),
        std::bit_cast<uint32_t>(30.0F)};
    std::array<uint8_t, 16> atRecord{};
    atRecord[1] = 40U;
    Store<float>(atRecord, 4U, 55.0F);
    Store<int16_t>(atRecord, 8U, 1);
    Store<int16_t>(atRecord, 10U, 2);
    Store<int16_t>(atRecord, 12U, 3);

    auto& memory = fixture.Process.Memory();
    Expect(
        memory.WriteBytes(kScript, script) &&
            memory.Write16(
                kCutsceneContext + 0x20U, scenario.Frame) &&
            memory.Write8(kCutsceneContext + 8U, 0U) &&
            memory.Write8(kProbeState + 0x0CU, 0U) &&
            memory.Write8(kCameraGlobal + 1U, 1U) &&
            memory.Write16(kCameraGlobal + 8U, 2U) &&
            memory.Write32(kPlay + 0x20ACU, kCamera) &&
            memory.WriteBytes(
                kCamera + 0x28U,
                std::span<const uint8_t>(
                    reinterpret_cast<const uint8_t*>(
                        cameraAt.data()),
                    sizeof(cameraAt))) &&
            memory.Write32(kRendererGuard, 1U) &&
            memory.Write32(kFrameScaleSlot, kFrameScaleObject) &&
            memory.Write16(kFrameScaleObject + 0x110U, 30U) &&
            memory.Write32(kTitleGlobal + 0xF3CU, 0U) &&
            memory.Write32(kPlay + 0x114U, 1U) &&
            memory.WriteBytes(kAtRecord, atRecord) &&
            memory.Write32(
                kCutsceneContext + 0x38U, kAtRecord) &&
            memory.Write8(
                kCutsceneContext + 0x34U,
                scenario.Kind == ScenarioKind::CameraScratch
                    ? 1U
                    : 0U) &&
            memory.Write32(
                kCallbackObject, kCallbackVtable) &&
            memory.Write32(
                kCallbackVtable, kDynamicCallback) &&
            memory.Write32(
                kCutsceneContext + 0x27CU,
                scenario.Kind == ScenarioKind::DynamicCallback
                    ? kCallbackObject
                    : 0U),
        "seed Cutscene_ProcessCommands state");

    fixture.Process.SetNativeFunctionCallback(
        &HandleNativeFunction, &fixture.Probe,
        {kGetCutsceneGate, kGameplayCameraChangeSetting,
         kGameplayChangeCameraStatus, kTitleCardInitPlaceName,
         kCameraSetParam, kSetCameraViewAngle,
         kCutsceneFrameLerp, kSetEnvironmentRange,
         kGameplayCameraSetAtEye,
         kGameplayGetCamera, kQuakeSetCountdown,
         kQuakeSetQuakeValues, kQuakeSetSpeed, kQuakeAdd,
         kZarGetCtxbByIndex, kAudioPlaySoundGeneral,
         kSetGameplayMode, kDynamicCallback, kMemcpy});
}

oot3d::recomp::a32::GuestState InitialOwnerState(
    const ProcessFixture& fixture) {
    auto state = fixture.Process.PrimaryThreadState();
    state.r[0] = kPlay;
    state.r[1] = kCutsceneContext;
    state.r[2] = kScript;
    for (size_t index = 4U; index <= 11U; ++index) {
        state.r[index] =
            0x44000000U + static_cast<uint32_t>(index);
    }
    for (size_t index = 16U; index < 32U; ++index) {
        state.vfp[index] =
            0x55000000U + static_cast<uint32_t>(index);
    }
    return state;
}

void RunReferenceOwner(
    ProcessFixture& fixture, const char* scenarioName) {
    auto state = InitialOwnerState(fixture);
    std::string error;
    const bool invoked = fixture.Process.InvokeFunctionWithState(
        kOwnerEntry, state, kOwnerReturn, &error);
    const std::string lastCall =
        fixture.Probe.Calls.empty()
            ? "none"
            : std::to_string(fixture.Probe.Calls.back().Entry);
    Expect(invoked,
           std::string(scenarioName) +
               ": original Cutscene_ProcessCommands failed: " + error +
               " last_dependency=" + lastCall +
               (fixture.Probe.Error.empty()
                    ? std::string{}
                    : " (" + fixture.Probe.Error + ")"));
    fixture.FinalState = state;
}

Oot3dNativeGame::SourceCutsceneProcessCommandsStats
RunSourceOwner(ProcessFixture& fixture) {
    auto state = InitialOwnerState(fixture);
    state.r[14] = kOwnerReturn;
    oot3d::recomp::a32::ExecutionResult result;
    uint32_t blocksConsumed = 0U;
    fixture.SourceRuntime.ResetStats();
    Expect(fixture.SourceRuntime.Execute(
               kOwnerEntry, state, fixture.Process.Memory(),
               &result, &blocksConsumed),
           "source Cutscene_ProcessCommands was not selected");
    Expect(
        result.kind == oot3d::recomp::a32::ExitKind::Branch &&
            result.pc == kOwnerReturn &&
            state.r[15] == kOwnerReturn &&
            blocksConsumed == 1U,
        "source Cutscene_ProcessCommands did not return: " +
            fixture.SourceRuntime.LastError());
    fixture.FinalState = state;
    return fixture.SourceRuntime.Stats();
}

void CompareRange(
    const Oot3dNativeGame::NativeA32Memory& reference,
    const Oot3dNativeGame::NativeA32Memory& source,
    uint32_t address, size_t size, const char* name) {
    std::vector<uint8_t> referenceBytes(size);
    std::vector<uint8_t> sourceBytes(size);
    Expect(reference.ReadBytes(address, referenceBytes) &&
               source.ReadBytes(address, sourceBytes),
           std::string("read differential range ") + name);
    if (referenceBytes == sourceBytes) {
        return;
    }
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

void CompareOutcome(
    const ProcessFixture& reference, const ProcessFixture& source,
    const Scenario& scenario,
    const Oot3dNativeGame::SourceCutsceneProcessCommandsStats& stats) {
    CompareRange(
        reference.Process.Memory(), source.Process.Memory(),
        kRam, kRamSize, "cutscene RAM");
    Expect(
        reference.Probe.Calls == source.Probe.Calls,
        std::string(scenario.Name) +
            ": dependency ABI or order differs");
    Expect(
        reference.Probe.MemcpyCalls ==
                scenario.ExpectedMemcpyCalls &&
            source.Probe.MemcpyCalls == 0U,
        std::string(scenario.Name) +
            ": memcpy lowering differs");
    Expect(
        reference.Probe.Error.empty() && source.Probe.Error.empty(),
        std::string(scenario.Name) +
            ": dependency callback failed");
    Expect(
        reference.FinalState.fpscr == source.FinalState.fpscr,
        std::string(scenario.Name) + ": FPSCR differs");
    for (size_t index = 4U; index <= 11U; ++index) {
        Expect(
            reference.FinalState.r[index] ==
                source.FinalState.r[index],
            std::string(scenario.Name) +
                ": callee-saved core register differs");
    }
    for (size_t index = 16U; index < 32U; ++index) {
        Expect(
            reference.FinalState.vfp[index] ==
                source.FinalState.vfp[index],
            std::string(scenario.Name) +
                ": callee-saved VFP register differs");
    }
    Expect(
        reference.FinalState.r[13] == source.FinalState.r[13] &&
            reference.FinalState.r[15] == source.FinalState.r[15] &&
            reference.FinalState.thread_pointer ==
                source.FinalState.thread_pointer,
        std::string(scenario.Name) +
            ": owner return machine state differs");
    Expect(
        stats.OwnerCalls == 1U &&
            stats.NestedGuestCalls ==
                scenario.ExpectedNestedCalls &&
            stats.DirectDependencyCalls ==
                scenario.ExpectedDirectCalls &&
            stats.DynamicCallbackCalls ==
                scenario.ExpectedDynamicCalls &&
            stats.ScratchCalls ==
                scenario.ExpectedScratchCalls &&
            stats.HardFloatCalls ==
                scenario.ExpectedHardFloatCalls &&
            stats.ConversionOperations ==
                scenario.ExpectedConversions &&
            stats.GuestReadCalls > 0U &&
            stats.GuestWriteCalls > 0U &&
            stats.Failures == 0U,
        std::string(scenario.Name) +
            ": source owner statistics differ: nested=" +
            std::to_string(stats.NestedGuestCalls) +
            " direct=" +
            std::to_string(stats.DirectDependencyCalls) +
            " dynamic=" +
            std::to_string(stats.DynamicCallbackCalls) +
            " scratch=" + std::to_string(stats.ScratchCalls) +
            " hard_float=" +
            std::to_string(stats.HardFloatCalls) +
            " conversions=" +
            std::to_string(stats.ConversionOperations));
}

void RunScenario(const Scenario& scenario) {
    ProcessFixture reference;
    ProcessFixture source;
    ConfigureProcess(reference, scenario);
    ConfigureProcess(source, scenario);
    const auto stats = RunSourceOwner(source);
    Expect(
        source.SourceRuntime.LastError().empty(),
        std::string(scenario.Name) + ": source bridge failed: " +
            source.SourceRuntime.LastError());
    RunReferenceOwner(reference, scenario.Name);
    CompareOutcome(reference, source, scenario, stats);
}

void TestCutsceneProcessCommandsDifferential() {
    constexpr std::array scenarios{
        Scenario{"empty", ScenarioKind::Empty, 0U, 5U,
                 1U, 1U, 0U, 0U, 0U, 0U},
        Scenario{"generic_stride", ScenarioKind::GenericStride,
                 0U, 8U, 1U, 1U, 0U, 0U, 0U, 0U},
        Scenario{"service_matrix", ScenarioKind::ServiceMatrix,
                 5U, 7U, 13U, 13U, 0U, 1U, 5U, 1U},
        Scenario{"environment_flag_abi",
                 ScenarioKind::EnvironmentFlagAbi,
                 15U, 7U, 4U, 4U, 0U, 0U, 1U, 0U},
        Scenario{"camera_scratch", ScenarioKind::CameraScratch,
                 2U, 6U, 8U, 8U, 0U, 2U, 1U, 7U},
        Scenario{"dynamic_callback", ScenarioKind::DynamicCallback,
                 5U, 7U, 2U, 1U, 1U, 0U, 0U, 0U},
    };
    for (const auto& scenario : scenarios) {
        RunScenario(scenario);
    }
}

} // namespace

int main() {
    try {
        TestCutsceneProcessCommandsDifferential();
        std::cout
            << "oot3d_source_cutscene_process_commands_tests: ok\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr
            << "oot3d_source_cutscene_process_commands_tests: "
            << exception.what() << '\n';
        return 1;
    }
}
