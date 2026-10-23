#include "fast/oot3d/grass_world_placement_cache.h"
#include "fast/oot3d/grass_visibility.h"
#include "fast/oot3d/grass_actor_cells.h"
#include "fast/oot3d/grass_distant_tuft.h"
#include "fast/oot3d/grass_indexed_topology.h"
#include "fast/oot3d/grass_selection_cache.h"
#include "fast/oot3d/grass_selection_budget.h"
#include "fast/oot3d/grass_surface_reference.h"
#include "fast/renderer3ds/pica_surface_lighting_shader.h"
#include "fast/renderer3ds/pica_surface_color_response.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
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

TEST(Oot3dGrassSurfaceReference, PreservesNativeIndicesAndNormalizedWeights) {
    using namespace Fast::Oot3d;
    const auto reference = PackGrassSurfaceReference({0, 65535, 12345}, 0.25F, 0.5F);
    EXPECT_EQ(reference[0] & 65535U, 0U);
    EXPECT_EQ(reference[0] >> 16U, 65535U);
    EXPECT_EQ(reference[1] & 65535U, 12345U);
    EXPECT_NEAR(float((reference[1] >> 16U) & 255U) / 255.0F, 0.25F, 1.0F / 255);
    EXPECT_NEAR(float(reference[1] >> 24U) / 255.0F, 0.5F, 1.0F / 255);
    const auto corner = PackGrassSurfaceReference({1, 2, 3}, 1, 1);
    EXPECT_EQ((corner[1] >> 16U) & 255U, 255U);
    EXPECT_EQ(corner[1] >> 24U, 0U);
}

TEST(Oot3dGrassSurfaceLighting, DecodesMaterialScalesWithoutSceneExceptions) {
    using namespace Fast::Renderer3ds;
    std::array<uint32_t, 0x300> registers{};
    registers[0xc0] = 0x30;
    registers[0xc2] = 1;
    for (auto base : {0xc8, 0xd0, 0xd8, 0xf0, 0xf8}) registers[base] = 15;
    for (uint32_t scale = 0; scale < 3; ++scale) {
        registers[0xc4] = scale;
        const auto decoded = DecodePicaSurfaceColorResponse(registers);
        ASSERT_TRUE(decoded.Available);
        EXPECT_EQ(decoded.PackedScales, scale);
    }
    registers[0xc0] = 0x03;
    EXPECT_TRUE(DecodePicaSurfaceColorResponse(registers).Available);
    registers[0xcc] = 1;
    EXPECT_EQ(DecodePicaSurfaceColorResponse(registers).PackedScales, 2U | (1U << 2U));
    registers[0xca] = 1;
    EXPECT_FALSE(DecodePicaSurfaceColorResponse(registers).Available);
    registers[0xca] = 0;
    registers[0xc1] = 1;
    EXPECT_FALSE(DecodePicaSurfaceColorResponse(registers).Available);
    EXPECT_FALSE(DecodePicaSurfaceColorResponse({}).Available);
}

TEST(Oot3dGrassSurfaceLighting, RejectsMissingNativeShaderHooks) {
    EXPECT_TRUE(Fast::Renderer3ds::BuildPicaSurfaceLightingVertexShader(
        "void main() {}", {}, false).empty());
}


TEST(Oot3dGrassSurfaceReference, SubdivisionRetainsOriginalTriangleCoordinates) {
    using namespace Fast::Oot3d;
    std::array<GrassSourceVertex, 3> vertices{};
    vertices[1].Position = {1000, 0, 0};
    vertices[2].Position = {0, 0, 1000};
    const std::array<uint32_t, 3> indices{0, 1, 2};
    GrassSourceSurface surface;
    surface.Vertices = vertices;
    surface.Indices = indices;
    surface.PlacementView.Enabled = true;
    surface.PlacementView.DrawDistance = 10000;
    surface.PlacementView.FullDensityDistance = 10000;
    surface.PlacementView.FarDensity = 1;
    GrassScalarMask mask;
    mask.Width = mask.Height = 1;
    mask.Samples = {255};
    GrassGenerationSettings generation;
    generation.InstancesPerSquareMeter = 8;
    const auto anchors = GrassSurfaceExtractor::Extract(surface, {}, generation, mask, 100);
    ASSERT_FALSE(anchors.empty());
    for (const auto& anchor : anchors) {
        const auto ref = anchor.SurfaceReference;
        EXPECT_EQ(ref[0], 1U << 16U);
        EXPECT_EQ(ref[1] & 65535U, 2U);
        EXPECT_NEAR(float((ref[1] >> 16U) & 255U) * 1000.0F / 255, anchor.LocalPosition[0], 1000.0F / 255);
        EXPECT_NEAR(float(ref[1] >> 24U) * 1000.0F / 255, anchor.LocalPosition[2], 1000.0F / 255);
    }
}

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
    const std::vector<uint16_t> tuft(topology.Indices.begin()+topology.Tuft.First,
                                   topology.Indices.begin()+topology.Tuft.First+topology.Tuft.Count);
    EXPECT_EQ(tuft, (std::vector<uint16_t>{0,1,2,0,2,3}));
    EXPECT_LT((topology.Tuft.First+topology.Tuft.Count)*sizeof(uint16_t), 4096);
    for (uint32_t segments = 1; segments <= 2; ++segments)
        for (uint32_t planes = 1; planes <= 2; ++planes) {
            const auto blade = topology.Blades[segments-1][planes-1];
            const auto group = topology.Groups[segments-1][planes-1];
            ASSERT_EQ(group.Count, kGrassMidrangeClusterCapacity*blade.Count);
            for (uint32_t child = 0; child < kGrassMidrangeClusterCapacity; ++child)
                for (uint32_t i = 0; i < blade.Count; ++i)
                    EXPECT_EQ(topology.Indices[group.First+child*blade.Count+i],
                        topology.Indices[blade.First+i]+child*planes*(2*segments+1));
        }
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

TEST(Oot3dGrassSurfaceColor, WorldAnchorsRetainGridColorsWithoutChangingPlacement) {
    using namespace Fast::Oot3d;
    std::array<GrassAnchor, 2> anchors{};
    anchors[0].LocalPosition = {1, 2, 3};
    anchors[1].LocalPosition = {4, 5, 6};
    anchors[0].Uv = {0.125F, 0.125F};
    anchors[1].Uv = {0.375F, 0.125F};
    auto request = BaseRequest(anchors, {});
    const auto before = BuildGrassWorldPlacement(request);
    auto grid = std::make_shared<GrassTextureColorGrid>();
    grid->Rgb[0] = {32, 64, 128};
    grid->Rgb[1] = {192, 128, 64};
    request.ColorSource = {grid};
    const auto after = BuildGrassWorldPlacement(request);
    ASSERT_EQ(after.Anchors.size(), 2U);
    EXPECT_EQ(after.Anchors[0].SurfaceColor, 0xff804020U);
    EXPECT_EQ(after.Anchors[1].SurfaceColor, 0xff4080c0U);
    EXPECT_NE(before.ContentVersion, after.ContentVersion);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(after.Anchors[i].BaseHeight, before.Anchors[i].BaseHeight);
        EXPECT_EQ(after.Anchors[i].StableId, before.Anchors[i].StableId);
    }
}

TEST(Oot3dGrassSurfaceColor, RebuildsColorsWhenPresentGridChanges) {
    using namespace Fast::Oot3d;
    std::array<GrassAnchor, 1> anchors{};
    anchors[0].Uv = {0.125F, 0.125F};
    auto request = BaseRequest(anchors, {});
    auto red = std::make_shared<GrassTextureColorGrid>();
    red->Rgb[0] = {255, 0, 0};
    auto blue = std::make_shared<GrassTextureColorGrid>();
    blue->Rgb[0] = {0, 0, 255};
    request.ColorSource = {red};
    GrassWorldPlacementCache cache;
    const auto first = cache.Resolve(request);
    request.ColorSource = {blue};
    const auto second = cache.Resolve(request);
    ASSERT_NE(first, second);
    EXPECT_EQ(first->Anchors[0].SurfaceColor, 0xff0000ffU);
    EXPECT_EQ(second->Anchors[0].SurfaceColor, 0xffff0000U);
    EXPECT_EQ(first->Anchors[0].BaseHeight, second->Anchors[0].BaseHeight);
    auto sameBlue = std::make_shared<GrassTextureColorGrid>(*blue);
    request.ColorSource = {sameBlue};
    EXPECT_EQ(cache.Resolve(request), second);
}

TEST(Oot3dGrassAnchorCodec, DirectionsRoundTripAcrossBothHemispheres) {
    using namespace Fast::Oot3d;
    EXPECT_EQ(sizeof(GrassWorldAnchor), 48U);
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

namespace {

// Column-major clip matrix of a perspective camera at eye looking along yaw/pitch.
std::array<float, 16> GrassTestViewProjection(const std::array<float, 3>& eye, float yaw, float pitch,
                                              float fovY, float aspect) {
    const std::array<float, 3> forward{std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                       -std::cos(pitch) * std::cos(yaw)};
    std::array<float, 3> right{-forward[2], 0.0F, forward[0]};
    const float rightLength = std::hypot(right[0], right[2]);
    right[0] /= rightLength;
    right[2] /= rightLength;
    const std::array<float, 3> up{right[1] * forward[2] - right[2] * forward[1],
                                  right[2] * forward[0] - right[0] * forward[2],
                                  right[0] * forward[1] - right[1] * forward[0]};
    const auto dot = [](const auto& a, const auto& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const float sy = 1.0F / std::tan(fovY * 0.5F), sx = sy / aspect, nearPlane = 1.0F, farPlane = 100000.0F;
    const float a = (farPlane + nearPlane) / (nearPlane - farPlane), b = 2.0F * farPlane * nearPlane / (nearPlane - farPlane);
    // View rows: right, up, -forward; clip w is the distance along forward.
    std::array<float, 16> m{};
    for (size_t column = 0; column < 3U; ++column) {
        m[column * 4U + 0U] = sx * right[column];
        m[column * 4U + 1U] = sy * up[column];
        m[column * 4U + 2U] = -a * forward[column];
        m[column * 4U + 3U] = forward[column];
    }
    m[12] = -sx * dot(right, eye);
    m[13] = -sy * dot(up, eye);
    m[14] = a * dot(forward, eye) + b;
    m[15] = -dot(forward, eye);
    return m;
}

} // namespace

TEST(Oot3dGrassVisibility, RetentionBoundDoesNotIncreaseWithDistance) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings settings;
    settings.DrawDistance = 50000;
    settings.LodReferenceDistance = 2282;
    settings.FarTuftsEnabled = true;
    for (float density : {0.5F, 1.0F, 2.0F, 4.0F}) {
        settings.FarTuftDensity = density;
        for (float far : {0.05F, 0.6F, 1.0F}) {
            settings.FarDensity = far;
            const auto policy = BuildGrassLodPolicy(settings);
            float previous = GrassLodRetentionUpperBound(policy, 0.0F);
            for (float distance = 0.25F; distance <= 12000.0F; distance += 0.25F) {
                const float bound = GrassLodRetentionUpperBound(policy, distance);
                ASSERT_LE(bound, previous + 1.0e-6F) << "density " << density << " far " << far << " at " << distance;
                previous = bound;
            }
        }
    }
}

TEST(Oot3dGrassVisibility, CandidateEnvelopeRefinesToTheExactClusterWork) {
    using namespace Fast::Oot3d;
    std::vector<GrassAnchor> anchors;
    std::vector<GrassAnchorCluster> clusters;
    uint32_t stable = 1U;
    for (uint32_t i = 0; i < 96U * 96U; ++i) {
        const std::array<float, 3> center{(static_cast<float>(i % 96U) - 48.0F) * 60.0F,
            std::sin(static_cast<float>(i) * 0.37F) * 40.0F, -(static_cast<float>(i / 96U) - 8.0F) * 60.0F};
        const uint32_t first = static_cast<uint32_t>(anchors.size());
        std::vector<GrassAnchor> local;
        for (uint32_t j = 0; j < 6U; ++j) {
            local.push_back({.LocalPosition = {center[0] + (static_cast<float>(j % 3U) - 1.0F) * 15.0F, center[1],
                                               center[2] + (static_cast<float>(j / 3U) - 0.5F) * 20.0F},
                             .BladeHeight = 8.0F, .StableId = stable++});
        }
        // Culling anchors are ordered by stable visibility inside a cluster.
        std::sort(local.begin(), local.end(), [](const auto& x, const auto& y) {
            return GrassStableVisibilityValue(x.StableId) < GrassStableVisibilityValue(y.StableId);
        });
        anchors.insert(anchors.end(), local.begin(), local.end());
        clusters.push_back({.FirstAnchor = first, .AnchorCount = 6U, .LocalCenter = center, .LocalRadius = 25.0F,
                            .MaximumBladeHeight = 8.0F});
    }
    auto request = BaseRequest(anchors, clusters);
    request.TransformBakedIntoVertices = true;
    request.NormalOffset = 0.0F;
    const auto placement = BuildGrassWorldPlacement(request);
    ASSERT_GT(placement.VisibilityNodes.size(), 1U);
    // The canonical decision is per cluster: a flat scan without hierarchy.
    auto flat = placement;
    flat.VisibilityNodes.clear();
    for (uint32_t c = 0; c < placement.Clusters.size(); ++c) {
        const auto& cluster = placement.Clusters[c];
        for (uint32_t a = cluster.FirstAnchor + 1U; a < cluster.FirstAnchor + cluster.AnchorCount; ++a)
            ASSERT_LE(placement.CullingAnchors[a - 1U].StableVisibility, placement.CullingAnchors[a].StableVisibility);
    }

    InteractiveGrassSettings settings;
    settings.DrawDistance = 5000;
    settings.LodReferenceDistance = 700;
    settings.LodStartFraction = 0.1F;
    settings.LodEndFraction = 0.3F;
    settings.TuftTransitionFraction = 0.6F;
    settings.FarDensity = 0.7F;
    settings.FarTuftsEnabled = true;
    uint32_t seed = 12345U;
    const auto random = [&seed](float low, float high) {
        seed = seed * 1664525U + 1013904223U;
        return low + (high - low) * static_cast<float>(seed >> 8U) / static_cast<float>(1U << 24U);
    };
    size_t contained = 0, refined = 0;
    for (float density : {1.0F, 4.0F}) {
        settings.FarTuftDensity = density;
        const auto policy = BuildGrassLodPolicy(settings);
        for (int base = 0; base < 6; ++base) {
            const std::array<float, 3> eye0{random(-1500, 1500), random(60, 200), random(-1500, 500)};
            const float yaw0 = random(-3.1F, 3.1F), pitch0 = random(-0.4F, 0.05F), fov0 = random(0.8F, 1.1F);
            const auto clip0 = GrassTestViewProjection(eye0, yaw0, pitch0, fov0, 16.0F / 9.0F);
            for (bool frustum : {true, false}) {
                const auto envelope = MakeGrassCandidateEnvelope(clip0, eye0, 16.0F, 0.02F, 2.0F);
                ASSERT_TRUE(GrassCandidateEnvelopeContains(envelope, clip0, eye0));
                std::vector<GrassClusterCandidate> candidates, part;
                (void)SelectGrassClusterCandidates(placement, envelope, policy, 3.0F, frustum, 0U, candidates);
                // A split traversal covers the same candidates.
                std::vector<uint32_t> roots;
                (void)SplitGrassClusterCandidates(placement, envelope, policy, 3.0F, frustum, 8U, roots);
                std::vector<GrassClusterCandidate> merged;
                for (const auto root : roots) {
                    (void)SelectGrassClusterCandidates(placement, envelope, policy, 3.0F, frustum, root, part);
                    merged.insert(merged.end(), part.begin(), part.end());
                }
                const auto byIndex = [](const auto& x, const auto& y) { return x.ClusterIndex < y.ClusterIndex; };
                std::sort(candidates.begin(), candidates.end(), byIndex);
                std::sort(merged.begin(), merged.end(), byIndex);
                EXPECT_EQ(merged, candidates);
                for (int sample = 0; sample < 40; ++sample) {
                    // Half of the samples stay well inside, the rest probe the edge.
                    const float scale = sample < 20 ? 0.5F : 1.0F;
                    const std::array<float, 3> eye{eye0[0] + random(-9, 9) * scale, eye0[1] + random(-9, 9) * scale,
                                                   eye0[2] + random(-9, 9) * scale};
                    const auto clip = GrassTestViewProjection(eye, yaw0 + random(-0.012F, 0.012F) * scale,
                        pitch0 + random(-0.01F, 0.01F) * scale, fov0 + random(-0.01F, 0.01F) * scale, 16.0F / 9.0F);
                    if (!GrassCandidateEnvelopeContains(envelope, clip, eye)) continue;
                    ++contained;
                    std::vector<GrassClusterWork> exact, cached;
                    const auto exactStats = SelectGrassClusterWork(placement, clip, eye, policy, 3.0F, frustum, exact);
                    std::vector<GrassClusterWork> canonical;
                    (void)SelectGrassClusterWork(flat, clip, eye, policy, 3.0F, frustum, canonical);
                    EXPECT_EQ(exact, canonical) << "hierarchy differs from the flat scan";
                    uint64_t anchorsFromCandidates = 0;
                    if (!RefineGrassClusterCandidates(placement, candidates, clip, eye, policy, 3.0F, frustum, cached,
                                                      anchorsFromCandidates))
                        continue;
                    ++refined;
                    EXPECT_EQ(cached, exact) << "density " << density << " base " << base << " sample " << sample;
                    EXPECT_EQ(anchorsFromCandidates, exactStats.CandidateAnchors);
                }
            }
        }
    }
    // Refinement inside the envelope never reports a violation.
    EXPECT_EQ(refined, contained);
    EXPECT_GT(contained, 200U);
    // Views far outside are not contained.
    const std::array<float, 3> eye{0, 100, 0};
    const auto clip = GrassTestViewProjection(eye, 0, 0, 1, 1.5F);
    const auto envelope = MakeGrassCandidateEnvelope(clip, eye, 16.0F, 0.02F, 2.0F);
    EXPECT_FALSE(GrassCandidateEnvelopeContains(envelope, GrassTestViewProjection(eye, 0.2F, 0, 1, 1.5F), eye));
    EXPECT_FALSE(GrassCandidateEnvelopeContains(envelope, clip, {0, 100, 40}));
}

TEST(Oot3dGrassActorCells, EveryActorThatCanTouchARootIsListedInItsCellInOrder) {
    using namespace Fast::Oot3d;
    uint32_t seed = 777U;
    const auto random = [&seed](float low, float high) {
        seed = seed * 1664525U + 1013904223U;
        return low + (high - low) * static_cast<float>(seed >> 8U) / static_cast<float>(1U << 24U);
    };
    for (int scene = 0; scene < 20; ++scene) {
        std::vector<GrassInteractor> actors(static_cast<size_t>(1 + scene * 5));
        const float spread = scene % 2 == 0 ? 8000.0F : 60.0F;
        for (auto& actor : actors) {
            actor.Position = {random(-spread, spread), random(-50, 50), random(-spread, spread)};
            actor.PreviousPosition = {actor.Position[0] + random(-30, 30), actor.Position[1],
                                      actor.Position[2] + random(-30, 30)};
            actor.Radius = random(0.0F, 40.0F);
        }
        const float multiplier = 1.25F;
        const auto cells = BuildGrassActorCells(actors, multiplier);
        ASSERT_GT(cells.Width, 0U);
        ASSERT_LE(cells.Width, kGrassActorCellsMaximumExtent);
        ASSERT_LE(cells.Height, kGrassActorCellsMaximumExtent);
        const uint32_t cellCount = cells.Width * cells.Height;
        ASSERT_GE(cells.Words.size(), cellCount + 1U);
        for (int sample = 0; sample < 4000; ++sample) {
            // Roots near actors, and anywhere in the scene.
            const auto& near = actors[static_cast<size_t>(sample) % actors.size()];
            const std::array<float, 2> root = sample % 3 == 0
                ? std::array<float, 2>{random(-spread * 1.2F, spread * 1.2F), random(-spread * 1.2F, spread * 1.2F)}
                : std::array<float, 2>{near.Position[0] + random(-60, 60), near.Position[2] + random(-60, 60)};
            std::vector<uint32_t> touching;
            for (uint32_t i = 0; i < actors.size(); ++i) {
                const auto box = GrassActorSweptBox(actors[i], multiplier);
                if (!(root[0] < box[0] || root[1] < box[1] || root[0] > box[2] || root[1] > box[3]))
                    touching.push_back(i);
            }
            // The shader's cell lookup.
            const float cx = std::floor((root[0] - cells.Origin[0]) * cells.InverseCellSize);
            const float cz = std::floor((root[1] - cells.Origin[1]) * cells.InverseCellSize);
            std::vector<uint32_t> listed;
            if (cx >= 0 && cz >= 0 && cx < static_cast<float>(cells.Width) && cz < static_cast<float>(cells.Height)) {
                const uint32_t index = static_cast<uint32_t>(cz) * cells.Width + static_cast<uint32_t>(cx);
                for (uint32_t i = cells.Words[index]; i < cells.Words[index + 1U]; ++i)
                    listed.push_back(cells.Words[cellCount + 1U + i]);
            }
            ASSERT_TRUE(std::is_sorted(listed.begin(), listed.end()));
            ASSERT_TRUE(std::includes(listed.begin(), listed.end(), touching.begin(), touching.end()))
                << "scene " << scene << " sample " << sample;
        }
    }
    // Non-finite actors disable the grid: the shader iterates every actor.
    std::vector<GrassInteractor> invalid(2);
    invalid[1].Position[0] = NAN;
    EXPECT_EQ(BuildGrassActorCells(invalid, 1.0F).Width, 0U);
    EXPECT_EQ(BuildGrassActorCells({}, 1.0F).Width, 0U);
}

TEST(Oot3dGrassVisibility, LodEvaluatorFarBandIsBitIdentical) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings base;
    base.DrawDistance = 50000;
    base.LodReferenceDistance = 2282;
    base.LodStartFraction = 0.32F;
    base.LodEndFraction = 0.32F;
    base.TuftTransitionFraction = 0.48F;
    base.FarTuftsEnabled = true;
    base.FarTuftBladeCount = 4;
    base.FarTuftDensity = 2;
    base.FarDensity = 1;
    base.Appearance.BladeSegments = 5;
    base.FarBladeSegments = 1;
    base.SegmentLodStartDistance = 501;
    base.SegmentLodEndDistance = 1000;
    base.SegmentLodSoftness = 0.75F;
    std::vector<InteractiveGrassSettings> variants(8, base);
    variants[1].TuftTransitionFraction = 0;
    variants[2].FarTuftsEnabled = false;
    variants[3].SegmentLodSoftness = 0;
    variants[4].SegmentLodSoftness = 1;
    variants[5].SegmentLodEndDistance = 400;  // end below start
    variants[6].LodReferenceDistance = 0;
    variants[6].DrawDistance = 3000;
    variants[7].FarDensity = 0.3F;
    variants[7].FarTuftDensity = 0.5F;
    size_t far = 0;
    for (const auto& settings : variants) {
        const auto policy = BuildGrassLodPolicy(settings);
        const GrassLodEvaluator evaluator(policy);
        for (float distance = -10.0F; distance <= 9000.0F; distance += 0.37F) {
            for (uint32_t id = 1; id < 400; id += 7) {
                const float stable = GrassStableVisibilityValue(id * 2654435761U);
                const auto expected = ResolveGrassLodWithStableVisibility(policy, distance, stable);
                const auto actual = evaluator.Resolve(distance, stable);
                ASSERT_EQ(actual.Visible, expected.Visible) << distance;
                ASSERT_EQ(actual.BladeSegments, expected.BladeSegments) << distance;
                ASSERT_EQ(actual.PlaneCount, expected.PlaneCount) << distance;
                ASSERT_EQ(actual.DistantTuft, expected.DistantTuft) << distance;
                ASSERT_EQ(std::bit_cast<uint32_t>(actual.Retention), std::bit_cast<uint32_t>(expected.Retention)) << distance;
            }
            far += distance >= 1826.0F;
        }
    }
    EXPECT_GT(far, 0U);
}
