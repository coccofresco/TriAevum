#pragma once

#include <atomic>

namespace Ship {

// A signal may interrupt allocation or a driver call. Only publish intent;
// destruction belongs to the main loop after the interrupted work returns.
class ShutdownRequest final {
  public:
    static_assert(std::atomic<bool>::is_always_lock_free);

    static void HandleSignal(int) noexcept {
        sRequested.store(true, std::memory_order_relaxed);
    }

    static bool Requested() noexcept {
        return sRequested.load(std::memory_order_relaxed);
    }

    static void Reset() noexcept {
        sRequested.store(false, std::memory_order_relaxed);
    }

  private:
    inline static std::atomic<bool> sRequested{false};
};

} // namespace Ship
