#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Oot3dNativeGame {

class NativeA32Process;
class SourcePlayerUpdateBridge;

constexpr uint32_t kSourcePlayerUpdateEntry = 0x001E1B54U;

struct SourcePlayerUpdateStats {
    uint64_t OwnerCalls = 0U;
    uint64_t NestedGuestCalls = 0U;
    uint64_t DirectDependencyCalls = 0U;
    uint64_t PlayerUpdateCommonCalls = 0U;
    uint64_t ActorSpawnCalls = 0U;
    uint64_t GuestReadCalls = 0U;
    uint64_t GuestWriteCalls = 0U;
    uint64_t ScratchCalls = 0U;
    uint64_t HardFloatCalls = 0U;
    uint64_t VfpOperations = 0U;
    uint64_t FloatConversions = 0U;
    uint64_t Failures = 0U;
};

// Executes the complete d5c2927 Player_Update source owner against the
// original 32-bit process image. Player_UpdateCommon and the six remaining
// direct dependencies retain target-address dispatch.
class SourcePlayerUpdateRuntime {
  public:
    explicit SourcePlayerUpdateRuntime(NativeA32Process& process);
    ~SourcePlayerUpdateRuntime();

    SourcePlayerUpdateRuntime(const SourcePlayerUpdateRuntime&) = delete;
    SourcePlayerUpdateRuntime& operator=(
        const SourcePlayerUpdateRuntime&) = delete;

    bool Execute(
        uint32_t pc, oot3d::recomp::a32::GuestState& state,
        oot3d::recomp::a32::MemoryBus& memory,
        oot3d::recomp::a32::ExecutionResult* result,
        uint32_t* blocksConsumed);

    SourcePlayerUpdateStats Stats() const noexcept;
    void ResetStats() noexcept;
    const std::string& LastError() const noexcept;

  private:
    std::unique_ptr<SourcePlayerUpdateBridge> mBridge;
};

} // namespace Oot3dNativeGame
