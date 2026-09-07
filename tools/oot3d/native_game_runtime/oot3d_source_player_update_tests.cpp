#include "oot3d_native_a32_process.h"
#include "oot3d_source_player_update_runtime.h"
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
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kOwnerEntry = 0x001E1B54U;
constexpr uint32_t kOwnerReturn = 0x0BADF40CU;
constexpr uint32_t kCodeBase = 0x00100000U;
constexpr uint32_t kTextBase = 0x001E0000U;
constexpr uint32_t kTextSize = 0x00002000U;
constexpr uint32_t kDependencyTextBase = 0x00250000U;
constexpr uint32_t kDependencyTextSize = 0x00130000U;
constexpr uint32_t kRam = 0x10000000U;
constexpr uint32_t kRamSize = 0x00020000U;
constexpr uint32_t kPlayer = kRam;
constexpr uint32_t kPlay = kRam + 0x4000U;
constexpr uint32_t kGlobalState = kRam + 0x8000U;
constexpr uint32_t kStaticState = kRam + 0x8100U;
constexpr uint32_t kSpawnOffset = kRam + 0x8300U;
constexpr uint32_t kStaticGuard = kRam + 0x8400U;
constexpr uint32_t kSpawnPosition = kRam + 0x8500U;
constexpr uint32_t kInputMaskA = kRam + 0x8600U;
constexpr uint32_t kInputMaskB = kRam + 0x8604U;
constexpr uint32_t kOutputStateCell = kRam + 0x8610U;
constexpr uint32_t kOutputState = kRam + 0x8700U;
constexpr uint32_t kSpawnedActor = kRam + 0x9000U;
constexpr uint32_t kAttachedActor = kRam + 0xA000U;
constexpr uint32_t kLinkedActor = kRam + 0xA400U;
constexpr uint32_t kStack = 0x20000000U;
constexpr uint32_t kTls = 0x20010000U;
constexpr uint32_t kInitialFpscr = 0x03C00000U;

constexpr uint32_t kObjectGetIndex = 0x00363C10U;
constexpr uint32_t kStaticInitGuardAcquire = 0x003679B4U;
constexpr uint32_t kCosIdx8 = 0x00338F60U;
constexpr uint32_t kSinIdx8 = 0x002CFCA0U;
constexpr uint32_t kActorSpawn = 0x003738D0U;
constexpr uint32_t kResetLinkedActor = 0x0036B02CU;
constexpr uint32_t kPlayerUpdateCommon = 0x00250AD0U;

constexpr std::array<uint32_t, 13> kOriginalLiteralWords{
    0x00588E58U,
    0x0000016BU,
    0x0053A07CU,
    0x005A3254U,
    0x0053A184U,
    0x00000000U,
    0xC1F00000U,
    0x005A3260U,
    0x0000019BU,
    0x20000020U,
    0x004FC650U,
    0x004FC654U,
    0x0051B2F4U,
};

constexpr std::array<uint32_t, 13> kTestLiteralWords{
    kGlobalState,
    0x0000016BU,
    kStaticState,
    kSpawnOffset,
    kStaticGuard,
    0x00000000U,
    0xC1F00000U,
    kSpawnPosition,
    0x0000019BU,
    0x20000020U,
    kInputMaskA,
    kInputMaskB,
    kOutputStateCell,
};

enum class ScenarioKind {
    ObjectUnavailable,
    SpawnAndInitialize,
    StaleActorLinks,
    InputPassthrough,
    InputSuppressed,
    InputBlocked,
};

struct Scenario {
    const char* Name;
    ScenarioKind Kind;
    uint32_t ExpectedSpawnCalls;
    uint32_t ExpectedResetCalls;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T>
void WriteValue(
    Oot3dNativeGame::NativeA32Memory& memory,
    uint32_t address, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto bytes = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(&value), sizeof(value));
    Expect(
        memory.WriteBytes(address, bytes),
        "test guest value write failed");
}

std::vector<uint8_t> ReadBinary(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    Expect(
        static_cast<bool>(stream),
        "cannot open Player_Update code.bin: " + path.string());
    const std::streampos end = stream.tellg();
    Expect(end >= 0, "cannot determine Player_Update code.bin size");
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    stream.seekg(0, std::ios::beg);
    Expect(
        bytes.empty() ||
            static_cast<bool>(stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()))),
        "cannot read complete Player_Update code.bin");
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

uint32_t ReadMappedWord(
    const std::vector<uint8_t>& bytes, uint32_t base,
    uint32_t address) {
    Expect(
        address >= base &&
            static_cast<uint64_t>(address - base) + 4U <=
                bytes.size(),
        "literal lies outside mapped owner text");
    uint32_t value = 0U;
    std::memcpy(
        &value, bytes.data() + (address - base), sizeof(value));
    return value;
}

void WriteMappedWord(
    std::vector<uint8_t>& bytes, uint32_t base,
    uint32_t address, uint32_t value) {
    Expect(
        address >= base &&
            static_cast<uint64_t>(address - base) + 4U <=
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
    ScenarioKind Kind = ScenarioKind::InputPassthrough;
    std::vector<ObservedCall> Calls;
    uint32_t SpawnCalls = 0U;
    uint32_t ResetCalls = 0U;
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
                address + static_cast<uint32_t>(index * 4U),
                &value)) {
            return false;
        }
        values->push_back(value);
    }
    return true;
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
    case kObjectGetIndex:
        probe.Calls.push_back(
            {pc, {state.r[0], state.r[1]}});
        state.r[0] =
            probe.Kind == ScenarioKind::ObjectUnavailable
                ? 0xFFFFFFFFU
                : 2U;
        break;
    case kStaticInitGuardAcquire:
        probe.Calls.push_back({pc, {state.r[0]}});
        state.r[0] = 1U;
        break;
    case kCosIdx8:
        probe.Calls.push_back({pc, {state.r[0]}});
        state.vfp[0] = std::bit_cast<uint32_t>(0.5F);
        break;
    case kSinIdx8:
        probe.Calls.push_back({pc, {state.r[0]}});
        state.vfp[0] = std::bit_cast<uint32_t>(-0.25F);
        break;
    case kActorSpawn: {
        ObservedCall call{
            pc,
            {state.r[0], state.r[1], state.r[2], state.r[3],
             state.vfp[0], state.vfp[1], state.vfp[2],
             state.r[13]}};
        if (!ReadWords(
                processMemory, state.r[13], 4U, &call.Values)) {
            probe.Error = "Actor_Spawn stack ABI read failed";
            return false;
        }
        probe.Calls.push_back(std::move(call));
        ++probe.SpawnCalls;
        state.r[0] = kSpawnedActor;
        break;
    }
    case kResetLinkedActor:
        probe.Calls.push_back(
            {pc, {state.r[0], state.r[1]}});
        ++probe.ResetCalls;
        break;
    case kPlayerUpdateCommon: {
        ObservedCall call{
            pc, {state.r[0], state.r[1], state.r[2], state.r[13]}};
        if (!ReadWords(
                processMemory, state.r[2], 12U, &call.Values)) {
            probe.Error =
                "Player_UpdateCommon input packet read failed";
            return false;
        }
        probe.Calls.push_back(std::move(call));

        WriteValue(
            processMemory, kPlayer + 0x28U, 1.75F);
        WriteValue(
            processMemory, kPlayer + 0x2CU, -2.75F);
        WriteValue(
            processMemory, kPlayer + 0x30U,
            std::bit_cast<float>(0x7FC12345U));
        WriteValue<uint16_t>(
            processMemory, kPlayer + 0x36U, 0x4321U);
        break;
    }
    default:
        probe.Error =
            "unhandled Player_Update dependency " +
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
        uint32_t immediate,
        oot3d::recomp::a32::GuestState&,
        Oot3dNativeGame::NativeA32Memory&,
        Oot3dNativeGame::NativeA32HostContext&) override {
        return {
            Oot3dNativeGame::NativeA32HostAction::Fault,
            std::nullopt,
            immediate,
            "unexpected Player_Update SVC",
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
    Oot3dNativeGame::SourcePlayerUpdateRuntime SourceRuntime;
    DependencyProbe Probe;
    oot3d::recomp::a32::GuestState FinalState{};
};

void ConfigureProcess(
    ProcessFixture& fixture, const Scenario& scenario,
    std::span<const uint8_t> codeBin) {
    std::vector<uint8_t> text =
        CopyCodeRegion(codeBin, kTextBase, kTextSize);
    std::vector<uint8_t> dependencyText = CopyCodeRegion(
        codeBin, kDependencyTextBase, kDependencyTextSize);
    for (size_t index = 0U;
         index < kOriginalLiteralWords.size(); ++index) {
        const uint32_t cell = 0x001E1DC8U +
                              static_cast<uint32_t>(index * 4U);
        Expect(
            ReadMappedWord(text, kTextBase, cell) ==
                kOriginalLiteralWords[index],
            "Player_Update code.bin literal identity mismatch at " +
                std::to_string(cell));
        WriteMappedWord(
            text, kTextBase, cell, kTestLiteralWords[index]);
    }

    std::string error;
    const auto map = [&](const Oot3dNativeGame::
                             NativeA32MemoryRegionConfig& config) {
        Expect(
            fixture.Process.MapRegion(config, &error),
            "map " + config.Name + ": " + error);
    };
    map({"player_update_text", kTextBase, text.size(),
         false, true, text});
    map({"player_update_dependency_text", kDependencyTextBase,
         dependencyText.size(), false, true, dependencyText});
    map({"player_update_ram", kRam, kRamSize,
         true, false, {}});
    Expect(
        fixture.Process.CreatePrimaryThread(
            {kOwnerEntry, kStack, 0x8000U, kTls, 0x1000U,
             0U, 0U, 0x10U, kInitialFpscr},
            &error),
        "create Player_Update test thread: " + error);

    auto& memory = fixture.Process.Memory();
    const bool objectScenario =
        scenario.Kind == ScenarioKind::ObjectUnavailable ||
        scenario.Kind == ScenarioKind::SpawnAndInitialize;
    const uint16_t globalFlags =
        objectScenario ? 0x9234U : 0x1234U;
    WriteValue(memory, kGlobalState + 0x4EU, globalFlags);
    WriteValue<uint32_t>(
        memory, kStaticState + 0x108U, 0U);
    WriteValue<uint32_t>(memory, kSpawnOffset, 0x3F800000U);
    WriteValue<uint32_t>(
        memory, kSpawnOffset + 4U, 0x40000000U);
    WriteValue<uint32_t>(
        memory, kSpawnOffset + 8U, 0x40400000U);
    WriteValue<uint32_t>(memory, kStaticGuard, 0U);
    WriteValue<uint32_t>(
        memory, kInputMaskA, 0x00000004U);
    WriteValue<uint32_t>(
        memory, kInputMaskB, 0x00000100U);
    WriteValue<uint32_t>(
        memory, kOutputStateCell, kOutputState);
    WriteValue<float>(memory, kPlayer + 0x28U, 10.0F);
    WriteValue<float>(memory, kPlayer + 0x2CU, 20.0F);
    WriteValue<float>(memory, kPlayer + 0x30U, 30.0F);
    WriteValue<int16_t>(
        memory, kPlayer + 0x36U, 0x1234);
    WriteValue<int16_t>(
        memory, kPlayer + 0xBEU, -1000);
    WriteValue<uint32_t>(
        memory, kPlayer + 0x1710U,
        scenario.Kind == ScenarioKind::InputBlocked
            ? 0x20000020U
            : 0U);
    WriteValue<uint8_t>(
        memory, kPlayer + 0x227AU,
        scenario.Kind == ScenarioKind::InputSuppressed
            ? 1U
            : 0U);
    WriteValue<uint32_t>(
        memory, kPlayer + 0x12B0U,
        scenario.Kind == ScenarioKind::StaleActorLinks
            ? kAttachedActor
            : 0U);
    WriteValue<uint32_t>(
        memory, kPlayer + 0x1224U,
        scenario.Kind == ScenarioKind::StaleActorLinks
            ? kLinkedActor
            : 0U);
    WriteValue<uint32_t>(
        memory, kAttachedActor + 0x13CU, 0U);
    WriteValue<uint32_t>(
        memory, kLinkedActor + 0x13CU, 0U);
    WriteValue<uint8_t>(
        memory, kSpawnedActor + 3U, 0xA5U);
    for (uint32_t index = 0U; index < 12U; ++index) {
        WriteValue<uint32_t>(
            memory, kPlay + 0x14U + index * 4U,
            0xF0002000U + index * 0x101U);
    }

    fixture.Probe.Kind = scenario.Kind;
    fixture.Process.SetNativeFunctionCallback(
        &HandleNativeFunction, &fixture.Probe,
        {kObjectGetIndex, kStaticInitGuardAcquire, kCosIdx8,
         kSinIdx8, kActorSpawn, kResetLinkedActor,
         kPlayerUpdateCommon});
}

oot3d::recomp::a32::GuestState InitialOwnerState(
    const ProcessFixture& fixture) {
    auto state = fixture.Process.PrimaryThreadState();
    state.r[0] = kPlayer;
    state.r[1] = kPlay;
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
    Expect(
        invoked,
        std::string(scenarioName) +
            ": original Player_Update failed: " + error +
            (fixture.Probe.Error.empty()
                 ? std::string{}
                 : " (" + fixture.Probe.Error + ")"));
    fixture.FinalState = state;
}

Oot3dNativeGame::SourcePlayerUpdateStats
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
        "source Player_Update was not selected");
    Expect(
        result.kind == oot3d::recomp::a32::ExitKind::Branch &&
            result.pc == kOwnerReturn &&
            state.r[15] == kOwnerReturn &&
            blocksConsumed == 1U,
        "source Player_Update did not return: " +
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
    throw std::runtime_error(
        std::string(name) + " differs at guest address " +
        std::to_string(
            address + static_cast<uint32_t>(offset)) +
        " reference=" +
        std::to_string(referenceBytes[offset]) +
        " source=" + std::to_string(sourceBytes[offset]));
}

void CompareOutcome(
    const ProcessFixture& reference,
    const ProcessFixture& source, const Scenario& scenario,
    const Oot3dNativeGame::SourcePlayerUpdateStats& stats) {
    CompareRange(
        reference.Process.Memory(), source.Process.Memory(),
        kRam, kRamSize, scenario.Name);
    Expect(
        reference.Probe.Calls == source.Probe.Calls,
        std::string(scenario.Name) +
            ": dependency ABI or order differs");
    Expect(
        reference.Probe.Error.empty() &&
            source.Probe.Error.empty(),
        std::string(scenario.Name) +
            ": dependency callback failed");
    Expect(
        reference.Probe.SpawnCalls ==
                scenario.ExpectedSpawnCalls &&
            source.Probe.SpawnCalls ==
                scenario.ExpectedSpawnCalls &&
            reference.Probe.ResetCalls ==
                scenario.ExpectedResetCalls &&
            source.Probe.ResetCalls ==
                scenario.ExpectedResetCalls,
        std::string(scenario.Name) +
            ": dependency branch count differs");
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
            reference.FinalState.r[15] ==
                source.FinalState.r[15] &&
            reference.FinalState.thread_pointer ==
                source.FinalState.thread_pointer,
        std::string(scenario.Name) +
            ": owner return machine state differs");
    Expect(
        stats.OwnerCalls == 1U &&
            stats.NestedGuestCalls ==
                source.Probe.Calls.size() &&
            stats.DirectDependencyCalls ==
                stats.NestedGuestCalls &&
            stats.PlayerUpdateCommonCalls == 1U &&
            stats.ActorSpawnCalls ==
                scenario.ExpectedSpawnCalls &&
            stats.ScratchCalls == 1U &&
            stats.FloatConversions == 3U &&
            stats.VfpOperations >= 3U &&
            stats.GuestReadCalls > 0U &&
            stats.GuestWriteCalls > 0U &&
            stats.Failures == 0U,
        std::string(scenario.Name) +
            ": source owner statistics differ");
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
        std::string(scenario.Name) +
            ": source bridge failed: " +
            source.SourceRuntime.LastError());
    RunReferenceOwner(reference, scenario.Name);
    CompareOutcome(reference, source, scenario, stats);
}

void TestPlayerUpdateDifferential(
    std::span<const uint8_t> codeBin) {
    constexpr std::array scenarios{
        Scenario{
            "object_unavailable", ScenarioKind::ObjectUnavailable,
            0U, 0U},
        Scenario{
            "spawn_and_initialize",
            ScenarioKind::SpawnAndInitialize, 1U, 0U},
        Scenario{
            "stale_actor_links", ScenarioKind::StaleActorLinks,
            0U, 1U},
        Scenario{
            "input_passthrough", ScenarioKind::InputPassthrough,
            0U, 0U},
        Scenario{
            "input_suppressed", ScenarioKind::InputSuppressed,
            0U, 0U},
        Scenario{
            "input_blocked", ScenarioKind::InputBlocked,
            0U, 0U},
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
                "usage: oot3d_source_player_update_tests "
                "<code.bin>");
        }
        const auto codeBin = ReadBinary(argv[1]);
        TestPlayerUpdateDifferential(codeBin);
        std::cout << "oot3d_source_player_update_tests: ok\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_source_player_update_tests: "
                  << exception.what() << '\n';
        return 1;
    }
}
