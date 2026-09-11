#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <span>
#include <tuple>
#include <vector>

namespace Fast::Oot3d {

inline constexpr uint32_t kGrassMidrangeClusterCapacity = 50;

struct GrassMidrangeRoot {
    std::array<float, 3> Position{};
    uint32_t StableId = 0;
    // Exact support identity, not interpolated barycentric weights.
    uint64_t Support = 0;
};

struct GrassMidrangeCluster {
    uint32_t FirstMember = 0;
    uint32_t MemberCount = 0;
    std::array<float, 3> Center{};
    float RootRadius = 0;
};

struct GrassMidrangeClusters {
    // Indices into the existing immutable roots; no duplicate root payload.
    std::vector<uint32_t> Members;
    std::vector<GrassMidrangeCluster> Groups;
    std::vector<uint32_t> GroupForRoot;
};

// Invoke separately per placement/mask owner, after mask acceptance. Cell
// extent is explicit world space, never enlarged to compensate sparse masks.
// RootAt is a view, avoiding an additional scene-sized copy of root data.
template <class RootAt>
GrassMidrangeClusters BuildGrassMidrangeClusters(
    size_t count, float cellExtent, RootAt rootAt) {
    if (!std::isfinite(cellExtent) || cellExtent <= 0 || count > UINT32_MAX)
        throw std::invalid_argument("invalid Grass midrange cluster extent/count");
    struct Entry {
        std::array<int64_t, 3> Cell;
        uint64_t Support;
        uint32_t StableId;
        uint32_t Index;
        auto Key() const { return std::tie(Cell, Support, StableId, Index); }
    };
    std::vector<Entry> entries;
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto root = rootAt(i);
        Entry entry{{}, root.Support, root.StableId, i};
        for (size_t axis = 0; axis < 3; ++axis) {
            const double cell = std::floor(double(root.Position[axis]) / cellExtent);
            if (!std::isfinite(cell) || cell < -0x1p63 || cell >= 0x1p63)
                throw std::invalid_argument("invalid Grass midrange root coordinate");
            entry.Cell[axis] = static_cast<int64_t>(cell);
        }
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.Key() < b.Key();
    });
    GrassMidrangeClusters result;
    result.Members.reserve(count);
    result.GroupForRoot.resize(count);
    for (size_t first = 0; first < entries.size();) {
        size_t end = first + 1;
        while (end < entries.size() && end - first < kGrassMidrangeClusterCapacity &&
               entries[end].Cell == entries[first].Cell &&
               entries[end].Support == entries[first].Support) ++end;
        auto lo = rootAt(entries[first].Index).Position;
        auto hi = lo;
        for (size_t i = first; i < end; ++i) {
            const auto p = rootAt(entries[i].Index).Position;
            for (size_t axis = 0; axis < 3; ++axis) {
                lo[axis] = std::min(lo[axis], p[axis]);
                hi[axis] = std::max(hi[axis], p[axis]);
            }
            result.Members.push_back(entries[i].Index);
            result.GroupForRoot[entries[i].Index] = static_cast<uint32_t>(result.Groups.size());
        }
        GrassMidrangeCluster group;
        group.FirstMember = static_cast<uint32_t>(first);
        group.MemberCount = static_cast<uint32_t>(end - first);
        for (size_t axis = 0; axis < 3; ++axis)
            group.Center[axis] = float((double(lo[axis]) + hi[axis]) * 0.5);
        double radiusSquared = 0;
        for (size_t i = first; i < end; ++i) {
            const auto p = rootAt(entries[i].Index).Position;
            double squared = 0;
            for (size_t axis = 0; axis < 3; ++axis) {
                const double delta = double(p[axis]) - group.Center[axis];
                squared += delta * delta;
            }
            radiusSquared = std::max(radiusSquared, squared);
        }
        group.RootRadius = std::nextafter(float(std::sqrt(radiusSquared)),
                                         std::numeric_limits<float>::infinity());
        result.Groups.push_back(group);
        first = end;
    }
    return result;
}

struct GrassMidrangeTopology {
    uint32_t VerticesPerBlade = 0;
    std::vector<uint16_t> Indices;
};

// Reorder only selected indices; bucket descriptors by occupancy so sparse
// groups draw an exact prefix rather than fifty mostly unused children.
inline std::vector<std::array<uint32_t, 2>> PackGrassMidrangeDraws(
    std::span<uint32_t> selected, std::span<const uint32_t> groupForRoot,
    uint32_t rootBase, uint32_t preparedBase) {
    for (uint32_t root : selected)
        if (root < rootBase || root-rootBase >= groupForRoot.size())
            throw std::invalid_argument("Grass group references an unrelated placement");
    std::sort(selected.begin(), selected.end(), [&](uint32_t a, uint32_t b) {
        return std::pair{groupForRoot[a-rootBase], a} < std::pair{groupForRoot[b-rootBase], b};
    });
    std::vector<std::array<uint32_t, 2>> result;
    for (uint32_t first = 0; first < selected.size();) {
        uint32_t end = first+1;
        while (end < selected.size() && groupForRoot[selected[end]-rootBase] == groupForRoot[selected[first]-rootBase]) ++end;
        if (end-first > kGrassMidrangeClusterCapacity)
            throw std::invalid_argument("Grass draw group exceeds capacity");
        result.push_back({preparedBase+first, end-first});
        first = end;
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return std::pair{a[1],a[0]} < std::pair{b[1],b[0]};
    });
    return result;
}

// One reusable indexed strip per child. Vertex / VerticesPerBlade selects
// the accepted member; a partial group draws the corresponding index prefix.
// No alpha-cutout quad or geometry shader is required for this representation.
inline GrassMidrangeTopology BuildGrassMidrangeTopology(uint32_t segments) {
    if (segments < 1 || segments > 2)
        throw std::invalid_argument("Grass midrange topology supports 1-2 segments");
    GrassMidrangeTopology result{2 * (segments + 1), {}};
    result.Indices.reserve(kGrassMidrangeClusterCapacity * segments * 6);
    for (uint32_t child = 0; child < kGrassMidrangeClusterCapacity; ++child) {
        for (uint32_t segment = 0; segment < segments; ++segment) {
            const auto a = static_cast<uint16_t>(child * result.VerticesPerBlade + segment * 2);
            for (uint16_t index : {a, uint16_t(a + 1), uint16_t(a + 2),
                                   uint16_t(a + 2), uint16_t(a + 1), uint16_t(a + 3)})
                result.Indices.push_back(index);
        }
    }
    return result;
}

} // namespace Fast::Oot3d
