#include "fast/oot3d/effect_graph_pass_barriers.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace Fast::Oot3d {
namespace {

[[nodiscard]] ResourceAccess DispatchAccessFor(
    EffectResource resource) noexcept {
    switch (resource) {
        case EffectResource::NormalGuide:
        case EffectResource::MaterialGuide:
        case EffectResource::RigidMotionGuide:
        case EffectResource::AmbientGuide:
        case EffectResource::FogGuide:
        case EffectResource::OutlineGeometryGuide:
            return ResourceAccess::ColorAttachment;
        case EffectResource::NativeDepth:
        case EffectResource::NativeShadow2D:
        case EffectResource::DirectionalShadowMap:
            return ResourceAccess::DepthAttachment;
        case EffectResource::PresentationOutput:
            return ResourceAccess::ColorAttachment;
        case EffectResource::PicaSceneFrame:
        case EffectResource::NativeSceneView:
        case EffectResource::ExtensionGeometry:
        case EffectResource::Count:
            return ResourceAccess::Undefined;
        default:
            return ResourceAccess::ComputeWrite;
    }
}

[[nodiscard]] bool HasDownstreamRead(
    const CompiledEffectGraph& graph, size_t producerIndex,
    EffectResource resource) noexcept {
    for (size_t passIndex = producerIndex + 1U;
         passIndex < graph.Passes.size(); ++passIndex) {
        for (const auto& use : graph.Passes[passIndex].Resources) {
            if (use.Resource != resource) {
                continue;
            }
            if (ReadsEffectResource(use.Access)) {
                return true;
            }
            if (WritesEffectResource(use.Access)) {
                return false;
            }
        }
    }
    return graph.ExportsResource(resource);
}

[[nodiscard]] ResourceAccess CompletionAccessFor(
    EffectResource resource, ResourceAccess dispatchAccess,
    bool downstreamRead) noexcept {
    if (resource == EffectResource::PresentationOutput) {
        return ResourceAccess::Present;
    }
    if (downstreamRead) {
        return ResourceAccess::ShaderRead;
    }
    return dispatchAccess;
}

[[nodiscard]] uint32_t PlannedTransitionCount(
    uint32_t outputCount, uint32_t physicalMultiplicity) noexcept {
    const uint64_t count = static_cast<uint64_t>(outputCount) * 2U *
                           physicalMultiplicity;
    return static_cast<uint32_t>(std::min<uint64_t>(
        count, std::numeric_limits<uint32_t>::max()));
}

} // namespace

uint32_t EffectPassBarrierExecution::PlannedGraphTransitionCount() const
    noexcept {
    return mPlannedGraphTransitions;
}

uint32_t EffectPassBarrierExecution::EmittedGraphTransitionCount() const
    noexcept {
    return mEmittedGraphTransitions;
}

uint32_t EffectPassBarrierExecution::ElidedGraphTransitionCount() const
    noexcept {
    return mPlannedGraphTransitions > mEmittedGraphTransitions
        ? mPlannedGraphTransitions - mEmittedGraphTransitions
        : 0U;
}

uint32_t EffectPassBarrierExecution::PlannedPrivateTransitionCount() const
    noexcept {
    return mPlannedPrivateTransitions;
}

uint32_t EffectPassBarrierExecution::EmittedPrivateTransitionCount() const
    noexcept {
    return mEmittedPrivateTransitions;
}

uint32_t EffectPassBarrierExecution::ElidedPrivateTransitionCount() const
    noexcept {
    return mPlannedPrivateTransitions > mEmittedPrivateTransitions
        ? mPlannedPrivateTransitions - mEmittedPrivateTransitions
        : 0U;
}

void EffectPassBarrierExecution::RecordGraphTransition(
    const ResourceTransition& transition) noexcept {
    mEmittedGraphTransitions += transition.Required() ? 1U : 0U;
}

void EffectPassBarrierExecution::RecordPrivateTransition(
    const ResourceTransition& transition) noexcept {
    ++mPlannedPrivateTransitions;
    mEmittedPrivateTransitions += transition.Required() ? 1U : 0U;
}

const EffectPassOutputBarrier* EffectPassBarrierPlan::Find(
    EffectResource resource) const noexcept {
    const auto end = mOutputs.begin() + mOutputCount;
    const auto found = std::find_if(
        mOutputs.begin(), end,
        [resource](const EffectPassOutputBarrier& output) {
            return output.Resource == resource;
        });
    return found == end ? nullptr : &*found;
}

const EffectPassOutputBarrier& EffectPassBarrierPlan::OutputAt(
    size_t index) const noexcept {
    return mOutputs[index];
}

size_t EffectPassBarrierPlan::OutputCount() const noexcept {
    return mOutputCount;
}

uint32_t EffectPassBarrierPlan::ManagedOutputCount() const noexcept {
    return mManagedOutputCount;
}

uint32_t EffectPassBarrierPlan::ShaderReadableOutputCount() const noexcept {
    return mShaderReadableOutputCount;
}

uint32_t EffectPassBarrierPlan::WriteOnlyOutputCount() const noexcept {
    return mWriteOnlyOutputCount;
}

bool EffectPassBarrierPlan::Valid() const noexcept {
    return mValid;
}

EffectPassBarrierExecution EffectPassBarrierPlan::BeginExecution(
    uint32_t physicalMultiplicity) const noexcept {
    EffectPassBarrierExecution execution;
    if (mValid) {
        execution.mPlannedGraphTransitions = PlannedTransitionCount(
            mManagedOutputCount, physicalMultiplicity);
    }
    return execution;
}

EffectPassBarrierExecution EffectPassBarrierPlan::BeginOutputExecution(
    EffectResource resource, uint32_t physicalMultiplicity) const noexcept {
    EffectPassBarrierExecution execution;
    const auto* output = Find(resource);
    if (mValid && output != nullptr && output->Managed()) {
        execution.mPlannedGraphTransitions = PlannedTransitionCount(
            1U, physicalMultiplicity);
    }
    return execution;
}

EffectPassBarrierPlan BuildEffectPassBarrierPlan(
    const CompiledEffectGraph& graph,
    std::string_view passName) noexcept {
    EffectPassBarrierPlan plan;
    if (!graph.Valid()) {
        return plan;
    }
    const auto pass = std::find_if(
        graph.Passes.begin(), graph.Passes.end(),
        [passName](const CompiledEffectPass& candidate) {
            return candidate.Name == passName;
        });
    if (pass == graph.Passes.end()) {
        return plan;
    }
    const size_t passIndex = static_cast<size_t>(
        std::distance(graph.Passes.begin(), pass));
    for (const auto& use : pass->Resources) {
        if (!WritesEffectResource(use.Access) ||
            plan.mOutputCount >= plan.mOutputs.size()) {
            continue;
        }
        const ResourceAccess dispatch =
            DispatchAccessFor(use.Resource);
        const bool downstreamRead = HasDownstreamRead(
            graph, passIndex, use.Resource);
        const ResourceAccess completion = CompletionAccessFor(
            use.Resource, dispatch, downstreamRead);
        plan.mOutputs[plan.mOutputCount++] = {
            use.Resource, dispatch, completion, downstreamRead};
        if (dispatch == ResourceAccess::Undefined) {
            continue;
        }
        ++plan.mManagedOutputCount;
        if (completion == ResourceAccess::ShaderRead) {
            ++plan.mShaderReadableOutputCount;
        } else if (completion == dispatch) {
            ++plan.mWriteOnlyOutputCount;
        }
    }
    plan.mValid = true;
    return plan;
}

} // namespace Fast::Oot3d
