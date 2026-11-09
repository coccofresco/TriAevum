#include "oot3d_native_direct_aot.h"

#include "oot3d_native_a32_memory.h"
#include "recomp/a32_runtime.h"
#include "triaevum_title_whole_aot_abi.h"
#include "triaevum_title_plugin_loader.h"
#include "triaevum_pc_counter_table.h"
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <atomic>
#include <optional>
#include <span>
#include <vector>

namespace Oot3dNativeGame {
namespace {

// Lock-free (G2-F5): a fixed per-PC table over the title's observable exit
// sites and an atomic unwind total; no mutex or allocation per exit.
struct ObservableExitCounters {
  std::atomic<uint64_t> UnwindNanoseconds{0};
  PcCounterTable<256> ExitsByPc;
};

ObservableExitCounters &ObservableExits() {
  static ObservableExitCounters counters;
  return counters;
}

// Set by the request, read when the title module's Execute returns on this
// thread: the exception unwinds exactly to the innermost Execute.
thread_local std::optional<std::chrono::steady_clock::time_point> gObservableExitRequestedAt;

namespace a32 = oot3d::recomp::a32;

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
std::filesystem::path SelectedPlugin;
bool PluginQueried = false;

#if defined(_WIN32)
HMODULE TitleModule() noexcept {
  static HMODULE module = []() noexcept -> HMODULE {
    PluginQueried = true;
    try {
      auto path = SelectedPlugin;
      if (path.empty()) {
        wchar_t executable[32768];
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        if (length == 0 || length == 32768) return nullptr;
        path = std::filesystem::path(executable).parent_path() / L"triaevum_title_aot.dll";
        // The Windows package keeps its DLLs in runtime/ beside TriAevum.exe.
        if (std::error_code error; !std::filesystem::is_regular_file(path, error))
          path = path.parent_path() / L"runtime" / L"triaevum_title_aot.dll";
      }
      return LoadLibraryExW(path.c_str(), nullptr,
          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    } catch (...) {
      return nullptr;
    }
  }();
  return module;
}
#else
void* TitleModule() noexcept {
  // ABI tables contain function pointers used for the entire process lifetime.
#if defined(__ANDROID__)
  if (SelectedPlugin.empty()) return nullptr; // see WholeAotProgram
#endif
  static void* module = []() noexcept -> void* {
    PluginQueried = true;
    try {
      auto path = SelectedPlugin;
      if (path.empty()) {
#if defined(__APPLE__)
        uint32_t size = 4096;
        std::vector<char> executable(size);
        if (_NSGetExecutablePath(executable.data(), &size) != 0) {
          executable.resize(size);
          if (_NSGetExecutablePath(executable.data(), &size) != 0) return nullptr;
        }
        path = std::filesystem::weakly_canonical(executable.data()).parent_path() /
               "triaevum_title_aot.dylib";
#else
        path = std::filesystem::read_symlink("/proc/self/exe").parent_path() /
               "triaevum_title_aot.so";
#endif
      }
      return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    } catch (...) {
      return nullptr;
    }
  }();
  return module;
}
#endif

template <class Result>
const Result* QueryTitle(const char* name, uint32_t abi) noexcept {
  auto module = TitleModule();
  if (!module) return nullptr;
  auto query = reinterpret_cast<const Result* (*)(uint32_t) noexcept>(
#if defined(_WIN32)
      GetProcAddress(module, name));
#else
      dlsym(module, name));
#endif
  return query ? query(abi) : nullptr;
}
#endif

const Oot3dWholeAotProgramV2 *WholeAotProgram() noexcept {
#if defined(__ANDROID__)
  // Android names its title library explicitly (--title-plugin, or the
  // Forge's --verify-title-plugin): there is no executable folder to search.
  // The Forge runs these tool modes inside its own process, so a query made
  // before a selection (--product-info) must not latch "no title" for the
  // process's life.
  if (SelectedPlugin.empty()) return nullptr;
#endif
  static const Oot3dWholeAotProgramV2 *program = []() noexcept {
    const Oot3dWholeAotProgramV2 *candidate =
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
        QueryTitle<Oot3dWholeAotProgramV2>("triaevum_title_whole_aot_query", kOot3dWholeAotPluginAbiV2);
#else
        triaevum_title_whole_aot_query(kOot3dWholeAotPluginAbiV2);
#endif
    if (candidate == nullptr ||
        candidate->AbiVersion != kOot3dWholeAotPluginAbiV2 ||
        candidate->StructSize < kOot3dWholeAotProgramV2MinimumSize ||
        candidate->MemoryWriteObservationVersion != NativeA32Memory::WriteObservationAbiVersion() ||
        candidate->EntryPoints == nullptr || candidate->EntryPointCount == 0U ||
        candidate->Execute == nullptr || candidate->ExecutionActive == nullptr ||
        candidate->ObservableExit == nullptr) {
      return static_cast<const Oot3dWholeAotProgramV2 *>(nullptr);
    }
    for (size_t index = 1U; index < candidate->EntryPointCount; ++index) {
      if (candidate->EntryPoints[index - 1U] >= candidate->EntryPoints[index]) {
        return static_cast<const Oot3dWholeAotProgramV2 *>(nullptr);
      }
    }
    return candidate;
  }();
  return program;
}

const Oot3dDirectAotProgramV1 *DirectAotProgram() noexcept {
#if defined(__ANDROID__)
  // Android names its title library explicitly (--title-plugin, or the
  // Forge's --verify-title-plugin): there is no executable folder to search.
  // The Forge runs these tool modes inside its own process, so a query made
  // before a selection (--product-info) must not latch "no title" for the
  // process's life.
  if (SelectedPlugin.empty()) return nullptr;
#endif
  static const Oot3dDirectAotProgramV1 *program = []() noexcept {
    const Oot3dDirectAotProgramV1 *candidate =
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
        QueryTitle<Oot3dDirectAotProgramV1>("triaevum_title_aot_query", kOot3dDirectAotPluginAbiV1);
#else
        triaevum_title_aot_query(kOot3dDirectAotPluginAbiV1);
#endif
    if (candidate == nullptr ||
        candidate->AbiVersion != kOot3dDirectAotPluginAbiV1 ||
        candidate->StructSize < sizeof(Oot3dDirectAotProgramV1) ||
        candidate->DispatchPcs == nullptr ||
        candidate->DispatchFunctionIndices == nullptr ||
        candidate->DispatchCount == 0U || candidate->Functions == nullptr ||
        candidate->FunctionCount == 0U) {
      return static_cast<const Oot3dDirectAotProgramV1 *>(nullptr);
    }
    for (size_t index = 0U; index < candidate->DispatchCount; ++index) {
      if (candidate->DispatchFunctionIndices[index] >=
              candidate->FunctionCount ||
          (index != 0U && candidate->DispatchPcs[index - 1U] >=
                              candidate->DispatchPcs[index])) {
        return static_cast<const Oot3dDirectAotProgramV1 *>(nullptr);
      }
    }
    return candidate;
  }();
  return program;
}

const Oot3dDirectAotFunctionV1 *
FindDirectAotFunction(const Oot3dDirectAotProgramV1 &program,
                      uint32_t pc) noexcept {
  const uint32_t *begin = program.DispatchPcs;
  const uint32_t *end = begin + program.DispatchCount;
  const uint32_t *found = std::lower_bound(begin, end, pc);
  if (found == end || *found != pc) {
    return nullptr;
  }
  const size_t index = static_cast<size_t>(found - begin);
  return &program.Functions[program.DispatchFunctionIndices[index]];
}

void ExecuteDirectAotBlock(Oot3dWholeAotFlow *output, Oot3dWholeAotFrame *frame,
                           Oot3dWholeAotContext *context, uint32_t pc,
                           const a32::PackedOp *operations,
                           uint32_t operationCount) {
  if (output == nullptr || frame == nullptr || context == nullptr ||
      operations == nullptr || operationCount == 0U) {
    if (output != nullptr) {
      *output = {Oot3dWholeAotFlowKind::Unsupported, pc, 0U};
    }
    return;
  }
  if (!Oot3dAotEnterBlock(*context, *frame, pc)) {
    *output = Oot3dAotBlockLimit(*context, pc);
    return;
  }

  const a32::Block block{pc, operations, operationCount, false};
  const a32::ExecutionResult result =
      a32::ExecuteBlock(block, frame->Guest, context->Memory);
  switch (result.kind) {
  case a32::ExitKind::Fallthrough:
  case a32::ExitKind::Branch:
    *output = Oot3dAotBranch(result.pc);
    return;
  case a32::ExitKind::Svc:
    *output = Oot3dAotSvc(*context, result.pc, result.detail);
    return;
  case a32::ExitKind::MemoryFault:
    *output = Oot3dAotMemoryFault(*context, result.pc, result.detail);
    return;
  case a32::ExitKind::BlockLimit:
    *output = Oot3dAotBlockLimit(*context, result.pc);
    return;
  case a32::ExitKind::Wait:
  case a32::ExitKind::MissingBlock:
  case a32::ExitKind::Fallback:
  case a32::ExitKind::Unsupported:
    *output = {Oot3dWholeAotFlowKind::Unsupported, result.pc, result.detail};
    return;
  }
  *output = {Oot3dWholeAotFlowKind::Unsupported, pc, 0U};
}

bool DispatchDirectAot(const Oot3dDirectAotProgramV1 &program, uint32_t pc,
                       Oot3dWholeAotFrame &frame, Oot3dWholeAotContext &context,
                       Oot3dWholeAotFlow *output) {
  const Oot3dDirectAotFunctionV1 *function = FindDirectAotFunction(program, pc);
  if (function == nullptr || *function == nullptr) {
    return false;
  }
  (*function)(output, &frame, &context, pc, ExecuteDirectAotBlock);
  return true;
}

} // namespace

void ConfigureTitlePlugin(const std::filesystem::path& path) {
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
  const auto requested = std::filesystem::absolute(path).lexically_normal();
  if (PluginQueried) {
    // The Android Forge runs the tool modes inside its own process
    // (triaevum_forge_tool), so an install verifies the same title library
    // twice (activation, then the runtime preflight) after the first query
    // loaded it for the process's life. The same library is the same answer;
    // any other one stays refused.
    if (!SelectedPlugin.empty() && requested == SelectedPlugin) return;
    throw std::runtime_error("Title plugin was already queried");
  }
  SelectedPlugin = requested;
  if (!std::filesystem::is_regular_file(SelectedPlugin))
    throw std::runtime_error("Selected title plugin is missing: " + SelectedPlugin.string());
#else
  throw std::runtime_error("Explicit title plugins are not supported on this platform yet");
#endif
}

bool Oot3dWholeAotPluginV2Available() noexcept {
  return WholeAotProgram() != nullptr;
}

bool Oot3dWholeAotPluginObservesMemoryWrites() noexcept {
  const auto* program = WholeAotProgram();
  if (program == nullptr) return DirectAotProgram() != nullptr;
  return program->StructSize >= kOot3dWholeAotProgramV2MinimumSize &&
         program->MemoryWriteObservationVersion == NativeA32Memory::WriteObservationAbiVersion();
}

bool Oot3dWholeAotPluginExitRequestSupported() noexcept {
  const Oot3dWholeAotProgramV2 *program = WholeAotProgram();
  return program != nullptr &&
         program->StructSize >= sizeof(Oot3dWholeAotProgramV2) &&
         (program->Capabilities & kOot3dWholeAotCapabilityExitRequest) != 0U;
}

bool Oot3dWholeAotPluginExecutionActive() noexcept {
  const Oot3dWholeAotProgramV2 *program = WholeAotProgram();
  return program != nullptr && program->ExecutionActive();
}

[[noreturn]] void Oot3dWholeAotPluginObservableExit(
    uint32_t pc, const a32::GuestState &state) {
  const Oot3dWholeAotProgramV2 *program = WholeAotProgram();
  if (program != nullptr) {
    ObservableExits().ExitsByPc.Add(pc);
    gObservableExitRequestedAt = std::chrono::steady_clock::now();
    program->ObservableExit(pc, &state);
  }
  std::terminate();
}

Oot3dWholeAotObservableExitReport Oot3dWholeAotPluginObservableExitReport() {
  auto &counters = ObservableExits();
  Oot3dWholeAotObservableExitReport report;
  report.UnwindNanoseconds = counters.UnwindNanoseconds.load(std::memory_order_relaxed);
  report.ExitsByPc = counters.ExitsByPc.Snapshot(&report.Exits);
  return report;
}

std::span<const uint32_t> Oot3dWholeAotEntryPoints() noexcept {
  if (const Oot3dWholeAotProgramV2 *program = WholeAotProgram();
      program != nullptr) {
    return {program->EntryPoints, program->EntryPointCount};
  }
  const Oot3dDirectAotProgramV1 *program = DirectAotProgram();
  return program == nullptr ? std::span<const uint32_t>{}
                            : std::span<const uint32_t>(program->DispatchPcs,
                                                        program->DispatchCount);
}

bool ExecuteOot3dWholeAotFunction(
    uint32_t pc, a32::GuestState &state, NativeA32Memory &memory,
    a32::ExecutionResult *result, Oot3dWholeAotStats *stats,
    Oot3dWholeAotExternalCall externalCall, uint32_t blockBudget,
    uint32_t *blocksConsumed, a32::BlockEntryCallback blockEntry,
    void *blockEntryUser, const uint32_t *blockEntryPcs,
    size_t blockEntryPcCount, const Oot3dAotBlockEntryFilter *blockEntryFilter,
    bool skipFirstBlockEntry, uint32_t stopPc) {
  if (const Oot3dWholeAotProgramV2 *program = WholeAotProgram();
      program != nullptr) {
    const bool executed =
        program->Execute(pc, state, memory, result, stats, externalCall,
                         blockBudget, blocksConsumed, blockEntry,
                         blockEntryUser, blockEntryPcs, blockEntryPcCount,
                         blockEntryFilter, skipFirstBlockEntry, stopPc);
    if (gObservableExitRequestedAt.has_value()) {
      const auto unwind = std::chrono::steady_clock::now() - *gObservableExitRequestedAt;
      gObservableExitRequestedAt.reset();
      ObservableExits().UnwindNanoseconds.fetch_add(
          static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(unwind).count()),
          std::memory_order_relaxed);
    }
    return executed;
  }
  const Oot3dDirectAotProgramV1 *program = DirectAotProgram();
  if (program == nullptr || result == nullptr || stats == nullptr ||
      blockBudget == 0U) {
    return false;
  }
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 0U;
  }

  Oot3dWholeAotFrame frame(state);
  Oot3dWholeAotContext context{memory,           memory,
                               *stats,           externalCall,
                               blockEntry,       blockEntryUser,
                               blockEntryPcs,    blockEntryPcCount,
                               blockEntryFilter, skipFirstBlockEntry,
                               blockBudget,      0U};
  Oot3dWholeAotFlow flow{};
  a32::GuestState observableExitState{};
  bool restoreObservableExitState = false;
  try {
    Oot3dWholeAotExecutionScope executionScope;
    if (!DispatchDirectAot(*program, pc, frame, context, &flow)) {
      return false;
    }
    while ((flow.Kind == Oot3dWholeAotFlowKind::Returned ||
            flow.Kind == Oot3dWholeAotFlowKind::Branch) &&
           flow.Pc != stopPc && context.BlocksRemaining != 0U) {
      if (!DispatchDirectAot(*program, flow.Pc, frame, context, &flow)) {
        break;
      }
      ++context.Stats.ResolvedIndirectCalls;
    }
  } catch (const Oot3dWholeAotObservableExit &exit) {
    restoreObservableExitState =
        Oot3dWholeAotTakeObservableExitSnapshot(exit.Pc, &observableExitState);
    flow = Oot3dAotBranch(exit.Pc);
  }
  if (restoreObservableExitState) {
    state = observableExitState;
  }
  ++stats->Calls;
  state.r[15] = flow.Pc;
  if (blocksConsumed != nullptr) {
    *blocksConsumed =
        context.BlocksConsumed != 0U ? context.BlocksConsumed : 1U;
  }
  switch (flow.Kind) {
  case Oot3dWholeAotFlowKind::Returned:
  case Oot3dWholeAotFlowKind::Branch:
    *result = {a32::ExitKind::Branch, flow.Pc, a32::FallbackReason::None, 0U};
    return true;
  case Oot3dWholeAotFlowKind::Svc:
    *result = {a32::ExitKind::Svc, flow.Pc, a32::FallbackReason::None,
               flow.Detail};
    return true;
  case Oot3dWholeAotFlowKind::BlockLimit:
    *result = {a32::ExitKind::BlockLimit, flow.Pc, a32::FallbackReason::None,
               context.BlocksConsumed};
    return true;
  case Oot3dWholeAotFlowKind::MemoryFault:
    *result = {a32::ExitKind::MemoryFault, flow.Pc, a32::FallbackReason::None,
               flow.Detail};
    return true;
  case Oot3dWholeAotFlowKind::Unsupported:
    ++stats->UnsupportedExits;
    *result = {a32::ExitKind::Unsupported, flow.Pc,
               a32::FallbackReason::Unsupported, flow.Detail};
    return true;
  }
  return false;
}

} // namespace Oot3dNativeGame
