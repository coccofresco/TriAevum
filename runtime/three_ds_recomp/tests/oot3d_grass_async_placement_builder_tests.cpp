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

TEST(Oot3dGrassStaticPlacement, NonblockingAdmissionPublishesCompletedWorkOnRetry) {
    using namespace Fast::Oot3d;
    std::vector<GrassAsyncPlacementRequest> requests{ BaseRequest() };
    GrassStaticPlacementCache cache;
    const auto pending = cache.Resolve(requests, false);
    ASSERT_EQ(pending.size(), 1U);
    EXPECT_TRUE(pending[0].Queued);
    EXPECT_EQ(pending[0].State, GrassAsyncPlacementState::Pending);
    EXPECT_EQ(pending[0].Placement, nullptr);
    cache.WaitForIdle();
    const auto ready = cache.Resolve(requests, false);
    ASSERT_NE(ready[0].Placement, nullptr);
    EXPECT_FALSE(ready[0].Placement->Anchors.empty());
    EXPECT_EQ(ready[0].State, GrassAsyncPlacementState::Ready);
    EXPECT_FALSE(ready[0].Queued);
    EXPECT_EQ(cache.Resolve(requests)[0].Placement, ready[0].Placement);
    EXPECT_EQ(cache.Stats().Builds, 1U);
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
