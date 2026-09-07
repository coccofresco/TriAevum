#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Oot3dNativeGame {

class NativeA32Process;
class SourceActorUpdateAllBridge;

constexpr uint32_t kSourceActorUpdateAllEntry = 0x00461344U;

struct SourceActorUpdateAllStats {
    uint64_t OwnerCalls = 0U;
    uint64_t NestedGuestCalls = 0U;
    uint64_t SvcCalls = 0U;
    uint64_t CallbackCalls = 0U;
    uint64_t InitCallbackCalls = 0U;
    uint64_t UpdateCallbackCalls = 0U;
    uint64_t SourceCallbackCalls = 0U;
    uint64_t SourceObjHanaInitCalls = 0U;
    uint64_t SourceCallbackFailures = 0U;
    uint64_t DestroyCalls = 0U;
    uint64_t FreeCalls = 0U;
    uint64_t RegistryReleaseScans = 0U;
    uint64_t Failures = 0U;
};

// Executes the revision-pinned Actor_UpdateAll source owner against the
// process guest address space. Every unresolved dependency is dispatched by
// guest address through NativeA32Process; no target pointer is called as a
// host function.
class SourceActorUpdateAllRuntime {
  public:
    explicit SourceActorUpdateAllRuntime(NativeA32Process& process);
    ~SourceActorUpdateAllRuntime();

    SourceActorUpdateAllRuntime(const SourceActorUpdateAllRuntime&) = delete;
    SourceActorUpdateAllRuntime& operator=(
        const SourceActorUpdateAllRuntime&) = delete;

    bool Execute(uint32_t pc, oot3d::recomp::a32::GuestState& state,
                 oot3d::recomp::a32::MemoryBus& memory,
                 oot3d::recomp::a32::ExecutionResult* result,
                 uint32_t* blocksConsumed);

    SourceActorUpdateAllStats Stats() const noexcept;
    void ResetStats() noexcept;
    const std::string& LastError() const noexcept;

  private:
    std::unique_ptr<SourceActorUpdateAllBridge> mBridge;
};

} // namespace Oot3dNativeGame
