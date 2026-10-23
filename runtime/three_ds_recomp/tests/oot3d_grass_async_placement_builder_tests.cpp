#include "fast/oot3d/grass_persistent_placement_cache.h"
#include "fast/oot3d/grass_overlay_mask.h"
#include "fast/oot3d/graphics_settings_persistence.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include "fast/oot3d/grass_async_placement_builder.h"
#include "fast/oot3d/grass_static_placement_cache.h"
#include "fast/oot3d/grass_visibility.h"

#include <gtest/gtest.h>

#include <memory>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace {

Fast::Oot3d::GrassAsyncPlacementRequest BaseRequest() {
    using namespace Fast::Oot3d;
    auto vertices = std::make_shared<const std::vector<GrassSourceVertex>>(std::vector<GrassSourceVertex>{
        { { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F } },
        { { 100.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 1.0F, 0.0F } },
        { { 0.0F, 0.0F, 100.0F }, { 0.0F, 1.0F, 0.0F }, { 0.0F, 1.0F } },
    });
    auto indices = std::make_shared<const std::vector<uint32_t>>(std::vector<uint32_t>{ 0U, 1U, 2U });
    auto mask = std::make_shared<GrassScalarMask>();
    mask->Width = 1U;
    mask->Height = 1U;
    mask->Channel = GrassSampleChannel::Luminance;
    mask->Samples = { 255U };

    GrassAsyncPlacementRequest request;
    request.PlacementKey = { 11U, 22U, 33U, 44U };
    request.SourceContentVersion = 77U;
    request.WorldIdentity = 55U;
    request.FrameId = 1U;
    request.InstanceId = 66U;
    request.TextureHash = 33U;
    request.ModelToWorld = {
        1.0F, 0.0F, 0.0F, 10.0F, 0.0F, 1.0F, 0.0F, 20.0F, 0.0F, 0.0F, 1.0F, 30.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
    request.TransformBakedIntoVertices = false;
    request.Vertices = std::move(vertices);
    request.Indices = std::move(indices);
    request.Mask = std::move(mask);
    request.Rule.RuleId = 44U;
    request.Rule.InputBlack = 0.0F;
    request.Rule.InputWhite = 1.0F;
    request.Rule.MaximumSlopeDegrees = 90.0F;
    request.Generation.InstancesPerSquareMeter = 10.0F;
    request.Generation.MinimumSpacing = 0.0F;
    request.Generation.Seed = 7U;
    request.Budget = 32U;
    request.ClusterSize = 50.0F;
    request.NormalOffset = 2.0F;
    request.HeightScale = 0.5F;
    return request;
}

Fast::Oot3d::GrassWorldPlacement BuildSynchronously(const Fast::Oot3d::GrassAsyncPlacementRequest& request) {
    using namespace Fast::Oot3d;
    GrassSourceSurface surface;
    surface.GeometryId = request.PlacementKey.GeometryId;
    surface.ContentVersion = request.SourceContentVersion;
    surface.InstanceId = request.InstanceId;
    surface.TextureHash = request.TextureHash;
    surface.MapperSlot = request.MapperSlot;
    surface.MaterialWrapS = request.MaterialWrapS;
    surface.MaterialWrapT = request.MaterialWrapT;
    surface.ModelToWorld = request.ModelToWorld;
    surface.TransformBakedIntoVertices = request.TransformBakedIntoVertices;
    surface.Vertices = *request.Vertices;
    surface.Indices = *request.Indices;
    auto local = BuildGrassPlacementSet(
        GrassSurfaceExtractor::Extract(surface, request.Rule, request.Generation, *request.Mask, request.Budget),
        request.ClusterSize);
    GrassWorldPlacementRequest world;
    world.Identity = request.WorldIdentity;
    world.ContentVersion = request.PlacementKey.ContentVersion;
    world.FrameId = request.FrameId;
    world.Anchors = local.Anchors;
    world.Clusters = local.Clusters;
    world.ModelToWorld = request.ModelToWorld;
    world.TransformBakedIntoVertices = request.TransformBakedIntoVertices;
    world.NormalOffset = request.NormalOffset;
    world.HeightScale = request.HeightScale;
    return BuildGrassWorldPlacement(world);
}

} // namespace

TEST(Oot3dGrassAsyncPlacementBuilder, PublishesImmutablePlacementAndDeduplicatesPendingWork) {
    Fast::Oot3d::GrassAsyncPlacementBuilder builder(8U, 1U);
    auto request = BaseRequest();
    const auto expected = BuildSynchronously(request);
    const auto queued = builder.ResolveOrQueue(request);
    EXPECT_EQ(queued.State, Fast::Oot3d::GrassAsyncPlacementState::Pending);
    EXPECT_TRUE(queued.Queued);

    const auto duplicate = builder.ResolveOrQueue(request);
    EXPECT_FALSE(duplicate.Queued);
    builder.WaitForIdle();

    request.FrameId = 2U;
    const auto ready = builder.ResolveOrQueue(request);
    ASSERT_EQ(ready.State, Fast::Oot3d::GrassAsyncPlacementState::Ready);
    ASSERT_NE(ready.Placement, nullptr);
    ASSERT_FALSE(ready.Placement->Anchors.empty());
    EXPECT_EQ(ready.Placement->Identity, 55U);
    ASSERT_EQ(ready.Placement->Anchors.size(), expected.Anchors.size());
    ASSERT_EQ(ready.Placement->CullingAnchors.size(), expected.CullingAnchors.size());
    EXPECT_EQ(ready.Placement->Anchors.front().StableId, expected.Anchors.front().StableId);
    EXPECT_EQ(ready.Placement->Anchors.front().BaseHeight, expected.Anchors.front().BaseHeight);
    EXPECT_EQ(ready.Placement->CullingAnchors.front().Position, expected.CullingAnchors.front().Position);
    EXPECT_FLOAT_EQ(ready.Placement->CullingAnchors.front().StableVisibility,
                    expected.CullingAnchors.front().StableVisibility);
    EXPECT_FLOAT_EQ(ready.Placement->Anchors.front().BaseHeight[1], 18.0F);

    request.FrameId = 3U;
    const auto reused = builder.ResolveOrQueue(request);
    EXPECT_EQ(reused.Placement, ready.Placement);
    const auto stats = builder.Stats();
    EXPECT_EQ(stats.Misses, 1U);
    EXPECT_EQ(stats.Builds, 1U);
    EXPECT_EQ(stats.Failures, 0U);
    EXPECT_EQ(stats.Pending, 0U);
    EXPECT_GE(stats.Hits, 2U);
}

TEST(Oot3dGrassAsyncPlacementBuilder, ShadowedPlacementServesAnUnshadowedScene) {
    Fast::Oot3d::GrassAsyncPlacementBuilder builder(8U, 1U);
    auto request = BaseRequest();
    request.ShadowGrid = true;
    ASSERT_TRUE(builder.ResolveOrQueue(request).Queued);
    builder.WaitForIdle();
    const auto shadowed = builder.ResolveOrQueue(request);
    ASSERT_EQ(shadowed.State, Fast::Oot3d::GrassAsyncPlacementState::Ready);
    // A scene without directional shadows reuses the shadowed placement.
    request.ShadowGrid = false;
    const auto plain = builder.ResolveOrQueue(request);
    EXPECT_FALSE(plain.Queued);
    ASSERT_EQ(plain.State, Fast::Oot3d::GrassAsyncPlacementState::Ready);
    EXPECT_EQ(plain.Placement, shadowed.Placement);
    // The reverse needs the grid, so it builds.
    Fast::Oot3d::GrassAsyncPlacementBuilder unshadowedFirst(8U, 1U);
    ASSERT_TRUE(unshadowedFirst.ResolveOrQueue(request).Queued);
    unshadowedFirst.WaitForIdle();
    request.ShadowGrid = true;
    EXPECT_TRUE(unshadowedFirst.ResolveOrQueue(request).Queued);
    unshadowedFirst.WaitForIdle();
}

TEST(Oot3dGrassAsyncPlacementBuilder, CompletionTicketsSurviveResidentEviction) {
    using namespace Fast::Oot3d;
    GrassAsyncPlacementBuilder builder(1U,2U);
    std::vector<GrassAsyncPlacementResult> tickets;
    auto request=BaseRequest();
    for (uint64_t i=0;i<20;++i) {
        request.WorldIdentity=100+i;
        ++request.FrameId;
        tickets.push_back(builder.ResolveOrQueue(request));
    }
    builder.WaitForIdle();
    for (size_t i=0;i<tickets.size();++i) {
        tickets[i].WaitUntilReady();
        EXPECT_EQ(tickets[i].State,GrassAsyncPlacementState::Ready);
        ASSERT_NE(tickets[i].Placement,nullptr);
        EXPECT_EQ(tickets[i].Placement->Identity,100+i);
        const auto first=tickets[i].Placement;
        tickets[i].WaitUntilReady();
        EXPECT_EQ(tickets[i].Placement,first);
    }
    EXPECT_LE(builder.Stats().Entries,1U);
    EXPECT_EQ(builder.Stats().Builds,20U);
}

TEST(Oot3dGrassAsyncPlacementBuilder, CancelledCompletionDoesNotLeaveAnUnresolvedWait) {
    using namespace Fast::Oot3d;
    GrassAsyncPlacementResult ticket;
    ticket.State=GrassAsyncPlacementState::Pending;
    {
        std::promise<GrassPlacementCompletion> completion;
        ticket.Completion=completion.get_future().share();
    }
    ticket.WaitUntilReady();
    EXPECT_EQ(ticket.State,GrassAsyncPlacementState::Failed);
    EXPECT_EQ(ticket.Placement,nullptr);
}

TEST(Oot3dGrassPlacement, BudgetCoversTheWholeSurfaceInsteadOfAnIndexPrefix) {
    using namespace Fast::Oot3d;
    const std::vector<GrassSourceVertex> vertices{
        {{0, 0, 0}}, {{100, 0, 0}}, {{0, 0, 100}},
        {{1000, 0, 0}}, {{1100, 0, 0}}, {{1000, 0, 100}},
        {{2000, 0, 0}}, {{2100, 0, 0}}, {{2000, 0, 100}},
    };
    const std::vector<uint32_t> indices{0, 1, 2, 3, 4, 5, 6, 7, 8};
    GrassSourceSurface surface;
    surface.GeometryId = 1;
    surface.ContentVersion = 2;
    surface.Vertices = vertices;
    surface.Indices = indices;
    surface.TransformBakedIntoVertices = true;
    const auto request = BaseRequest();
    auto generation = request.Generation;
    generation.InstancesPerSquareMeter = 4096;
    const auto first = GrassSurfaceExtractor::Extract(surface, request.Rule, generation, *request.Mask, 30);
    const auto second = GrassSurfaceExtractor::Extract(surface, request.Rule, generation, *request.Mask, 30);
    ASSERT_EQ(first.size(), 30U);
    ASSERT_EQ(second.size(), first.size());
    std::array<size_t, 3> counts{};
    for (size_t i = 0; i < first.size(); ++i) {
        ++counts[static_cast<size_t>(first[i].LocalPosition[0] / 1000)];
        EXPECT_EQ(first[i].LocalPosition, second[i].LocalPosition);
        EXPECT_EQ(first[i].StableId, second[i].StableId);
    }
    EXPECT_EQ(counts, (std::array<size_t, 3>{10, 10, 10}));
    auto black = *request.Mask;
    black.Samples = {0};
    EXPECT_TRUE(GrassSurfaceExtractor::Extract(surface, request.Rule, generation, black, 30).empty());
    generation.InstancesPerSquareMeter = 0;
    EXPECT_TRUE(GrassSurfaceExtractor::Extract(surface, request.Rule, generation, *request.Mask, 30).empty());
}

TEST(Oot3dGrassAsyncPlacementBuilder, ByteBudgetEvictsOldestReadyEntry) {
    using namespace Fast::Oot3d;
    const auto request=BaseRequest();
    GrassAsyncPlacementBuilder probe;
    (void)probe.ResolveOrQueue(request);
    probe.WaitForIdle();
    const auto oneEntryBytes=probe.Stats().ResidentBytes;
    ASSERT_GT(oneEntryBytes,0U);
    GrassAsyncPlacementBuilder cache(32U,1U,oneEntryBytes+16U);
    (void)cache.ResolveOrQueue(request);
    cache.WaitForIdle();
    auto newer=request;
    ++newer.WorldIdentity;
    ++newer.FrameId;
    (void)cache.ResolveOrQueue(newer);
    cache.WaitForIdle();
    EXPECT_LE(cache.Stats().ResidentBytes,oneEntryBytes+16U);
    EXPECT_EQ(cache.Stats().Entries,1U);
    EXPECT_FALSE(cache.ResolveOrQueue(newer).Queued);
    EXPECT_TRUE(cache.ResolveOrQueue(request).Queued);
    cache.WaitForIdle();
}

namespace {
Fast::Oot3d::GrassPlacementView TestView() {
    Fast::Oot3d::GrassPlacementView view;
    view.Enabled = true;
    view.WorldToClip = {0.005F,0,0,0, 0,0,0.001F,0, 0,0.005F,0,0, -0.25F,-0.25F,0,1};
    view.Eye = {50,100,50};
    view.DrawDistance = 5000;
    view.FullDensityDistance = 5000;
    return view;
}
}

TEST(Oot3dGrassBudget, ConservesCapacityAndRedistributesSaturatedShares) {
    using namespace Fast::Oot3d;
    EXPECT_EQ(AllocateGrassBudget({{100,1},{100,1},{100,1}}, 5), (std::vector<uint32_t>{2,2,1}));
    EXPECT_EQ(AllocateGrassBudget({{2,1000},{100,1},{100,1}}, 20), (std::vector<uint32_t>{2,9,9}));
    EXPECT_EQ(AllocateGrassBudget({{2,1},{3,1}}, 200), (std::vector<uint32_t>{2,3}));
    EXPECT_EQ(AllocateGrassBudget({{100,1},{100,0},{100,-1},{100,std::numeric_limits<double>::infinity()},
                                  {100,std::numeric_limits<double>::quiet_NaN()}}, 20),
              (std::vector<uint32_t>{20,0,0,0,0}));
    EXPECT_EQ(AllocateGrassBudget({{2,1.0e200},{100,1.0e-200},{100,1.0e-200}}, 20),
              (std::vector<uint32_t>{2,9,9}));
    EXPECT_EQ(AllocateGrassBudget({{100,1}}, 0), (std::vector<uint32_t>{0}));
    for (uint32_t budget = 0; budget < 250; ++budget) {
        const std::vector<GrassBudgetDemand> demands{{33,0.3},{99,100},{120,1.0e-8},{1,3}};
        const auto result = AllocateGrassBudget(demands, budget);
        EXPECT_EQ(std::accumulate(result.begin(), result.end(), uint64_t{0}), budget);
        for (size_t i=0; i<result.size(); ++i) EXPECT_LE(result[i], demands[i].Maximum);
    }
}

TEST(Oot3dGrassPlacement, FocusesTheFrustumWithoutMovingRetainedBlades) {
    using namespace Fast::Oot3d;
    const std::vector<GrassSourceVertex> vertices{
        {{0,0,0}}, {{100,0,0}}, {{0,0,100}},
        {{1000,0,0}}, {{1100,0,0}}, {{1000,0,100}},
        {{2000,0,0}}, {{2100,0,0}}, {{2000,0,100}}};
    const std::vector<uint32_t> indices{0,1,2,3,4,5,6,7,8};
    auto request = BaseRequest();
    request.Generation.InstancesPerSquareMeter = 4096;
    GrassSourceSurface surface;
    surface.GeometryId = 1;
    surface.ContentVersion = 2;
    surface.Vertices = vertices;
    surface.Indices = indices;
    surface.TransformBakedIntoVertices = true;
    surface.PlacementView = TestView();
    const auto first = GrassSurfaceExtractor::Extract(surface, request.Rule, request.Generation, *request.Mask, 100);
    ASSERT_EQ(first.size(), 100U);
    std::array<size_t,3> counts{};
    std::unordered_map<uint32_t, GrassAnchor> old;
    for (const auto& a : first) { ++counts[static_cast<size_t>(a.LocalPosition[0]/1000)]; old.emplace(a.StableId,a); }
    EXPECT_GE(counts[0], 90U);
    EXPECT_GT(counts[1], 0U);
    EXPECT_GT(counts[2], 0U);
    const auto expanded = GrassSurfaceExtractor::Extract(surface, request.Rule, request.Generation, *request.Mask, 200);
    size_t retained=0;
    for (const auto& a : expanded) if (auto it=old.find(a.StableId); it!=old.end()) {
        ++retained;
        EXPECT_EQ(a.LocalPosition,it->second.LocalPosition);
        EXPECT_EQ(a.BladeHeight,it->second.BladeHeight);
    }
    EXPECT_EQ(retained,first.size());
    surface.PlacementView.WorldToClip[12] -= 10.0F;
    surface.PlacementView.Eye[0] += 2000.0F;
    const auto moved = GrassSurfaceExtractor::Extract(surface, request.Rule, request.Generation, *request.Mask, 100);
    counts = {};
    for (const auto& a : moved) {
        ++counts[static_cast<size_t>(a.LocalPosition[0]/1000)];
        if (auto it=old.find(a.StableId); it!=old.end()) EXPECT_EQ(a.LocalPosition,it->second.LocalPosition);
    }
    EXPECT_GE(counts[2],90U);
    auto black = *request.Mask;
    black.Samples = {0};
    EXPECT_TRUE(GrassSurfaceExtractor::Extract(surface, request.Rule, request.Generation, black, 100).empty());
}

TEST(Oot3dGrassViewPlacement, RefreshesOnlyOutsideTheGuardOrOnProjectionChanges) {
    using namespace Fast::Oot3d;
    auto view = TestView();
    view.GuardDistance = 100;
    auto next = view;
    next.Eye[0] += 49;
    EXPECT_FALSE(GrassPlacementViewNeedsRefresh(view,next));
    next.Eye[0] += 2;
    EXPECT_TRUE(GrassPlacementViewNeedsRefresh(view,next));
    next = view;
    next.WorldToClip[0] *= 0.8F;
    EXPECT_TRUE(GrassPlacementViewNeedsRefresh(view,next));
    next = view;
    next.WorldToClip[0] *= -1;
    EXPECT_TRUE(GrassPlacementViewNeedsRefresh(view,next));
    next = view;
    next.FarDensity = 0.1F;
    EXPECT_TRUE(GrassPlacementViewNeedsRefresh(view,next));
    EXPECT_FALSE(GrassPlacementViewNeedsRefresh(view,view));
}

TEST(Oot3dGrassStaticPlacement, MeasuresWholeWorldCandidateDemand) {
    using namespace Fast::Oot3d;
    auto request = BaseRequest();
    request.TransformBakedIntoVertices = true;
    request.PlacementView = TestView();
    const double count = MeasureGrassSurfaceCandidates(request);
    EXPECT_DOUBLE_EQ(count,5.0);
    request.PlacementView.WorldToClip[12] = -10;
    EXPECT_DOUBLE_EQ(MeasureGrassSurfaceCandidates(request),count);
    request.PlacementView = TestView();
    request.PlacementView.WorldToClip[0] = 0.02F;
    request.PlacementView.WorldToClip[12] = -2;
    EXPECT_DOUBLE_EQ(MeasureGrassSurfaceCandidates(request),count);
    request.PlacementView = TestView();
    request.PlacementView.FullDensityDistance = 50;
    const auto near = MeasureGrassSurfaceCandidates(request);
    request.PlacementView.Eye[1] = 1000;
    EXPECT_DOUBLE_EQ(MeasureGrassSurfaceCandidates(request),near);
}

TEST(Oot3dGrassStaticPlacement, CameraCutsDistanceChangesAndReturningSourcesNeverRegenerate) {
    using namespace Fast::Oot3d;
    GrassStaticPlacementCache builder;
    builder.SetCandidateCapacity(80);
    auto request = BaseRequest();
    request.Generation.InstancesPerSquareMeter = 4096;
    request.PlacementView = TestView();
    request.PlacementView.GuardDistance = 100;
    std::vector<GrassAsyncPlacementRequest> requests{request,request};
    requests[1].WorldIdentity += 1;
    EXPECT_EQ(builder.Resolve(requests).size(),2U);
    builder.WaitForIdle();
    auto ready = builder.Resolve(requests);
    ASSERT_NE(ready[0].Placement,nullptr);
    ASSERT_NE(ready[1].Placement,nullptr);
    EXPECT_EQ(ready[0].Placement->Anchors.size()+ready[1].Placement->Anchors.size(),80U);
    EXPECT_EQ(builder.Resolve(requests)[0].Placement,ready[0].Placement);
    const auto builds = builder.Stats().Builds;
    for (auto& r : requests) { r.PlacementView.Eye[0]+=200; ++r.FrameId; }
    requests[0].PlacementView.DrawDistance *= 10;
    const auto moved = builder.Resolve(requests);
    ASSERT_NE(moved[0].Placement,nullptr);
    EXPECT_EQ(moved[0].Placement,ready[0].Placement);
    EXPECT_EQ(builder.Stats().Builds,builds);
    EXPECT_TRUE(builder.Resolve({}).empty());
    for (auto& r : requests) r.FrameId += 5000;
    EXPECT_EQ(builder.Resolve(requests)[0].Placement,ready[0].Placement);
    requests[0].HeightScale *= 2;
    const auto resized = builder.Resolve(requests);
    EXPECT_TRUE(resized[0].Queued);
    ASSERT_NE(resized[0].Placement,nullptr);
    EXPECT_NE(resized[0].Placement,moved[0].Placement);
    builder.WaitForIdle();
    const auto resizedReady = builder.Resolve(requests);
    EXPECT_NE(resizedReady[0].Placement,moved[0].Placement);
    const auto beforeTransient = builder.Stats().Builds;
    auto transient = requests;
    for (auto& r : transient) r.PlacementView.Eye[0] += 2000;
    EXPECT_FALSE(builder.Resolve(transient)[0].Queued);
    EXPECT_FALSE(builder.Resolve(requests)[0].Queued);
    EXPECT_EQ(builder.Stats().Builds,beforeTransient);
}

TEST(Oot3dGrassStaticPlacement, OverlayAndShadowGridChangesKeepTheShownPlacementUntilReady) {
    using namespace Fast::Oot3d;
    auto request = BaseRequest();
    request.Generation.InstancesPerSquareMeter = 4096;
    std::vector<GrassAsyncPlacementRequest> requests{ request };
    GrassStaticPlacementCache cache;
    const auto shown = cache.Resolve(requests)[0].Placement;
    ASSERT_NE(shown, nullptr);
    // Overlays change twice in a row (a decal appears, then fades) before
    // the first rebuild finishes: the frame never waits for either.
    const auto overlays = [](uint64_t version) {
        auto set = std::make_shared<GrassOverlaySet>();
        set->Version = set->PlacementVersion = version;
        return set;
    };
    for (uint64_t version : { 1U, 2U }) {
        requests[0].Overlays = overlays(version);
        ++requests[0].FrameId;
        const auto result = cache.Resolve(requests)[0];
        EXPECT_EQ(result.State, GrassAsyncPlacementState::Ready);
        EXPECT_EQ(result.Placement, shown);
    }
    cache.WaitForIdle();
    ++requests[0].FrameId;
    const auto rebuilt = cache.Resolve(requests)[0].Placement;
    ASSERT_NE(rebuilt, nullptr);
    EXPECT_NE(rebuilt, shown);
    EXPECT_TRUE(rebuilt->ShadowPoints.empty());
    // Shadows begin to reach the grass: the placement without a shadow grid
    // stays (drawn unshadowed) until the one with it is built.
    requests[0].ShadowGrid = true;
    ++requests[0].FrameId;
    const auto shadowing = cache.Resolve(requests)[0];
    EXPECT_EQ(shadowing.State, GrassAsyncPlacementState::Ready);
    EXPECT_EQ(shadowing.Placement, rebuilt);
    cache.WaitForIdle();
    ++requests[0].FrameId;
    const auto shadowed = cache.Resolve(requests)[0].Placement;
    ASSERT_NE(shadowed, nullptr);
    EXPECT_FALSE(shadowed->ShadowPoints.empty());
    EXPECT_EQ(shadowed->ShadowReferences.size(), shadowed->Anchors.size());
    // Anything else still applies in the same frame.
    requests[0].HeightScale *= 2;
    ++requests[0].FrameId;
    const auto resized = cache.Resolve(requests)[0];
    EXPECT_EQ(resized.State, GrassAsyncPlacementState::Ready);
    EXPECT_NE(resized.Placement, shadowed);
}

TEST(Oot3dGrassStaticPlacement, FirstAdmissionHasCompleteCoverageWithoutIgnoringBlackMasks) {
    using namespace Fast::Oot3d;
    auto request = BaseRequest();
    request.Generation.InstancesPerSquareMeter = 4096;
    std::vector<GrassAsyncPlacementRequest> requests{request};
    GrassStaticPlacementCache cache;
    auto cold = cache.Resolve(requests);
    ASSERT_NE(cold[0].Placement, nullptr);
    EXPECT_FALSE(cold[0].Placement->Anchors.empty());
    EXPECT_EQ(cold[0].State, GrassAsyncPlacementState::Ready);
    const auto reserve = cold[0].Placement;
    ++requests[0].FrameId;
    requests[0].PlacementView = TestView();
    requests[0].PlacementView.Eye = {10000, 0, 0};
    EXPECT_NE(cache.Resolve(requests)[0].Placement, nullptr);
    cache.WaitForIdle();
    auto full = cache.Resolve(requests);
    EXPECT_EQ(full[0].State, GrassAsyncPlacementState::Ready);
    EXPECT_EQ(full[0].Placement, reserve);
    EXPECT_EQ(cache.Stats().Builds, 1U);
    auto black = std::make_shared<GrassScalarMask>(*request.Mask);
    black->Samples = {0};
    requests[0].Mask = black;
    requests[0].PlacementKey.TextureHash += 1;
    auto masked = cache.Resolve(requests);
    ASSERT_NE(masked[0].Placement, nullptr);
    EXPECT_TRUE(masked[0].Placement->Anchors.empty());
    cache.WaitForIdle();
    EXPECT_TRUE(cache.Resolve(requests)[0].Placement->Anchors.empty());
}

namespace {
struct GrassDiskFixture : testing::Test {
    std::filesystem::path Directory =
        std::filesystem::temp_directory_path() /
        ("triaevum-grass-round37-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(Directory, ec);
    }
};
void ExpectSamePlacement(const Fast::Oot3d::GrassWorldPlacement& a, const Fast::Oot3d::GrassWorldPlacement& b) {
    const auto equal = [](const auto& a, const auto& b) {
        ASSERT_EQ(a.size(), b.size());
        if (!a.empty())
            EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])), 0);
    };
    equal(a.Anchors, b.Anchors);
    equal(a.CullingAnchors, b.CullingAnchors);
    equal(a.Clusters, b.Clusters);
    equal(a.VisibilityClusterOrder, b.VisibilityClusterOrder);
    equal(a.VisibilityNodes, b.VisibilityNodes);
    equal(a.Midrange.Members, b.Midrange.Members);
    equal(a.Midrange.Groups, b.Midrange.Groups);
    equal(a.Midrange.GroupForRoot, b.Midrange.GroupForRoot);
    equal(a.Midrange.GroupOrder, b.Midrange.GroupOrder);
    equal(a.Midrange.Nodes, b.Midrange.Nodes);
    equal(a.ShadowPoints, b.ShadowPoints);
    equal(a.ShadowReferences, b.ShadowReferences);
    equal(a.ShadowSurfaces, b.ShadowSurfaces);
}
} // namespace
TEST_F(GrassDiskFixture, RoundTripFinalAnchorsClustersColorsAndShadowSupport) {
    using namespace Fast::Oot3d;
    auto r = BaseRequest();
    r.ShadowGrid = true;
    r.MidrangeCellExtent = 20;
    r.MidrangeAdaptive = true;
    auto grid = std::make_shared<GrassTextureColorGrid>();
    for (auto& rgb : grid->Rgb)
        rgb = { 32, 128, 64 };
    r.ColorSource.Grid = grid;
    GrassAsyncPlacementBuilder builder;
    auto result = builder.ResolveOrQueue(r);
    result.WaitUntilReady();
    ASSERT_TRUE(result.Placement);
    ASSERT_FALSE(result.Placement->ShadowPoints.empty());
    GrassPersistentPlacementCache disk(Directory);
    const auto key = GrassPlacementPersistentKey(r);
    ASSERT_TRUE(disk.Store(key, *result.Placement));
    bool rejected = true;
    const auto loaded = disk.Load(key, &rejected);
    ASSERT_TRUE(loaded);
    EXPECT_FALSE(rejected);
    ExpectSamePlacement(*result.Placement, *loaded);
    EXPECT_EQ(loaded->Identity, 0);
    EXPECT_NE(loaded->Anchors[0].SurfaceColor, 0);
}
TEST_F(GrassDiskFixture, RejectsVersionTruncationChecksumOverflowAndWrongKeyThenRepairs) {
    using namespace Fast::Oot3d;
    auto r = BaseRequest();
    const auto p = BuildSynchronously(r);
    GrassPersistentPlacementCache disk(Directory);
    const auto key = GrassPlacementPersistentKey(r);
    ASSERT_TRUE(disk.Store(key, p));
    std::ifstream in(disk.Path(key), std::ios::binary);
    const std::vector<char> original{ std::istreambuf_iterator<char>(in), {} };
    in.close();
    const auto replace = [&](const auto& bytes) {
        std::ofstream out(disk.Path(key), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), bytes.size());
    };
    for (unsigned kind = 0; kind < 4; ++kind) {
        auto bytes = original;
        if (kind == 0)
            bytes[8] ^= 1;
        else if (kind == 1)
            bytes.resize(20);
        else if (kind == 2)
            bytes.back() ^= 1;
        else { // the first vector count in the private ABI header (aligned after sizes)
            for (size_t i = 104; i < 112; ++i)
                bytes[i] = char(0xff);
        }
        replace(bytes);
        bool rejected = false;
        EXPECT_FALSE(disk.Load(key, &rejected));
        EXPECT_TRUE(rejected);
    }
    ASSERT_TRUE(disk.Store(key, p));
    ASSERT_TRUE(disk.Load(key));
    auto wrong = key;
    ++wrong.Content;
    std::filesystem::copy_file(disk.Path(key), disk.Path(wrong));
    EXPECT_FALSE(disk.Load(wrong));
}
TEST_F(GrassDiskFixture, BoundsStorageAndIgnoresUnrelatedFiles) {
    using namespace Fast::Oot3d;
    auto r = BaseRequest();
    const auto p = BuildSynchronously(r);
    GrassPersistentPlacementCache disk(Directory, 1024 * 1024, 1);
    const auto first = GrassPlacementPersistentKey(r);
    ASSERT_TRUE(disk.Store(first, p));
    std::ofstream(Directory / "keep.txt") << "not a grass cache";
    auto second = first;
    ++second.Settings;
    ASSERT_TRUE(disk.Store(second, p));
    EXPECT_FALSE(disk.Load(first));
    EXPECT_TRUE(disk.Load(second));
    EXPECT_TRUE(std::filesystem::exists(Directory / "keep.txt"));
    GrassPersistentPlacementCache tiny(Directory / "tiny", 1, 1);
    EXPECT_FALSE(tiny.Store(first, p));
}
TEST_F(GrassDiskFixture, NewBuilderLoadsWithoutExtractionMaskDecodeOrColorSource) {
    using namespace Fast::Oot3d;
    auto r = BaseRequest();
    r.Mask = {};
    r.LazyMaskTexture = r.LazyColorTexture = 0x123477;
    const std::array<uint8_t, 4> rgba{ 100, 180, 90, 255 };
    auto& textures = GrassTextureSourceCache::Instance();
    textures.ObserveDecoded(r.LazyMaskTexture, 1, 1, rgba);
    std::shared_ptr<const GrassWorldPlacement> original;
    {
        GrassAsyncPlacementBuilder first;
        first.SetCacheDirectory(Directory);
        first.SetDiskSettleDelay({});
        auto result = first.ResolveOrQueue(r);
        result.WaitUntilReady();
        first.WaitForIdle();
        ASSERT_TRUE(result.Placement);
        original = result.Placement;
        EXPECT_EQ(first.Stats().DiskWrites, 0); // written once it is used again
        (void)first.ResolveOrQueue(r);
        first.WaitForIdle();
        EXPECT_EQ(first.Stats().DiskWrites, 1);
    }
    textures.Clear(); // There is no texture payload left to decode or sample.
    r.WorldIdentity = 12345;
    r.PlacementKey.ContentVersion += 10;
    r.SourceContentVersion += 10;
    r.FrameId = 55;
    {
        GrassAsyncPlacementBuilder second;
        second.SetCacheDirectory(Directory);
        auto result = second.ResolveOrQueue(r);
        result.WaitUntilReady();
        second.WaitForIdle();
        ASSERT_TRUE(result.Placement);
        EXPECT_EQ(second.Stats().DiskHits, 1);
        EXPECT_EQ(second.Stats().Builds, 0);
        EXPECT_EQ(result.Placement->Identity, r.WorldIdentity);
        EXPECT_EQ(result.Placement->LastUsedFrame, 55);
        ExpectSamePlacement(*original, *result.Placement);
    }
}
TEST_F(GrassDiskFixture, OnlySettledPlacementsAreWritten) {
    using namespace Fast::Oot3d;
    GrassAsyncPlacementBuilder builder;
    builder.SetCacheDirectory(Directory);
    builder.SetDiskSettleDelay(std::chrono::hours(1));
    // An F1 edit: every step is a new placement, used for a moment, then replaced.
    auto r = BaseRequest();
    for (int step = 0; step < 3; ++step) {
        r.PlacementKey.ContentVersion += 1;
        r.Generation.InstancesPerSquareMeter += 1.0F;
        auto result = builder.ResolveOrQueue(r);
        result.WaitUntilReady();
        builder.WaitForIdle();
        ASSERT_TRUE(result.Placement);
        (void)builder.ResolveOrQueue(r);
        builder.WaitForIdle();
    }
    EXPECT_EQ(builder.Stats().DiskWrites, 0);
    EXPECT_TRUE(!std::filesystem::exists(Directory) || std::filesystem::is_empty(Directory));
    // The last one stays in use past the settle delay: written once.
    builder.SetDiskSettleDelay({});
    (void)builder.ResolveOrQueue(r);
    builder.WaitForIdle();
    (void)builder.ResolveOrQueue(r);
    builder.WaitForIdle();
    EXPECT_EQ(builder.Stats().DiskWrites, 1);
    EXPECT_TRUE(GrassPersistentPlacementCache(Directory).Load(GrassPlacementPersistentKey(r)));
}
TEST_F(GrassDiskFixture, PersistentFingerprintUsesGeometryPayloadAndEveryBakeSetting) {
    using namespace Fast::Oot3d;
    const auto r = BaseRequest();
    const auto key = GrassPlacementPersistentKey(r);
    auto changed = r;
    changed.SourceContentVersion++;
    changed.PlacementKey.ContentVersion++;
    changed.FrameId++;
    changed.WorldIdentity++;
    EXPECT_EQ(key, GrassPlacementPersistentKey(changed));
    const std::vector<std::function<void(GrassAsyncPlacementRequest&)>> changes{
        [](auto& r) { r.PlacementKey.GeometryId++; },
        [](auto& r) { r.TextureHash++; },
        [](auto& r) { r.MapperSlot++; },
        [](auto& r) { r.Rule.InputBlack = .3F; },
        [](auto& r) { r.Rule.Channel = GrassSampleChannel::Red; },
        [](auto& r) { r.Rule.ActiveRects.push_back({ 0, 0, .5F, .5F }); },
        [](auto& r) { r.Rule.SampleScale = .5F; },
        [](auto& r) { r.Generation.Seed++; },
        [](auto& r) { r.Generation.MinimumSpacing++; },
        [](auto& r) { r.ModelToWorld[3]++; },
        [](auto& r) { r.TransformBakedIntoVertices = true; },
        [](auto& r) { r.MaterialWrapS = GrassTextureWrap::Clamp; },
        [](auto& r) { r.MaterialWrapT = GrassTextureWrap::Mirror; },
        [](auto& r) { r.Budget++; },
        [](auto& r) { r.ClusterSize++; },
        [](auto& r) { r.NormalOffset++; },
        [](auto& r) { r.HeightScale++; },
        [](auto& r) { r.MidrangeCellExtent = 1; },
        [](auto& r) { r.MidrangeAdaptive = true; },
        [](auto& r) { r.MidrangeAdaptiveCapacity++; },
        [](auto& r) { r.ShadowGrid = true; },
        [](auto& r) {
            auto v = std::make_shared<std::vector<GrassSourceVertex>>(*r.Vertices);
            (*v)[0].Position[0]++;
            r.Vertices = v;
        },
        [](auto& r) {
            auto v = std::make_shared<std::vector<uint32_t>>(*r.Indices);
            std::swap((*v)[1], (*v)[2]);
            r.Indices = v;
        },
        [](auto& r) {
            auto m = std::make_shared<GrassScalarMask>(*r.Mask);
            m->Samples[0]--;
            r.Mask = m;
        },
        [](auto& r) {
            auto grid = std::make_shared<GrassTextureColorGrid>();
            r.ColorSource.Grid = grid;
        }
    };
    for (size_t i = 0; i < changes.size(); ++i) {
        SCOPED_TRACE(i);
        auto value = r;
        changes[i](value);
        EXPECT_NE(key, GrassPlacementPersistentKey(value));
    }
}
TEST_F(GrassDiskFixture, ExplicitOverlayMaskLayoutAndChannelSlotsInvalidateDiskKey) {
    using namespace Fast::Oot3d;
    auto r = BaseRequest();
    auto scalar = std::make_shared<GrassScalarMask>();
    scalar->Width = 2;
    scalar->Height = 1;
    scalar->Samples = { 0, 255 };
    auto alpha = std::make_shared<GrassOverlayAlpha>();
    alpha->Version = 77;
    alpha->Texture[0] = scalar;
    auto set = std::make_shared<GrassOverlaySet>();
    GrassOverlayTriangle triangle;
    triangle.Alpha = alpha;
    set->Triangles.push_back(triangle);
    r.Overlays = set;
    const auto native = GrassPlacementPersistentKey(r);
    alpha->Texture[1] = alpha->Texture[0];
    alpha->Texture[0].reset();
    EXPECT_NE(native, GrassPlacementPersistentKey(r));
    alpha->Texture[1].reset();
    alpha->CustomMask = scalar;
    const auto custom = GrassPlacementPersistentKey(r);
    std::swap(scalar->Width, scalar->Height);
    EXPECT_NE(custom, GrassPlacementPersistentKey(r));
    // An overlay that cannot cover leaves the key alone, wherever it scrolls.
    alpha->CustomMask.reset();
    alpha->NativeSupported = false;
    const auto hidden = GrassPlacementPersistentKey(r);
    set->Triangles[0].Vertices[0].Uv = { 0.5F, 0.5F };
    EXPECT_EQ(hidden, GrassPlacementPersistentKey(r));
    alpha->NativeSupported = true;
    EXPECT_NE(hidden, GrassPlacementPersistentKey(r));
}
TEST_F(GrassDiskFixture, F1MaskEditInvalidatesMemoryAndDiskWithoutRunningDeferredDecoderOnHit) {
    using namespace Fast::Oot3d;
    auto r = BaseRequest();
    r.TransformBakedIntoVertices = true;
    r.NormalOffset = 0;
    r.Generation.InstancesPerSquareMeter = 100;
    std::atomic<unsigned> decodes = 0;
    GraphicsSettings settings;
    GrassMaskRule mask;
    mask.Mode = GrassMaskMode::Custom;
    mask.Coverage.Target = { "", 90, 1, 1 };
    settings.Grass.Masks = { mask };
    const auto overlay = [&](const GrassMaskRule& rule) {
        auto alpha = std::make_shared<GrassOverlayAlpha>();
        alpha->Rule = rule;
        alpha->Version = GrassOverlayAlphaVersion(*alpha, 90);
        alpha->LoadTextures = [&](auto& target) {
            ++decodes;
            auto scalar = std::make_shared<GrassScalarMask>();
            scalar->Width = scalar->Height = 1;
            scalar->Samples = { 255 };
            target.CustomMask = scalar;
        };
        auto set = std::make_shared<GrassOverlaySet>();
        set->Version = set->PlacementVersion = alpha->Version;
        GrassOverlayTriangle t;
        t.Draw = 20;
        t.Alpha = alpha;
        for (size_t i = 0; i < 3; ++i)
            t.Vertices[i] = { (*r.Vertices)[i].Position, (*r.Vertices)[i].Uv, 1 };
        EXPECT_TRUE(PrepareGrassOverlayTriangle(t));
        set->Triangles = { t };
        return set;
    };
    r.Overlays = overlay(settings.Grass.Masks[0]);
    GrassAsyncPlacementBuilder builder;
    builder.SetCacheDirectory(Directory);
    builder.SetDiskSettleDelay({});
    auto masked = builder.ResolveOrQueue(r);
    masked.WaitUntilReady();
    builder.WaitForIdle();
    ASSERT_TRUE(masked.Placement);
    EXPECT_TRUE(masked.Placement->Anchors.empty());
    EXPECT_EQ(decodes, 1);
    (void)builder.ResolveOrQueue(r); // settled: persisted
    builder.WaitForIdle();
    builder.Clear();
    auto hit = builder.ResolveOrQueue(r);
    hit.WaitUntilReady();
    builder.WaitForIdle();
    EXPECT_EQ(builder.Stats().DiskHits, 1);
    EXPECT_EQ(decodes, 1);
    // The same settings transaction used by F1 persistence changes the owner.
    auto document = SerializeGraphicsSettings(settings);
    document["Grass"]["Masks"][0]["Mode"] = "Off";
    auto edited = DeserializeGraphicsSettings(document).Value;
    ASSERT_EQ(edited.Grass.Masks[0].Mode, GrassMaskMode::Off);
    r.Overlays = overlay(edited.Grass.Masks[0]);
    auto rebuilt = builder.ResolveOrQueue(r);
    rebuilt.WaitUntilReady();
    builder.WaitForIdle();
    ASSERT_TRUE(rebuilt.Placement);
    EXPECT_FALSE(rebuilt.Placement->Anchors.empty());
    EXPECT_EQ(builder.Stats().DiskMisses, 1);
    EXPECT_EQ(builder.Stats().Builds, 1);
}
