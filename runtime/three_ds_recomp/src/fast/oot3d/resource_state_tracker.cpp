#include "fast/oot3d/resource_state_tracker.h"

namespace Fast::Oot3d {

ResourceTransition ResourceStateTracker::PlanTransition(
    uintptr_t resource, ResourceState desired) const {
    const auto found = mStates.find(resource);
    const ResourceState before = found == mStates.end() ? ResourceState{} : found->second;
    return { resource, before, desired };
}

void ResourceStateTracker::Commit(const ResourceTransition& transition) {
    mStates[transition.Resource] = transition.After;
}

ResourceTransition ResourceStateTracker::Transition(
    uintptr_t resource, ResourceState desired) {
    const ResourceTransition transition = PlanTransition(resource, desired);
    Commit(transition);
    return transition;
}

std::optional<ResourceState> ResourceStateTracker::Find(uintptr_t resource) const {
    const auto it = mStates.find(resource);
    return it == mStates.end() ? std::nullopt : std::optional<ResourceState>(it->second);
}

void ResourceStateTracker::Forget(uintptr_t resource) { mStates.erase(resource); }
void ResourceStateTracker::Clear() { mStates.clear(); }

} // namespace Fast::Oot3d
