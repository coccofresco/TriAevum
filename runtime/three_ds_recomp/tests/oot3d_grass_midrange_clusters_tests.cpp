#include "fast/oot3d/grass_midrange_clusters.h"
#include "fast/oot3d/grass_indexed_topology.h"

#include <iostream>
#include <numeric>
#include <set>

using namespace Fast::Oot3d;

static void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
        std::vector<GrassMidrangeRoot> roots;
        for (uint32_t i = 0; i < 123; ++i)
            roots.push_back({{float(i % 10), 0, float(i / 10)}, i, 7});
        const auto build = [&](float extent) {
            return BuildGrassMidrangeClusters(roots.size(), extent, [&](uint32_t i) { return roots[i]; });
        };
        const auto groups = build(22);
        Check(groups.Groups.size() == 3, "50+50+23 partition");
        auto members = groups.Members;
        std::sort(members.begin(), members.end());
        for (uint32_t i = 0; i < roots.size(); ++i)
            Check(members[i] == i, "accepted roots must occur exactly once");
        for (const auto& group : groups.Groups) {
            Check(group.MemberCount <= 50, "cluster capacity");
            for (uint32_t i = 0; i < group.MemberCount; ++i) {
                const auto& root = roots[groups.Members[group.FirstMember + i]];
                double squared = 0;
                for (size_t axis = 0; axis < 3; ++axis)
                    squared += std::pow(double(root.Position[axis]) - group.Center[axis], 2);
                Check(squared <= double(group.RootRadius) * group.RootRadius, "root bound");
            }
        }
        std::reverse(roots.begin(), roots.end());
        const auto reversed = build(22);
        for (size_t i = 0; i < roots.size(); ++i)
            Check(roots[reversed.Members[i]].StableId == i, "stable membership across input order");

        roots = {{{-0.1F, 0, 0}, 0, 7}, {{0.1F, 0, 0}, 1, 7},
                 {{0.2F, 0, 0}, 2, 8}, {{0.2F, 23, 0}, 3, 8}};
        Check(build(22).Groups.size() == 4, "negative cells, supports and floors stay separate");
        roots.erase(roots.begin() + 1);
        Check(build(22).Members.size() == 3, "mask holes are never filled with new children");
        roots.clear();
        Check(build(22).Groups.empty(), "empty masks");
        bool rejected = false;
        try { (void)build(0); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "invalid extent");
        roots.push_back({{std::numeric_limits<float>::infinity(), 0, 0}, 1, 7});
        rejected = false;
        try { (void)build(22); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "nonfinite coordinates");

        for (uint32_t segments : {1U, 2U}) {
            const auto topology = BuildGrassMidrangeTopology(segments);
            Check(topology.Indices.size() == 50 * segments * 6, "shared topology size");
            for (size_t i = 0; i < topology.Indices.size(); ++i) {
                const uint32_t child = uint32_t(i) / (segments * 6);
                Check(topology.Indices[i] / topology.VerticesPerBlade == child,
                      "indices must not connect different children");
            }
        }
        const auto indexed = BuildGrassIndexedTopology();
        for (uint32_t segments : {1U, 2U})
            for (uint32_t planes : {1U, 2U}) {
                const auto blade = indexed.Blades[segments-1][planes-1];
                const auto cluster = indexed.Groups[segments-1][planes-1];
                Check(cluster.Count == 50*blade.Count, "indexed cluster capacity");
                for (uint32_t child = 0; child < 50; ++child)
                    for (uint32_t i = 0; i < blade.Count; ++i)
                        Check(indexed.Indices[cluster.First+child*blade.Count+i] ==
                            indexed.Indices[blade.First+i]+child*planes*(2*segments+1), "indexed child parity");
            }
        std::vector<uint32_t> selected{104,100,102,101};
        const std::vector<uint32_t> map{0,0,1,1,2};
        const auto packed = PackGrassMidrangeDraws(selected,map,100,10);
        Check(selected == std::vector<uint32_t>({100,101,102,104}), "selected stream grouped");
        Check(packed == std::vector<std::array<uint32_t,2>>({{12,1},{13,1},{10,2}}), "occupancy buckets and prepared offsets");
        Check(map[3] == 1 && selected.size() == 4, "unselected child remains absent");
        std::cout << "Grass midrange cluster invariants passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
