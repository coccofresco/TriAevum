// Developer-only proxy DLL. Never package: it deliberately pauses execution to
// replay one real invocation on private memory copies, not to measure game FPS.
#include "triaevum_title_whole_aot_abi.h"
#include "oot3d_native_a32_memory.h"
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace Oot3dNativeGame;
namespace a32 = oot3d::recomp::a32;
namespace {
const Oot3dWholeAotProgramV2* baseline;
const Oot3dWholeAotProgramV2* candidate;
uint32_t target;
unsigned occurrences;
unsigned captureOccurrence = 64;
bool completed;
using InputSelector = bool (*)(const a32::GuestState*, const NativeA32Memory*) noexcept;
InputSelector inputSelector;
struct Route {
    a32::BlockEntryCallback Callback;
    void* User;
    const uint32_t* Pcs;
    size_t Count;
    const Oot3dAotBlockEntryFilter* Filter;
    NativeA32Memory* Memory;
};
bool Equal(const a32::GuestState& a, const a32::GuestState& b) {
    return a.r == b.r && a.cpsr == b.cpsr && a.fpscr == b.fpscr &&
        a.vfp == b.vfp && a.thread_pointer == b.thread_pointer &&
        a.exclusive_address == b.exclusive_address &&
        a.exclusive_token == b.exclusive_token &&
        a.exclusive_size == b.exclusive_size && a.exclusive_valid == b.exclusive_valid;
}
bool Notifies(const Route& route, uint32_t pc) {
    return route.Callback && (!route.Count ||
        std::binary_search(route.Pcs, route.Pcs + route.Count, pc));
}
struct Guard {
    const Oot3dWholeAotProgramV2* Module;
    bool Observed = false;
};
void RejectObserver(uint32_t pc, a32::GuestState& state, a32::MemoryBus&, void* user) {
    auto& guard = *static_cast<Guard*>(user);
    guard.Observed = true;
    guard.Module->ObservableExit(pc, &state);
}
Oot3dWholeAotFlow RejectExternal(uint32_t pc, Oot3dWholeAotFrame&, NativeA32Memory&) {
    return {Oot3dWholeAotFlowKind::Unsupported, pc, 0};
}
void Replay(a32::GuestState input, const Route& route) {
    const char* path = std::getenv("TRIAEVUM_INVOCATION_OUTPUT");
    if (!path) return;
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot write invocation diagnostic");
    out << "diagnostic_only,not_game_timing\n";
    out << "original_callback," << (route.Callback != nullptr) << ",hook_count," << route.Count << '\n';
    out << "original_hook_pcs";
    for (size_t i = 0; i < route.Count; ++i) out << ',' << route.Pcs[i];
    out << "\ntrial,variant,nanoseconds,thread_cycles,valid,matched,blocks,return_pc\n";
    auto originalMemory = route.Memory->ContentFingerprint();
    auto originalGeneration = route.Memory->WriteGeneration();
    NativeA32Memory memory(*route.Memory);
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency)) throw std::runtime_error("QPC unavailable");
    a32::GuestState expected{};
    a32::ExecutionResult expectedExit{};
    uint64_t expectedMemory = 0, expectedGeneration = 0;
    uint32_t expectedBlocks = 0;
    bool allValid = true;
    using HitCounter = uint64_t (*)() noexcept;
    HMODULE candidateModule = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(candidate->Execute), &candidateModule);
    auto hitCounter = candidateModule ? reinterpret_cast<HitCounter>(
        GetProcAddress(candidateModule, "triaevum_invocation_candidate_hits")) : nullptr;
    const auto hitsBefore = hitCounter ? hitCounter() : 0;
    for (unsigned trial = 0; trial < 16; ++trial) {
        memory = *route.Memory; // Copy/reset and fingerprints are outside timing.
        auto state = input;
        Oot3dWholeAotStats stats{};
        a32::ExecutionResult result{};
        uint32_t blocks = 0;
        bool useCandidate = trial % 4 == 1 || trial % 4 == 2;
        const auto* module = useCandidate ? candidate : baseline;
        Guard guard{module};
        LARGE_INTEGER begin{}, end{};
        ULONG64 cyclesBegin = 0, cyclesEnd = 0;
        bool clocks = QueryThreadCycleTime(GetCurrentThread(), &cyclesBegin) != 0;
        QueryPerformanceCounter(&begin);
        bool executed = module->Execute(target, state, memory, &result, &stats,
            RejectExternal, 100000, &blocks, route.Callback ? RejectObserver : nullptr,
            &guard, route.Pcs, route.Count, route.Filter, true, input.r[14]);
        QueryPerformanceCounter(&end);
        clocks = QueryThreadCycleTime(GetCurrentThread(), &cyclesEnd) != 0 && clocks;
        const auto fingerprint = memory.ContentFingerprint();
        const auto generation = memory.WriteGeneration();
        bool valid = clocks && executed && !guard.Observed &&
            result.pc == input.r[14] && result.kind == a32::ExitKind::Branch &&
            stats.MemoryFaults == 0 && stats.UnsupportedExits == 0 && stats.ExternalCalls == 0;
        if (!trial) {
            expected = state; expectedExit = result; expectedMemory = fingerprint;
            expectedGeneration = generation; expectedBlocks = blocks;
        }
        bool match = Equal(expected, state) && fingerprint == expectedMemory &&
            generation == expectedGeneration && blocks == expectedBlocks &&
            result.kind == expectedExit.kind && result.pc == expectedExit.pc &&
            result.detail == expectedExit.detail && result.fallback == expectedExit.fallback;
        out << trial << ',' << (useCandidate ? "candidate" : "baseline") << ','
            << (end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart << ','
            << cyclesEnd - cyclesBegin << ',' << valid << ',' << match << ','
            << blocks << ',' << result.pc << '\n';
        allValid = allValid && valid && match;
        if (!allValid) break;
    }
    const bool untouched = originalMemory == route.Memory->ContentFingerprint() &&
        originalGeneration == route.Memory->WriteGeneration();
    if (hitCounter) out << "candidate_fast_hits," << hitCounter() - hitsBefore << '\n';
    out << "summary," << allValid << ",original_memory_untouched," << untouched << '\n';
    out.flush();
    if (!untouched) throw std::runtime_error("Invocation probe changed live memory");
}
void Observe(uint32_t pc, a32::GuestState& state, a32::MemoryBus& bus, void* user) {
    auto& route = *static_cast<Route*>(user);
    if (Notifies(route, pc)) route.Callback(pc, state, bus, route.User);
    if (!completed && pc == target && (!inputSelector || inputSelector(&state,route.Memory)) &&
        ++occurrences == captureOccurrence) {
        completed = true;
        Replay(state, route);
    }
}
bool Execute(uint32_t pc, a32::GuestState& state, NativeA32Memory& memory,
    a32::ExecutionResult* result, Oot3dWholeAotStats* stats,
    Oot3dWholeAotExternalCall external, uint32_t budget, uint32_t* consumed,
    a32::BlockEntryCallback callback, void* user, const uint32_t* pcs, size_t count,
    const Oot3dAotBlockEntryFilter* filter, bool skipFirst, uint32_t stop) {
    if (completed) return baseline->Execute(pc, state, memory, result, stats, external,
        budget, consumed, callback, user, pcs, count, filter, skipFirst, stop);
    Route route{callback, user, pcs, count, filter, &memory};
    std::vector<uint32_t> hooks;
    if (count) hooks.assign(pcs, pcs + count);
    hooks.push_back(target);
    std::sort(hooks.begin(), hooks.end());
    hooks.erase(std::unique(hooks.begin(), hooks.end()), hooks.end());
    Oot3dAotBlockEntryFilter merged;
    for (auto hook : hooks) merged.Insert(hook);
    // An existing unfiltered callback must remain unfiltered.
    const bool all = callback && !count;
    return baseline->Execute(pc, state, memory, result, stats, external, budget,
        consumed, Observe, &route, all ? nullptr : hooks.data(), all ? 0 : hooks.size(),
        all ? nullptr : &merged, skipFirst, stop);
}
const Oot3dWholeAotProgramV2* Load(const char* path) {
    if (!path) return nullptr;
    auto library = LoadLibraryExA(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!library) return nullptr;
    using Query = const Oot3dWholeAotProgramV2* (*)(uint32_t) noexcept;
    auto query = reinterpret_cast<Query>(GetProcAddress(library, "triaevum_title_whole_aot_query"));
    auto result = query ? query(kOot3dWholeAotPluginAbiV2) : nullptr;
    if (!result || result->StructSize != sizeof(*result) || !result->Execute ||
        !result->ObservableExit) return nullptr;
    return result; // Keep loaded for the lifetime of the diagnostic process.
}
}
extern "C" __declspec(dllexport) const Oot3dWholeAotProgramV2*
triaevum_title_whole_aot_query(uint32_t abi) noexcept {
    if (abi != kOot3dWholeAotPluginAbiV2) return nullptr;
    static Oot3dWholeAotProgramV2 proxy{};
    if (!proxy.Execute) {
        baseline = Load(std::getenv("TRIAEVUM_INVOCATION_BASELINE"));
        const char* other = std::getenv("TRIAEVUM_INVOCATION_CANDIDATE");
        candidate = other ? Load(other) : baseline;
        if (candidate) {
            HMODULE module=nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(candidate->Execute), &module);
            inputSelector=module ? reinterpret_cast<InputSelector>(
                GetProcAddress(module,"triaevum_invocation_select_input")) : nullptr;
        }
        const char* entry = std::getenv("TRIAEVUM_INVOCATION_ENTRY");
        if (!baseline || !candidate || !entry) return nullptr;
        target = static_cast<uint32_t>(std::strtoul(entry, nullptr, 16));
        if (const char* occurrence = std::getenv("TRIAEVUM_INVOCATION_OCCURRENCE")) {
            auto requested = std::strtoul(occurrence, nullptr, 10);
            if (!requested || requested > 8192) return nullptr;
            captureOccurrence = static_cast<unsigned>(requested);
        }
        proxy = *baseline;
        proxy.Execute = Execute;
    }
    return &proxy;
}
