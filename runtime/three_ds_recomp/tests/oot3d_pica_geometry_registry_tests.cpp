#include "fast/oot3d/pica_geometry_registry.h"

#include <gtest/gtest.h>

#include <array>

namespace {

Fast::Oot3d::PicaGeometryRequest BuildRequest(uint64_t identity, uint64_t version, std::span<const uint8_t> indices,
                                              std::span<const Fast::Oot3d::PicaNriSourceVertexStream> streams,
                                              std::span<const Fast::Oot3d::PicaNriSourceVertexAttribute> attributes) {
    return {
        identity, version, true, 7U, streams, attributes, indices, true, false, 3U, true,
    };
}

TEST(Oot3dPicaGeometryRegistry, ReusesPackedPayloadAndExpandsEightBitIndicesOnce) {
    const std::array<float, 9> vertexValues{ 0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F };
    const auto vertexBytes =
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(vertexValues.data()), sizeof(vertexValues));
    const std::array<uint8_t, 3> indices{ 0U, 2U, 1U };
    const std::array<Fast::Oot3d::PicaNriSourceVertexStream, 1> streams{ {
        { { 0U, 3U * sizeof(float), false }, vertexBytes },
    } };
    const std::array<Fast::Oot3d::PicaNriSourceVertexAttribute, 1> attributes{ {
        { 0U, 0U, Fast::Oot3d::PicaNriVertexScalar::Float, 3U, 0U },
    } };

    Fast::Oot3d::PicaGeometryRegistry registry;
    const auto request = BuildRequest(31U, 41U, indices, streams, attributes);
    const auto first = registry.Resolve(request);
    ASSERT_NE(first.Geometry, nullptr);
    EXPECT_FALSE(first.CacheHit);
    EXPECT_TRUE(first.Cacheable);
    EXPECT_TRUE(first.Geometry->PackedValid);
    EXPECT_EQ(first.Geometry->SourceBindings.size(), 1U);
    EXPECT_EQ(first.Geometry->PackedBindings.size(), 1U);
    EXPECT_EQ(first.Geometry->PackedBindings[0].Stride, 4U * sizeof(float));
    EXPECT_EQ(first.Geometry->VertexOrIndexCount, 3U);
    EXPECT_EQ(first.Geometry->Payload.size() - first.Geometry->IndexOffset, indices.size() * sizeof(uint16_t));

    const auto second = registry.Resolve(request);
    ASSERT_NE(second.Geometry, nullptr);
    EXPECT_TRUE(second.CacheHit);
    EXPECT_EQ(first.Geometry, second.Geometry);
    const auto stats = registry.Stats();
    EXPECT_EQ(stats.Hits, 1U);
    EXPECT_EQ(stats.Misses, 1U);
    EXPECT_EQ(stats.Entries, 1U);
}

TEST(Oot3dPicaGeometryRegistry, ContentVersionUpdatesInPlaceAndCapacityEvictsByIdentity) {
    const std::array<float, 3> vertexValues{ 0.0F, 1.0F, 2.0F };
    const auto vertexBytes =
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(vertexValues.data()), sizeof(vertexValues));
    const std::array<uint8_t, 1> indices{ 0U };
    const std::array<Fast::Oot3d::PicaNriSourceVertexStream, 1> streams{ {
        { { 0U, 3U * sizeof(float), false }, vertexBytes },
    } };
    const std::array<Fast::Oot3d::PicaNriSourceVertexAttribute, 1> attributes{ {
        { 0U, 0U, Fast::Oot3d::PicaNriVertexScalar::Float, 3U, 0U },
    } };

    Fast::Oot3d::PicaGeometryRegistry registry(1U);
    auto request = BuildRequest(3U, 5U, indices, streams, attributes);
    ASSERT_NE(registry.Resolve(request).Geometry, nullptr);
    request.ContentVersion = 6U;
    ASSERT_NE(registry.Resolve(request).Geometry, nullptr);
    EXPECT_EQ(registry.Stats().Updates, 1U);

    request.Identity = 4U;
    request.ContentVersion = 7U;
    ASSERT_NE(registry.Resolve(request).Geometry, nullptr);
    const auto evicted = registry.TakeEvictedIdentities();
    ASSERT_EQ(evicted.size(), 1U);
    EXPECT_EQ(evicted[0], 3U);
    EXPECT_EQ(registry.Stats().Evictions, 1U);

    request.IdentityAvailable = false;
    ASSERT_NE(registry.Resolve(request).Geometry, nullptr);
    EXPECT_EQ(registry.Stats().DynamicBuilds, 1U);
    EXPECT_EQ(registry.Stats().Entries, 1U);
}

} // namespace
