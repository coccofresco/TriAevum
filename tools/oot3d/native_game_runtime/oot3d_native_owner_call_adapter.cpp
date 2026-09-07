#include "oot3d_native_owner_call_adapter.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kAapcsStackAlignment = 8U;

bool CheckedSubtract(uint32_t value, uint32_t amount, uint32_t* result) {
    if (result == nullptr || amount > value) {
        return false;
    }
    *result = value - amount;
    return true;
}

} // namespace

NativeA32OwnerExecutionScope::NativeA32OwnerExecutionScope(
    NativeA32OwnerCallAdapter* adapter) noexcept
    : mAdapter(adapter) {
}

NativeA32OwnerExecutionScope::NativeA32OwnerExecutionScope(
    NativeA32OwnerExecutionScope&& other) noexcept
    : mAdapter(std::exchange(other.mAdapter, nullptr)) {
}

NativeA32OwnerExecutionScope& NativeA32OwnerExecutionScope::operator=(
    NativeA32OwnerExecutionScope&& other) noexcept {
    if (this != &other) {
        Reset();
        mAdapter = std::exchange(other.mAdapter, nullptr);
    }
    return *this;
}

NativeA32OwnerExecutionScope::~NativeA32OwnerExecutionScope() {
    Reset();
}

NativeA32OwnerExecutionScope::operator bool() const noexcept {
    return mAdapter != nullptr;
}

void NativeA32OwnerExecutionScope::Reset() noexcept {
    if (mAdapter != nullptr) {
        mAdapter->EndExecution();
        mAdapter = nullptr;
    }
}

NativeA32OwnerCallAdapter::NativeA32OwnerCallAdapter(
    NativeA32Process& process) noexcept
    : mProcess(process) {
}

NativeA32OwnerExecutionScope NativeA32OwnerCallAdapter::BeginExecution(
    uint32_t ownerEntry,
    const oot3d::recomp::a32::GuestState& callerState,
    std::string* error) {
    if (mActive) {
        SetError(error, "native A32 source owner is already active");
        return {};
    }
    if (ownerEntry == 0U) {
        SetError(error, "native A32 source owner entry is zero");
        return {};
    }
    mCallerState = callerState;
    mOwnerEntry = ownerEntry;
    mActive = true;
    return NativeA32OwnerExecutionScope(this);
}

const uint8_t* NativeA32OwnerCallAdapter::ResolveRead(
    uint32_t address, size_t size) const {
    return mActive ? mProcess.Memory().GetReadPointer(address, size) : nullptr;
}

uint8_t* NativeA32OwnerCallAdapter::ResolveWrite(
    uint32_t address, size_t size) {
    return mActive ? mProcess.Memory().GetWritePointer(address, size) : nullptr;
}

std::optional<uint32_t> NativeA32OwnerCallAdapter::ResolveGuestAddress(
    const void* pointer, size_t size) const noexcept {
    return mActive ? mProcess.Memory().GetGuestAddress(pointer, size)
                   : std::nullopt;
}

bool NativeA32OwnerCallAdapter::Invoke(
    const NativeA32OwnerGuestCall& call,
    NativeA32OwnerGuestCallResult* result,
    std::string* error) {
    if (!mActive) {
        SetError(error, "native A32 source owner is not active");
        return false;
    }
    if (result == nullptr) {
        SetError(error, "native A32 owner call result is null");
        return false;
    }
    if (call.EntryAddress == 0U || call.ReturnAddress == 0U) {
        SetError(error, "native A32 owner call address is zero");
        return false;
    }
    if (call.CoreArgumentCount > call.CoreArguments.size() ||
        call.VfpArgumentCount > call.VfpArguments.size()) {
        SetError(error, "native A32 owner call argument count is invalid");
        return false;
    }
    if (call.Limits.BlockLimitPerDispatch == 0U ||
        call.Limits.MaxHostTransitions == 0U) {
        SetError(error, "native A32 owner call limits must be nonzero");
        return false;
    }
    const uint64_t stackArgumentBytes =
        static_cast<uint64_t>(call.StackArguments.size()) * sizeof(uint32_t);
    if (stackArgumentBytes > call.CallerFrameSize ||
        stackArgumentBytes > std::numeric_limits<uint32_t>::max()) {
        SetError(error, "native A32 owner stack arguments exceed its frame");
        return false;
    }

    uint32_t callStackPointer = 0U;
    if (!CheckedSubtract(mCallerState.r[13], call.CallerFrameSize,
                         &callStackPointer) ||
        (callStackPointer & (kAapcsStackAlignment - 1U)) != 0U) {
        SetError(error, "native A32 owner call stack is invalid");
        return false;
    }
    if (!call.StackArguments.empty() &&
        !mProcess.Memory().IsWritable(
            callStackPointer, static_cast<size_t>(stackArgumentBytes))) {
        SetError(error, "native A32 owner call stack is not writable");
        return false;
    }

    auto state = mCallerState;
    state.r[13] = callStackPointer;
    std::copy_n(call.CoreArguments.begin(), call.CoreArgumentCount,
                state.r.begin());
    std::copy_n(call.VfpArguments.begin(), call.VfpArgumentCount,
                state.vfp.begin());
    for (size_t index = 0U; index < call.StackArguments.size(); ++index) {
        if (!mProcess.Memory().WriteFast(
                callStackPointer + static_cast<uint32_t>(
                                       index * sizeof(uint32_t)),
                call.StackArguments[index])) {
            SetError(error, "native A32 owner stack argument write failed");
            return false;
        }
    }

    if (!mProcess.InvokeFunctionWithState(
            call.EntryAddress, state, call.ReturnAddress, error,
            call.Limits.BlockLimitPerDispatch,
            call.Limits.MaxHostTransitions)) {
        return false;
    }
    result->State = state;
    return true;
}

bool NativeA32OwnerCallAdapter::InvokeSvc(
    uint32_t immediate, NativeA32OwnerGuestCallResult* result,
    std::string* error) {
    if (!mActive) {
        SetError(error, "native A32 source owner is not active");
        return false;
    }
    if (result == nullptr) {
        SetError(error, "native A32 owner SVC result is null");
        return false;
    }
    auto state = mCallerState;
    if (!mProcess.InvokeSvcWithState(immediate, state, error)) {
        return false;
    }
    result->State = state;
    return true;
}

bool NativeA32OwnerCallAdapter::IsActive() const noexcept {
    return mActive;
}

uint32_t NativeA32OwnerCallAdapter::ActiveOwnerEntry() const noexcept {
    return mOwnerEntry;
}

void NativeA32OwnerCallAdapter::SetActiveFpscr(uint32_t fpscr) noexcept {
    if (mActive) {
        mCallerState.fpscr = fpscr;
    }
}

void NativeA32OwnerCallAdapter::EndExecution() noexcept {
    mCallerState = {};
    mOwnerEntry = 0U;
    mActive = false;
}

void NativeA32OwnerCallAdapter::SetError(
    std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

} // namespace Oot3dNativeGame
