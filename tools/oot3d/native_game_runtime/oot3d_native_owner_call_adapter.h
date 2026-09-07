#pragma once

#include "oot3d_native_a32_process.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Oot3dNativeGame {

struct NativeA32OwnerGuestCallLimits {
    uint32_t BlockLimitPerDispatch = 1'000'000U;
    uint32_t MaxHostTransitions = 1024U;
};

struct NativeA32OwnerGuestCall {
    uint32_t EntryAddress = 0U;
    uint32_t ReturnAddress = 0U;
    // Exact guest stack space owned by the promoted source owner while it
    // invokes this callee. Stack arguments begin at the resulting SP.
    uint32_t CallerFrameSize = 0U;
    std::array<uint32_t, 4> CoreArguments{};
    size_t CoreArgumentCount = 0U;
    std::array<uint32_t, 16> VfpArguments{};
    size_t VfpArgumentCount = 0U;
    std::span<const uint32_t> StackArguments;
    NativeA32OwnerGuestCallLimits Limits;
};

struct NativeA32OwnerGuestCallResult {
    oot3d::recomp::a32::GuestState State;
};

class NativeA32OwnerCallAdapter;

class NativeA32OwnerExecutionScope {
  public:
    NativeA32OwnerExecutionScope() = default;
    NativeA32OwnerExecutionScope(const NativeA32OwnerExecutionScope&) = delete;
    NativeA32OwnerExecutionScope& operator=(
        const NativeA32OwnerExecutionScope&) = delete;
    NativeA32OwnerExecutionScope(
        NativeA32OwnerExecutionScope&& other) noexcept;
    NativeA32OwnerExecutionScope& operator=(
        NativeA32OwnerExecutionScope&& other) noexcept;
    ~NativeA32OwnerExecutionScope();

    explicit operator bool() const noexcept;
    void Reset() noexcept;

  private:
    friend class NativeA32OwnerCallAdapter;
    explicit NativeA32OwnerExecutionScope(
        NativeA32OwnerCallAdapter* adapter) noexcept;

    NativeA32OwnerCallAdapter* mAdapter = nullptr;
};

// Bridges revision-pinned source owners to the existing guest address space
// and dispatcher. It owns no gameplay policy and never casts a guest callback
// address to a host function pointer.
class NativeA32OwnerCallAdapter {
  public:
    explicit NativeA32OwnerCallAdapter(NativeA32Process& process) noexcept;

    NativeA32OwnerExecutionScope BeginExecution(
        uint32_t ownerEntry,
        const oot3d::recomp::a32::GuestState& callerState,
        std::string* error = nullptr);

    const uint8_t* ResolveRead(uint32_t address, size_t size = 1U) const;
    uint8_t* ResolveWrite(uint32_t address, size_t size = 1U);
    std::optional<uint32_t>
    ResolveGuestAddress(const void* pointer, size_t size = 1U) const noexcept;

    bool Invoke(const NativeA32OwnerGuestCall& call,
                NativeA32OwnerGuestCallResult* result,
                std::string* error = nullptr);
    bool InvokeSvc(uint32_t immediate,
                   NativeA32OwnerGuestCallResult* result,
                   std::string* error = nullptr);

    bool IsActive() const noexcept;
    uint32_t ActiveOwnerEntry() const noexcept;
    void SetActiveFpscr(uint32_t fpscr) noexcept;

  private:
    friend class NativeA32OwnerExecutionScope;
    void EndExecution() noexcept;
    static void SetError(std::string* error, const char* message);

    NativeA32Process& mProcess;
    oot3d::recomp::a32::GuestState mCallerState{};
    uint32_t mOwnerEntry = 0U;
    bool mActive = false;
};

} // namespace Oot3dNativeGame
