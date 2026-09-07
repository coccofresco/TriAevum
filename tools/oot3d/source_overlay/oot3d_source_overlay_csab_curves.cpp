#include "oot3d_source_overlay_csab_curves.h"

#include "oot3d_source_csab_curve_core.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace Oot3dSourceOverlay::CsabCurves {
namespace {

Oot3dNativeGame::SourceCsabCurveCore gCore;
std::uint64_t gCalls = 0U;
std::uint64_t gHandled = 0U;
std::uint64_t gRejected = 0U;
std::uint32_t gCompareCalls = 0U;
std::uint64_t gDifferentialMismatches = 0U;
bool gCompareInitialized = false;

bool CompareEnabled() {
    if (!gCompareInitialized) {
        const char* value = nullptr;
#if defined(_WIN32)
        char* allocated = nullptr;
        std::size_t allocatedSize = 0U;
        if (_dupenv_s(&allocated, &allocatedSize,
                      "OOT3D_SOURCE_OVERLAY_CSAB_COMPARE") == 0) {
            value = allocated;
        }
#else
        value = std::getenv("OOT3D_SOURCE_OVERLAY_CSAB_COMPARE");
#endif
        gCompareInitialized = true;
        if (value != nullptr &&
            (std::strcmp(value, "1") == 0 ||
             std::strcmp(value, "true") == 0 ||
             std::strcmp(value, "on") == 0)) {
            gCompareCalls = 8U;
        }
#if defined(_WIN32)
        std::free(const_cast<char*>(value));
#endif
    }
    return gCompareCalls != 0U;
}

bool IsWritable(void* context, std::uint32_t address, std::size_t size) {
    const auto& host = *static_cast<const Oot3dSourceOverlayHostApi*>(context);
    return host.ProbeMemory != nullptr &&
           (host.ProbeMemory(host.Context, address, size) &
            OOT3D_SOURCE_OVERLAY_MEMORY_WRITE) != 0U;
}

bool Read(void* context, std::uint32_t address, void* bytes,
          std::size_t size) {
    const auto& host = *static_cast<const Oot3dSourceOverlayHostApi*>(context);
    return host.ReadMemory != nullptr &&
           host.ReadMemory(host.Context, address, bytes, size) != 0;
}

bool Write32(void* context, std::uint32_t address, std::uint32_t value) {
    const auto& host = *static_cast<const Oot3dSourceOverlayHostApi*>(context);
    if (host.WriteMemory == nullptr) {
        return false;
    }
    std::uint8_t bytes[sizeof(value)]{};
    std::memcpy(bytes, &value, sizeof(value));
    return host.WriteMemory(host.Context, address, bytes, sizeof(bytes)) != 0;
}

void ReportComparison(
    const Oot3dSourceOverlayHostApi& host, std::uint32_t entry,
    const Oot3dSourceOverlayGuestState& initial,
    const Oot3dSourceOverlayGuestState& source,
    const Oot3dSourceOverlayGuestState& arm,
    std::uint32_t initialCurveAddress,
    const std::uint8_t* sourceStack, const std::uint8_t* armStack,
    std::size_t stackSize) {
    if (host.Log == nullptr) return;
    std::size_t stackMismatch = stackSize;
    for (std::size_t index = 0U; index < stackSize; ++index) {
        if (sourceStack[index] != armStack[index]) {
            stackMismatch = index;
            break;
        }
    }
    std::uint32_t registerDiffMask = 0U;
    std::uint32_t vfpDiffMask = 0U;
    for (std::uint32_t index = 0U; index < 16U; ++index) {
        if (source.Registers[index] != arm.Registers[index]) {
            registerDiffMask |= 1U << index;
        }
    }
    for (std::uint32_t index = 0U; index < 32U; ++index) {
        if (source.Vfp[index] != arm.Vfp[index]) {
            vfpDiffMask |= 1U << index;
        }
    }
    const bool scalarStateEqual =
        source.Cpsr == arm.Cpsr && source.Fpscr == arm.Fpscr &&
        source.ThreadPointer == arm.ThreadPointer &&
        source.ExclusiveAddress == arm.ExclusiveAddress &&
        source.ExclusiveToken == arm.ExclusiveToken &&
        source.ExclusiveSize == arm.ExclusiveSize &&
        source.ExclusiveValid == arm.ExclusiveValid;
    if (registerDiffMask == 0U && vfpDiffMask == 0U &&
        scalarStateEqual && stackMismatch == stackSize) {
        return;
    }
    ++gDifferentialMismatches;
    char message[2600]{};
    std::snprintf(
        message, sizeof(message),
        "CSAB differential mismatch entry=0x%08X initial={r0=0x%08X,r1=0x%08X,r2=0x%08X,r3=0x%08X,r12=0x%08X} curve=0x%08X source_vfp0=0x%08X arm_vfp0=0x%08X source_vfp1_8={0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X} arm_vfp1_8={0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X} source_fpscr=0x%08X arm_fpscr=0x%08X source_r0=0x%08X arm_r0=0x%08X source_r1=0x%08X arm_r1=0x%08X source_r2=0x%08X arm_r2=0x%08X source_r3=0x%08X arm_r3=0x%08X source_r12=0x%08X arm_r12=0x%08X source_r14=0x%08X arm_r14=0x%08X source_r15=0x%08X arm_r15=0x%08X reg_diff=0x%08X vfp_diff=0x%08X source_cpsr=0x%08X arm_cpsr=0x%08X source_excl={0x%08X,0x%016llX,%u,%u} arm_excl={0x%08X,0x%016llX,%u,%u} stack_offset=%llu",
        entry, initial.Registers[0], initial.Registers[1],
        initial.Registers[2], initial.Registers[3], initial.Registers[12],
        initialCurveAddress, source.Vfp[0], arm.Vfp[0],
        source.Vfp[1], source.Vfp[2], source.Vfp[3], source.Vfp[4],
        source.Vfp[5], source.Vfp[6], source.Vfp[7], source.Vfp[8],
        arm.Vfp[1], arm.Vfp[2], arm.Vfp[3], arm.Vfp[4],
        arm.Vfp[5], arm.Vfp[6], arm.Vfp[7], arm.Vfp[8],
        source.Fpscr, arm.Fpscr,
        source.Registers[0], arm.Registers[0], source.Registers[1],
        arm.Registers[1], source.Registers[2], arm.Registers[2],
        source.Registers[3], arm.Registers[3], source.Registers[12],
        arm.Registers[12], source.Registers[14], arm.Registers[14],
        source.Registers[15], arm.Registers[15],
        registerDiffMask, vfpDiffMask, source.Cpsr, arm.Cpsr,
        source.ExclusiveAddress,
        static_cast<unsigned long long>(source.ExclusiveToken),
        source.ExclusiveSize, source.ExclusiveValid,
        arm.ExclusiveAddress,
        static_cast<unsigned long long>(arm.ExclusiveToken),
        arm.ExclusiveSize, arm.ExclusiveValid,
        static_cast<unsigned long long>(stackMismatch));
    host.Log(host.Context, 2U, message);
}

bool CaptureGuestBytes(
    const Oot3dSourceOverlayHostApi& host, std::uint32_t address,
    std::uint8_t* bytes, std::size_t size) {
    return host.ProbeMemory != nullptr && host.ReadMemory != nullptr &&
           (host.ProbeMemory(host.Context, address, size) &
            OOT3D_SOURCE_OVERLAY_MEMORY_READ) != 0U &&
           host.ReadMemory(host.Context, address, bytes, size) != 0;
}

void CopyToCore(const Oot3dSourceOverlayGuestState& source,
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

void CopyFromCore(const oot3d::recomp::a32::GuestState& source,
                  Oot3dSourceOverlayGuestState* destination) {
    std::copy(source.r.begin(), source.r.end(),
              std::begin(destination->Registers));
    destination->Cpsr = source.cpsr;
    destination->Fpscr = source.fpscr;
    destination->ThreadPointer = source.thread_pointer;
    std::copy(source.vfp.begin(), source.vfp.end(),
              std::begin(destination->Vfp));
    destination->ExclusiveAddress = source.exclusive_address;
    destination->ExclusiveToken = source.exclusive_token;
    destination->ExclusiveSize = source.exclusive_size;
    destination->ExclusiveValid = source.exclusive_valid ? 1U : 0U;
}

} // namespace

int Execute(std::uint32_t entry, Oot3dSourceOverlayGuestState* state,
            Oot3dSourceOverlayExecutionResult* result,
            const Oot3dSourceOverlayHostApi* host) {
    if ((entry != kS16Entry && entry != kF32Entry) || state == nullptr ||
        result == nullptr || host == nullptr || host->ProbeMemory == nullptr ||
        host->ReadMemory == nullptr || host->WriteMemory == nullptr) {
        return 0;
    }

    ++gCalls;
    const bool compare = CompareEnabled();
    const Oot3dSourceOverlayGuestState initialState = *state;
    Oot3dSourceOverlayGuestState armState{};
    std::uint32_t initialCurveAddress = 0U;
    if (compare) {
        host->ReadMemory(host->Context, initialState.Registers[0],
                         &initialCurveAddress, sizeof(initialCurveAddress));
    }
    std::array<std::uint8_t, 64U> originalStack{};
    std::array<std::uint8_t, 64U> armStack{};
    bool compareStack = false;
    if (compare && host->CallGuest != nullptr && state->Registers[13] >= 64U) {
        const std::uint32_t stackAddress = state->Registers[13] - 64U;
        compareStack = CaptureGuestBytes(
            *host, stackAddress, originalStack.data(), originalStack.size());
        if (compareStack) {
            armState = *state;
            Oot3dSourceOverlayExecutionResult armResult{
                sizeof(armResult)};
            compareStack = host->CallGuest(
                               host->Context, entry, &armState, &armResult,
                               10000U) != 0 &&
                           CaptureGuestBytes(
                               *host, stackAddress, armStack.data(),
                               armStack.size()) &&
                           host->WriteMemory(
                               host->Context, stackAddress,
                               originalStack.data(), originalStack.size()) != 0;
        }
    }
    oot3d::recomp::a32::GuestState coreState{};
    CopyToCore(*state, &coreState);
    oot3d::recomp::a32::ExecutionResult coreResult{};
    std::uint32_t blocksConsumed = 0U;
    const Oot3dNativeGame::SourceCsabCurveMemory memory{
        const_cast<Oot3dSourceOverlayHostApi*>(host), IsWritable, Read,
        Write32};
    if (!gCore.Execute(entry, coreState, memory, &coreResult,
                       &blocksConsumed)) {
        ++gRejected;
        return 0;
    }

    CopyFromCore(coreState, state);
    result->StructSize = sizeof(*result);
    result->Kind = OOT3D_SOURCE_OVERLAY_BRANCH;
    result->Pc = coreResult.pc;
    result->Detail = coreResult.detail;
    result->BlocksConsumed = blocksConsumed;
    ++gHandled;
    if (compareStack) {
        std::array<std::uint8_t, 64U> sourceStack{};
        const std::uint32_t stackAddress = state->Registers[13] - 64U;
        if (CaptureGuestBytes(
                *host, stackAddress, sourceStack.data(), sourceStack.size())) {
            ReportComparison(*host, entry, initialState, *state, armState,
                             initialCurveAddress,
                             sourceStack.data(), armStack.data(),
                             sourceStack.size());
        }
    }
    if (gCompareCalls != 0U) {
        --gCompareCalls;
    }
    return 1;
}

Stats GetStats() {
    const auto coreStats = gCore.Stats();
    return {
        gCalls,
        gHandled,
        gRejected,
        coreStats.S16Calls,
        coreStats.F32Calls,
        coreStats.GuestBytesRead,
        coreStats.Fallbacks,
        coreStats.FpscrExceptionUpdates,
        gDifferentialMismatches,
    };
}

} // namespace Oot3dSourceOverlay::CsabCurves
