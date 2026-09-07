#include "oot3d_native_a32_process.h"
#include "oot3d_source_camera_update_runtime.h"
#include "oot3d_a32_generated.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kOwnerEntry = 0x002D84C4U;
constexpr uint32_t kOwnerReturn = 0x0BADF30CU;
constexpr uint32_t kCodeBase = 0x00100000U;
constexpr uint32_t kLowTextBase = 0x002C0000U;
constexpr uint32_t kLowTextSize = 0x00030000U;
constexpr uint32_t kMidTextBase = 0x00300000U;
constexpr uint32_t kMidTextSize = 0x00080000U;
constexpr uint32_t kHighTextBase = 0x00470000U;
constexpr uint32_t kHighTextSize = 0x00010000U;
constexpr uint32_t kRam = 0x10000000U;
constexpr uint32_t kRamSize = 0x00040000U;
constexpr uint32_t kCamera = kRam + 0x0000U;
constexpr uint32_t kResult = kRam + 0x1000U;
constexpr uint32_t kPlay = kRam + 0x2000U;
constexpr uint32_t kActorContext = kRam + 0xB000U;
constexpr uint32_t kPlayer = kRam + 0xC000U;
constexpr uint32_t kMainPlayer = kRam + 0xD000U;
constexpr uint32_t kCameraState = kRam + 0xE000U;
constexpr uint32_t kPlayerStateCell = kRam + 0xF000U;
constexpr uint32_t kPlayerState = kRam + 0xF100U;
constexpr uint32_t kCameraSettings = kRam + 0x10000U;
constexpr uint32_t kCameraModes = kRam + 0x10100U;
constexpr uint32_t kCameraFunctionTable = kRam + 0x10200U;
constexpr uint32_t kGameModeState = kRam + 0x11000U;
constexpr uint32_t kInputConfig = kRam + 0x12000U;
constexpr uint32_t kFloorPoly = kRam + 0x13000U;
constexpr uint32_t kDynamicMarker = kRam + 0x14000U;
constexpr uint32_t kStack = 0x20000000U;
constexpr uint32_t kTls = 0x20010000U;

constexpr uint32_t kActorGetWorldPosShapeRot = 0x00331764U;
constexpr uint32_t kBgCheckEntityRaycastFloor5 = 0x00316C18U;
constexpr uint32_t kCameraCalcUp = 0x002D052CU;
constexpr uint32_t kCameraDataResolver = 0x002D064CU;
constexpr uint32_t kCameraUpdateInterface = 0x00330D84U;
constexpr uint32_t kFlag22A0 = 0x0037571CU;
constexpr uint32_t kOlibClampMaxDist = 0x00355804U;
constexpr uint32_t kOlibVec3fDiffToVecSphGeo = 0x00372474U;
constexpr uint32_t kOlibVec3fDistXZ = 0x00367E60U;
constexpr uint32_t kPlayerGetHeight = 0x00367EF0U;
constexpr uint32_t kCopy18 = 0x00371738U;
constexpr uint32_t kQuakeAggregate = 0x004787E8U;
constexpr uint32_t kPlayerSpeedScale = 0x00479718U;
constexpr uint32_t kSetView = 0x002D77DCU;
constexpr uint32_t kSetViewScale = 0x004710F8U;
constexpr uint32_t kBgCameraDataIndex = 0x0047BFF8U;
constexpr uint32_t kDynamicCamera = 0x00350000U;
constexpr uint32_t kInitialFpscr = 0x03C00010U;

enum class ScenarioKind {
    Status0,
    Status1,
    DynamicStatus3,
    PlayerRaycastStatus1,
    NormalStatus7,
};

struct Scenario {
    const char* Name;
    ScenarioKind Kind;
    uint32_t ExpectedDynamicCalls;
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

std::vector<uint8_t> ReadBinary(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    Expect(
        static_cast<bool>(stream),
        "cannot open Camera_Update code.bin: " + path.string());
    const std::streampos end = stream.tellg();
    Expect(end >= 0, "cannot determine Camera_Update code.bin size");
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    stream.seekg(0, std::ios::beg);
    Expect(
        bytes.empty() ||
            static_cast<bool>(stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()))),
        "cannot read complete Camera_Update code.bin");
    return bytes;
}

std::vector<uint8_t> CopyCodeRegion(
    std::span<const uint8_t> codeBin, uint32_t address,
    uint32_t size) {
    Expect(address >= kCodeBase, "code region precedes code.bin base");
    const size_t offset = static_cast<size_t>(address - kCodeBase);
    Expect(
        offset <= codeBin.size() &&
            static_cast<size_t>(size) <= codeBin.size() - offset,
        "code region exceeds code.bin");
    return std::vector<uint8_t>(
        codeBin.begin() + static_cast<std::ptrdiff_t>(offset),
        codeBin.begin() +
            static_cast<std::ptrdiff_t>(offset + size));
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
    uint32_t CopyCalls = 0U;
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

bool ReadVec3fBits(
    Oot3dNativeGame::NativeA32Memory& memory,
    uint32_t address, std::vector<uint32_t>* values) {
    return ReadWords(memory, address, 3U, values);
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
    case kCopy18: {
        ++probe.CopyCalls;
        std::vector<uint8_t> bytes(state.r[2]);
        if (!processMemory.ReadBytes(state.r[1], bytes) ||
            !processMemory.WriteBytes(state.r[0], bytes)) {
            probe.Error = "copy helper failed";
            return false;
        }
        break;
    }
    case kDynamicCamera:
        probe.Calls.push_back({pc, {state.r[0]}});
        if (!processMemory.Write32(
                kDynamicMarker, 0xCA4E1234U)) {
            probe.Error = "dynamic camera marker write failed";
            return false;
        }
        break;
    case kActorGetWorldPosShapeRot: {
        probe.Calls.push_back({pc, {state.r[1]}});
        std::array<uint8_t, 20> transform{};
        Store<float>(transform, 0U, 10.0F);
        Store<float>(transform, 4U, 20.0F);
        Store<float>(transform, 8U, 30.0F);
        Store<int16_t>(transform, 12U, 100);
        Store<int16_t>(transform, 14U, 200);
        Store<int16_t>(transform, 16U, 300);
        if (!processMemory.WriteBytes(state.r[0], transform)) {
            probe.Error = "Actor_GetWorldPosShapeRot output failed";
            return false;
        }
        break;
    }
    case kOlibVec3fDistXZ: {
        ObservedCall call{pc, {}};
        if (!ReadVec3fBits(processMemory, state.r[0], &call.Values) ||
            !ReadVec3fBits(processMemory, state.r[1], &call.Values)) {
            probe.Error = "OLib_Vec3fDistXZ input read failed";
            return false;
        }
        probe.Calls.push_back(std::move(call));
        state.vfp[0] = std::bit_cast<uint32_t>(5.0F);
        break;
    }
    case kPlayerSpeedScale:
        probe.Calls.push_back({pc, {state.r[0]}});
        state.vfp[0] = std::bit_cast<uint32_t>(4.0F);
        break;
    case kOlibClampMaxDist:
        probe.Calls.push_back(
            {pc, {state.vfp[0], state.vfp[1]}});
        state.vfp[0] = std::bit_cast<uint32_t>(0.75F);
        break;
    case kPlayerGetHeight:
        probe.Calls.push_back({pc, {state.r[0]}});
        state.vfp[0] = std::bit_cast<uint32_t>(40.0F);
        break;
    case kBgCheckEntityRaycastFloor5: {
        uint32_t actor = 0U;
        uint32_t position = 0U;
        if (!processMemory.Read32(state.r[13], &actor) ||
            !processMemory.Read32(state.r[13] + 4U, &position)) {
            probe.Error = "raycast stack argument read failed";
            return false;
        }
        ObservedCall call{
            pc, {state.r[0], state.r[1], state.r[2], actor}};
        if (!ReadVec3fBits(
                processMemory, position, &call.Values)) {
            probe.Error = "raycast position read failed";
            return false;
        }
        probe.Calls.push_back(std::move(call));
        if (!processMemory.Write32(state.r[2], kFloorPoly) ||
            !processMemory.Write32(state.r[3], 0x32U)) {
            probe.Error = "raycast output write failed";
            return false;
        }
        state.vfp[0] = std::bit_cast<uint32_t>(18.0F);
        break;
    }
    case kBgCameraDataIndex:
        probe.Calls.push_back(
            {pc, {state.r[0], state.r[1], state.r[2]}});
        state.r[0] = 2U;
        break;
    case kCameraDataResolver:
        probe.Calls.push_back(
            {pc, {state.r[0], state.r[1], state.r[2]}});
        state.r[0] = 1U;
        break;
    case kCameraUpdateInterface:
        probe.Calls.push_back({pc, {state.r[0]}});
        break;
    case kFlag22A0:
        probe.Calls.push_back({pc, {state.r[0]}});
        state.r[0] = 0U;
        break;
    case kQuakeAggregate: {
        probe.Calls.push_back({pc, {state.r[0]}});
        const std::array<uint8_t, 32> shake{};
        if (!processMemory.WriteBytes(state.r[1], shake)) {
            probe.Error = "quake output write failed";
            return false;
        }
        state.r[0] = 0U;
        break;
    }
    case kOlibVec3fDiffToVecSphGeo: {
        ObservedCall call{pc, {}};
        if (!ReadVec3fBits(processMemory, state.r[1], &call.Values) ||
            !ReadVec3fBits(processMemory, state.r[2], &call.Values)) {
            probe.Error = "spherical vector input read failed";
            return false;
        }
        probe.Calls.push_back(std::move(call));
        std::array<uint8_t, 8> spherical{};
        Store<float>(spherical, 0U, 25.0F);
        Store<int16_t>(spherical, 4U, 1000);
        Store<int16_t>(spherical, 6U, 2000);
        if (!processMemory.WriteBytes(state.r[0], spherical)) {
            probe.Error = "spherical vector output write failed";
            return false;
        }
        break;
    }
    case kCameraCalcUp:
        probe.Calls.push_back(
            {pc, {state.r[0], state.r[1], state.r[2]}});
        state.vfp[0] = std::bit_cast<uint32_t>(0.0F);
        state.vfp[1] = std::bit_cast<uint32_t>(1.0F);
        state.vfp[2] = std::bit_cast<uint32_t>(0.0F);
        break;
    case kSetViewScale:
        probe.Calls.push_back(
            {pc, {state.r[0], state.vfp[0]}});
        break;
    case kSetView: {
        ObservedCall call{pc, {state.r[0]}};
        if (!ReadVec3fBits(processMemory, state.r[1], &call.Values) ||
            !ReadVec3fBits(processMemory, state.r[2], &call.Values) ||
            !ReadVec3fBits(processMemory, state.r[3], &call.Values)) {
            probe.Error = "view vector read failed";
            return false;
        }
        probe.Calls.push_back(std::move(call));
        break;
    }
    default:
        probe.Error =
            "unhandled Camera_Update dependency " +
            std::to_string(pc);
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
            "unexpected Camera_Update SVC",
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
    Oot3dNativeGame::SourceCameraUpdateRuntime SourceRuntime;
    DependencyProbe Probe;
    oot3d::recomp::a32::GuestState FinalState{};
};

int16_t CameraStatus(const Scenario& scenario) {
    switch (scenario.Kind) {
    case ScenarioKind::Status0:
        return 0;
    case ScenarioKind::Status1:
    case ScenarioKind::PlayerRaycastStatus1:
        return 1;
    case ScenarioKind::DynamicStatus3:
        return 3;
    case ScenarioKind::NormalStatus7:
        return 7;
    }
    return 0;
}

void ConfigureProcess(
    ProcessFixture& fixture, const Scenario& scenario,
    std::span<const uint8_t> codeBin) {
    std::vector<uint8_t> lowText =
        CopyCodeRegion(codeBin, kLowTextBase, kLowTextSize);
    std::vector<uint8_t> midText =
        CopyCodeRegion(codeBin, kMidTextBase, kMidTextSize);
    std::vector<uint8_t> highText =
        CopyCodeRegion(codeBin, kHighTextBase, kHighTextSize);
    uint32_t floorNormalScale = 0U;
    std::memcpy(
        &floorNormalScale,
        lowText.data() + (0x002D88C0U - kLowTextBase),
        sizeof(floorNormalScale));
    Expect(
        floorNormalScale == 0x38000100U,
        "Camera_Update code.bin identity/literal mismatch");
    WriteMappedWord(
        lowText, kLowTextBase, 0x002D88A8U, kCameraState);
    WriteMappedWord(
        lowText, kLowTextBase, 0x002D88B4U, kPlayerStateCell);
    WriteMappedWord(
        lowText, kLowTextBase, 0x002D88BCU, 0x0000172AU);
    WriteMappedWord(
        lowText, kLowTextBase, 0x002D88C8U, kCameraSettings);
    WriteMappedWord(
        lowText, kLowTextBase, 0x002D8C20U, kCameraFunctionTable);
    WriteMappedWord(
        lowText, kLowTextBase, 0x002D8C28U, kGameModeState);
    WriteMappedWord(
        lowText, kLowTextBase, 0x002D8C2CU, 0x0000225CU);
    WriteMappedWord(
        lowText, kLowTextBase, 0x002D8C30U, 0x00002262U);
    WriteMappedWord(
        lowText, kLowTextBase, 0x002D8C38U, 0x00007F12U);
    WriteMappedWord(
        lowText, kLowTextBase, 0x002D8C40U, kInputConfig);

    std::string error;
    const auto map = [&](const Oot3dNativeGame::NativeA32MemoryRegionConfig&
                             config) {
        Expect(fixture.Process.MapRegion(config, &error),
               "map " + config.Name + ": " + error);
    };
    map({"camera_low_text", kLowTextBase, lowText.size(),
         false, true, lowText});
    map({"camera_mid_text", kMidTextBase, kMidTextSize,
         false, true, midText});
    map({"camera_high_text", kHighTextBase, kHighTextSize,
         false, true, highText});
    map({"camera_ram", kRam, kRamSize, true, false, {}});
    Expect(fixture.Process.CreatePrimaryThread(
               {kOwnerEntry, kStack, 0x8000U, kTls, 0x1000U, 0U,
                0U, 0x10U, kInitialFpscr},
               &error),
           "create Camera_Update test thread: " + error);

    auto& memory = fixture.Process.Memory();
    const bool playerScenario =
        scenario.Kind == ScenarioKind::PlayerRaycastStatus1;
    Expect(
        memory.Write32(kCamera + 0xD4U, kPlay) &&
            memory.Write32(
                kCamera + 0xD8U,
                playerScenario ? kPlayer : 0U) &&
            memory.Write16(
                kCamera + 0x188U,
                static_cast<uint16_t>(CameraStatus(scenario))) &&
            memory.Write16(kCamera + 0x18AU, 0U) &&
            memory.Write16(kCamera + 0x18CU, 0U) &&
            memory.Write16(
                kCamera + 0x194U,
                playerScenario ? 5U : 0U) &&
            memory.Write16(kCamera + 0x19AU, 0U) &&
            memory.Write16(kCamera + 0x1A2U, 3000U) &&
            memory.Write16(kCamera + 0x17CU, 111U) &&
            memory.Write16(kCamera + 0x17EU, 222U) &&
            memory.Write16(kCamera + 0x180U, 333U) &&
            memory.Write32(kPlay + 0xA54U, kActorContext) &&
            memory.Write32(kActorContext + 0xD8U, kMainPlayer) &&
            memory.Write32(kPlayerStateCell, kPlayerState) &&
            memory.Write16(kPlayerState + 0x1A4U, 100U) &&
            memory.Write32(kCameraSettings + 4U, kCameraModes) &&
            memory.Write16(kCameraModes, 0U) &&
            memory.Write32(kCameraFunctionTable, kDynamicCamera) &&
            memory.Write32(kGameModeState + 0x4E4U, 0U) &&
            memory.Write8(kInputConfig + 0xFU, 1U) &&
            memory.Write32(kCameraState + 0x9CU, 0U) &&
            memory.Write32(kCameraState + 0x14U, 0x1234U) &&
            memory.Write32(
                kCamera + 0x80U, std::bit_cast<uint32_t>(1.0F)) &&
            memory.Write32(
                kCamera + 0x84U, std::bit_cast<uint32_t>(2.0F)) &&
            memory.Write32(
                kCamera + 0x88U, std::bit_cast<uint32_t>(3.0F)) &&
            memory.Write32(
                kCamera + 0x8CU, std::bit_cast<uint32_t>(4.0F)) &&
            memory.Write32(
                kCamera + 0x90U, std::bit_cast<uint32_t>(5.0F)) &&
            memory.Write32(
                kCamera + 0x94U, std::bit_cast<uint32_t>(6.0F)) &&
            memory.Write32(
                kCamera + 0x144U, std::bit_cast<uint32_t>(60.0F)) &&
            memory.Write32(
                kCamera + 0xD0U, std::bit_cast<uint32_t>(100.0F)) &&
            memory.Write32(
                kCamera + 0xDCU, std::bit_cast<uint32_t>(9.0F)) &&
            memory.Write32(
                kCamera + 0xE0U, std::bit_cast<uint32_t>(19.0F)) &&
            memory.Write32(
                kCamera + 0xE4U, std::bit_cast<uint32_t>(29.0F)) &&
            memory.Write16(kPlay + 0x104U, 0U) &&
            memory.Write16(kFloorPoly + 0xAU, 0U) &&
            memory.Write16(kFloorPoly + 0xCU, 0x7FFFU) &&
            memory.Write16(kFloorPoly + 0xEU, 0U),
        "seed Camera_Update state");

    fixture.Process.SetNativeFunctionCallback(
        &HandleNativeFunction, &fixture.Probe,
        {kActorGetWorldPosShapeRot,
         kBgCheckEntityRaycastFloor5,
         kBgCameraDataIndex,
         kCameraCalcUp,
         kCameraDataResolver,
         kCameraUpdateInterface,
         kFlag22A0,
         kOlibClampMaxDist,
         kOlibVec3fDiffToVecSphGeo,
         kOlibVec3fDistXZ,
         kPlayerGetHeight,
         kCopy18,
         kQuakeAggregate,
         kPlayerSpeedScale,
         kSetView,
         kSetViewScale,
         kDynamicCamera});
}

oot3d::recomp::a32::GuestState InitialOwnerState(
    const ProcessFixture& fixture) {
    auto state = fixture.Process.PrimaryThreadState();
    state.r[0] = kResult;
    state.r[1] = kCamera;
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
    Expect(
        invoked,
        std::string(scenarioName) +
            ": original Camera_Update failed: " + error +
            " last_dependency=" + lastCall +
            (fixture.Probe.Error.empty()
                 ? std::string{}
                 : " (" + fixture.Probe.Error + ")"));
    fixture.FinalState = state;
}

Oot3dNativeGame::SourceCameraUpdateStats
RunSourceOwner(ProcessFixture& fixture) {
    auto state = InitialOwnerState(fixture);
    state.r[14] = kOwnerReturn;
    oot3d::recomp::a32::ExecutionResult result;
    uint32_t blocksConsumed = 0U;
    fixture.SourceRuntime.ResetStats();
    Expect(
        fixture.SourceRuntime.Execute(
            kOwnerEntry, state, fixture.Process.Memory(),
            &result, &blocksConsumed),
        "source Camera_Update was not selected");
    Expect(
        result.kind == oot3d::recomp::a32::ExitKind::Branch &&
            result.pc == kOwnerReturn &&
            state.r[15] == kOwnerReturn &&
            blocksConsumed == 1U,
        "source Camera_Update did not return: " +
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
    Expect(
        reference.ReadBytes(address, referenceBytes) &&
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
    const size_t wordOffset = offset & ~size_t{3U};
    uint32_t referenceWord = 0U;
    uint32_t sourceWord = 0U;
    std::memcpy(
        &referenceWord, referenceBytes.data() + wordOffset,
        sizeof(referenceWord));
    std::memcpy(
        &sourceWord, sourceBytes.data() + wordOffset,
        sizeof(sourceWord));
    throw std::runtime_error(
        std::string(name) + " differs at guest address " +
        std::to_string(address + static_cast<uint32_t>(offset)) +
        " reference=" + std::to_string(referenceBytes[offset]) +
        " source=" + std::to_string(sourceBytes[offset]) +
        " reference_word=" + std::to_string(referenceWord) +
        " source_word=" + std::to_string(sourceWord));
}

void CompareOutcome(
    const ProcessFixture& reference, const ProcessFixture& source,
    const Scenario& scenario,
    const Oot3dNativeGame::SourceCameraUpdateStats& stats) {
    CompareRange(
        reference.Process.Memory(), source.Process.Memory(),
        kRam, kRamSize, scenario.Name);
    Expect(
        reference.Probe.Calls == source.Probe.Calls,
        std::string(scenario.Name) +
            ": dependency ABI or order differs");
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
            stats.NestedGuestCalls == source.Probe.Calls.size() &&
            stats.DirectDependencyCalls +
                    stats.DynamicCameraFunctionCalls ==
                stats.NestedGuestCalls &&
            stats.DynamicCameraFunctionCalls ==
                scenario.ExpectedDynamicCalls &&
            stats.GuestReadCalls > 0U &&
            stats.GuestWriteCalls > 0U &&
            stats.Failures == 0U,
        std::string(scenario.Name) +
            ": source owner statistics differ: nested=" +
            std::to_string(stats.NestedGuestCalls) +
            " direct=" +
            std::to_string(stats.DirectDependencyCalls) +
            " dynamic=" +
            std::to_string(stats.DynamicCameraFunctionCalls) +
            " reads=" + std::to_string(stats.GuestReadCalls) +
            " writes=" + std::to_string(stats.GuestWriteCalls));
}

void RunScenario(
    const Scenario& scenario, std::span<const uint8_t> codeBin) {
    ProcessFixture reference;
    ProcessFixture source;
    ConfigureProcess(reference, scenario, codeBin);
    ConfigureProcess(source, scenario, codeBin);
    const auto stats = RunSourceOwner(source);
    Expect(
        source.SourceRuntime.LastError().empty(),
        std::string(scenario.Name) + ": source bridge failed: " +
            source.SourceRuntime.LastError());
    RunReferenceOwner(reference, scenario.Name);
    CompareOutcome(reference, source, scenario, stats);
}

void TestCameraUpdateDifferential(std::span<const uint8_t> codeBin) {
    constexpr std::array scenarios{
        Scenario{"status_0_hidden_sret", ScenarioKind::Status0, 0U},
        Scenario{"status_1_early_return", ScenarioKind::Status1, 0U},
        Scenario{"status_3_dynamic_camera",
                 ScenarioKind::DynamicStatus3, 1U},
        Scenario{"status_1_player_floor_camera",
                 ScenarioKind::PlayerRaycastStatus1, 0U},
        Scenario{"status_7_normal_camera",
                 ScenarioKind::NormalStatus7, 1U},
    };
    for (const auto& scenario : scenarios) {
        RunScenario(scenario, codeBin);
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error(
                "usage: oot3d_source_camera_update_tests <code.bin>");
        }
        const auto codeBin = ReadBinary(argv[1]);
        TestCameraUpdateDifferential(codeBin);
        std::cout << "oot3d_source_camera_update_tests: ok\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_source_camera_update_tests: "
                  << exception.what() << '\n';
        return 1;
    }
}
