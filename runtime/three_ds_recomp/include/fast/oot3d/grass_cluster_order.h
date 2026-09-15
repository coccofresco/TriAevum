#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace Fast::Oot3d {

// Visibility traversal is spatial, but budget admission must remain in original
// cluster-index order. Large moving views should not pay comparison-sort costs.
template<class T, class Key>
void OrderGrassClusters(std::vector<T>& values, Key key) {
    if (values.size() < 1024) {
        std::sort(values.begin(), values.end(),
                  [&](const T& a, const T& b) { return key(a) < key(b); });
        return;
    }
    uint32_t maximum = 0;
    for (const auto& value : values) maximum = std::max(maximum, key(value));
    if (maximum == 0) return;
    constexpr uint32_t bits = 11;
    constexpr uint32_t buckets = 1U << bits;
    std::vector<T> scratch(values.size());
    T* source = values.data();
    T* destination = scratch.data();
    for (uint32_t shift = 0; shift < 32 && (maximum >> shift) != 0; shift += bits) {
        std::array<size_t, buckets> offsets{};
        for (size_t i = 0; i < values.size(); ++i) ++offsets[(key(source[i]) >> shift) & (buckets - 1)];
        size_t end = 0;
        for (auto& offset : offsets) {
            const auto count = offset;
            offset = end;
            end += count;
        }
        for (size_t i = 0; i < values.size(); ++i)
            destination[offsets[(key(source[i]) >> shift) & (buckets - 1)]++] = source[i];
        std::swap(source, destination);
    }
    if (source != values.data()) std::copy_n(source, values.size(), values.begin());
}

} // namespace Fast::Oot3d
