#include "fast/oot3d/grass_cluster_order.h"
#include "fast/oot3d/grass_visibility.h"
#include <iostream>
#include <random>
#include <stdexcept>

int main() try {
    using namespace Fast::Oot3d;
    std::mt19937 random(813);
    for (size_t count : {0U,1U,1023U,1024U,65536U,100000U})
    for (uint32_t mask : {0U,1023U,0x3ffffU,0xffffffffU}) {
        std::vector<uint32_t> values(count);
        for (auto& value : values) value = random() & mask;
        auto expected = values;
        std::sort(expected.begin(), expected.end());
        OrderGrassClusters(values, [](uint32_t key) { return key; });
        if (values != expected) throw std::runtime_error("cluster index order mismatch");
        std::vector<GrassClusterWork> work;
        for (uint32_t i = 0; i < count; ++i) work.push_back({i, i * 2 + 7, (i % 2) != 0});
        std::shuffle(work.begin(), work.end(), random);
        OrderGrassClusters(work, [](const auto& v) { return v.ClusterIndex; });
        for (uint32_t i = 0; i < count; ++i)
            if (work[i].ClusterIndex != i || work[i].AnchorEnd != i * 2 + 7 ||
                work[i].CullIndividualBlades != ((i % 2) != 0))
                throw std::runtime_error("cluster payload changed");
    }
    std::cout << "Cluster ordering matches comparison-sort across 24 distributions\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
