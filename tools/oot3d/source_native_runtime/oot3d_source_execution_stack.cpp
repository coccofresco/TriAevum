#include "oot3d_source_execution_stack.h"

#include <csetjmp>
#include <cstdlib>
#include <utility>

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Oot3dSourceRuntime {
namespace {

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
constexpr std::uintptr_t kLowStackArenaBegin = 0x60000000ULL;
constexpr std::uintptr_t kLowStackArenaEnd = 0x7F000000ULL;
constexpr std::size_t kStackSlotSize = 8U * 1024U * 1024U;
constexpr std::size_t kGuardSize = 64U * 1024U;

extern "C" void oot3d_source_call_on_stack(
    void* stackLimit, void* stackBase, void (*callback)(void*),
    void* context);

thread_local std::jmp_buf* gThreadExitJump = nullptr;
thread_local std::uintptr_t gSourceStackLimit = 0U;
thread_local std::uintptr_t gSourceStackBase = 0U;

struct DispatchContext {
    SourceExecutionCallback Callback = nullptr;
    void* Context = nullptr;
    std::exception_ptr Exception;
    bool ThreadExitRequested = false;
    std::uintptr_t StackLimit = 0U;
    std::uintptr_t StackBase = 0U;
};

void DispatchOnSourceStack(void* opaque) noexcept {
    auto& dispatch = *static_cast<DispatchContext*>(opaque);
    std::jmp_buf threadExitJump;
    std::jmp_buf* const previousJump = gThreadExitJump;
    const std::uintptr_t previousLimit = gSourceStackLimit;
    const std::uintptr_t previousBase = gSourceStackBase;
    gThreadExitJump = &threadExitJump;
    gSourceStackLimit = dispatch.StackLimit;
    gSourceStackBase = dispatch.StackBase;
    if (setjmp(threadExitJump) == 0) {
        try {
            dispatch.Callback(dispatch.Context);
        } catch (...) {
            dispatch.Exception = std::current_exception();
        }
    } else {
        dispatch.ThreadExitRequested = true;
    }
    gSourceStackLimit = previousLimit;
    gSourceStackBase = previousBase;
    gThreadExitJump = previousJump;
}
#endif

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

} // namespace

SourceExecutionStack::~SourceExecutionStack() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (mAllocation != nullptr) {
        VirtualFree(mAllocation, 0U, MEM_RELEASE);
    }
#endif
}

bool SourceExecutionStack::Initialize(std::string* error) {
    if (mAllocation != nullptr) {
        return true;
    }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    for (std::uintptr_t address = kLowStackArenaBegin;
         address + kStackSlotSize <= kLowStackArenaEnd;
         address += kStackSlotSize) {
        void* const requested = reinterpret_cast<void*>(address);
        void* const allocation = VirtualAlloc(
            requested, kStackSlotSize, MEM_RESERVE, PAGE_NOACCESS);
        if (allocation == nullptr) {
            continue;
        }
        if (allocation != requested) {
            VirtualFree(allocation, 0U, MEM_RELEASE);
            continue;
        }
        auto* const stackLimit =
            static_cast<unsigned char*>(allocation) + kGuardSize;
        const std::size_t committedSize = kStackSlotSize - kGuardSize;
        if (VirtualAlloc(stackLimit, committedSize, MEM_COMMIT,
                         PAGE_READWRITE) == nullptr) {
            VirtualFree(allocation, 0U, MEM_RELEASE);
            continue;
        }
        mAllocation = allocation;
        mStackLimit = stackLimit;
        mStackBase =
            static_cast<unsigned char*>(allocation) + kStackSlotSize;
        return true;
    }
    SetError(error, "no free source execution stack below 2 GiB");
    return false;
#else
    SetError(error, "source execution stacks require Windows x64");
    return false;
#endif
}

SourceExecutionResult SourceExecutionStack::Invoke(
    SourceExecutionCallback callback, void* context) noexcept {
    SourceExecutionResult result;
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (mAllocation == nullptr || callback == nullptr || mInvoking) {
        return result;
    }
    DispatchContext dispatch;
    dispatch.Callback = callback;
    dispatch.Context = context;
    dispatch.StackLimit = StackLimit();
    dispatch.StackBase = StackBase();
    mInvoking = true;
    oot3d_source_call_on_stack(mStackLimit, mStackBase,
                               &DispatchOnSourceStack, &dispatch);
    mInvoking = false;
    result.Invoked = true;
    result.ThreadExitRequested = dispatch.ThreadExitRequested;
    result.Exception = std::move(dispatch.Exception);
#else
    (void)callback;
    (void)context;
#endif
    return result;
}

bool SourceExecutionStack::IsInitialized() const {
    return mAllocation != nullptr;
}

std::uintptr_t SourceExecutionStack::StackLimit() const {
    return reinterpret_cast<std::uintptr_t>(mStackLimit);
}

std::uintptr_t SourceExecutionStack::StackBase() const {
    return reinterpret_cast<std::uintptr_t>(mStackBase);
}

[[noreturn]] void ExitCurrentSourceThread() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (gThreadExitJump != nullptr) {
        std::longjmp(*gThreadExitJump, 1);
    }
    ExitThread(0U);
#endif
    std::abort();
}

std::span<const std::byte> ResolveCurrentSourceStackRead(
    std::uint32_t address, std::size_t size) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    const std::uintptr_t begin = address;
    if (gSourceStackLimit == 0U || begin < gSourceStackLimit ||
        begin > gSourceStackBase || size > gSourceStackBase - begin) {
        return {};
    }
    return {reinterpret_cast<const std::byte*>(begin), size};
#else
    (void)address;
    (void)size;
    return {};
#endif
}

std::span<std::byte> ResolveCurrentSourceStackWrite(
    std::uint32_t address, std::size_t size) {
    const auto readable = ResolveCurrentSourceStackRead(address, size);
    return {const_cast<std::byte*>(readable.data()), readable.size()};
}

} // namespace Oot3dSourceRuntime
