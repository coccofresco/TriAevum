#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>

namespace Oot3dSourceRuntime {

using SourceExecutionCallback = void (*)(void* context);

struct SourceExecutionResult {
    bool Invoked = false;
    bool ThreadExitRequested = false;
    std::exception_ptr Exception;
};

class SourceExecutionStack {
  public:
    SourceExecutionStack() = default;
    ~SourceExecutionStack();

    SourceExecutionStack(const SourceExecutionStack&) = delete;
    SourceExecutionStack& operator=(const SourceExecutionStack&) = delete;
    SourceExecutionStack(SourceExecutionStack&&) = delete;
    SourceExecutionStack& operator=(SourceExecutionStack&&) = delete;

    bool Initialize(std::string* error = nullptr);
    SourceExecutionResult Invoke(SourceExecutionCallback callback,
                                 void* context) noexcept;

    bool IsInitialized() const;
    std::uintptr_t StackLimit() const;
    std::uintptr_t StackBase() const;

  private:
    void* mAllocation = nullptr;
    void* mStackLimit = nullptr;
    void* mStackBase = nullptr;
    bool mInvoking = false;
};

[[noreturn]] void ExitCurrentSourceThread();

std::span<const std::byte> ResolveCurrentSourceStackRead(
    std::uint32_t address, std::size_t size);
std::span<std::byte> ResolveCurrentSourceStackWrite(
    std::uint32_t address, std::size_t size);

} // namespace Oot3dSourceRuntime
