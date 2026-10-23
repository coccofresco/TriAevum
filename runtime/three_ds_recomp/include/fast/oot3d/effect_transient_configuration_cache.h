#pragma once

#include "fast/oot3d/effect_graph_physical_plan.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Fast::Oot3d {

// Equal slots and resources (formats, extents, lifetimes and alias slots):
// the same images serve both plans.
[[nodiscard]] bool SameEffectTransientAllocation(
    const EffectTransientAllocationPlan& lhs,
    const EffectTransientAllocationPlan& rhs) noexcept;

// The images of `cached` can serve `plan`: each requested resource exists
// there with the same requirement and storage class, and resources that
// `plan` keeps in distinct slots stay in distinct images (no new aliasing).
// A pause page that drops one scene effect keeps the gameplay images.
[[nodiscard]] bool EffectTransientAllocationCovers(
    const EffectTransientAllocationPlan& cached,
    const EffectTransientAllocationPlan& plan) noexcept;

// The display graph runs several invocations per frame (native boundaries,
// presentations), each with its own transient plan. One image configuration
// per recurring plan lets them alternate without destroying images, the
// providers' descriptors or the temporal history; only replacing the least
// recently used configuration does.
class EffectTransientConfigurationCache final {
  public:
    static constexpr size_t kCapacity = 4U;

    struct Selection {
        size_t Index = 0U;
        // A cached configuration already serves the plan.
        bool Hit = false;
        // The configuration at Index serves another plan and is replaced.
        bool Evicts = false;
        // Hit through a configuration whose plan covers this one: it keeps
        // its own plan (Touch, not Commit).
        bool Covers = false;
    };

    [[nodiscard]] Selection Select(
        const EffectTransientAllocationPlan& plan) const noexcept;
    // Index now serves the plan and is the most recently used.
    void Commit(size_t index, const EffectTransientAllocationPlan& plan) noexcept;
    // Index is the most recently used and keeps its plan.
    void Touch(size_t index) noexcept;
    void Remove(size_t index) noexcept;
    void Clear() noexcept;
    [[nodiscard]] size_t Count() const noexcept;

  private:
    std::array<EffectTransientAllocationPlan, kCapacity> mPlans{};
    std::array<uint64_t, kCapacity> mLastUse{};
    std::array<bool, kCapacity> mOccupied{};
    uint64_t mClock = 0U;
};

} // namespace Fast::Oot3d
