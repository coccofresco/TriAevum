#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Oot3dNativeGame {

class NativeA32Process;
class SourceActorInitContextBridge;

constexpr uint32_t kSourceActorInitContextEntry = 0x0044E7C0U;

struct SourceActorInitContextStats {
    uint64_t OwnerCalls = 0U;
    uint64_t NestedGuestCalls = 0U;
    uint64_t DirectDependencyCalls = 0U;
    uint64_t DynamicFactoryCalls = 0U;
    uint64_t DynamicAllocatorCalls = 0U;
    uint64_t ActorSpawnCalls = 0U;
    uint64_t LightSetupCalls = 0U;
    uint64_t TailHelperCalls = 0U;
    uint64_t Failures = 0U;
};

// Executes the d5c2927 Actor_InitContext source owner against the original
// 32-bit process image. All target globals and callbacks remain guest
// addresses and are resolved through NativeA32OwnerCallAdapter.
class SourceActorInitContextRuntime {
  public:
    explicit SourceActorInitContextRuntime(NativeA32Process& process);
    ~SourceActorInitContextRuntime();

    SourceActorInitContextRuntime(
        const SourceActorInitContextRuntime&) = delete;
    SourceActorInitContextRuntime& operator=(
        const SourceActorInitContextRuntime&) = delete;

    bool Execute(uint32_t pc, oot3d::recomp::a32::GuestState& state,
                 oot3d::recomp::a32::MemoryBus& memory,
                 oot3d::recomp::a32::ExecutionResult* result,
                 uint32_t* blocksConsumed);

    SourceActorInitContextStats Stats() const noexcept;
    void ResetStats() noexcept;
    const std::string& LastError() const noexcept;

  private:
    std::unique_ptr<SourceActorInitContextBridge> mBridge;
};

} // namespace Oot3dNativeGame
