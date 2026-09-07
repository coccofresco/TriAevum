#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Oot3dNativeGame {

class NativeA32Process;
class SourceCameraUpdateBridge;

constexpr uint32_t kSourceCameraUpdateEntry = 0x002D84C4U;

struct SourceCameraUpdateStats {
    uint64_t OwnerCalls = 0U;
    uint64_t NestedGuestCalls = 0U;
    uint64_t DirectDependencyCalls = 0U;
    uint64_t DynamicCameraFunctionCalls = 0U;
    uint64_t GuestReadCalls = 0U;
    uint64_t GuestWriteCalls = 0U;
    uint64_t ScratchCalls = 0U;
    uint64_t HardFloatCalls = 0U;
    uint64_t SquareRootOperations = 0U;
    uint64_t Failures = 0U;
};

// Executes the complete d5c2927 Camera_Update source owner against the
// original 32-bit process image. Camera mode functions and direct
// dependencies remain target-address calls until promoted independently.
class SourceCameraUpdateRuntime {
  public:
    explicit SourceCameraUpdateRuntime(NativeA32Process& process);
    ~SourceCameraUpdateRuntime();

    SourceCameraUpdateRuntime(const SourceCameraUpdateRuntime&) = delete;
    SourceCameraUpdateRuntime& operator=(
        const SourceCameraUpdateRuntime&) = delete;

    bool Execute(uint32_t pc, oot3d::recomp::a32::GuestState& state,
                 oot3d::recomp::a32::MemoryBus& memory,
                 oot3d::recomp::a32::ExecutionResult* result,
                 uint32_t* blocksConsumed);

    SourceCameraUpdateStats Stats() const noexcept;
    void ResetStats() noexcept;
    const std::string& LastError() const noexcept;

  private:
    std::unique_ptr<SourceCameraUpdateBridge> mBridge;
};

} // namespace Oot3dNativeGame
