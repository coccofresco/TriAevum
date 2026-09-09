#include "fast/oot3d/grass_static_placement_cache.h"
#include "fast/oot3d/grass_visibility.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace Fast::Oot3d {
namespace {
using Point = std::array<double, 3>;
Point Sub(const Point& a, const Point& b) {
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}
Point Cross(const Point& a, const Point& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
Point World(const GrassAsyncPlacementRequest& r, const Point& p) {
    if (r.TransformBakedIntoVertices)
        return p;
    const auto& m = r.ModelToWorld;
    const double w = m[12] * p[0] + m[13] * p[1] + m[14] * p[2] + m[15];
    const double inverse = std::abs(w) > 1.0e-6 ? 1.0 / w : 1.0;
    return { (m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3]) * inverse,
             (m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7]) * inverse,
             (m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]) * inverse };
}
} // namespace

double MeasureGrassSurfaceCandidates(const GrassAsyncPlacementRequest& request) {
    if (!request.Vertices || !request.Indices)
        return 0;
    double candidates = 0;
    for (size_t i = 0; i + 2 < request.Indices->size(); i += 3) {
        std::array<Point, 3> triangle;
        bool valid = true;
        for (size_t j = 0; j < 3; ++j) {
            const auto index = (*request.Indices)[i + j];
            if (index >= request.Vertices->size()) {
                valid = false;
                break;
            }
            const auto& p = (*request.Vertices)[index].Position;
            triangle[j] = World(request, { p[0], p[1], p[2] });
        }
        if (!valid)
            continue;
        const auto n = Cross(Sub(triangle[1], triangle[0]), Sub(triangle[2], triangle[0]));
        const double area2 = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (!(area2 > 1.0e-6) || !std::isfinite(area2))
            continue;
        if (std::acos(std::clamp(std::abs(n[1]) / area2, 0.0, 1.0)) * 180.0 / std::numbers::pi >
            request.Rule.MaximumSlopeDegrees)
            continue;
        candidates += std::ceil(area2 * 0.5 * 0.0001 * request.Generation.InstancesPerSquareMeter);
    }
    return std::isfinite(candidates) ? std::max(0.0, candidates) : 0.0;
}

std::vector<GrassAsyncPlacementResult>
GrassStaticPlacementCache::Resolve(std::span<const GrassAsyncPlacementRequest> requests, bool waitUntilReady) {
    std::vector<GrassBudgetDemand> demands;
    std::vector<uint64_t> activeKeys;
    const auto demandKey = [](const auto& request) {
        const auto source = GrassPlacementSourceVersion(request);
        return source ^ (request.WorldIdentity + 0x9e3779b97f4a7c15ULL + (source << 6U) + (source >> 2U));
    };
    for (const auto& request : requests) {
        const auto key = demandKey(request);
        activeKeys.push_back(key);
        auto [entry, inserted] = mDemands.try_emplace(key);
        if (inserted)
            entry->second.Candidates = MeasureGrassSurfaceCandidates(request);
        entry->second.LastFrame = request.FrameId;
        demands.push_back({ static_cast<uint32_t>(std::min(entry->second.Candidates,
                                                           static_cast<double>(std::numeric_limits<uint32_t>::max()))),
                            entry->second.Candidates });
    }
    const auto budgets = AllocateGrassBudget(demands, mCandidateCapacity);
    std::vector<GrassAsyncPlacementResult> results;
    for (size_t i = 0; i < requests.size(); ++i) {
        auto request = requests[i];
        request.PlacementView = {};
        request.Budget = budgets[i];
        results.push_back(mBuilder.ResolveOrQueue(std::move(request)));
    }
    // Admit the scene only when its requested geometry exists. Queue the whole
    // batch first so construction remains parallel. Tickets pin results even
    // when the resident cache evicts an entry before the batch finishes.
    if (waitUntilReady) {
        for (auto& result : results) result.WaitUntilReady();
    }
    while (mDemands.size() > std::max<size_t>(32U, activeKeys.size())) {
        auto oldest = mDemands.end();
        for (auto entry = mDemands.begin(); entry != mDemands.end(); ++entry) {
            if (std::find(activeKeys.begin(), activeKeys.end(), entry->first) != activeKeys.end()) continue;
            if (oldest == mDemands.end() || entry->second.LastFrame < oldest->second.LastFrame) oldest = entry;
        }
        if (oldest == mDemands.end()) break;
        mDemands.erase(oldest);
    }
    return results;
}

void GrassStaticPlacementCache::Clear() {
    mBuilder.Clear();
    mDemands.clear();
}
} // namespace Fast::Oot3d
