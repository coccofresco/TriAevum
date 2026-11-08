#pragma once

#include <atomic>
#include <cstddef>

// An error exit abandons the renderer's background startup preparation: the
// parts no worker has started are skipped (what already finished still
// reaches the persisted caches), so the fatal teardown, which joins the
// preparation workers, never waits minutes for the remaining parts
// (Q-INTEG-R10-2). A normal quit never abandons it: preparation then
// finishes for the caches as before. Thread lifecycle only: it never changes
// which program or pipeline a draw requests.
namespace Fast::StartupPreparation {

inline std::atomic<bool> gAbandoned{false};

inline void Abandon() noexcept { gAbandoned.store(true, std::memory_order_release); }

[[nodiscard]] inline bool Abandoned() noexcept { return gAbandoned.load(std::memory_order_acquire); }

// Claims the next of `count` startup parts into `index`: false once every
// part is claimed or startup preparation was abandoned.
[[nodiscard]] inline bool ClaimPart(std::atomic<size_t>& next, size_t count, size_t& index) noexcept {
    if (Abandoned()) return false;
    index = next++;
    return index < count;
}

} // namespace Fast::StartupPreparation
