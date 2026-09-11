#include "fast/oot3d/grass_midrange_clusters.h"
#include "fast/oot3d/grass_indexed_topology.h"
#include "fast/oot3d/grass_cluster_selection.h"

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
            Check(topology.Indices.size() == kGrassMidrangeClusterCapacity * segments * 6, "shared topology size");
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
                Check(cluster.Count == kGrassMidrangeClusterCapacity*blade.Count, "indexed cluster capacity");
                for (uint32_t child = 0; child < kGrassMidrangeClusterCapacity; ++child)
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
        auto ordered = selected;
        Check(PackGrassMidrangeDraws(ordered,map,100,10,true) == packed && ordered == selected,
              "cluster-owned stream needs no root sort");
        Check(GrassClusterChildCount(50,200,500,2000,0.25F) == 50, "full medium cluster");
        Check(GrassClusterChildCount(50,2000,500,2000,0.25F) == 13, "reduced far cluster");
        GrassWorldPlacement world;
        for (uint32_t i = 0; i < 50; ++i) {
            world.CullingAnchors.push_back({{float(i)*0.01F,0,0}, i == 0 ? 0.1F : 0.99F});
        }
        world.Midrange = BuildGrassMidrangeClusters(50,22,[&](uint32_t i) {
            return GrassMidrangeRoot{world.CullingAnchors[i].Position,i,7,1,world.CullingAnchors[i].StableVisibility};
        });
        GrassLodPolicy policy;
        policy.DrawDistance = 2000; policy.LodReferenceDistance = 2000;
        policy.FarDensity = 0.2F; policy.NearBladeSegments = 2; policy.FarBladeSegments = 1;
        policy.SegmentStartDistance = 100; policy.SegmentEndDistance = 500;
        std::array<float,16> clip{}; clip[0]=clip[5]=clip[10]=clip[15]=1;
        std::vector<uint32_t> emitted;
        uint64_t evaluated=0;
        const auto select = [&](float distance, float fraction, uint32_t budget) {
            emitted.clear();
            return SelectGrassDrawableClusters(world,clip,{0,0,distance},policy,fraction,1,false,budget,evaluated,
                [&](uint32_t index,const GrassLodDecision&) { emitted.push_back(index); });
        };
        Check(select(1000,1,100) == 50, "group retention must precede child retention");
        Check(select(1000,1,49) == 0, "budget never tears a cluster");
        Check(select(2000,0.25F,100) == 13 && emitted.front() == 0, "far prefix preserves representative");
        Check(select(2100,1,100) == 0, "draw distance exclusion");
        world.CullingAnchors[0].StableVisibility=0.99F;
        Check(select(1000,1,100) == 0, "whole cluster rejection");
        world.Midrange.Nodes[0].MinimumStableVisibility=0.99F;
        evaluated=0;
        Check(select(1000,1,100) == 0 && evaluated==0, "density bound skips entire node");
        const auto tree = BuildGrassMidrangeClusters(100,22,[](uint32_t i) {
            return GrassMidrangeRoot{{float(i)*30,0,0},i,7,2};
        });
        Check(tree.Nodes.size()>1 && tree.Nodes.front().Escape==tree.Nodes.size(), "spatial hierarchy complete");
        std::set<uint32_t> leaves;
        for (uint32_t i=0; i<tree.Nodes.size(); ++i) {
            const auto& node=tree.Nodes[i];
            Check(node.Escape>i && node.Escape<=tree.Nodes.size(), "valid stackless escape");
            for (uint32_t j=0; j<node.Count; ++j) {
                const auto groupIndex=tree.GroupOrder[node.First+j];
                const auto& group=tree.Groups[groupIndex];
                const float delta=group.Center[0]-node.Center[0];
                Check(std::abs(delta)+group.RootRadius<=node.Radius+0.001F, "conservative leaf bounds");
                leaves.insert(groupIndex);
            }
        }
        Check(leaves.size()==100, "every cluster occurs in one leaf");
        std::vector<uint8_t> maskPixels(64*64,255);
        const auto makeInterior = [&] { return GrassMaskInterior(64,64,maskPixels,[](uint8_t x) { return x>0; }); };
        const auto white = makeInterior();
        Check(white.Contains({0,0},{1,1}), "uniform mask interior");
        maskPixels[32*64+32]=0;
        const auto hole = makeInterior();
        Check(!hole.Contains({0.2F,0.2F},{0.8F,0.8F}), "single texel hole cannot be hidden by coarse index");
        Check(hole.Contains({0,0},{0.1F,0.1F}), "unaffected interior remains usable");
        std::vector<uint8_t> grayPixels(64*64,128);
        const GrassMaskInterior gray(64,64,grayPixels,[](uint8_t x) { return x>0; });
        Check(gray.Contains({0,0},{1,1}), "accepted grayscale is not a mask hole");
        std::vector<GrassMidrangeRoot> adaptiveRoots;
        for (uint32_t i=0; i<120; ++i) {
            const float x=float(i%60);
            adaptiveRoots.push_back({{x,0,float(i/60)},i,7,1,0.1F,{0.2F+x/100,0.5F}});
        }
        const auto makeAdaptive = [&](const GrassMaskInterior& mask) {
            return BuildGrassMidrangeClusters(adaptiveRoots.size(),22,[&](uint32_t i) { return adaptiveRoots[i]; },
                true,[&](auto lo,auto hi) { return mask.Contains(lo,hi); });
        };
        const auto interiorGroups=makeAdaptive(white);
        Check(interiorGroups.Groups.size()==1 && interiorGroups.LargeGroupCount==1 &&
              interiorGroups.Groups[0].MemberCount==120, "uniform support merges across small cells");
        const auto boundaryGroups=makeAdaptive(hole);
        Check(boundaryGroups.Groups.size()>1 && boundaryGroups.Members.size()==adaptiveRoots.size(), "mask hole forces adaptive split without dropping roots");
        for (const auto& group: boundaryGroups.Groups) {
            if (group.MemberCount>50) {
                auto lo=adaptiveRoots[boundaryGroups.Members[group.FirstMember]].Uv, hi=lo;
                for (uint32_t i=0;i<group.MemberCount;++i) {
                    const auto uv=adaptiveRoots[boundaryGroups.Members[group.FirstMember+i]].Uv;
                    for (size_t axis=0;axis<2;++axis) { lo[axis]=std::min(lo[axis],uv[axis]); hi[axis]=std::max(hi[axis],uv[axis]); }
                }
                Check(hole.Contains(lo,hi), "large groups require certified interior");
            }
        }
        std::reverse(adaptiveRoots.begin(),adaptiveRoots.end());
        const auto stableAdaptive=makeAdaptive(white);
        for (uint32_t i=0;i<120;++i) Check(adaptiveRoots[stableAdaptive.Members[i]].StableId==i,
            "adaptive membership independent of input traversal");
        uint32_t previous = 50;
        for (int distance = 0; distance <= 2200; ++distance) {
            const auto count = GrassClusterChildCount(50,float(distance),500,2000,0.25F);
            Check(count <= previous && count >= 13, "nested monotonic far detail");
            previous = count;
            Check(GrassClusterChildCount(1,float(distance),500,2000,0.25F) == 1, "sparse mask retains its root");
        }
        std::cout << "Grass midrange cluster invariants passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
