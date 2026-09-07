#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace Fast::Oot3d {

// Worker bins are sorted subsequences of the original anchor traversal, with
// each anchor in exactly one bin. Keep the same prefix a serial budget would
// choose, without sorting/copying all visible indices or favoring a topology.
template <size_t N>
uint32_t LimitGrassSelectionBins(std::array<std::vector<uint32_t>, N>& bins, uint32_t budget) {
    size_t total = 0;
    uint32_t low = std::numeric_limits<uint32_t>::max();
    uint32_t high = 0;
    for (const auto& bin : bins) {
        total += bin.size();
        if (!bin.empty()) {
            low = std::min(low, bin.front());
            high = std::max(high, bin.back());
        }
    }
    if (total <= budget) return static_cast<uint32_t>(total);
    if (budget == 0) {
        for (auto& bin : bins) bin.clear();
        return 0;
    }
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2;
        size_t prefix = 0;
        for (const auto& bin : bins)
            prefix += std::upper_bound(bin.begin(), bin.end(), middle) - bin.begin();
        if (prefix < budget) low = middle + 1;
        else high = middle;
    }
    for (auto& bin : bins)
        bin.resize(std::upper_bound(bin.begin(), bin.end(), low) - bin.begin());
    return budget;
}

} // namespace Fast::Oot3d
