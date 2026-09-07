#include "fast/oot3d/grass_world_placement_cache.h"
#include "fast/oot3d/grass_visibility.h"
#include "fast/oot3d/grass_distant_tuft.h"
#include "fast/oot3d/grass_indexed_topology.h"
#include "fast/oot3d/grass_selection_cache.h"
#include "fast/oot3d/grass_selection_budget.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>
#include <cmath>

namespace {

Fast::Oot3d::GrassWorldPlacementRequest BaseRequest(std::span<const Fast::Oot3d::GrassAnchor> anchors,
                                                    std::span<const Fast::Oot3d::GrassAnchorCluster> clusters) {
    Fast::Oot3d::GrassWorldPlacementRequest request;
    request.Identity = 41U;
    request.ContentVersion = 7U;
    request.FrameId = 10U;
    request.Anchors = anchors;
    request.Clusters = clusters;
    request.ModelToWorld = { 2.0F, 0.0F, 0.0F, 10.0F, 0.0F, 3.0F, 0.0F, 20.0F,
                             0.0F, 0.0F, 2.0F, 30.0F, 0.0F, 0.0F, 0.0F, 1.0F };
    request.TransformBakedIntoVertices = false;
    request.NormalOffset = 2.0F;
    request.HeightScale = 0.5F;
    return request;
}

} // namespace

TEST(Oot3dGrassIndexedTopology, PreservesEveryExpandedTriangleAndWinding) {
    using namespace Fast::Oot3d;
    const auto topology = BuildGrassIndexedTopology();
    for (uint8_t segments = 1; segments <= kMaximumGrassBladeSegments; ++segments) {
        std::vector<GrassBladeGeometryVertex> expanded;
        AppendGrassBladeGeometry(expanded, {.HalfWidth=1, .Height=1, .Segments=segments});
        for (uint8_t planes = 1; planes <= 2; ++planes) {
            const auto range = topology.Blades[segments-1][planes-1];
            ASSERT_EQ(range.Count, expanded.size()*planes);
            for (uint32_t i = 0; i < range.Count; ++i) {
                const uint32_t index = topology.Indices[range.First+i];
                EXPECT_EQ(index / (2*segments+1), i / expanded.size());
                const uint32_t local = index % (2*segments+1);
                const float height = static_cast<float>(local/2)/segments;
                const float width = local == 2*segments ? 0.0F : (local%2 == 0 ? -1.0F : 1.0F);
                const auto& old = expanded[i%expanded.size()];
                EXPECT_FLOAT_EQ(height, old.HeightFactor);
                EXPECT_FLOAT_EQ(width*(1.0F-0.75F*height), old.Position[0]);
            }
        }
    }
    EXPECT_EQ(topology.Tuft.Count, 6);
    const std::vector<uint16_t> tuft(topology.Indices.begin()+topology.Tuft.First, topology.Indices.end());
    EXPECT_EQ(tuft, (std::vector<uint16_t>{0,1,2,0,2,3}));
    EXPECT_LT(topology.Indices.size()*sizeof(uint16_t), 4096);
}

TEST(Oot3dGrassSelectionCache, ReusesOnlyIdenticalSelectionInputs) {
    using namespace Fast::Oot3d;
    GrassSelectionCache cache;
    GrassSelectionKey key{1, BuildGrassLodPolicy(InteractiveGrassSettings{}), 3, 500000, true,
        {GrassSelectionView{}}};
    EXPECT_FALSE(cache.Matches(key));
    cache.Store(key);
    EXPECT_TRUE(cache.Matches(key));
    const auto changes = [&](auto edit) { auto changed=key; edit(changed); EXPECT_FALSE(cache.Matches(changed)); };
    changes([](auto& v){++v.StaticRevision;});
    changes([](auto& v){++v.Budget;});
    changes([](auto& v){v.BladeRadiusScale+=1;});
    changes([](auto& v){v.FrustumCulling=!v.FrustumCulling;});
    changes([](auto& v){v.Views[0].PositionToClip[12]+=0.000001F;});
    changes([](auto& v){v.Views[0].Eye[0]+=0.000001F;});
    changes([](auto& v){v.Views.push_back({});});
    changes([](auto& v){v.Policy.DrawDistance+=1;});
    changes([](auto& v){v.Policy.LodStart+=0.01F;});
    changes([](auto& v){v.Policy.LodEnd+=0.01F;});
    changes([](auto& v){v.Policy.FarDensity+=0.01F;});
    changes([](auto& v){v.Policy.SegmentStartDistance+=1;});
    changes([](auto& v){v.Policy.SegmentEndDistance+=1;});
    changes([](auto& v){++v.Policy.NearBladeSegments;});
    changes([](auto& v){++v.Policy.FarBladeSegments;});
    changes([](auto& v){v.Policy.LodReferenceDistance+=1;});
    changes([](auto& v){v.Policy.FarTuftsEnabled=!v.Policy.FarTuftsEnabled;});
    changes([](auto& v){++v.Policy.FarTuftBladeCount;});
    changes([](auto& v){v.Policy.TuftTransitionFraction+=0.01F;});
    changes([](auto& v){v.Policy.FarTuftDensity+=0.01F;});
    changes([](auto& v){v.Policy.SegmentLodSoftness+=0.01F;});
    cache.Reset();
    EXPECT_FALSE(cache.Matches(key));
}

TEST(Oot3dGrassSelectionBudget, ParallelBinsKeepExactlyTheSerialPrefixAtEveryBudget) {
    using namespace Fast::Oot3d;
    constexpr size_t binCount = 25;
    std::vector<uint32_t> all;
    for(uint32_t i=0;i<400;++i) if(i%7!=0) all.push_back(10000+i*11);
    for(uint32_t budget=0;budget<=all.size()+3;++budget) {
        for(size_t workers : {1U,2U,3U,10U,12U}) {
            std::array<std::vector<uint32_t>,binCount> merged;
            uint32_t remaining=budget;
            for(size_t worker=0;worker<workers;++worker) {
                std::array<std::vector<uint32_t>,binCount> bins;
                for(size_t i=all.size()*worker/workers;i<all.size()*(worker+1)/workers;++i)
                    bins[(i*23)%binCount].push_back(all[i]);
                remaining-=LimitGrassSelectionBins(bins,remaining);
                for(size_t i=0;i<binCount;++i) merged[i].insert(merged[i].end(),bins[i].begin(),bins[i].end());
            }
            std::array<std::vector<uint32_t>,binCount> expected;
            for(size_t i=0;i<std::min<size_t>(budget,all.size());++i) expected[(i*23)%binCount].push_back(all[i]);
            EXPECT_EQ(merged,expected) << "budget " << budget << " workers " << workers;
        }
    }
}

TEST(Oot3dGrassAnchorCodec, DirectionsRoundTripAcrossBothHemispheres) {
    using namespace Fast::Oot3d;
    EXPECT_EQ(sizeof(GrassWorldAnchor), 36U);
    for (int x = -10; x <= 10; ++x) for (int y = -10; y <= 10; ++y) for (int z = -10; z <= 10; ++z) {
        const float length = std::sqrt(static_cast<float>(x*x+y*y+z*z));
        if (length == 0) continue;
        const std::array<float, 3> normal{x/length,y/length,z/length};
        const auto decoded = UnpackGrassNormal(PackGrassNormal(normal));
        for (size_t axis=0; axis<3; ++axis) EXPECT_NEAR(decoded[axis],normal[axis],0.00015F);
        const std::array<float, 2> direction{normal[0],normal[1]};
        const auto decodedDirection=UnpackGrassDirection(PackGrassDirection(direction));
        for (size_t axis=0; axis<2; ++axis) EXPECT_NEAR(decodedDirection[axis],direction[axis],0.00002F);
    }
}

TEST(Oot3dGrassVisibility, DistantClusterWorkOnlyContainsLodPrefix) {
    using namespace Fast::Oot3d;
    GrassWorldPlacement placement;
    placement.Clusters.push_back({.FirstAnchor=0,.AnchorCount=1000,.Center={0,0,-2000}});
    for (size_t i=0;i<1000;++i) placement.CullingAnchors.push_back({{0,0,-2000},static_cast<float>(i)/1000});
    InteractiveGrassSettings settings;
    settings.FarTuftsEnabled=false;
    settings.DrawDistance=10000;
    settings.LodReferenceDistance=1000;
    settings.FarDensity=0.1F;
    const std::array<uint32_t,1> indices{0};
    const std::array<float,16> projection{1,0,0,0, 0,1,0,0, 0,0,-1,-1, 0,0,0,0};
    std::vector<GrassClusterWork> work;
    const auto count=PrepareGrassClusterWork(placement,indices,projection,{0,0,0},
        BuildGrassLodPolicy(settings),1,true,work);
    ASSERT_EQ(work.size(),1U);
    EXPECT_EQ(count,26U);
    EXPECT_EQ(work[0].AnchorEnd,26U);
    EXPECT_FALSE(work[0].CullIndividualBlades);
    EXPECT_TRUE(ResolveGrassLodWithStableVisibility(settings,2000,placement.CullingAnchors[25].StableVisibility).Visible);
    EXPECT_FALSE(ResolveGrassLodWithStableVisibility(settings,2000,placement.CullingAnchors[26].StableVisibility).Visible);
    settings.DrawDistance=1000;
    EXPECT_EQ(PrepareGrassClusterWork(placement,indices,projection,{0,0,0},BuildGrassLodPolicy(settings),1,true,work),0U);
    EXPECT_TRUE(work.empty());
}

TEST(Oot3dGrassVisibility, LongerVisibilityDoesNotIncreaseExistingDetail) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings settings;
    settings.FarTuftsEnabled=false;
    settings.DrawDistance=5000;
    settings.LodReferenceDistance=5000;
    settings.FarDensity=0.01F;
    const auto original=settings;
    settings.DrawDistance=20000;
    for (int distance=0;distance<=5000;distance+=10) {
        const auto a=ResolveGrassLod(original,static_cast<float>(distance),42);
        const auto b=ResolveGrassLod(settings,static_cast<float>(distance),42);
        EXPECT_EQ(a.Visible,b.Visible);
        EXPECT_EQ(a.BladeSegments,b.BladeSegments);
        EXPECT_EQ(a.PlaneCount,b.PlaneCount);
        EXPECT_FLOAT_EQ(a.Retention,b.Retention);
    }
    EXPECT_FLOAT_EQ(GrassLodRetention(settings,10000),0.0025F);
    EXPECT_FLOAT_EQ(GrassLodRetention(settings,20000),0.000625F);
    EXPECT_FLOAT_EQ(GrassLodRetention(settings,20001),0.0F);
}

TEST(Oot3dGrassVisibility, DistantTuftsTradeInstancesForCoverageWithoutChangingNearLod) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings detailed;
    detailed.DrawDistance=10000;
    detailed.LodReferenceDistance=1000;
    detailed.LodEndFraction=0.7F;
    detailed.FarDensity=0.1F;
    detailed.FarTuftsEnabled=false;
    auto tufts=detailed;
    tufts.FarTuftsEnabled=true;
    tufts.FarTuftBladeCount=5;
    float previousRetention=1.0F;
    for (int distance=0;distance<=10000;++distance) {
        const float d=static_cast<float>(distance);
        const auto oldLod=ResolveGrassLodWithStableVisibility(detailed,d,0.0F);
        const auto newLod=ResolveGrassLodWithStableVisibility(tufts,d,0.0F);
        const float weight=GrassTuftWeight(d/1000.0F,0.7F,0.7F+tufts.TuftTransitionFraction);
        const float coverage=GrassTuftCoverage(GrassTuftMeanGrowth(weight),5);
        EXPECT_NEAR(newLod.Retention*coverage,oldLod.Retention,1.0e-6F);
        EXPECT_LE(newLod.Retention,previousRetention);
        previousRetention=newLod.Retention;
        if (distance<=700) {
            EXPECT_FALSE(newLod.DistantTuft);
            EXPECT_EQ(newLod.BladeSegments,oldLod.BladeSegments);
            EXPECT_EQ(newLod.PlaneCount,oldLod.PlaneCount);
            EXPECT_FLOAT_EQ(newLod.Retention,oldLod.Retention);
        } else {
            EXPECT_TRUE(newLod.DistantTuft);
            EXPECT_EQ(newLod.PlaneCount,1U);
        }
    }
    EXPECT_FLOAT_EQ(GrassTuftWeight(0.7F,0.7F),0.0F);
    EXPECT_FLOAT_EQ(GrassTuftWeight(1.0F,0.7F),1.0F);
    EXPECT_FLOAT_EQ(GrassLodRetention(tufts,2000),GrassLodRetention(detailed,2000)/5.0F);
    tufts.DrawDistance=20000;
    EXPECT_FLOAT_EQ(GrassLodRetention(tufts,2000),GrassLodRetention(detailed,2000)/5.0F);
}

TEST(Oot3dGrassVisibility, DistantTuftClusterPrefixMatchesPerInstanceRetention) {
    using namespace Fast::Oot3d;
    GrassWorldPlacement placement;
    placement.Clusters.push_back({.FirstAnchor=0,.AnchorCount=1000,.Center={0,0,-2000}});
    for (size_t i=0;i<1000;++i) placement.CullingAnchors.push_back({{0,0,-2000},static_cast<float>(i)/1000});
    InteractiveGrassSettings settings;
    settings.DrawDistance=10000;
    settings.LodReferenceDistance=1000;
    settings.FarDensity=0.1F;
    settings.FarTuftsEnabled=true;
    settings.FarTuftBladeCount=5;
    const std::array<uint32_t,1> indices{0};
    const std::array<float,16> projection{1,0,0,0, 0,1,0,0, 0,0,-1,-1, 0,0,0,0};
    std::vector<GrassClusterWork> work;
    const auto count=PrepareGrassClusterWork(placement,indices,projection,{0,0,0},
        BuildGrassLodPolicy(settings),1,true,work);
    ASSERT_EQ(work.size(),1U);
    EXPECT_EQ(count,6U);
    for (uint32_t i=0;i<1000;++i) {
        EXPECT_EQ(ResolveGrassLodWithStableVisibility(settings,2000,placement.CullingAnchors[i].StableVisibility).Visible,
                  i<count);
    }
}

TEST(Oot3dGrassVisibility, TuftTransitionMixesStableTopologiesWithoutADistanceRing) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings settings;
    settings.DrawDistance = 5000;
    settings.LodReferenceDistance = 1000;
    settings.LodEndFraction = 0.5F;
    settings.TuftTransitionFraction = 0.4F;
    settings.FarDensity = 1;
    size_t tufts = 0;
    for (uint32_t i = 0; i < 10000; ++i) {
        const float stable = static_cast<float>(i) / 10000;
        EXPECT_FALSE(ResolveGrassLodWithStableVisibility(settings, 500, stable).DistantTuft);
        EXPECT_TRUE(ResolveGrassLodWithStableVisibility(settings, 900, stable).DistantTuft);
        const auto middle = ResolveGrassLodWithStableVisibility(settings, 700, stable);
        tufts += middle.DistantTuft;
        EXPECT_EQ(middle.DistantTuft, ResolveGrassLodWithStableVisibility(settings, 700, stable).DistantTuft);
    }
    EXPECT_GT(tufts, 4700U);
    EXPECT_LT(tufts, 5300U);
}

TEST(Oot3dGrassVisibility, IncreasedTuftDensityKeepsConservativeClusterBounds) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings settings;
    settings.DrawDistance = 5000;
    settings.LodReferenceDistance = 1000;
    settings.LodEndFraction = 0.45F;
    settings.TuftTransitionFraction = 0.45F;
    settings.FarTuftBladeCount = 2;
    settings.FarDensity = 0.7F;
    const auto base = BuildGrassLodPolicy(settings);
    settings.FarTuftDensity = 4;
    const auto dense = BuildGrassLodPolicy(settings);
    EXPECT_FLOAT_EQ(GrassLodRetention(base, 200), GrassLodRetention(dense, 200));
    EXPECT_GT(GrassLodRetention(dense, 2000), GrassLodRetention(base, 2000));
    for (float nearest = 0; nearest <= 5000; nearest += 20) {
        const float bound = GrassLodRetentionUpperBound(dense, nearest);
        for (float distance = nearest; distance <= 5000; distance += 20)
            EXPECT_LE(GrassLodRetention(dense, distance), bound + 1.0e-6F);
    }
}

TEST(Oot3dGrassVisibility, FadeHasContinuousEndpointsAndDoesNotEraseSparseFarGrass) {
    using namespace Fast::Oot3d;
    EXPECT_FLOAT_EQ(GrassDistanceFade(800, 1000, 0.2F), 1);
    EXPECT_NEAR(GrassDistanceFade(900, 1000, 0.2F), 0.5F, 1.0e-6F);
    EXPECT_FLOAT_EQ(GrassDistanceFade(1000, 1000, 0.2F), 0);
    EXPECT_FLOAT_EQ(GrassDistanceFade(1100, 1000, 0.2F), 0);
    EXPECT_FLOAT_EQ(GrassVisibilityFade(0, 0, 0.1F), 0);
    EXPECT_FLOAT_EQ(GrassVisibilityFade(1, 0.999F, 0.1F), 1);
    EXPECT_FLOAT_EQ(GrassVisibilityFade(0.0001F, 0.00005F, 0.1F), 1);
    EXPECT_NEAR(GrassVisibilityFade(0.2F, 0.19F, 0.1F), 0.5F, 1.0e-5F);
    EXPECT_FLOAT_EQ(GrassVisibilityFade(0.2F, 0.2F, 0.1F), 0);
    EXPECT_FLOAT_EQ(GrassVisibilityFade(0.2F, 0.21F, 0.1F), 0);
}

TEST(Oot3dGrassVisibility, SegmentTransitionSpreadsChangesWithoutAddingTopologyBins) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings settings;
    settings.DrawDistance = 5000;
    settings.Appearance.BladeSegments = 12;
    settings.FarBladeSegments = 2;
    settings.SegmentLodStartDistance = 0;
    settings.SegmentLodEndDistance = 900;
    settings.SegmentLodSoftness = 1;
    size_t middleAtBoundary = 0;
    for (uint32_t id = 0; id < 1000; ++id) {
        middleAtBoundary += ResolveGrassLod(settings, 300, id).BladeSegments == 6;
        uint8_t previous = 12;
        for (float distance = 0; distance <= 1000; distance += 10) {
            const auto lod = ResolveGrassLod(settings, distance, id);
            EXPECT_TRUE(lod.BladeSegments == 12 || lod.BladeSegments == 6 || lod.BladeSegments == 2);
            EXPECT_LE(lod.BladeSegments, previous);
            previous = lod.BladeSegments;
        }
    }
    EXPECT_GT(middleAtBoundary, 430U);
    EXPECT_LT(middleAtBoundary, 570U);
}

TEST(Oot3dGrassVisibility, SoftLodSettingsClampInvalidRanges) {
    using namespace Fast::Oot3d;
    GraphicsSettings settings;
    settings.Preset = GraphicsPreset::Custom;
    settings.Grass.Quality = GrassQuality::Custom;
    settings.Grass.DrawFadeFraction = 3;
    settings.Grass.DensityFadeFraction = -1;
    settings.Grass.TuftTransitionFraction = 5;
    settings.Grass.FarTuftDensity = 100;
    settings.Grass.FarTuftSpread = -2;
    settings.Grass.SegmentLodSoftness = -1;
    const auto value = GraphicsSettingsService::Validate(settings, {}).Value.Grass;
    EXPECT_FLOAT_EQ(value.DrawFadeFraction, 1);
    EXPECT_FLOAT_EQ(value.DensityFadeFraction, 0);
    EXPECT_FLOAT_EQ(value.TuftTransitionFraction, 1);
    EXPECT_FLOAT_EQ(value.FarTuftDensity, 4);
    EXPECT_FLOAT_EQ(value.FarTuftSpread, 0.25F);
    EXPECT_FLOAT_EQ(value.SegmentLodSoftness, 0);
}

TEST(Oot3dGrassWorldPlacementCache, ReusesWorldAnchorsAndAppliesNativeTransformOnce) {
    const std::vector<Fast::Oot3d::GrassAnchor> anchors{ {
        .LocalPosition = { 1.0F, 2.0F, 3.0F },
        .LocalNormal = { 0.0F, 1.0F, 0.0F },
        .BladeHeight = 8.0F,
        .BladeWidth = 2.0F,
        .Phase = 0.25F,
        .WidthAxis = { 0.0F, 1.0F },
        .StableId = 99U,
    } };
    const std::vector<Fast::Oot3d::GrassAnchorCluster> clusters{ {
        .FirstAnchor = 0U,
        .AnchorCount = 1U,
        .LocalCenter = { 1.0F, 2.0F, 3.0F },
        .LocalRadius = 4.0F,
        .MaximumBladeHeight = 8.0F,
    } };
    auto request = BaseRequest(anchors, clusters);
    Fast::Oot3d::GrassWorldPlacementCache cache;
    const auto first = cache.Resolve(request);
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->Anchors.size(), 1U);
    EXPECT_EQ(first->Anchors.front().BaseHeight, (std::array<float, 4>{ 12.0F, 28.0F, 36.0F, 4.0F }));
    EXPECT_EQ(Fast::Oot3d::UnpackGrassNormal(first->Anchors.front().PackedWorldNormal),
              (std::array<float, 4>{ 0.0F, 1.0F, 0.0F, 0.0F }));
    EXPECT_EQ(first->Anchors.front().StableId, 99U);
    ASSERT_EQ(first->CullingAnchors.size(), 1U);
    EXPECT_EQ(first->CullingAnchors.front().Position, (std::array<float, 3>{ 12.0F, 28.0F, 36.0F }));
    EXPECT_FLOAT_EQ(first->CullingAnchors.front().StableVisibility, Fast::Oot3d::GrassStableVisibilityValue(99U));
    ASSERT_EQ(first->Clusters.size(), 1U);
    EXPECT_FLOAT_EQ(first->Clusters.front().Radius, 18.0F);
    EXPECT_FLOAT_EQ(first->Clusters.front().MaximumBladeHeight, 4.0F);

    request.FrameId = 11U;
    const auto second = cache.Resolve(request);
    EXPECT_EQ(first, second);
    EXPECT_EQ(cache.Stats().Hits, 1U);
    EXPECT_EQ(cache.Stats().Misses, 1U);
}

TEST(Oot3dGrassWorldPlacementCache, RebuildsOnContentOrTransformChange) {
    const std::vector<Fast::Oot3d::GrassAnchor> anchors(1U);
    const std::vector<Fast::Oot3d::GrassAnchorCluster> clusters(1U);
    auto request = BaseRequest(anchors, clusters);
    Fast::Oot3d::GrassWorldPlacementCache cache;
    const auto first = cache.Resolve(request);
    ASSERT_NE(first, nullptr);
    request.ContentVersion = 8U;
    const auto contentUpdate = cache.Resolve(request);
    ASSERT_NE(contentUpdate, nullptr);
    EXPECT_NE(first, contentUpdate);
    request.NormalOffset = 3.0F;
    const auto transformUpdate = cache.Resolve(request);
    ASSERT_NE(transformUpdate, nullptr);
    EXPECT_NE(contentUpdate, transformUpdate);
    EXPECT_EQ(cache.Stats().Misses, 3U);
    EXPECT_EQ(cache.Stats().Updates, 1U);
    EXPECT_EQ(cache.Stats().Entries, 2U);
}

TEST(Oot3dGrassWorldPlacementCache, SmallModelScaleDoesNotShrinkWorldHeightBounds) {
    const std::vector<Fast::Oot3d::GrassAnchor> anchors{{.BladeHeight = 10.0F}};
    const std::vector<Fast::Oot3d::GrassAnchorCluster> clusters{{
        .AnchorCount = 1U, .LocalRadius = 1.0F, .MaximumBladeHeight = 10.0F}};
    auto request = BaseRequest(anchors, clusters);
    request.ModelToWorld = {0.01F,0,0,0, 0,0.01F,0,0, 0,0,0.01F,0, 0,0,0,1};
    request.HeightScale = 1.0F;
    const auto world = Fast::Oot3d::BuildGrassWorldPlacement(request);
    ASSERT_EQ(world.Clusters.size(), 1U);
    EXPECT_NEAR(world.Clusters.front().Radius, 12.01F, 0.0001F);
    EXPECT_FLOAT_EQ(world.Clusters.front().MaximumBladeHeight, 10.0F);
}

TEST(Oot3dGrassVisibility, CameraComesFromRenderedProjectionNotNextGameplayTick) {
    using namespace Fast::Oot3d;
    // Perspective looking down -Z, with eye (100,50,25), stored column-major.
    std::array<float, 16> matrix{1,0,0,0, 0,2,0,0, 0,0,-1,-1, -100,-100,24,25};
    const std::array<float, 3> expected{100,50,25};
    ASSERT_TRUE(GrassCameraFromProjection(matrix).has_value());
    EXPECT_EQ(*GrassCameraFromProjection(matrix), expected);
    // Mirrored output and an off-center frustum still have the same eye.
    for (size_t column = 0; column < 4U; ++column) {
        matrix[column * 4U] += matrix[column * 4U + 3U] * 0.3F;
        matrix[column * 4U + 1U] *= -1.0F;
    }
    const auto modified = GrassCameraFromProjection(matrix);
    ASSERT_TRUE(modified.has_value());
    for (size_t i = 0; i < 3U; ++i) EXPECT_NEAR((*modified)[i], expected[i], 0.0001F);
    matrix[3] = matrix[7] = matrix[11] = 0.0F;
    matrix[15] = 1.0F;
    EXPECT_FALSE(GrassCameraFromProjection(matrix).has_value());
    matrix.fill(0.0F);
    EXPECT_FALSE(GrassCameraFromProjection(matrix).has_value());
}

TEST(Oot3dGrassVisibility, SpatialQueryMatchesExhaustiveCullingWithoutChangingAnchors) {
    using namespace Fast::Oot3d;
    std::vector<GrassAnchor> anchors;
    std::vector<GrassAnchorCluster> clusters;
    for (uint32_t i = 0; i < 4096U; ++i) {
        const std::array<float, 3> position{
            (static_cast<float>(i % 64U) - 32.0F) * 100.0F, 0.0F,
            -static_cast<float>(i / 64U) * 100.0F};
        anchors.push_back({.LocalPosition=position, .BladeHeight=8.0F, .StableId=i+1U});
        clusters.push_back({.FirstAnchor=i, .AnchorCount=1U, .LocalCenter=position,
                           .LocalRadius=4.0F, .MaximumBladeHeight=8.0F});
    }
    auto request = BaseRequest(anchors, clusters);
    request.TransformBakedIntoVertices = true;
    request.NormalOffset = 0.0F;
    auto indexed = BuildGrassWorldPlacement(request);
    auto exhaustive = indexed;
    exhaustive.VisibilityNodes.clear();
    ASSERT_GT(indexed.VisibilityNodes.size(), 1U);
    std::vector<uint32_t> selected, reference, firstSelection;
    for (int sample = 0; sample < 15; ++sample) {
        const float x = static_cast<float>(sample % 7) * 80.0F;
        std::array<float, 16> projection{1.5F,0,0,0, 0,2,0,0, 0,0,-1,-1, -x*1.5F,-100,24,25};
        const auto eye = GrassCameraFromProjection(projection);
        ASSERT_TRUE(eye.has_value());
        for (bool frustum : {false, true}) {
            const auto stats = SelectGrassVisibleClusters(indexed, projection, *eye, 1200.0F, 3.0F, frustum, selected);
            const auto brute = SelectGrassVisibleClusters(exhaustive, projection, *eye, 1200.0F, 3.0F, frustum, reference);
            EXPECT_EQ(selected, reference);
            EXPECT_EQ(stats.CandidateAnchors, brute.CandidateAnchors);
            EXPECT_LT(stats.TestedNodes, indexed.VisibilityNodes.size());
            InteractiveGrassSettings lodSettings;
            lodSettings.DrawDistance=1200;
            lodSettings.LodReferenceDistance=100;
            lodSettings.FarDensity=0.01F;
            const auto policy=BuildGrassLodPolicy(lodSettings);
            std::vector<uint32_t> lodSelected, lodReference;
            const auto lodStats=SelectGrassVisibleClusters(indexed,projection,*eye,1200,3,frustum,lodSelected,&policy);
            (void)SelectGrassVisibleClusters(exhaustive,projection,*eye,1200,3,frustum,lodReference,&policy);
            EXPECT_EQ(lodSelected,lodReference);
            EXPECT_LT(lodStats.CandidateClusters,stats.CandidateClusters);
            EXPECT_LE(lodStats.TestedNodes,stats.TestedNodes);
            lodSettings.FarTuftsEnabled=true;
            lodSettings.LodReferenceDistance=700;
            lodSettings.LodStartFraction=0.1F;
            lodSettings.LodEndFraction=0.3F;
            lodSettings.TuftTransitionFraction=0.6F;
            lodSettings.FarDensity=0.7F;
            for(float density : {0.1F,1.0F,2.0F,4.0F}) {
                lodSettings.FarTuftDensity=density;
                const auto fusedPolicy=BuildGrassLodPolicy(lodSettings);
                std::vector<GrassClusterWork> expectedWork, fusedWork;
                (void)SelectGrassVisibleClusters(indexed,projection,*eye,1200,3,frustum,lodSelected,&fusedPolicy);
                const auto expectedCount=PrepareGrassClusterWork(indexed,lodSelected,projection,*eye,fusedPolicy,3,frustum,expectedWork);
                const auto fused=SelectGrassClusterWork(indexed,projection,*eye,fusedPolicy,3,frustum,fusedWork);
                EXPECT_EQ(fusedWork,expectedWork) << "density " << density << " camera " << sample;
                EXPECT_EQ(fused.CandidateAnchors,expectedCount);
            }
        }
        if (sample == 0) firstSelection = selected;
        if (sample == 14) EXPECT_EQ(selected, firstSelection);
    }
    EXPECT_EQ(indexed.CullingAnchors.front().Position, anchors.front().LocalPosition);
    EXPECT_EQ(indexed.Anchors.back().StableId, anchors.back().StableId);
}
