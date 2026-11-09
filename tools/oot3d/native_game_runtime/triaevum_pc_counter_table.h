#pragma once

// A fixed, lock-free per-PC counter table (G2-F5). Whole-AOT observable exits
// leave compiled code at a small, known set of guest PCs (about 19 exits a
// frame); counting them must not take a global mutex or allocate. Each slot
// holds one PC (stored as PC + 1, so 0 marks a free slot) and its count; an
// add probes linearly from the PC's hash and claims a free slot once with a
// compare-exchange. A table that is full counts the exit as Overflow, so the
// total stays exact.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {

template <std::size_t Slots>
class PcCounterTable {
  static_assert(Slots != 0U && (Slots & (Slots - 1U)) == 0U, "Slots must be a power of two");

public:
  void Add(uint32_t pc, uint64_t amount = 1U) noexcept {
    const uint64_t key = uint64_t(pc) + 1U;
    std::size_t index = (uint64_t(pc) * 0x9E3779B97F4A7C15ULL) >> 32U;
    for (std::size_t probe = 0; probe < Slots; ++probe, ++index) {
      auto& slot = mSlots[index & (Slots - 1U)];
      uint64_t current = slot.Key.load(std::memory_order_acquire);
      if (current == 0U &&
          slot.Key.compare_exchange_strong(current, key, std::memory_order_acq_rel, std::memory_order_acquire))
        current = key;
      if (current == key) {
        slot.Count.fetch_add(amount, std::memory_order_relaxed);
        return;
      }
    }
    mOverflow.fetch_add(amount, std::memory_order_relaxed);
  }

  // The counted PCs, ascending, and the total (overflow included).
  [[nodiscard]] std::vector<std::pair<uint32_t, uint64_t>> Snapshot(uint64_t* total = nullptr) const {
    std::vector<std::pair<uint32_t, uint64_t>> result;
    uint64_t sum = mOverflow.load(std::memory_order_relaxed);
    for (const auto& slot : mSlots) {
      const uint64_t key = slot.Key.load(std::memory_order_acquire);
      if (key == 0U) continue;
      const uint64_t count = slot.Count.load(std::memory_order_relaxed);
      sum += count;
      result.emplace_back(uint32_t(key - 1U), count);
    }
    std::sort(result.begin(), result.end());
    if (total != nullptr) *total = sum;
    return result;
  }

  [[nodiscard]] uint64_t Overflow() const noexcept { return mOverflow.load(std::memory_order_relaxed); }

private:
  struct Slot {
    std::atomic<uint64_t> Key{0};
    std::atomic<uint64_t> Count{0};
  };
  std::array<Slot, Slots> mSlots{};
  std::atomic<uint64_t> mOverflow{0};
};

} // namespace Oot3dNativeGame
