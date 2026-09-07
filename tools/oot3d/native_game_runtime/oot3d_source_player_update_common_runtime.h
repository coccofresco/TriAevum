#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Oot3dNativeGame {

class NativeA32Process;
class SourcePlayerUpdateCommonBridge;

constexpr uint32_t kSourcePlayerUpdateCommonEntry = 0x00250AD0U;

struct SourcePlayerUpdateCommonStats {
    uint64_t OwnerCalls = 0U;
    uint64_t NestedGuestCalls = 0U;
    uint64_t DirectDependencyCalls = 0U;
    uint64_t DynamicPlayerActionCalls = 0U;
    uint64_t GuestReadCalls = 0U;
    uint64_t GuestWriteCalls = 0U;
    uint64_t HardFloatCalls = 0U;
    uint64_t VfpOperations = 0U;
    uint64_t FloatConversions = 0U;
    uint64_t SquareRootOperations = 0U;
    uint64_t Failures = 0U;
};

// Executes the complete d5c2927 Player_UpdateCommon source owner against
// the original 32-bit process image. Its action callback and all direct
// dependencies retain checked target-address dispatch.
class SourcePlayerUpdateCommonRuntime {
  public:
    explicit SourcePlayerUpdateCommonRuntime(NativeA32Process& process);
    ~SourcePlayerUpdateCommonRuntime();

    SourcePlayerUpdateCommonRuntime(
        const SourcePlayerUpdateCommonRuntime&) = delete;
    SourcePlayerUpdateCommonRuntime& operator=(
        const SourcePlayerUpdateCommonRuntime&) = delete;

    bool Execute(
        uint32_t pc, oot3d::recomp::a32::GuestState& state,
        oot3d::recomp::a32::MemoryBus& memory,
        oot3d::recomp::a32::ExecutionResult* result,
        uint32_t* blocksConsumed);

    SourcePlayerUpdateCommonStats Stats() const noexcept;
    void ResetStats() noexcept;
    const std::string& LastError() const noexcept;

  private:
    std::unique_ptr<SourcePlayerUpdateCommonBridge> mBridge;
};

} // namespace Oot3dNativeGame
