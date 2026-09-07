#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Oot3dNativeGame {

class NativeA32Process;
class SourceCutsceneUpdateFrameBridge;

constexpr uint32_t kSourceCutsceneUpdateFrameEntry = 0x00321F50U;

struct SourceCutsceneUpdateFrameStats {
    uint64_t OwnerCalls = 0U;
    uint64_t NestedGuestCalls = 0U;
    uint64_t ClockQueryCalls = 0U;
    uint64_t ClockCommitCalls = 0U;
    uint64_t ProcessCommandCalls = 0U;
    uint64_t Failures = 0U;
};

// Executes the d5c2927 Cutscene_UpdateFrame source owner against the original
// 32-bit process image. The command parser remains a measured guest
// dependency until its independently large owner is promoted.
class SourceCutsceneUpdateFrameRuntime {
  public:
    explicit SourceCutsceneUpdateFrameRuntime(NativeA32Process& process);
    ~SourceCutsceneUpdateFrameRuntime();

    SourceCutsceneUpdateFrameRuntime(
        const SourceCutsceneUpdateFrameRuntime&) = delete;
    SourceCutsceneUpdateFrameRuntime& operator=(
        const SourceCutsceneUpdateFrameRuntime&) = delete;

    bool Execute(uint32_t pc, oot3d::recomp::a32::GuestState& state,
                 oot3d::recomp::a32::MemoryBus& memory,
                 oot3d::recomp::a32::ExecutionResult* result,
                 uint32_t* blocksConsumed);

    SourceCutsceneUpdateFrameStats Stats() const noexcept;
    void ResetStats() noexcept;
    const std::string& LastError() const noexcept;

  private:
    std::unique_ptr<SourceCutsceneUpdateFrameBridge> mBridge;
};

} // namespace Oot3dNativeGame
