#include "fast/oot3d/grass_visibility.h"
#include "fast/oot3d/grass_world_placement_cache.h"
#include "fast/oot3d/grass_cluster_order.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main() try {
    using namespace Fast::Oot3d;
    GrassWorldPlacement world;
    for (uint32_t i=0; i<128; ++i) {
        std::array<float,3> center{float(i%16)*40-300, float(i%3)*10, float(i/16)*40-140};
        world.Clusters.push_back({i*8,8,center,12,2,0.02F});
        world.VisibilityClusterOrder.push_back(i);
        for (uint32_t j=0; j<8; ++j) world.CullingAnchors.push_back({center,0.02F+j*0.13F});
    }
    const auto build = [&](auto&& self, uint32_t first, uint32_t count) -> void {
        const auto index = world.VisibilityNodes.size();
        world.VisibilityNodes.emplace_back();
        std::array<float,3> lo,hi;
        lo.fill(std::numeric_limits<float>::max());
        hi.fill(std::numeric_limits<float>::lowest());
        for (uint32_t i=first; i<first+count; ++i) for (size_t axis=0; axis<3; ++axis) {
            lo[axis]=std::min(lo[axis],world.Clusters[i].Center[axis]-12);
            hi[axis]=std::max(hi[axis],world.Clusters[i].Center[axis]+12);
        }
        float squared=0;
        for (size_t axis=0; axis<3; ++axis) {
            world.VisibilityNodes[index].Center[axis]=(lo[axis]+hi[axis])*0.5F;
            const auto half=(hi[axis]-lo[axis])*0.5F;
            squared+=half*half;
        }
        world.VisibilityNodes[index].Radius=std::sqrt(squared);
        world.VisibilityNodes[index].MaximumBladeHeight=2;
        world.VisibilityNodes[index].MinimumStableVisibility=0.02F;
        if (count<=4) {
            world.VisibilityNodes[index].FirstCluster=first;
            world.VisibilityNodes[index].ClusterCount=count;
        } else {
            self(self,first,count/2);
            self(self,first+count/2,count-count/2);
        }
        world.VisibilityNodes[index].Escape=static_cast<uint32_t>(world.VisibilityNodes.size());
    };
    build(build,0,128);
    const std::array<float,16> clip{0.006F,0,0,0, 0,0.006F,0,0, 0,0,0.001F,0, 0,0,0,1};
    {
        std::vector<uint32_t> roots{17};
        GrassLodPolicy policy;
        if (SplitGrassClusterSelection(world,clip,{0,0,0},policy,1,true,0,roots)!=0 || !roots.empty())
            throw std::runtime_error("zero-job partition must clear stale roots");
        roots.push_back(17);
        if (SplitGrassClusterSelection({},clip,{0,0,0},policy,1,true,8,roots)!=0 || !roots.empty())
            throw std::runtime_error("empty partition must clear stale roots");
    }
    size_t cases=0;
    for (bool frustum : {false,true}) for (float distance : {50.0F,250.0F,1000.0F})
    for (float x : {0.0F,200.0F,10000.0F}) for (uint32_t jobs : {1U,2U,3U,10U,64U}) {
        GrassLodPolicy policy;
        policy.DrawDistance=distance;
        policy.LodStart=0.2F;
        policy.LodEnd=0.8F;
        policy.FarDensity=0.1F;
        std::array<float,3> eye{x,0,0};
        std::vector<GrassClusterWork> expected,actual,partial;
        const auto reference=SelectGrassClusterWork(world,clip,eye,policy,3,frustum,expected);
        std::vector<uint32_t> roots;
        (void)SplitGrassClusterSelection(world,clip,eye,policy,3,frustum,jobs,roots);
        if (roots.size()>jobs) throw std::runtime_error("unbounded selection jobs");
        uint64_t anchors=0;
        for (auto root=roots.rbegin(); root!=roots.rend(); ++root) {
            const auto stats=SelectGrassClusterSubtreeWork(world,clip,eye,policy,3,frustum,*root,partial);
            anchors+=stats.CandidateAnchors;
            actual.insert(actual.end(),partial.begin(),partial.end());
        }
        OrderGrassClusters(actual,[](const auto& w){return w.ClusterIndex;});
        if (actual!=expected || anchors!=reference.CandidateAnchors)
            throw std::runtime_error("partition changes work, visibility or retained prefix");
        ++cases;
    }
    std::cout << "Exact serial/partitioned visibility: " << cases << " cases\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
