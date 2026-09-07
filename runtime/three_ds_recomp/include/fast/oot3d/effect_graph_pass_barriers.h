#pragma once

#include "fast/oot3d/effect_graph.h"
#include "fast/oot3d/resource_state_tracker.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Fast::Oot3d {

struct EffectPassOutputBarrier {
    EffectResource Resource = EffectResource::Count;
    ResourceAccess DispatchAccess = ResourceAccess::Undefined;
    ResourceAccess CompletionAccess = ResourceAccess::Undefined;
    bool DownstreamRead = false;

    [[nodiscard]] bool Managed() const noexcept {
        return Resource != EffectResource::Count &&
               DispatchAccess != ResourceAccess::Undefined;
    }
};

class EffectPassBarrierExecution final {
  public:
    [[nodiscard]] uint32_t PlannedGraphTransitionCount() const noexcept;
    [[nodiscard]] uint32_t EmittedGraphTransitionCount() const noexcept;
    [[nodiscard]] uint32_t ElidedGraphTransitionCount() const noexcept;
    [[nodiscard]] uint32_t PlannedPrivateTransitionCount() const noexcept;
    [[nodiscard]] uint32_t EmittedPrivateTransitionCount() const noexcept;
    [[nodiscard]] uint32_t ElidedPrivateTransitionCount() const noexcept;

    void RecordGraphTransition(
        const ResourceTransition& transition) noexcept;
    void RecordPrivateTransition(
        const ResourceTransition& transition) noexcept;

  private:
    friend class EffectPassBarrierPlan;

    uint32_t mPlannedGraphTransitions = 0U;
    uint32_t mEmittedGraphTransitions = 0U;
    uint32_t mPlannedPrivateTransitions = 0U;
    uint32_t mEmittedPrivateTransitions = 0U;
};

class EffectPassBarrierPlan final {
  public:
    static constexpr size_t kOutputCapacity =
        static_cast<size_t>(EffectResource::Count);

    [[nodiscard]] const EffectPassOutputBarrier* Find(
        EffectResource resource) const noexcept;
    [[nodiscard]] const EffectPassOutputBarrier& OutputAt(
        size_t index) const noexcept;
    [[nodiscard]] size_t OutputCount() const noexcept;
    [[nodiscard]] uint32_t ManagedOutputCount() const noexcept;
    [[nodiscard]] uint32_t ShaderReadableOutputCount() const noexcept;
    [[nodiscard]] uint32_t WriteOnlyOutputCount() const noexcept;
    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] EffectPassBarrierExecution BeginExecution(
        uint32_t physicalMultiplicity = 1U) const noexcept;
    [[nodiscard]] EffectPassBarrierExecution BeginOutputExecution(
        EffectResource resource,
        uint32_t physicalMultiplicity = 1U) const noexcept;

  private:
    friend EffectPassBarrierPlan BuildEffectPassBarrierPlan(
        const CompiledEffectGraph&, std::string_view) noexcept;

    std::array<EffectPassOutputBarrier, kOutputCapacity> mOutputs{};
    size_t mOutputCount = 0U;
    uint32_t mManagedOutputCount = 0U;
    uint32_t mShaderReadableOutputCount = 0U;
    uint32_t mWriteOnlyOutputCount = 0U;
    bool mValid = false;
};

// Converts graph-visible producer/consumer relationships into the desired
// states surrounding one provider dispatch. Providers still emit barriers
// through their native API, but no longer decide whether an output must be
// readable or can remain write-only.
[[nodiscard]] EffectPassBarrierPlan BuildEffectPassBarrierPlan(
    const CompiledEffectGraph& graph,
    std::string_view passName) noexcept;

} // namespace Fast::Oot3d
