#include "oot3d_native_direct_aot.h"

#include "oot3d_native_a32_memory.h"
#include "recomp/a32_runtime.h"
#include "triaevum_title_whole_aot_abi.h"
#include "triaevum_title_plugin_loader.h"
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>

namespace Oot3dNativeGame {
namespace {

namespace a32 = oot3d::recomp::a32;

std::filesystem::path SelectedPlugin;
bool PluginQueried = false;

#if defined(_WIN32)
using TitleModuleHandle = HMODULE;

std::filesystem::path DefaultPluginPath() {
  wchar_t executable[32768];
  const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
  if (length == 0 || length == 32768) return {};
  return std::filesystem::path(executable).parent_path() / L"triaevum_title_aot.dll";
}

TitleModuleHandle LoadTitleModule(const std::filesystem::path& path) noexcept {
  return LoadLibraryExW(path.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
}

void* TitleSymbol(TitleModuleHandle module, const char* name) noexcept {
  return reinterpret_cast<void*>(GetProcAddress(module, name));
}
#else
using TitleModuleHandle = void*;

std::filesystem::path DefaultPluginPath() {
  std::error_code error;
  auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
  if (error) return {};
  return executable.parent_path() / "triaevum_title_aot.so";
}

// RTLD_LOCAL is required: host and plugin both define the whole-AOT helpers
// and inline thread_locals, and must never interpose each other.
TitleModuleHandle LoadTitleModule(const std::filesystem::path& path) noexcept {
  return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void* TitleSymbol(TitleModuleHandle module, const char* name) noexcept {
  return dlsym(module, name);
}
#endif

TitleModuleHandle TitleModule() noexcept {
  static TitleModuleHandle module = []() noexcept -> TitleModuleHandle {
    PluginQueried = true;
    try {
      const auto path = SelectedPlugin.empty() ? DefaultPluginPath() : SelectedPlugin;
      if (path.empty()) return nullptr;
      return LoadTitleModule(path);
    } catch (...) {
      return nullptr;
    }
  }();
  return module;
}

template <class Result>
const Result* QueryTitle(const char* name, uint32_t abi) noexcept {
  auto module = TitleModule();
  if (!module) return nullptr;
  auto query = reinterpret_cast<const Result* (*)(uint32_t) noexcept>(
      TitleSymbol(module, name));
  return query ? query(abi) : nullptr;
}

const Oot3dWholeAotProgramV2 *WholeAotProgram() noexcept {
  static const Oot3dWholeAotProgramV2 *program = []() noexcept {
    const Oot3dWholeAotProgramV2 *candidate =
        QueryTitle<Oot3dWholeAotProgramV2>("triaevum_title_whole_aot_query", kOot3dWholeAotPluginAbiV2);
    if (candidate == nullptr ||
        candidate->AbiVersion != kOot3dWholeAotPluginAbiV2 ||
        candidate->StructSize < sizeof(Oot3dWholeAotProgramV2) ||
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
  static const Oot3dDirectAotProgramV1 *program = []() noexcept {
    const Oot3dDirectAotProgramV1 *candidate =
        QueryTitle<Oot3dDirectAotProgramV1>("triaevum_title_aot_query", kOot3dDirectAotPluginAbiV1);
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
  if (PluginQueried) throw std::runtime_error("Title plugin was already queried");
  SelectedPlugin = std::filesystem::absolute(path).lexically_normal();
  if (!std::filesystem::is_regular_file(SelectedPlugin))
    throw std::runtime_error("Selected title plugin is missing: " + SelectedPlugin.string());
}

bool Oot3dWholeAotPluginV2Available() noexcept {
  return WholeAotProgram() != nullptr;
}

bool Oot3dWholeAotPluginExecutionActive() noexcept {
  const Oot3dWholeAotProgramV2 *program = WholeAotProgram();
  return program != nullptr && program->ExecutionActive();
}

[[noreturn]] void Oot3dWholeAotPluginObservableExit(
    uint32_t pc, const a32::GuestState &state) {
  const Oot3dWholeAotProgramV2 *program = WholeAotProgram();
  if (program != nullptr) {
    program->ObservableExit(pc, &state);
  }
  std::terminate();
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
    return program->Execute(pc, state, memory, result, stats, externalCall,
                            blockBudget, blocksConsumed, blockEntry,
                            blockEntryUser, blockEntryPcs, blockEntryPcCount,
                            blockEntryFilter, skipFirstBlockEntry, stopPc);
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
