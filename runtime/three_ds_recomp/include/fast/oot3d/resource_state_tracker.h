#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace Fast::Oot3d {

enum class ResourceAccess : uint8_t {
    Undefined, ColorAttachment, DepthAttachment, ShaderRead, ComputeWrite, TransferRead,
    TransferWrite, StorageClear, StorageReadWrite, Present
};

struct ResourceState {
    ResourceAccess Access = ResourceAccess::Undefined;
    uint32_t QueueFamily = 0;
};

struct ResourceTransition {
    uintptr_t Resource = 0;
    ResourceState Before{};
    ResourceState After{};
    [[nodiscard]] bool Required() const { return Before.Access != After.Access || Before.QueueFamily != After.QueueFamily; }
};

class ResourceStateTracker final {
  public:
    [[nodiscard]] ResourceTransition PlanTransition(
        uintptr_t resource, ResourceState desired) const;
    void Commit(const ResourceTransition& transition);
    ResourceTransition Transition(uintptr_t resource, ResourceState desired);
    [[nodiscard]] std::optional<ResourceState> Find(uintptr_t resource) const;
    void Forget(uintptr_t resource);
    void Clear();

  private:
    std::unordered_map<uintptr_t, ResourceState> mStates;
};

} // namespace Fast::Oot3d
