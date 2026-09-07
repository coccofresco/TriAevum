#include "oot3d_source_overlay_abi.h"
#include "oot3d_source_overlay_boot_leaves.h"
#include "oot3d_source_overlay_csab_curves.h"
#include "oot3d_source_overlay_cutscene_commands.h"
#include "oot3d_source_overlay_cutscene_frame.h"
#include "oot3d_source_overlay_game_state.h"
#include "oot3d_source_overlay_pause_state.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
void Lib_MemSet(void* destination, size_t size, uint8_t value);
void Oot3d_ClearTenBytes(void* destination);
void* oot3d_memclear(void* destination, size_t size);
void* oot3d_memclear_counted_8byte_entries(void* buffer);
void* oot3d_memcpy_aligned_end(void* destination, const void* source,
                               size_t size);
void* oot3d_memcpy_end(void* destination, const void* source, size_t size);
}

namespace {

constexpr uint32_t kLibMemSet = 0x0034322CU;
constexpr uint32_t kClearTenBytes = 0x0034325CU;
constexpr uint32_t kMemclearCounted = 0x00343270U;
constexpr uint32_t kMemclear = 0x00343280U;
constexpr uint32_t kMemcpyEnd = 0x0034338CU;
constexpr uint32_t kRuntimeMemcpy = 0x00371738U;
Oot3dSourceOverlayHostApi gHost{};
uint64_t gCalls = 0U;
uint64_t gHandledCalls = 0U;
uint32_t gTraceMemcpyAddress = 0U;
bool gTraceMemcpyAddressAvailable = false;
bool gValidateAbi = false;
uint64_t gAbiValidationMismatches = 0U;
thread_local bool gGuestMemoryFault = false;
std::vector<Oot3dSourceOverlayEntry> gPublishedEntries;

constexpr Oot3dSourceOverlayEntry kEntries[]{
    {kLibMemSet, 0U, "runtime.memory", "Lib_MemSet"},
    {kClearTenBytes, 0U, "runtime.memory", "Oot3d_ClearTenBytes"},
    {kMemclearCounted, 0U, "runtime.memory",
     "oot3d_memclear_counted_8byte_entries"},
    {kMemclear, 0U, "runtime.memory", "oot3d_memclear"},
    {kMemcpyEnd, 0U, "runtime.memory", "oot3d_memcpy_end"},
    {kRuntimeMemcpy, 0U, "runtime.memory", "oot3d_memcpy_aligned_end"},
    Oot3dSourceOverlay::GameState::kEntries[0],
    Oot3dSourceOverlay::GameState::kEntries[1],
    Oot3dSourceOverlay::CutsceneCommands::kEntries[0],
    Oot3dSourceOverlay::CutsceneFrame::kEntries[0],
    Oot3dSourceOverlay::PauseState::kEntries[0],
    Oot3dSourceOverlay::BootLeaves::kEntries[0],
    Oot3dSourceOverlay::BootLeaves::kEntries[1],
    Oot3dSourceOverlay::CsabCurves::kEntries[0],
    Oot3dSourceOverlay::CsabCurves::kEntries[1],
};

std::string EnvironmentValue(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    size_t valueSize = 0U;
    _dupenv_s(&value, &valueSize, name);
    const std::string result = value == nullptr ? std::string{} : value;
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : value;
#endif
}

bool AddressRangeFits(uint32_t address, size_t size) {
    return static_cast<uint64_t>(address) + size <= (uint64_t{1} << 32U);
}

bool RangesOverlap(uint32_t left, size_t leftSize,
                   uint32_t right, size_t rightSize) {
    if (leftSize == 0U || rightSize == 0U) {
        return false;
    }
    const uint64_t leftEnd = static_cast<uint64_t>(left) + leftSize;
    const uint64_t rightEnd = static_cast<uint64_t>(right) + rightSize;
    return static_cast<uint64_t>(left) < rightEnd &&
           static_cast<uint64_t>(right) < leftEnd;
}

constexpr uint32_t kCpsrN = 1U << 31U;
constexpr uint32_t kCpsrZ = 1U << 30U;
constexpr uint32_t kCpsrC = 1U << 29U;
constexpr uint32_t kCpsrV = 1U << 28U;

void ConfigurePublishedEntries() {
    gPublishedEntries.clear();
    std::string selection = EnvironmentValue("OOT3D_SOURCE_OVERLAY_ENTRIES");
    if (selection.empty()) {
        selection =
            EnvironmentValue("OOT3D_SOURCE_OVERLAY_MEMORY_ENTRIES");
    }
    if (selection.empty() || selection == "all") {
        gPublishedEntries.assign(std::begin(kEntries), std::end(kEntries));
        return;
    }
    std::string_view remaining(selection);
    while (!remaining.empty()) {
        const size_t separator = remaining.find(',');
        std::string_view token = remaining.substr(0, separator);
        while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
        while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
        char tokenBuffer[64]{};
        const size_t tokenSize = std::min(token.size(), sizeof(tokenBuffer) - 1U);
        std::memcpy(tokenBuffer, token.data(), tokenSize);
        char* end = nullptr;
        const unsigned long long address = std::strtoull(tokenBuffer, &end, 0);
        for (const auto& candidate : kEntries) {
            if ((end != tokenBuffer && *end == '\0' &&
                 address == candidate.Address) ||
                token == candidate.Name) {
                gPublishedEntries.push_back(candidate);
                break;
            }
        }
        if (separator == std::string_view::npos) break;
        remaining.remove_prefix(separator + 1U);
    }
}

bool IsRuntimeMemoryEntry(uint32_t entry) {
    switch (entry) {
    case kLibMemSet:
    case kClearTenBytes:
    case kMemclearCounted:
    case kMemclear:
    case kMemcpyEnd:
    case kRuntimeMemcpy:
        return true;
    default:
        return false;
    }
}

void ResetSourceMemoryFault() {
    gGuestMemoryFault = false;
}

bool SourceMemoryFaulted() {
    return gGuestMemoryFault;
}

void SetNzcv(Oot3dSourceOverlayGuestState* state, uint32_t value,
             bool carry, bool overflow, bool updateOverflow) {
    uint32_t cpsr = state->Cpsr & ~(kCpsrN | kCpsrZ | kCpsrC);
    if (updateOverflow) {
        cpsr &= ~kCpsrV;
    }
    cpsr |= (value & 0x80000000U) != 0U ? kCpsrN : 0U;
    cpsr |= value == 0U ? kCpsrZ : 0U;
    cpsr |= carry ? kCpsrC : 0U;
    cpsr |= updateOverflow && overflow ? kCpsrV : 0U;
    state->Cpsr = cpsr;
}

bool ReadGuest(uint32_t address, void* bytes, size_t size) {
    return gHost.ReadMemory(gHost.Context, address, bytes, size) != 0;
}

bool WriteGuest(uint32_t address, const void* bytes, size_t size) {
    return gHost.WriteMemory(gHost.Context, address, bytes, size) != 0;
}

bool GuestAccessAllowed(uint32_t address, size_t size, uint32_t access) {
    return AddressRangeFits(address, size) &&
           (size == 0U ||
            (gHost.ProbeMemory(gHost.Context, address, size) & access) != 0U);
}

bool StackSpillAllowed(const Oot3dSourceOverlayGuestState& state,
                       uint32_t size) {
    return state.Registers[13] >= size &&
           GuestAccessAllowed(state.Registers[13] - size, size,
                              OOT3D_SOURCE_OVERLAY_MEMORY_WRITE);
}

void* GuestPointer(uint32_t address) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(address));
}

const void* ConstGuestPointer(uint32_t address) {
    return reinterpret_cast<const void*>(static_cast<uintptr_t>(address));
}

void SetBranchResult(Oot3dSourceOverlayGuestState* state,
                     Oot3dSourceOverlayExecutionResult* result,
                     uint32_t detail) {
    state->Registers[15] = state->Registers[14];
    result->StructSize = sizeof(*result);
    result->Kind = OOT3D_SOURCE_OVERLAY_BRANCH;
    result->Pc = state->Registers[15];
    result->Detail = detail;
    result->BlocksConsumed = 1U;
}

void ValidateAbiAgainstGuest(
    uint32_t entry, const Oot3dSourceOverlayGuestState& initialState,
    const Oot3dSourceOverlayGuestState& sourceState,
    const Oot3dSourceOverlayExecutionResult& sourceResult,
    uint32_t blockBudget) {
    if (!gValidateAbi) {
        return;
    }
    Oot3dSourceOverlayGuestState oracleState = initialState;
    Oot3dSourceOverlayExecutionResult oracleResult{sizeof(oracleResult)};
    bool executed =
        gHost.CallGuest(gHost.Context, entry, &oracleState, &oracleResult,
                        blockBudget) != 0;
    const bool stateMatches =
        executed && std::memcmp(&oracleState, &sourceState,
                                sizeof(sourceState)) == 0;
    const bool resultMatches =
        executed && oracleResult.Kind == sourceResult.Kind &&
        oracleResult.Pc == sourceResult.Pc;
    if (stateMatches && resultMatches) {
        return;
    }
    ++gAbiValidationMismatches;
    if (gHost.Log != nullptr && gAbiValidationMismatches <= 8U) {
        char message[320]{};
        std::snprintf(
            message, sizeof(message),
            "runtime-memory ABI mismatch entry=0x%08X executed=%u source={%08X,%08X,%08X,%08X,%08X} oracle={%08X,%08X,%08X,%08X,%08X} cpsr=%08X/%08X pc=%08X/%08X",
            entry, executed ? 1U : 0U, sourceState.Registers[0],
            sourceState.Registers[1], sourceState.Registers[2],
            sourceState.Registers[3], sourceState.Registers[12],
            oracleState.Registers[0], oracleState.Registers[1],
            oracleState.Registers[2], oracleState.Registers[3],
            oracleState.Registers[12], sourceState.Cpsr, oracleState.Cpsr,
            sourceResult.Pc, oracleResult.Pc);
        gHost.Log(gHost.Context, 2U, message);
    }
}

int ExecuteMemclearBody(Oot3dSourceOverlayGuestState* state,
                        Oot3dSourceOverlayExecutionResult* result,
                        uint32_t detail, uint32_t destination,
                        uint32_t size) {
    const uint32_t initialStackPointer = state->Registers[13];
    const uint32_t spillAddress = initialStackPointer - 4U;
    const uint32_t initialReturnAddress = state->Registers[14];
    if (!WriteGuest(spillAddress, &initialReturnAddress,
                    sizeof(initialReturnAddress))) {
        return 0;
    }

    gGuestMemoryFault = false;
    oot3d_memclear(GuestPointer(destination), size);
    if (gGuestMemoryFault) {
        return 0;
    }

    uint32_t returnAddress = 0U;
    if (!ReadGuest(spillAddress, &returnAddress, sizeof(returnAddress))) {
        return 0;
    }
    state->Registers[0] = destination + size;
    state->Registers[1] = (size & 3U) << 30U;
    state->Registers[2] = 0U;
    state->Registers[3] = 0U;
    state->Registers[12] = 0U;
    state->Registers[13] = initialStackPointer;
    state->Registers[14] = returnAddress;
    SetNzcv(state, size & 1U, (size & 4U) != 0U, false, true);
    SetBranchResult(state, result, detail);
    return 1;
}

const char* TraceAddressEnvironment() {
    static const std::string value =
        EnvironmentValue("OOT3D_SOURCE_OVERLAY_TRACE_MEMCPY_ADDRESS");
    return value.empty() ? nullptr : value.c_str();
}

void TraceMemcpy(uint32_t destination, uint32_t source, size_t size) {
    if (!gTraceMemcpyAddressAvailable || size == 0U ||
        gTraceMemcpyAddress < destination ||
        static_cast<uint64_t>(gTraceMemcpyAddress) >=
            static_cast<uint64_t>(destination) + size ||
        gHost.Log == nullptr) {
        return;
    }
    const uint32_t offset = gTraceMemcpyAddress - destination;
    const auto* sourceBytes = static_cast<const uint8_t*>(
        gHost.ResolveRead(gHost.Context, source + offset, 1U));
    const auto* destinationBytes = static_cast<const uint8_t*>(
        gHost.ResolveRead(gHost.Context, gTraceMemcpyAddress, 1U));
    char message[192]{};
    std::snprintf(
        message, sizeof(message),
        "memcpy-hit dst=0x%08X src=0x%08X size=%zu offset=%u old=%02X source=%02X",
        destination, source, size, offset,
        destinationBytes == nullptr ? 0U : destinationBytes[0],
        sourceBytes == nullptr ? 0U : sourceBytes[0]);
    gHost.Log(gHost.Context, 0U, message);
}

int ApplyAlignedMemcpyAbi(Oot3dSourceOverlayGuestState* state,
                          Oot3dSourceOverlayExecutionResult* result,
                          uint32_t initialDestination,
                          uint32_t initialSource, uint32_t size,
                          uint32_t detail, bool copyPayload) {
    const uint32_t stackPointer = state->Registers[13];
    const uint32_t stackBase = stackPointer - 32U;
    const std::array<uint32_t, 8> savedRegisters{
        state->Registers[4], state->Registers[5], state->Registers[6],
        state->Registers[7], state->Registers[8], state->Registers[9],
        state->Registers[10], state->Registers[14],
    };
    if (!WriteGuest(stackBase, savedRegisters.data(), sizeof(savedRegisters))) {
        return 0;
    }

    const uint32_t tailSize = size & 31U;
    const uint32_t blockBytes = size - tailSize;
    uint32_t scratchR3 = state->Registers[3];
    uint32_t scratchAddress = initialSource + blockBytes;
    if (blockBytes != 0U &&
        !ReadGuest(scratchAddress - 32U, &scratchR3, sizeof(scratchR3))) {
        return 0;
    }
    const auto readScratchWord = [&]() {
        if (!ReadGuest(scratchAddress, &scratchR3, sizeof(scratchR3))) {
            return false;
        }
        return true;
    };
    if ((tailSize & 16U) != 0U) {
        if (!readScratchWord()) {
            return 0;
        }
        scratchAddress += 16U;
    }
    if ((tailSize & 8U) != 0U) {
        if (!readScratchWord()) {
            return 0;
        }
        scratchAddress += 8U;
    }
    if ((tailSize & 4U) != 0U) {
        if (!readScratchWord()) {
            return 0;
        }
        scratchAddress += 4U;
    }
    if ((tailSize & 2U) != 0U) {
        uint16_t halfword = 0U;
        if (!ReadGuest(scratchAddress, &halfword, sizeof(halfword))) {
            return 0;
        }
        scratchR3 = halfword;
        scratchAddress += 2U;
    }
    uint8_t finalByte = 0U;
    if ((tailSize & 1U) != 0U &&
        !ReadGuest(scratchAddress, &finalByte, sizeof(finalByte))) {
        return 0;
    }

    if (copyPayload && size != 0U) {
        gGuestMemoryFault = false;
        oot3d_memcpy_aligned_end(
            reinterpret_cast<void*>(static_cast<uintptr_t>(initialDestination)),
            reinterpret_cast<const void*>(
                static_cast<uintptr_t>(initialSource)),
            size);
        if (gGuestMemoryFault) {
            return 0;
        }
    }

    const uint32_t tailR2 = tailSize - 32U;
    const uint32_t shift30 = tailR2 << 30U;
    if ((tailSize & 3U) == 0U) {
        SetNzcv(state, shift30, (tailR2 & 4U) != 0U, false, true);
        state->Registers[2] = tailR2;
    } else {
        const uint32_t shift31 = tailR2 << 31U;
        SetNzcv(state, shift31, (tailR2 & 2U) != 0U, false, true);
        state->Registers[2] = (tailSize & 1U) != 0U ? finalByte : shift31;
    }
    state->Registers[0] =
        initialDestination + size;
    state->Registers[1] = initialSource + size;
    state->Registers[3] = scratchR3;
    state->Registers[12] = shift30;

    SetBranchResult(state, result, detail);
    return 1;
}

bool AlignedMemcpyRangesAllowed(
    const Oot3dSourceOverlayGuestState& state, uint32_t destination,
    uint32_t source, uint32_t size) {
    if (!GuestAccessAllowed(destination, size,
                            OOT3D_SOURCE_OVERLAY_MEMORY_WRITE) ||
        !GuestAccessAllowed(source, size,
                            OOT3D_SOURCE_OVERLAY_MEMORY_READ) ||
        state.Registers[13] < 32U) {
        return false;
    }
    const uint32_t stackBase = state.Registers[13] - 32U;
    return GuestAccessAllowed(stackBase, 32U,
                              OOT3D_SOURCE_OVERLAY_MEMORY_WRITE) &&
           !RangesOverlap(stackBase, 32U, source, size) &&
           !RangesOverlap(stackBase, 32U, destination, size);
}

int ExecuteMemcpy(Oot3dSourceOverlayGuestState* state,
                  Oot3dSourceOverlayExecutionResult* result) {
    const uint32_t destination = state->Registers[0];
    const uint32_t source = state->Registers[1];
    const uint32_t size = state->Registers[2];
    if (!AlignedMemcpyRangesAllowed(*state, destination, source, size) ||
        (destination != source &&
         RangesOverlap(destination, size, source, size))) {
        return 0;
    }
    TraceMemcpy(destination, source, size);
    return ApplyAlignedMemcpyAbi(
        state, result, destination, source, size, kRuntimeMemcpy, true);
}

int ExecuteLibMemSet(Oot3dSourceOverlayGuestState* state,
                     Oot3dSourceOverlayExecutionResult* result,
                     uint32_t) {
    const uint32_t destination = state->Registers[0];
    const uint32_t size = state->Registers[1];
    if (!GuestAccessAllowed(destination, size,
                            OOT3D_SOURCE_OVERLAY_MEMORY_WRITE)) {
        return 0;
    }
    gGuestMemoryFault = false;
    Lib_MemSet(GuestPointer(destination), size,
               static_cast<uint8_t>(state->Registers[2]));
    if (gGuestMemoryFault) {
        return 0;
    }
    state->Registers[0] = destination + (size == 0U ? 0U : size - 1U);
    state->Registers[1] = 0U;
    SetNzcv(state, 0U, true, false, true);
    SetBranchResult(state, result, kLibMemSet);
    return 1;
}

int ExecuteClearTenBytes(Oot3dSourceOverlayGuestState* state,
                         Oot3dSourceOverlayExecutionResult* result,
                         uint32_t) {
    const uint32_t destination = state->Registers[0];
    if (!GuestAccessAllowed(destination, 10U,
                            OOT3D_SOURCE_OVERLAY_MEMORY_WRITE)) {
        return 0;
    }
    gGuestMemoryFault = false;
    Oot3d_ClearTenBytes(GuestPointer(destination));
    if (gGuestMemoryFault) {
        return 0;
    }
    state->Registers[1] = 0U;
    SetBranchResult(state, result, kClearTenBytes);
    return 1;
}

int ExecuteMemclear(Oot3dSourceOverlayGuestState* state,
                    Oot3dSourceOverlayExecutionResult* result,
                    uint32_t) {
    const uint32_t destination = state->Registers[0];
    const uint32_t size = state->Registers[1];
    if (!GuestAccessAllowed(destination, size,
                            OOT3D_SOURCE_OVERLAY_MEMORY_WRITE) ||
        !StackSpillAllowed(*state, 4U)) {
        return 0;
    }
    return ExecuteMemclearBody(state, result, kMemclear, destination, size);
}

int ExecuteMemclearCounted(Oot3dSourceOverlayGuestState* state,
                           Oot3dSourceOverlayExecutionResult* result,
                           uint32_t) {
    const uint32_t buffer = state->Registers[0];
    std::array<uint8_t, 32> descriptor{};
    if (!GuestAccessAllowed(buffer, descriptor.size(),
                            OOT3D_SOURCE_OVERLAY_MEMORY_READ) ||
        !ReadGuest(buffer, descriptor.data(), descriptor.size())) {
        return 0;
    }
    uint32_t count = 0U;
    uint32_t entries = 0U;
    std::memcpy(&count, descriptor.data(), sizeof(count));
    std::memcpy(&entries, descriptor.data() + 0x1CU, sizeof(entries));
    const uint32_t size = count << 3U;
    const bool targetWritable = GuestAccessAllowed(
        entries, size, OOT3D_SOURCE_OVERLAY_MEMORY_WRITE);
    const bool stackWritable = StackSpillAllowed(*state, 4U);
    if (!targetWritable || !stackWritable) {
        if (gHost.Log != nullptr) {
            char message[224]{};
            std::snprintf(
                message, sizeof(message),
                "counted-memclear rejected buffer=0x%08X count=%u entries=0x%08X size=%u writable=%u stack=%u",
                buffer, count, entries, size, targetWritable ? 1U : 0U,
                stackWritable ? 1U : 0U);
            gHost.Log(gHost.Context, 1U, message);
        }
        return 0;
    }
    return ExecuteMemclearBody(
        state, result, kMemclearCounted, entries, size);
}

int ExecuteMemcpyEnd(Oot3dSourceOverlayGuestState* state,
                     Oot3dSourceOverlayExecutionResult* result,
                     uint32_t blockBudget) {
    const uint32_t destination = state->Registers[0];
    const uint32_t source = state->Registers[1];
    const uint32_t size = state->Registers[2];
    if (!GuestAccessAllowed(destination, size,
                            OOT3D_SOURCE_OVERLAY_MEMORY_WRITE) ||
        !GuestAccessAllowed(source, size,
                            OOT3D_SOURCE_OVERLAY_MEMORY_READ) ||
        (destination != source &&
         RangesOverlap(destination, size, source, size))) {
        return 0;
    }

    uint32_t prefix = 0U;
    bool entersAlignedHelper = false;
    if (size > 3U) {
        prefix = (4U - (destination & 3U)) & 3U;
        entersAlignedHelper = ((source + prefix) & 3U) == 0U;
    }
    if (entersAlignedHelper &&
        (!AlignedMemcpyRangesAllowed(
             *state, destination, source, size) ||
         (destination != source &&
          RangesOverlap(destination, size, source, size)))) {
        return 0;
    }

    gGuestMemoryFault = false;
    oot3d_memcpy_end(GuestPointer(destination), ConstGuestPointer(source),
                     size);
    if (gGuestMemoryFault) {
        return 0;
    }

    uint32_t r0 = destination;
    uint32_t r1 = source;
    uint32_t r2 = size;
    uint32_t r3 = state->Registers[3];
    uint32_t r12 = state->Registers[12];
    const auto readByte = [](uint32_t address, uint32_t* value) {
        uint8_t byte = 0U;
        if (!ReadGuest(address, &byte, sizeof(byte))) {
            return false;
        }
        *value = byte;
        return true;
    };
    const auto readWord = [](uint32_t address, uint32_t* value) {
        return ReadGuest(address, value, sizeof(*value));
    };

    if (size > 3U) {
        const uint32_t destinationAlignment = r0 & 3U;
        r12 = destinationAlignment;
        if (destinationAlignment != 0U) {
            if (!readByte(r1, &r3)) return 0;
            ++r1;
            r2 += destinationAlignment;
            if (destinationAlignment <= 2U) {
                if (!readByte(r1, &r12)) return 0;
                ++r1;
            }
            ++r0;
            if (destinationAlignment < 2U) {
                if (!readByte(r1, &r3)) return 0;
                ++r1;
            }
            if (destinationAlignment <= 2U) ++r0;
            r2 -= 4U;
            if (destinationAlignment < 2U) ++r0;
        }

        r3 = r1 & 3U;
        if (r3 == 0U) {
            state->Registers[0] = r0;
            state->Registers[1] = r1;
            state->Registers[2] = r2;
            state->Registers[3] = r3;
            state->Registers[12] = r12;
            return ApplyAlignedMemcpyAbi(
                state, result, r0, r1, r2, kMemcpyEnd, false);
        }

        if (r2 >= 8U) {
            const uint32_t blockBytes = r2 & ~7U;
            const uint32_t finalBlock = r1 + blockBytes - 8U;
            if (!readWord(finalBlock, &r3) ||
                !readWord(finalBlock + 4U, &r12)) {
                return 0;
            }
            r0 += blockBytes;
            r1 += blockBytes;
            r2 -= blockBytes;
        }
        const uint32_t tailSeed = r2 - 4U;
        if (r2 >= 4U) {
            if (!readWord(r1, &r3)) return 0;
            r0 += 4U;
            r1 += 4U;
        }
        r2 = tailSeed;
    }

    const uint32_t tailBits = r2;
    if ((tailBits & 2U) != 0U) {
        if (!readByte(r1, &r3) || !readByte(r1 + 1U, &r12)) {
            return 0;
        }
        r0 += 2U;
        r1 += 2U;
    }
    if ((tailBits & 1U) != 0U) {
        if (!readByte(r1, &r2)) return 0;
        ++r0;
        ++r1;
    } else {
        r2 = 0U;
    }

    state->Registers[0] = r0;
    state->Registers[1] = r1;
    state->Registers[2] = r2;
    state->Registers[3] = r3;
    state->Registers[12] = r12;
    SetNzcv(state, (tailBits & 1U) != 0U ? 0x80000000U : 0U,
            (tailBits & 2U) != 0U, false, true);
    SetBranchResult(state, result, kMemcpyEnd);
    return 1;
}

int Execute(void*, uint32_t entry, Oot3dSourceOverlayGuestState* state,
            Oot3dSourceOverlayExecutionResult* result,
            uint32_t blockBudget) {
    ++gCalls;
    if (state == nullptr || result == nullptr) {
        return 0;
    }
    Oot3dSourceOverlayGuestState initialState{};
    if (gValidateAbi) {
        initialState = *state;
    }
    int handled = 0;
    switch (entry) {
    case kLibMemSet:
        handled = ExecuteLibMemSet(state, result, blockBudget);
        break;
    case kClearTenBytes:
        handled = ExecuteClearTenBytes(state, result, blockBudget);
        break;
    case kMemclearCounted:
        handled = ExecuteMemclearCounted(state, result, blockBudget);
        break;
    case kMemclear:
        handled = ExecuteMemclear(state, result, blockBudget);
        break;
    case kMemcpyEnd:
        handled = ExecuteMemcpyEnd(state, result, blockBudget);
        break;
    case kRuntimeMemcpy:
        handled = ExecuteMemcpy(state, result);
        break;
    case Oot3dSourceOverlay::GameState::kUpdateEntry:
    case Oot3dSourceOverlay::GameState::kMainReturn: {
        const Oot3dSourceOverlay::GameState::Services services{
            &gHost, ResetSourceMemoryFault, SourceMemoryFaulted};
        handled = Oot3dSourceOverlay::GameState::Execute(
            entry, state, result, services);
        break;
    }
    case Oot3dSourceOverlay::CutsceneFrame::kUpdateEntry:
        handled = Oot3dSourceOverlay::CutsceneFrame::Execute(
            entry, state, result, &gHost, blockBudget);
        break;
    case Oot3dSourceOverlay::CutsceneCommands::kProcessEntry:
        handled = Oot3dSourceOverlay::CutsceneCommands::Execute(
            entry, state, result, &gHost, blockBudget);
        break;
    case Oot3dSourceOverlay::PauseState::kInitializeEntry:
        handled = Oot3dSourceOverlay::PauseState::Execute(
            entry, state, result, &gHost);
        break;
    case Oot3dSourceOverlay::BootLeaves::kMemsetEntry:
    case Oot3dSourceOverlay::BootLeaves::kIdentityDestructorEntry:
        handled = Oot3dSourceOverlay::BootLeaves::Execute(
            entry, state, result, &gHost);
        break;
    case Oot3dSourceOverlay::CsabCurves::kS16Entry:
    case Oot3dSourceOverlay::CsabCurves::kF32Entry:
        handled = Oot3dSourceOverlay::CsabCurves::Execute(
            entry, state, result, &gHost);
        break;
    default:
        return 0;
    }
    if (handled == 0 && gHost.Log != nullptr) {
        char message[192]{};
        std::snprintf(
            message, sizeof(message),
            "source-owner overlay fallback entry=0x%08X regs={%08X,%08X,%08X,%08X} sp=%08X lr=%08X",
            entry, state->Registers[0], state->Registers[1],
            state->Registers[2], state->Registers[3], state->Registers[13],
            state->Registers[14]);
        gHost.Log(gHost.Context, 1U, message);
    }
    if (handled != 0 && gValidateAbi && IsRuntimeMemoryEntry(entry)) {
        ValidateAbiAgainstGuest(
            entry, initialState, *state, *result, blockBudget);
    }
    gHandledCalls += handled != 0 ? 1U : 0U;
    return handled;
}

void Shutdown(void*) {
    if (gHost.Log == nullptr) {
        return;
    }
    const auto gameStateStats = Oot3dSourceOverlay::GameState::GetStats();
    const auto cutsceneFrameStats =
        Oot3dSourceOverlay::CutsceneFrame::GetStats();
    const auto cutsceneCommandStats =
        Oot3dSourceOverlay::CutsceneCommands::GetStats();
    const auto pauseStateStats = Oot3dSourceOverlay::PauseState::GetStats();
    const auto bootLeafStats = Oot3dSourceOverlay::BootLeaves::GetStats();
    const auto csabStats = Oot3dSourceOverlay::CsabCurves::GetStats();
    char message[896]{};
    std::snprintf(message, sizeof(message),
                  "source-overlay-native-owners-v7 calls=%llu handled=%llu abi_mismatches=%llu game_state_update=%llu/%llu game_state_return=%llu/%llu cutscene_frame=%llu/%llu frame_a32=%llu frame_source=%llu process_commands=%llu commands=%llu/%llu commands_a32=%llu direct=%llu dynamic=%llu reads=%llu writes=%llu scratch=%llu hard_float=%llu conversions=%llu failures=%llu/%llu pause_state=%llu/%llu boot_leaves=%llu/%llu rejected=%llu",
                  static_cast<unsigned long long>(gCalls),
                  static_cast<unsigned long long>(gHandledCalls),
                  static_cast<unsigned long long>(gAbiValidationMismatches),
                  static_cast<unsigned long long>(gameStateStats.UpdateHandled),
                  static_cast<unsigned long long>(gameStateStats.UpdateCalls),
                  static_cast<unsigned long long>(gameStateStats.MainReturnHandled),
                  static_cast<unsigned long long>(gameStateStats.MainReturnCalls),
                  static_cast<unsigned long long>(cutsceneFrameStats.OwnerHandled),
                  static_cast<unsigned long long>(cutsceneFrameStats.OwnerCalls),
                  static_cast<unsigned long long>(cutsceneFrameStats.NestedGuestCalls),
                  static_cast<unsigned long long>(cutsceneFrameStats.SourceDependencyCalls),
                  static_cast<unsigned long long>(cutsceneFrameStats.ProcessCommandCalls),
                  static_cast<unsigned long long>(cutsceneCommandStats.OwnerHandled),
                  static_cast<unsigned long long>(cutsceneCommandStats.OwnerCalls),
                  static_cast<unsigned long long>(cutsceneCommandStats.NestedGuestCalls),
                  static_cast<unsigned long long>(cutsceneCommandStats.DirectDependencyCalls),
                  static_cast<unsigned long long>(cutsceneCommandStats.DynamicCallbackCalls),
                  static_cast<unsigned long long>(cutsceneCommandStats.GuestReadCalls),
                  static_cast<unsigned long long>(cutsceneCommandStats.GuestWriteCalls),
                  static_cast<unsigned long long>(cutsceneCommandStats.ScratchCalls),
                  static_cast<unsigned long long>(cutsceneCommandStats.HardFloatCalls),
                  static_cast<unsigned long long>(cutsceneCommandStats.ConversionOperations),
                  static_cast<unsigned long long>(cutsceneCommandStats.Failures),
                  static_cast<unsigned long long>(cutsceneFrameStats.Failures),
                  static_cast<unsigned long long>(pauseStateStats.Handled),
                  static_cast<unsigned long long>(pauseStateStats.Calls),
                  static_cast<unsigned long long>(bootLeafStats.Handled),
                  static_cast<unsigned long long>(bootLeafStats.Calls),
                  static_cast<unsigned long long>(pauseStateStats.Rejected));
    gHost.Log(gHost.Context, 0U, message);
    char csabMessage[256]{};
    std::snprintf(
        csabMessage, sizeof(csabMessage),
        "source-overlay-csab calls=%llu handled=%llu rejected=%llu s16=%llu f32=%llu bytes=%llu fallbacks=%llu fpscr_updates=%llu differential_mismatches=%llu",
        static_cast<unsigned long long>(csabStats.Calls),
        static_cast<unsigned long long>(csabStats.Handled),
        static_cast<unsigned long long>(csabStats.Rejected),
        static_cast<unsigned long long>(csabStats.S16Calls),
        static_cast<unsigned long long>(csabStats.F32Calls),
        static_cast<unsigned long long>(csabStats.GuestBytesRead),
        static_cast<unsigned long long>(csabStats.Fallbacks),
        static_cast<unsigned long long>(csabStats.FpscrExceptionUpdates),
        static_cast<unsigned long long>(csabStats.DifferentialMismatches));
    gHost.Log(gHost.Context, 0U, csabMessage);
}

Oot3dSourceOverlayPluginApi gPlugin{
    sizeof(Oot3dSourceOverlayPluginApi),
    OOT3D_SOURCE_OVERLAY_ABI_VERSION,
    "source-overlay-native-owners-v7",
    nullptr,
    kEntries,
    sizeof(kEntries) / sizeof(kEntries[0]),
    Execute,
    Shutdown,
};

} // namespace

extern "C" void oot3d_overlay_guest_read(uint32_t address, void* bytes,
                                           uint64_t size64) {
    if (gGuestMemoryFault || size64 > std::numeric_limits<size_t>::max() ||
        !AddressRangeFits(address, static_cast<size_t>(size64)) ||
        !ReadGuest(address, bytes, static_cast<size_t>(size64))) {
        gGuestMemoryFault = true;
    }
}

extern "C" void oot3d_overlay_guest_write(uint32_t address,
                                            const void* bytes,
                                            uint64_t size64) {
    if (gGuestMemoryFault || size64 > std::numeric_limits<size_t>::max() ||
        !AddressRangeFits(address, static_cast<size_t>(size64)) ||
        !WriteGuest(address, bytes, static_cast<size_t>(size64))) {
        gGuestMemoryFault = true;
    }
}

extern "C" void oot3d_overlay_guest_copy(uint32_t destination,
                                           uint32_t source,
                                           uint64_t size64) {
    if (gGuestMemoryFault || size64 > std::numeric_limits<size_t>::max()) {
        gGuestMemoryFault = true;
        return;
    }
    const size_t size = static_cast<size_t>(size64);
    if (!AddressRangeFits(destination, size) ||
        !AddressRangeFits(source, size)) {
        gGuestMemoryFault = true;
        return;
    }
    if (size == 0U) {
        return;
    }
    const void* sourceBytes = gHost.ResolveRead(gHost.Context, source, size);
    if (sourceBytes == nullptr) {
        gGuestMemoryFault = true;
        return;
    }
    if (!RangesOverlap(destination, size, source, size)) {
        gGuestMemoryFault = !WriteGuest(destination, sourceBytes, size);
        return;
    }
    try {
        std::vector<uint8_t> staging(size);
        std::memcpy(staging.data(), sourceBytes, size);
        gGuestMemoryFault = !WriteGuest(destination, staging.data(), size);
    } catch (...) {
        gGuestMemoryFault = true;
    }
}

extern "C" void oot3d_overlay_guest_fill(uint32_t destination,
                                           uint32_t value,
                                           uint64_t size64) {
    if (gGuestMemoryFault || size64 > std::numeric_limits<size_t>::max()) {
        gGuestMemoryFault = true;
        return;
    }
    const size_t size = static_cast<size_t>(size64);
    if (!AddressRangeFits(destination, size)) {
        gGuestMemoryFault = true;
        return;
    }
    std::array<uint8_t, 4096> chunk{};
    chunk.fill(static_cast<uint8_t>(value));
    size_t offset = 0U;
    while (offset < size) {
        const size_t count = std::min(chunk.size(), size - offset);
        if (!WriteGuest(destination + static_cast<uint32_t>(offset),
                        chunk.data(), count)) {
            gGuestMemoryFault = true;
            return;
        }
        offset += count;
    }
}

extern "C" OOT3D_SOURCE_OVERLAY_EXPORT
const Oot3dSourceOverlayPluginApi* Oot3dSourceOverlayQuery(
    uint32_t hostAbiVersion, const Oot3dSourceOverlayHostApi* hostApi) {
    if (hostAbiVersion != OOT3D_SOURCE_OVERLAY_ABI_VERSION ||
        hostApi == nullptr ||
        hostApi->StructSize < sizeof(Oot3dSourceOverlayHostApi) ||
        hostApi->AbiVersion != OOT3D_SOURCE_OVERLAY_ABI_VERSION ||
        hostApi->ProbeMemory == nullptr || hostApi->ReadMemory == nullptr ||
        hostApi->WriteMemory == nullptr || hostApi->ResolveRead == nullptr ||
        hostApi->ResolveWrite == nullptr || hostApi->CallGuest == nullptr) {
        return nullptr;
    }
    gHost = *hostApi;
    const std::string validateAbi =
        EnvironmentValue("OOT3D_SOURCE_OVERLAY_VALIDATE_ABI");
    gValidateAbi = validateAbi == "1" || validateAbi == "true" ||
                   validateAbi == "on";
    ConfigurePublishedEntries();
    if (gPublishedEntries.empty()) {
        return nullptr;
    }
    gPlugin.Entries = gPublishedEntries.data();
    gPlugin.EntryCount = gPublishedEntries.size();
    if (const char* traceAddress = TraceAddressEnvironment()) {
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(traceAddress, &end, 0);
        gTraceMemcpyAddressAvailable = end != traceAddress && *end == '\0' &&
                                       parsed <= 0xFFFFFFFFULL;
        gTraceMemcpyAddress = static_cast<uint32_t>(parsed);
    }
    return &gPlugin;
}
