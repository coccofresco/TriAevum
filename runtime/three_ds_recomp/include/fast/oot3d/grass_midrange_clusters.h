#pragma once
#include "fast/oot3d/grass_mask_interior.h"
#include "fast/oot3d/grass_cluster_limits.h"

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


struct GrassMidrangeRoot {
    std::array<float, 3> Position{};
    uint32_t StableId = 0;
    // Exact support identity, not interpolated barycentric weights.
    uint64_t Support = 0;
    float BladeRadius = 0;
    float StableVisibility = 0;
    std::array<float,2> Uv{};
};

struct GrassMidrangeCluster {
    uint32_t FirstMember = 0;
    uint32_t MemberCount = 0;
    std::array<float, 3> Center{};
    float RootRadius = 0;
    float MaximumBladeRadius = 0;
    float StableVisibility = 0;
};

struct GrassMidrangeClusters {
    struct Node {
        std::array<float,3> Center{};
        float Radius = 0;
        float MaximumBladeRadius = 0;
        float MinimumStableVisibility = 1;
        uint32_t First = 0, Count = 0, Escape = 0;
    };
    // Indices into the existing immutable roots; no duplicate root payload.
    std::vector<uint32_t> Members;
    std::vector<GrassMidrangeCluster> Groups;
    std::vector<uint32_t> GroupForRoot;
    std::vector<uint32_t> GroupOrder;
    std::vector<Node> Nodes;
    uint32_t LargeGroupCount = 0;
};

// Invoke separately per placement/mask owner, after mask acceptance. Cell
// extent is explicit world space, never enlarged to compensate sparse masks.
// RootAt is a view, avoiding an additional scene-sized copy of root data.
template <class RootAt, class Interior = GrassUnknownMaskInterior>
GrassMidrangeClusters BuildGrassMidrangeClusters(
    size_t count, float cellExtent, RootAt rootAt, bool adaptive = false, Interior interior = {}) {
    if (!std::isfinite(cellExtent) || cellExtent <= 0 || count > UINT32_MAX)
        throw std::invalid_argument("invalid Grass midrange cluster extent/count");
    struct Entry {
        std::array<int64_t, 3> Cell;
        uint64_t Support;
        uint32_t StableId;
        uint32_t Index;
        uint8_t Morton = 0;
        auto Key() const { return std::tie(Cell, Support, Morton, StableId, Index); }
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
            if (adaptive) {
                const auto local = static_cast<uint32_t>((entry.Cell[axis] % 4 + 4) % 4);
                entry.Cell[axis] = entry.Cell[axis] / 4 - (entry.Cell[axis] % 4 < 0 ? 1 : 0);
                entry.Morton |= static_cast<uint8_t>((local & 1U) << axis | (local >> 1U) << (axis+3));
            }
        }
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.Key() < b.Key();
    });
    GrassMidrangeClusters result;
    result.Members.reserve(count);
    result.GroupForRoot.resize(count);
    std::vector<std::pair<size_t,size_t>> ranges;
    const auto partition = [&](auto&& self,size_t first,size_t end,uint32_t depth) -> void {
        auto lo = rootAt(entries[first].Index).Uv, hi = lo;
        for (size_t i=first+1; i<end; ++i) {
            const auto uv = rootAt(entries[i].Index).Uv;
            for (size_t axis=0; axis<2; ++axis) { lo[axis]=std::min(lo[axis],uv[axis]); hi[axis]=std::max(hi[axis],uv[axis]); }
        }
        const bool uniform = adaptive && interior(lo,hi);
        const size_t capacity = uniform ? kGrassMidrangeClusterCapacity : kGrassBoundaryClusterCapacity;
        if ((uniform && end-first <= capacity) || depth == 0) {
            for (size_t begin=first; begin<end; begin+=capacity)
                ranges.emplace_back(begin,std::min(begin+capacity,end));
            return;
        }
        for (size_t begin=first; begin<end;) {
            size_t next=begin+1;
            const auto octant = entries[begin].Morton >> ((depth-1)*3);
            while (next<end && entries[next].Morton >> ((depth-1)*3) == octant) ++next;
            self(self,begin,next,depth-1);
            begin=next;
        }
    };
    for (size_t first = 0; first < entries.size();) {
        size_t end = first + 1;
        while (end < entries.size() &&
               entries[end].Cell == entries[first].Cell &&
               entries[end].Support == entries[first].Support) ++end;
        partition(partition,first,end,adaptive ? 2 : 0);
        first=end;
    }
    for (const auto& [first,end] : ranges) {
        // Spatial partitioning must not bias the nested far prefix toward one
        // corner: restore the camera-independent randomized identity order.
        if (adaptive) std::sort(entries.begin()+first,entries.begin()+end,[](const Entry& a,const Entry& b) {
            return std::pair{a.StableId,a.Index}<std::pair{b.StableId,b.Index};
        });
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
        if (group.MemberCount > kGrassBoundaryClusterCapacity) ++result.LargeGroupCount;
        group.StableVisibility = rootAt(entries[first].Index).StableVisibility;
        for (size_t axis = 0; axis < 3; ++axis)
            group.Center[axis] = float((double(lo[axis]) + hi[axis]) * 0.5);
        double radiusSquared = 0;
        for (size_t i = first; i < end; ++i) {
            group.MaximumBladeRadius = std::max(group.MaximumBladeRadius, rootAt(entries[i].Index).BladeRadius);
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
    }
    result.GroupOrder.resize(result.Groups.size());
    for (uint32_t i = 0; i < result.GroupOrder.size(); ++i) result.GroupOrder[i] = i;
    const auto buildNode = [&](auto&& self, uint32_t first, uint32_t count) -> void {
        const uint32_t nodeIndex = static_cast<uint32_t>(result.Nodes.size());
        result.Nodes.emplace_back();
        std::array<float,3> lo{INFINITY,INFINITY,INFINITY}, hi{-INFINITY,-INFINITY,-INFINITY};
        float blade = 0;
        for (uint32_t i=first; i<first+count; ++i) {
            const auto& group = result.Groups[result.GroupOrder[i]];
            for (size_t axis=0; axis<3; ++axis) {
                lo[axis]=std::min(lo[axis],group.Center[axis]-group.RootRadius);
                hi[axis]=std::max(hi[axis],group.Center[axis]+group.RootRadius);
            }
            blade=std::max(blade,group.MaximumBladeRadius);
            result.Nodes[nodeIndex].MinimumStableVisibility = std::min(
                result.Nodes[nodeIndex].MinimumStableVisibility, group.StableVisibility);
        }
        float squared=0;
        size_t split=0;
        for (size_t axis=0; axis<3; ++axis) {
            result.Nodes[nodeIndex].Center[axis]=(lo[axis]+hi[axis])*0.5F;
            const float half=(hi[axis]-lo[axis])*0.5F;
            squared+=half*half;
            if (hi[axis]-lo[axis]>hi[split]-lo[split]) split=axis;
        }
        result.Nodes[nodeIndex].Radius=std::nextafter(std::sqrt(squared),INFINITY);
        result.Nodes[nodeIndex].MaximumBladeRadius=blade;
        if (count<=16) {
            result.Nodes[nodeIndex].First=first;
            result.Nodes[nodeIndex].Count=count;
        } else {
            const auto begin=result.GroupOrder.begin()+first;
            std::nth_element(begin,begin+count/2,begin+count,[&](uint32_t a,uint32_t b) {
                return std::pair{result.Groups[a].Center[split],a}<std::pair{result.Groups[b].Center[split],b};
            });
            self(self,first,count/2);
            self(self,first+count/2,count-count/2);
        }
        result.Nodes[nodeIndex].Escape=static_cast<uint32_t>(result.Nodes.size());
    };
    if (!result.Groups.empty()) buildNode(buildNode,0,static_cast<uint32_t>(result.Groups.size()));
    return result;
}

struct GrassMidrangeTopology {
    uint32_t VerticesPerBlade = 0;
    std::vector<uint16_t> Indices;
};

// Nested stable prefixes: reducing detail never repositions a root or admits
// a previously rejected mask sample. Sparse clusters keep at least one root.
inline uint32_t GrassClusterChildCount(uint32_t members, float distance,
    float start, float end, float farFraction) noexcept {
    const float t = end > start ? std::clamp((distance-start)/(end-start), 0.0F, 1.0F) :
        (distance > start ? 1.0F : 0.0F);
    const float smooth = t*t*(3.0F-2.0F*t);
    return static_cast<uint32_t>(std::ceil(members * (1.0F-smooth*(1.0F-std::clamp(farFraction,0.02F,1.0F)))));
}

// Reorder only selected indices; bucket descriptors by occupancy so sparse
// groups draw an exact prefix rather than fifty mostly unused children.
inline std::vector<std::array<uint32_t, 2>> PackGrassMidrangeDraws(
    std::span<uint32_t> selected, std::span<const uint32_t> groupForRoot,
    uint32_t rootBase, uint32_t preparedBase, bool groupOrdered = false) {
    for (uint32_t root : selected)
        if (root < rootBase || root-rootBase >= groupForRoot.size())
            throw std::invalid_argument("Grass group references an unrelated placement");
    if (!groupOrdered) std::sort(selected.begin(), selected.end(), [&](uint32_t a, uint32_t b) {
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
