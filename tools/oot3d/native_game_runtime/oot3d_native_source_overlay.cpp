#include "oot3d_native_source_overlay.h"

#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)

namespace Oot3dNativeGame {

bool Oot3dSourceOverlayAvailable() noexcept {
    return false;
}

std::span<const uint32_t> Oot3dSourceOverlayEntryPoints() noexcept {
    return {};
}

bool Oot3dSourceOverlayEntryActive(uint32_t) noexcept {
    return false;
}

bool ExecuteOot3dSourceOverlay(
    uint32_t, oot3d::recomp::a32::GuestState&, NativeA32Memory&,
    oot3d::recomp::a32::ExecutionResult*, uint32_t, uint32_t*) {
    return false;
}

void ResetOot3dSourceOverlayStats() noexcept {
}

Oot3dSourceOverlayStats GetOot3dSourceOverlayStats() noexcept {
    return {};
}

} // namespace Oot3dNativeGame

#else

#include "oot3d_native_a32_memory.h"
#include "oot3d_native_compiled_functions.h"
#include "oot3d_source_overlay_loader.h"
#include "oot3d_a32_generated.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace Oot3dNativeGame {
namespace {

struct OverlayExecutionContext {
    NativeA32Memory* Memory = nullptr;
};

thread_local OverlayExecutionContext gExecutionContext;
thread_local std::vector<uint32_t> gActiveEntries;
Oot3dSourceOverlayStats gStats;

class OverlayManager final {
  public:
    void Initialize() noexcept {
        std::call_once(mInitializeOnce, [this] { LoadFromEnvironment(); });
    }

    Oot3dSourceOverlayModule Module;

  private:
    void LoadFromEnvironment() noexcept;
    std::once_flag mInitializeOnce;
};

OverlayManager& Manager() {
    static OverlayManager manager;
    return manager;
}

std::filesystem::path OverlayPathFromEnvironment() {
#if defined(_WIN32)
    const wchar_t* value = _wgetenv(L"OOT3D_SOURCE_OVERLAY");
    return value == nullptr ? std::filesystem::path{}
                            : std::filesystem::path(value);
#else
    const char* value = std::getenv("OOT3D_SOURCE_OVERLAY");
    return value == nullptr ? std::filesystem::path{}
                            : std::filesystem::path(value);
#endif
}

uint32_t ProbeMemory(void*, uint32_t address, size_t size) {
    NativeA32Memory* memory = gExecutionContext.Memory;
    if (memory == nullptr) {
        return 0U;
    }
    uint32_t access = 0U;
    if (memory->IsMapped(address, size)) {
        access |= OOT3D_SOURCE_OVERLAY_MEMORY_READ;
    }
    if (memory->IsWritable(address, size)) {
        access |= OOT3D_SOURCE_OVERLAY_MEMORY_WRITE;
    }
    return access;
}

int ReadMemory(void*, uint32_t address, void* bytes, size_t size) {
    NativeA32Memory* memory = gExecutionContext.Memory;
    if (memory == nullptr || (bytes == nullptr && size != 0U)) {
        return 0;
    }
    return memory->ReadBytes(
               address,
               std::span<uint8_t>(static_cast<uint8_t*>(bytes), size))
               ? 1
               : 0;
}

int WriteMemory(void*, uint32_t address, const void* bytes, size_t size) {
    NativeA32Memory* memory = gExecutionContext.Memory;
    if (memory == nullptr || (bytes == nullptr && size != 0U)) {
        return 0;
    }
    return memory->WriteBytes(
               address,
               std::span<const uint8_t>(
                   static_cast<const uint8_t*>(bytes), size))
               ? 1
               : 0;
}

const void* ResolveRead(void*, uint32_t address, size_t size) {
    NativeA32Memory* memory = gExecutionContext.Memory;
    return memory == nullptr ? nullptr : memory->GetReadPointer(address, size);
}

void* ResolveWrite(void*, uint32_t address, size_t size) {
    NativeA32Memory* memory = gExecutionContext.Memory;
    return memory == nullptr ? nullptr : memory->GetWritePointer(address, size);
}

void CopyToOverlay(const oot3d::recomp::a32::GuestState& source,
                   Oot3dSourceOverlayGuestState* destination) {
    std::copy(source.r.begin(), source.r.end(), destination->Registers);
    destination->Cpsr = source.cpsr;
    destination->Fpscr = source.fpscr;
    destination->ThreadPointer = source.thread_pointer;
    std::copy(source.vfp.begin(), source.vfp.end(), destination->Vfp);
    destination->ExclusiveAddress = source.exclusive_address;
    destination->ExclusiveToken = source.exclusive_token;
    destination->ExclusiveSize = source.exclusive_size;
    destination->ExclusiveValid = source.exclusive_valid ? 1U : 0U;
}

void CopyFromOverlay(const Oot3dSourceOverlayGuestState& source,
                     oot3d::recomp::a32::GuestState* destination) {
    std::copy(std::begin(source.Registers), std::end(source.Registers),
              destination->r.begin());
    destination->cpsr = source.Cpsr;
    destination->fpscr = source.Fpscr;
    destination->thread_pointer = source.ThreadPointer;
    std::copy(std::begin(source.Vfp), std::end(source.Vfp),
              destination->vfp.begin());
    destination->exclusive_address = source.ExclusiveAddress;
    destination->exclusive_token = source.ExclusiveToken;
    destination->exclusive_size = source.ExclusiveSize;
    destination->exclusive_valid = source.ExclusiveValid != 0U;
}

uint32_t OverlayKindForResult(
    const oot3d::recomp::a32::ExecutionResult& result) {
    using oot3d::recomp::a32::ExitKind;
    switch (result.kind) {
    case ExitKind::Branch:
    case ExitKind::Fallthrough:
        return OOT3D_SOURCE_OVERLAY_BRANCH;
    case ExitKind::Svc:
        return OOT3D_SOURCE_OVERLAY_SVC;
    case ExitKind::BlockLimit:
        return OOT3D_SOURCE_OVERLAY_BLOCK_LIMIT;
    case ExitKind::MemoryFault:
        return OOT3D_SOURCE_OVERLAY_MEMORY_FAULT;
    default:
        return OOT3D_SOURCE_OVERLAY_NOT_HANDLED;
    }
}

bool ExecuteDecodedGuestOracle(
    uint32_t entry, oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result, uint32_t blockBudget,
    uint32_t* blocksConsumed) {
    using oot3d::recomp::a32::ExitKind;
    using oot3d::recomp::a32::FallbackReason;
    if (result == nullptr || blockBudget == 0U) {
        return false;
    }

    const auto& registry = oot3d::recomp::GetA32GeneratedRegistry();
    const uint32_t returnAddress = state.r[14];
    uint32_t pc = entry;
    uint32_t consumed = 0U;
    while (consumed < blockBudget) {
        if (consumed != 0U && pc == returnAddress) {
            state.r[15] = pc;
            *result = {ExitKind::Branch, pc, FallbackReason::None, 0U};
            if (blocksConsumed != nullptr) {
                *blocksConsumed = consumed;
            }
            return true;
        }
        const auto* block = oot3d::recomp::a32::FindBlock(registry, pc);
        if (block == nullptr) {
            return false;
        }
        const auto step = oot3d::recomp::a32::ExecuteBlock(
            *block, state, memory);
        ++consumed;
        if (step.kind != ExitKind::Branch &&
            step.kind != ExitKind::Fallthrough) {
            *result = step;
            if (blocksConsumed != nullptr) {
                *blocksConsumed = consumed;
            }
            return step.kind == ExitKind::Svc ||
                   step.kind == ExitKind::MemoryFault;
        }
        pc = step.pc;
        state.r[15] = pc;
    }

    *result = {ExitKind::BlockLimit, pc, FallbackReason::None, consumed};
    if (blocksConsumed != nullptr) {
        *blocksConsumed = consumed;
    }
    return true;
}

int CallGuest(void*, uint32_t entry, Oot3dSourceOverlayGuestState* state,
              Oot3dSourceOverlayExecutionResult* result,
              uint32_t blockBudget) {
    NativeA32Memory* memory = gExecutionContext.Memory;
    if (memory == nullptr || state == nullptr || result == nullptr ||
        result->StructSize < sizeof(Oot3dSourceOverlayExecutionResult)) {
        return 0;
    }
    oot3d::recomp::a32::GuestState guest{};
    CopyFromOverlay(*state, &guest);
    oot3d::recomp::a32::ExecutionResult guestResult{};
    uint32_t blocksConsumed = 0U;
    ++gStats.GuestCalls;
    if (!ExecuteDecodedGuestOracle(entry, guest, *memory, &guestResult,
                                   blockBudget, &blocksConsumed)) {
        return 0;
    }
    const uint32_t kind = OverlayKindForResult(guestResult);
    if (kind == OOT3D_SOURCE_OVERLAY_NOT_HANDLED) {
        return 0;
    }
    CopyToOverlay(guest, state);
    result->Kind = kind;
    result->Pc = guestResult.pc;
    result->Detail = guestResult.detail;
    result->BlocksConsumed = blocksConsumed;
    return 1;
}

void Log(void*, uint32_t level, const char* message) {
    std::ostream& stream = level >= 2U ? std::cerr : std::cout;
    stream << "source-overlay: "
           << (message == nullptr ? "" : message) << '\n';
}

const Oot3dSourceOverlayHostApi& HostApi() {
    static const Oot3dSourceOverlayHostApi api{
        sizeof(Oot3dSourceOverlayHostApi),
        OOT3D_SOURCE_OVERLAY_ABI_VERSION,
        nullptr,
        ProbeMemory,
        ReadMemory,
        WriteMemory,
        ResolveRead,
        ResolveWrite,
        CallGuest,
        Log,
    };
    return api;
}

void OverlayManager::LoadFromEnvironment() noexcept {
    const std::filesystem::path path = OverlayPathFromEnvironment();
    if (path.empty()) {
        return;
    }
    std::string error;
    if (!Module.Load(path, HostApi(), &error)) {
        std::cerr << "Warning: " << error
                  << "; continuing with whole-AOT only.\n";
        return;
    }
    std::cout << "Source overlay loaded: " << Module.BuildId()
              << " (" << Module.EntryPoints().size() << " entries)\n";
}

bool ConvertOverlayResult(
    const Oot3dSourceOverlayExecutionResult& source,
    oot3d::recomp::a32::ExecutionResult* destination) {
    using oot3d::recomp::a32::ExitKind;
    ExitKind kind;
    switch (source.Kind) {
    case OOT3D_SOURCE_OVERLAY_BRANCH:
        kind = ExitKind::Branch;
        break;
    case OOT3D_SOURCE_OVERLAY_SVC:
        kind = ExitKind::Svc;
        break;
    case OOT3D_SOURCE_OVERLAY_BLOCK_LIMIT:
        kind = ExitKind::BlockLimit;
        break;
    case OOT3D_SOURCE_OVERLAY_MEMORY_FAULT:
        kind = ExitKind::MemoryFault;
        break;
    default:
        return false;
    }
    *destination = {kind, source.Pc,
                    oot3d::recomp::a32::FallbackReason::None,
                    source.Detail};
    return true;
}

class OverlayExecutionScope final {
  public:
    OverlayExecutionScope(uint32_t entry, NativeA32Memory& memory)
        : mPrevious(gExecutionContext) {
        gExecutionContext.Memory = &memory;
        gActiveEntries.push_back(entry);
    }

    ~OverlayExecutionScope() {
        gActiveEntries.pop_back();
        gExecutionContext = mPrevious;
    }

  private:
    OverlayExecutionContext mPrevious;
};

} // namespace

bool Oot3dSourceOverlayAvailable() noexcept {
    Manager().Initialize();
    return Manager().Module.IsLoaded();
}

std::span<const uint32_t> Oot3dSourceOverlayEntryPoints() noexcept {
    Manager().Initialize();
    return Manager().Module.EntryPoints();
}

bool Oot3dSourceOverlayEntryActive(uint32_t entry) noexcept {
    return std::find(gActiveEntries.begin(), gActiveEntries.end(), entry) !=
           gActiveEntries.end();
}

bool ExecuteOot3dSourceOverlay(
    uint32_t entry, oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory, oot3d::recomp::a32::ExecutionResult* result,
    uint32_t blockBudget, uint32_t* blocksConsumed) {
    if (result == nullptr || !Oot3dSourceOverlayAvailable() ||
        !Manager().Module.Contains(entry) ||
        Oot3dSourceOverlayEntryActive(entry)) {
        return false;
    }
    ++gStats.Calls;
    Oot3dSourceOverlayGuestState overlayState{};
    CopyToOverlay(state, &overlayState);
    Oot3dSourceOverlayExecutionResult overlayResult{
        sizeof(Oot3dSourceOverlayExecutionResult)};
    OverlayExecutionScope scope(entry, memory);
    if (Manager().Module.Execute(entry, &overlayState, &overlayResult,
                                 blockBudget) == 0) {
        ++gStats.FallbackCalls;
        return false;
    }
    if (overlayResult.StructSize < sizeof(Oot3dSourceOverlayExecutionResult) ||
        !ConvertOverlayResult(overlayResult, result)) {
        ++gStats.InvalidResults;
        ++gStats.FallbackCalls;
        return false;
    }
    CopyFromOverlay(overlayState, &state);
    state.r[15] = overlayResult.Pc;
    if (blocksConsumed != nullptr) {
        *blocksConsumed = std::max(overlayResult.BlocksConsumed, 1U);
    }
    ++gStats.HandledCalls;
    return true;
}

void ResetOot3dSourceOverlayStats() noexcept {
    gStats = {};
}

Oot3dSourceOverlayStats GetOot3dSourceOverlayStats() noexcept {
    return gStats;
}

} // namespace Oot3dNativeGame

#endif
