#include "oot3d_native_a32_execution.h"

#ifdef OOT3D_NATIVE_A32_AOT_AVAILABLE
#include "oot3d_a32_generated.h"
#endif

#include <limits>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kGuestStackBase = 0x70000000U;
constexpr size_t kGuestStackSize = 64U * 1024U;
constexpr uint32_t kGuestScratchBase = 0x71000000U;
constexpr size_t kGuestScratchSize = 1U * 1024U * 1024U;
constexpr uint32_t kReturnSentinel = 0xFFFF0000U;
constexpr uint32_t kReturnDetail = 0x4F325252U;

bool RangeFits(uint32_t baseAddress, size_t size) {
    return size != 0 &&
           static_cast<uint64_t>(baseAddress) + static_cast<uint64_t>(size) <=
               static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1U;
}

} // namespace

NativeA32ExecutionRuntime::NativeA32ExecutionRuntime(
    std::span<const uint8_t> codeImage, uint32_t codeBaseAddress)
    : mCodeBaseAddress(codeBaseAddress), mCodeImageSize(codeImage.size()) {
    if (!RangeFits(codeBaseAddress, codeImage.size())) {
        throw std::runtime_error("native A32 code image has an invalid guest range");
    }
    std::string error;
    if (!mMemory.MapRegion({"legacy_code_image", codeBaseAddress,
                            codeImage.size(), true, true, codeImage},
                           &error) ||
        !mMemory.MapRegion({"legacy_call_stack", kGuestStackBase,
                            kGuestStackSize, true, false, {}},
                           &error) ||
        !mMemory.MapRegion({"legacy_call_scratch", kGuestScratchBase,
                            kGuestScratchSize, true, false, {}},
                           &error)) {
        throw std::runtime_error(error);
    }
}

std::optional<uint32_t> NativeA32ExecutionRuntime::AllocateScratch(
    size_t size, size_t alignment) {
    if (size == 0 || alignment == 0 || (alignment & (alignment - 1U)) != 0) {
        return std::nullopt;
    }
    const size_t aligned =
        (mScratchOffset + alignment - 1U) & ~(alignment - 1U);
    if (aligned > kGuestScratchSize || size > kGuestScratchSize - aligned) {
        return std::nullopt;
    }
    const uint32_t address = kGuestScratchBase + static_cast<uint32_t>(aligned);
    if (!mMemory.Fill(address, size, 0)) {
        return std::nullopt;
    }
    mScratchOffset = aligned + size;
    return address;
}

void NativeA32ExecutionRuntime::ResetScratch() {
    mScratchOffset = 0;
}

bool NativeA32ExecutionRuntime::Available() const {
#ifdef OOT3D_NATIVE_A32_AOT_AVAILABLE
    return true;
#else
    return false;
#endif
}

NativeA32CallResult NativeA32ExecutionRuntime::Call(
    uint32_t entryAddress, oot3d::recomp::a32::GuestState& state,
    uint32_t blockLimit) {
    NativeA32CallResult result;
    if (!Available()) {
        result.Error = "native A32 AOT registry is unavailable";
        ++mFailedCallCount;
        return result;
    }
    if (entryAddress == 0 || blockLimit == 0) {
        result.Error = "native A32 call has invalid entry or block limit";
        ++mFailedCallCount;
        return result;
    }

    if (state.r[13] == 0) {
        state.r[13] = kGuestStackBase + static_cast<uint32_t>(kGuestStackSize);
    }
    state.r[14] = kReturnSentinel;
    ReturnContext context{kReturnSentinel, false};
#ifdef OOT3D_NATIVE_A32_AOT_AVAILABLE
    result.Exit = oot3d::recomp::a32::Dispatch(
        oot3d::recomp::GetA32GeneratedRegistry(), entryAddress, state, *this,
        &NativeA32ExecutionRuntime::HandleFallback, &context, blockLimit);
#endif
    result.Completed = context.Returned &&
                       result.Exit.kind == oot3d::recomp::a32::ExitKind::Wait &&
                       result.Exit.pc == kReturnSentinel &&
                       result.Exit.detail == kReturnDetail;
    if (!result.Completed) {
        result.Error = "native A32 call did not reach its return sentinel";
        ++mFailedCallCount;
        return result;
    }
    ++mSuccessfulCallCount;
    return result;
}

bool NativeA32ExecutionRuntime::Read8(uint32_t address, uint8_t* value) {
    return mMemory.Read8(address, value);
}

bool NativeA32ExecutionRuntime::Read16(uint32_t address, uint16_t* value) {
    return mMemory.Read16(address, value);
}

bool NativeA32ExecutionRuntime::Read32(uint32_t address, uint32_t* value) {
    return mMemory.Read32(address, value);
}

bool NativeA32ExecutionRuntime::Read64(uint32_t address, uint64_t* value,
                                       uint32_t* faultAddress) {
    return mMemory.Read64(address, value, faultAddress);
}

bool NativeA32ExecutionRuntime::Write8(uint32_t address, uint8_t value) {
    return mMemory.Write8(address, value);
}

bool NativeA32ExecutionRuntime::Write16(uint32_t address, uint16_t value) {
    return mMemory.Write16(address, value);
}

bool NativeA32ExecutionRuntime::Write32(uint32_t address, uint32_t value) {
    return mMemory.Write32(address, value);
}

bool NativeA32ExecutionRuntime::Write64(uint32_t address, uint64_t value,
                                        uint32_t* faultAddress) {
    return mMemory.Write64(address, value, faultAddress);
}

bool NativeA32ExecutionRuntime::LoadExclusive(
    uint32_t address, uint8_t size, uint64_t* value, uint64_t* token,
    uint32_t* faultAddress) {
    return mMemory.LoadExclusive(address, size, value, token, faultAddress);
}

oot3d::recomp::a32::ExclusiveStoreResult
NativeA32ExecutionRuntime::StoreExclusive(uint32_t address, uint8_t size,
                                          uint64_t value, uint64_t token,
                                          uint32_t* faultAddress) {
    return mMemory.StoreExclusive(address, size, value, token, faultAddress);
}

bool NativeA32ExecutionRuntime::AtomicSwap(uint32_t address, uint8_t size,
                                           uint32_t replacement,
                                           uint32_t* previous,
                                           uint32_t* faultAddress) {
    return mMemory.AtomicSwap(address, size, replacement, previous,
                              faultAddress);
}

oot3d::recomp::a32::ExecutionResult NativeA32ExecutionRuntime::HandleFallback(
    oot3d::recomp::a32::FallbackReason reason, uint32_t pc,
    const oot3d::recomp::a32::PackedOp& op,
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory, void* user) {
    static_cast<void>(state);
    static_cast<void>(memory);
    auto* context = static_cast<ReturnContext*>(user);
    if (context != nullptr &&
        reason == oot3d::recomp::a32::FallbackReason::MissingBlock &&
        pc == context->Sentinel) {
        context->Returned = true;
        return {oot3d::recomp::a32::ExitKind::Wait, pc,
                oot3d::recomp::a32::FallbackReason::None, kReturnDetail};
    }
    return {oot3d::recomp::a32::ExitKind::Fallback, pc, reason, op.raw};
}

uint32_t NativeA32ExecutionRuntime::CodeBaseAddress() const {
    return mCodeBaseAddress;
}

size_t NativeA32ExecutionRuntime::CodeImageSize() const {
    return mCodeImageSize;
}

uint64_t NativeA32ExecutionRuntime::SuccessfulCallCount() const {
    return mSuccessfulCallCount;
}

uint64_t NativeA32ExecutionRuntime::FailedCallCount() const {
    return mFailedCallCount;
}

} // namespace Oot3dNativeGame
