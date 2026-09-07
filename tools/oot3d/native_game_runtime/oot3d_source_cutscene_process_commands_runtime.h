#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Oot3dNativeGame {

class NativeA32Process;
class SourceCutsceneProcessCommandsBridge;

constexpr uint32_t kSourceCutsceneProcessCommandsEntry = 0x002C5BA0U;

struct SourceCutsceneProcessCommandsStats {
    uint64_t OwnerCalls = 0U;
    uint64_t NestedGuestCalls = 0U;
    uint64_t DirectDependencyCalls = 0U;
    uint64_t DynamicCallbackCalls = 0U;
    uint64_t GuestReadCalls = 0U;
    uint64_t GuestWriteCalls = 0U;
    uint64_t ScratchCalls = 0U;
    uint64_t HardFloatCalls = 0U;
    uint64_t ConversionOperations = 0U;
    uint64_t Failures = 0U;
};

// Executes the complete d5c2927 Cutscene_ProcessCommands source owner
// against the original 32-bit process image. Direct dependencies remain
// target-address calls until promoted independently.
class SourceCutsceneProcessCommandsRuntime {
  public:
    explicit SourceCutsceneProcessCommandsRuntime(NativeA32Process& process);
    ~SourceCutsceneProcessCommandsRuntime();

    SourceCutsceneProcessCommandsRuntime(
        const SourceCutsceneProcessCommandsRuntime&) = delete;
    SourceCutsceneProcessCommandsRuntime& operator=(
        const SourceCutsceneProcessCommandsRuntime&) = delete;

    bool Execute(uint32_t pc, oot3d::recomp::a32::GuestState& state,
                 oot3d::recomp::a32::MemoryBus& memory,
                 oot3d::recomp::a32::ExecutionResult* result,
                 uint32_t* blocksConsumed);

    SourceCutsceneProcessCommandsStats Stats() const noexcept;
    void ResetStats() noexcept;
    const std::string& LastError() const noexcept;

  private:
    std::unique_ptr<SourceCutsceneProcessCommandsBridge> mBridge;
};

} // namespace Oot3dNativeGame
