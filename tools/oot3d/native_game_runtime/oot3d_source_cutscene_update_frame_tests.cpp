#include "oot3d_native_a32_process.h"
#include "oot3d_source_cutscene_update_frame_runtime.h"
#include "oot3d_a32_generated.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

constexpr uint32_t kOwnerEntry = 0x00321F50U;
constexpr uint32_t kOwnerReturn = 0x0BADF10CU;
constexpr uint32_t kTextBase = 0x00321000U;
constexpr uint32_t kTextSize = 0x00002000U;
constexpr uint32_t kQueryTextBase = 0x002C2000U;
constexpr uint32_t kQueryTextSize = 0x00005000U;
constexpr uint32_t kCommitTextBase = 0x0048B000U;
constexpr uint32_t kCommitTextSize = 0x00002000U;
constexpr uint32_t kDataBase = 0x00500000U;
constexpr uint32_t kDataSize = 0x00100000U;
constexpr uint32_t kRam = 0x10000000U;
constexpr uint32_t kRamSize = 0x00020000U;
constexpr uint32_t kPlay = kRam;
constexpr uint32_t kCutsceneContext = kRam + 0x10000U;
constexpr uint32_t kCutsceneData = kRam + 0x18000U;
constexpr uint32_t kStack = 0x20000000U;
constexpr uint32_t kTls = 0x20010000U;
constexpr uint32_t kSchedulerState = 0x00587958U;

constexpr uint32_t kQueryBackendClock = 0x002C2D78U;
constexpr uint32_t kCommitBackendClock = 0x0048B198U;
constexpr uint32_t kCutsceneProcessCommands = 0x002C5BA0U;

constexpr uint32_t kInitialStateWord = 0xAABBCCDDU;
constexpr uint8_t kInitialCommandState = 0x7EU;
constexpr uint32_t kInitialClockState = 0x11223344U;
constexpr uint32_t kCommittedClockState = 0xC10C0001U;

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void WriteWord(std::vector<uint8_t>& bytes, uint32_t base,
               uint32_t address, uint32_t value) {
    Expect(address >= base &&
               static_cast<uint64_t>(address - base) + sizeof(value) <=
                   bytes.size(),
           "literal lies outside mapped cutscene text");
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

enum class DependencyKind : uint8_t {
    QueryClock,
    CommitClock,
    ProcessCommands,
};

struct DependencyCall {
    DependencyKind Kind = DependencyKind::QueryClock;
    std::array<uint32_t, 3> Arguments{};
    uint16_t Frame = 0U;

    bool operator==(const DependencyCall&) const = default;
};

struct OwnerProbe {
    int32_t QueryResult = 0;
    std::vector<DependencyCall> Calls;
    std::string Error;

    void Reset() {
        Calls.clear();
        Error.clear();
    }
};

bool HandleOwnerNativeFunction(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result, void* user) {
    if (result == nullptr || user == nullptr) {
        return false;
    }
    auto& probe = *static_cast<OwnerProbe*>(user);
    switch (pc) {
    case kQueryBackendClock:
        probe.Calls.push_back({DependencyKind::QueryClock, {}, 0U});
        state.r[0] = static_cast<uint32_t>(probe.QueryResult);
        ReturnToLinkRegister(state, result);
        return true;
    case kCommitBackendClock:
        probe.Calls.push_back(
            {DependencyKind::CommitClock, {state.r[0], 0U, 0U}, 0U});
        if (!memory.Write32(state.r[0], kCommittedClockState)) {
            probe.Error = "clock commit address is not writable";
            return false;
        }
        ReturnToLinkRegister(state, result);
        return true;
    case kCutsceneProcessCommands: {
        uint16_t frame = 0U;
        uint32_t processCount = 0U;
        if (!memory.Read16(state.r[1] + 0x20U, &frame) ||
            !memory.Read32(state.r[0] + 0x3000U, &processCount) ||
            !memory.Write32(state.r[0] + 0x3000U, processCount + 1U)) {
            probe.Error = "process-command callback memory failure";
            return false;
        }
        probe.Calls.push_back(
            {DependencyKind::ProcessCommands,
             {state.r[0], state.r[1], state.r[2]}, frame});
        ReturnToLinkRegister(state, result);
        return true;
    }
    default:
        return false;
    }
}

class OwnerHostServices final
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
            "unexpected Cutscene_UpdateFrame SVC",
        };
    }
};

struct ProcessFixture {
    ProcessFixture()
        : Process(oot3d::recomp::GetA32GeneratedRegistry(), Host),
          SourceRuntime(Process) {
    }

    OwnerHostServices Host;
    Oot3dNativeGame::NativeA32Process Process;
    Oot3dNativeGame::SourceCutsceneUpdateFrameRuntime SourceRuntime;
    OwnerProbe Probe;
    oot3d::recomp::a32::GuestState FinalState{};
    uint32_t FinalFpscr = 0U;
};

struct Scenario {
    const char* Name = "";
    bool SchedulerReady = true;
    bool BackendActive = false;
    uint8_t SceneStateMode = 2U;
    bool BackendClockReady = true;
    int32_t QueryResult = 0;
    int32_t BackendClockTicks = -1;
    uint16_t InitialFrame = 1U;
    uint16_t ExpectedFrame = 1U;
    uint32_t ExpectedQueries = 0U;
    uint32_t ExpectedCommits = 0U;
    uint32_t ExpectedProcessCalls = 0U;
    bool ExpectedInitialStateClear = false;
    uint32_t InitialFpscr = 0x03C00010U;
};

void ConfigureProcess(ProcessFixture& fixture, const Scenario& scenario) {
    std::vector<uint8_t> text(kTextSize);
    constexpr std::array<std::pair<uint32_t, uint32_t>, 5> literals{{
        {0x00322074U, kSchedulerState},
        {0x00322078U, 0x0000FFF0U},
        {0x0032207CU, 0x00007C60U},
        {0x00322080U, 0x38000000U},
        {0x00322084U, 0x41F00000U},
    }};
    for (const auto& [address, value] : literals) {
        WriteWord(text, kTextBase, address, value);
    }

    std::string error;
    const auto map = [&](const Oot3dNativeGame::NativeA32MemoryRegionConfig&
                             config) {
        Expect(fixture.Process.MapRegion(config, &error),
               "map " + config.Name + ": " + error);
    };
    map({"cutscene_text", kTextBase, text.size(), false, true, text});
    map({"cutscene_query_text", kQueryTextBase, kQueryTextSize,
         false, true, {}});
    map({"cutscene_commit_text", kCommitTextBase, kCommitTextSize,
         false, true, {}});
    map({"cutscene_data", kDataBase, kDataSize, true, false, {}});
    map({"cutscene_ram", kRam, kRamSize, true, false, {}});
    Expect(fixture.Process.CreatePrimaryThread(
               {kOwnerEntry, kStack, 0x8000U, kTls, 0x1000U, 0U,
                0U, 0x10U, scenario.InitialFpscr},
               &error),
           "create cutscene owner test thread: " + error);

    auto& memory = fixture.Process.Memory();
    Expect(
        memory.Write32(
            kSchedulerState + 8U,
            scenario.SchedulerReady ? 0x0000FFF0U : 0x0000FFEFU) &&
            memory.Write8(
                kPlay + 0x6028U,
                scenario.BackendActive ? 1U : 0U) &&
            memory.Write8(kPlay + 0x101U, scenario.SceneStateMode) &&
            memory.Write8(
                kPlay + 0x6029U,
                scenario.BackendClockReady ? 1U : 0U) &&
            memory.Write32(
                kPlay + 0x7C60U,
                static_cast<uint32_t>(scenario.BackendClockTicks)) &&
            memory.Write32(kPlay + 0x229CU, kCutsceneData) &&
            memory.Write32(kPlay + 0x21A0U, kInitialStateWord) &&
            memory.Write32(kPlay + 0x601CU, kInitialClockState) &&
            memory.Write32(kPlay + 0x3000U, 0U) &&
            memory.Write16(
                kCutsceneContext + 0x20U, scenario.InitialFrame) &&
            memory.Write8(
                kCutsceneContext + 0x27AU, kInitialCommandState),
        "seed Cutscene_UpdateFrame state");

    fixture.Probe.QueryResult = scenario.QueryResult;
    fixture.Process.SetNativeFunctionCallback(
        &HandleOwnerNativeFunction, &fixture.Probe,
        {kQueryBackendClock, kCommitBackendClock,
         kCutsceneProcessCommands});
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

void CompareOwnerState(
    const ProcessFixture& reference, const ProcessFixture& source,
    const Scenario& scenario) {
    CompareRange(
        reference.Process.Memory(), source.Process.Memory(),
        kDataBase, kDataSize, "cutscene data");
    CompareRange(
        reference.Process.Memory(), source.Process.Memory(),
        kRam, kRamSize, "cutscene ram");
    Expect(
        reference.Probe.Calls == source.Probe.Calls,
        std::string(scenario.Name) +
            ": dependency call sequence differs");
    Expect(
        reference.FinalFpscr == source.FinalFpscr,
        std::string(scenario.Name) + ": FPSCR differs");
    for (size_t index = 4U; index <= 11U; ++index) {
        Expect(
            reference.FinalState.r[index] == source.FinalState.r[index],
            std::string(scenario.Name) +
                ": callee-saved core register differs");
    }
    for (const size_t index : {13U, 15U}) {
        Expect(
            reference.FinalState.r[index] == source.FinalState.r[index],
            std::string(scenario.Name) +
                ": stack or return register differs");
    }
    for (size_t index = 16U; index < 32U; ++index) {
        Expect(
            reference.FinalState.vfp[index] ==
                source.FinalState.vfp[index],
            std::string(scenario.Name) +
                ": callee-saved VFP register differs");
    }
    Expect(
        reference.FinalState.thread_pointer ==
            source.FinalState.thread_pointer,
        std::string(scenario.Name) + ": thread pointer differs");
    Expect(
        reference.Probe.Error.empty() && source.Probe.Error.empty(),
        std::string(scenario.Name) +
            ": dependency callback failed");
}

void RunReferenceOwner(ProcessFixture& fixture) {
    auto state = fixture.Process.PrimaryThreadState();
    state.r[0] = kPlay;
    state.r[1] = kCutsceneContext;
    for (size_t index = 4U; index <= 11U; ++index) {
        state.r[index] = 0x44000000U + static_cast<uint32_t>(index);
    }
    for (size_t index = 16U; index < 32U; ++index) {
        state.vfp[index] = 0x55000000U + static_cast<uint32_t>(index);
    }
    std::string error;
    Expect(fixture.Process.InvokeFunctionWithState(
               kOwnerEntry, state, kOwnerReturn, &error),
           "original Cutscene_UpdateFrame failed: " + error);
    fixture.FinalState = state;
    fixture.FinalFpscr = state.fpscr;
}

Oot3dNativeGame::SourceCutsceneUpdateFrameStats
RunSourceOwner(ProcessFixture& fixture) {
    auto state = fixture.Process.PrimaryThreadState();
    state.r[0] = kPlay;
    state.r[1] = kCutsceneContext;
    for (size_t index = 4U; index <= 11U; ++index) {
        state.r[index] = 0x44000000U + static_cast<uint32_t>(index);
    }
    for (size_t index = 16U; index < 32U; ++index) {
        state.vfp[index] = 0x55000000U + static_cast<uint32_t>(index);
    }
    state.r[14] = kOwnerReturn;
    oot3d::recomp::a32::ExecutionResult result;
    uint32_t blocksConsumed = 0U;
    fixture.SourceRuntime.ResetStats();
    Expect(fixture.SourceRuntime.Execute(
               kOwnerEntry, state, fixture.Process.Memory(),
               &result, &blocksConsumed),
           "source Cutscene_UpdateFrame was not selected");
    Expect(
        result.kind == oot3d::recomp::a32::ExitKind::Branch &&
            result.pc == kOwnerReturn && state.r[15] == kOwnerReturn &&
            blocksConsumed == 1U,
        "source Cutscene_UpdateFrame did not return through guest LR: " +
            fixture.SourceRuntime.LastError());
    fixture.FinalState = state;
    fixture.FinalFpscr = state.fpscr;
    return fixture.SourceRuntime.Stats();
}

void ValidateOutcome(
    ProcessFixture& fixture, const Scenario& scenario,
    const Oot3dNativeGame::SourceCutsceneUpdateFrameStats* stats) {
    uint16_t frame = 0U;
    uint32_t initialState = 0U;
    uint8_t commandState = 0U;
    uint32_t clockState = 0U;
    uint32_t processCount = 0U;
    auto& memory = fixture.Process.Memory();
    Expect(
        memory.Read16(kCutsceneContext + 0x20U, &frame) &&
            memory.Read32(kPlay + 0x21A0U, &initialState) &&
            memory.Read8(kCutsceneContext + 0x27AU, &commandState) &&
            memory.Read32(kPlay + 0x601CU, &clockState) &&
            memory.Read32(kPlay + 0x3000U, &processCount),
        std::string(scenario.Name) + ": read final owner state");

    Expect(
        frame == scenario.ExpectedFrame &&
            processCount == scenario.ExpectedProcessCalls &&
            initialState ==
                (scenario.ExpectedInitialStateClear
                     ? 0U
                     : kInitialStateWord) &&
            commandState ==
                (scenario.ExpectedInitialStateClear
                     ? 0U
                     : kInitialCommandState) &&
            clockState ==
                (scenario.ExpectedCommits != 0U
                     ? kCommittedClockState
                     : kInitialClockState),
        std::string(scenario.Name) +
            ": final Cutscene_UpdateFrame state differs");

    uint32_t queries = 0U;
    uint32_t commits = 0U;
    uint32_t processes = 0U;
    uint16_t expectedProcessFrame = static_cast<uint16_t>(
        scenario.ExpectedFrame - scenario.ExpectedProcessCalls + 1U);
    for (const auto& call : fixture.Probe.Calls) {
        switch (call.Kind) {
        case DependencyKind::QueryClock:
            ++queries;
            break;
        case DependencyKind::CommitClock:
            ++commits;
            Expect(
                call.Arguments[0] == kPlay + 0x601CU,
                std::string(scenario.Name) +
                    ": clock commit ABI differs");
            break;
        case DependencyKind::ProcessCommands:
            ++processes;
            Expect(
                call.Arguments ==
                        std::array<uint32_t, 3>{
                            kPlay, kCutsceneContext, kCutsceneData} &&
                    call.Frame == expectedProcessFrame,
                std::string(scenario.Name) +
                    ": process-command ABI or cadence differs");
            ++expectedProcessFrame;
            break;
        }
    }
    Expect(
        queries == scenario.ExpectedQueries &&
            commits == scenario.ExpectedCommits &&
            processes == scenario.ExpectedProcessCalls,
        std::string(scenario.Name) +
            ": dependency call counts differ");

    if (stats != nullptr) {
        Expect(
            stats->OwnerCalls == 1U &&
                stats->NestedGuestCalls ==
                    scenario.ExpectedQueries +
                        scenario.ExpectedCommits +
                        scenario.ExpectedProcessCalls &&
                stats->ClockQueryCalls == scenario.ExpectedQueries &&
                stats->ClockCommitCalls == scenario.ExpectedCommits &&
                stats->ProcessCommandCalls ==
                    scenario.ExpectedProcessCalls &&
                stats->Failures == 0U,
            std::string(scenario.Name) +
                ": source owner statistics differ");
    }
}

void RunScenario(const Scenario& scenario) {
    ProcessFixture reference;
    ProcessFixture source;
    ConfigureProcess(reference, scenario);
    ConfigureProcess(source, scenario);
    const nlohmann::json referenceBefore =
        reference.Process.CaptureState();
    const nlohmann::json sourceBefore = source.Process.CaptureState();

    RunReferenceOwner(reference);
    const auto stats = RunSourceOwner(source);
    Expect(
        source.SourceRuntime.LastError().empty(),
        std::string(scenario.Name) + ": source bridge failed: " +
            source.SourceRuntime.LastError());
    CompareOwnerState(reference, source, scenario);
    ValidateOutcome(reference, scenario, nullptr);
    ValidateOutcome(source, scenario, &stats);

    std::string error;
    Expect(reference.Process.RestoreState(referenceBefore, &error),
           std::string(scenario.Name) +
               ": restore reference checkpoint: " + error);
    Expect(source.Process.RestoreState(sourceBefore, &error),
           std::string(scenario.Name) +
               ": restore source checkpoint: " + error);
    reference.Probe.Reset();
    source.Probe.Reset();
    RunReferenceOwner(reference);
    const auto restoredStats = RunSourceOwner(source);
    CompareOwnerState(reference, source, scenario);
    ValidateOutcome(reference, scenario, nullptr);
    ValidateOutcome(source, scenario, &restoredStats);
}

void TestCutsceneUpdateFrameDifferential() {
    constexpr std::array scenarios{
        Scenario{
            "scheduler_not_ready", false, false, 2U, true, 0, -1,
            7U, 7U, 0U, 0U, 0U, false},
        Scenario{
            "backend_wrong_scene_mode", true, true, 1U, true, 0, -1,
            7U, 7U, 0U, 0U, 0U, false},
        Scenario{
            "backend_query_not_ready", true, true, 2U, false, 0, -1,
            7U, 0U, 1U, 0U, 0U, false},
        Scenario{
            "backend_clock_commit", true, true, 2U, false, 1, -1,
            7U, 0U, 1U, 1U, 0U, false},
        Scenario{
            "normal_first_frame", true, false, 2U, true, 0, -1,
            0U, 1U, 0U, 0U, 1U, true},
        Scenario{
            "negative_backend_ticks", true, true, 2U, true, 0, -1,
            5U, 6U, 0U, 0U, 1U, false},
        Scenario{
            "backend_frame_catch_up", true, true, 2U, true, 0, 5000,
            1U, 4U, 0U, 0U, 3U, false},
        Scenario{
            "backend_target_already_reached", true, true, 2U, true,
            0, 2000, 3U, 3U, 0U, 0U, 0U, false},
        Scenario{
            "fpscr_inexact_clock_conversion", true, true, 2U, true,
            0, 16777217, 15360U, 15360U, 0U, 0U, 0U, false,
            0x03C00000U},
    };
    for (const auto& scenario : scenarios) {
        RunScenario(scenario);
    }
}

} // namespace

int main() {
    try {
        TestCutsceneUpdateFrameDifferential();
        std::cout
            << "oot3d_source_cutscene_update_frame_tests: ok\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr
            << "oot3d_source_cutscene_update_frame_tests: "
            << exception.what() << '\n';
        return 1;
    }
}
