#include "fast/oot3d/grass_geometry_registry.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

namespace {

template <typename Value> std::vector<uint8_t> BytesOf(const std::vector<Value>& values) {
    std::vector<uint8_t> bytes(values.size() * sizeof(Value));
    std::memcpy(bytes.data(), values.data(), bytes.size());
    return bytes;
}

Fast::Oot3d::GrassGeometryRequest MakeRequest(std::span<const uint8_t> positions, std::span<const uint8_t> uvs,
                                              std::span<const uint8_t> indices) {
    using namespace Fast::Oot3d;
    GrassGeometryRequest request;
    request.Identity = 91U;
    request.ContentVersion = 7U;
    request.IdentityAvailable = true;
    request.FrameId = 10U;
    request.Position = { positions, 3U * sizeof(float), ::Oot3d::Renderer::PicaVertexFormat::Float, 3U, 0U };
    request.TexCoord0 = { uvs, 2U * sizeof(float), ::Oot3d::Renderer::PicaVertexFormat::Float, 2U, 0U };
    request.IndexBytes = indices;
    request.Indexed = true;
    request.IndicesAre16Bit = true;
    request.VertexCount = 3U;
    request.Topology = ::Oot3d::Renderer::PicaTopology::TriangleList;
    request.TextureCoordinates.Eligibility = PicaGrassTextureCoordinateEligibility::Applied;
    return request;
}

} // namespace

TEST(Oot3dGrassGeometryRegistry, ReusesDecodedVerticesAndExpandedIndicesByContentVersion) {
    const std::vector<std::array<float, 3>> positions{
        { 0.0F, 0.0F, 0.0F },
        { 1.0F, 0.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F },
    };
    const std::vector<std::array<float, 2>> uvs{
        { 0.0F, 0.0F },
        { 1.0F, 0.0F },
        { 0.0F, 1.0F },
    };
    const std::vector<uint16_t> indices{ 0U, 1U, 2U };
    const auto positionBytes = BytesOf(positions);
    const auto uvBytes = BytesOf(uvs);
    const auto indexBytes = BytesOf(indices);
    auto request = MakeRequest(positionBytes, uvBytes, indexBytes);

    Fast::Oot3d::GrassGeometryRegistry registry(4U);
    const auto first = registry.Resolve(request);
    ASSERT_NE(first.Geometry, nullptr);
    EXPECT_FALSE(first.CacheHit);
    EXPECT_TRUE(first.Cacheable);
    ASSERT_NE(first.Geometry->Vertices, nullptr);
    ASSERT_NE(first.Geometry->Indices, nullptr);
    EXPECT_EQ(first.Geometry->Vertices->size(), 3U);
    EXPECT_EQ(first.Geometry->Indices->size(), 3U);
    const auto vertices = first.Geometry->Vertices;
    const auto expanded = first.Geometry->Indices;

    request.FrameId = 11U;
    const auto second = registry.Resolve(request);
    ASSERT_NE(second.Geometry, nullptr);
    EXPECT_TRUE(second.CacheHit);
    EXPECT_EQ(second.Geometry->Vertices, vertices);
    EXPECT_EQ(second.Geometry->Indices, expanded);
    const auto stats = registry.Stats();
    EXPECT_EQ(stats.Hits, 1U);
    EXPECT_EQ(stats.Misses, 1U);
}

TEST(Oot3dGrassGeometryRegistry, RebuildsOnlyWhenNativeContentVersionChanges) {
    const std::vector<std::array<float, 3>> positions{
        { 0.0F, 0.0F, 0.0F },
        { 1.0F, 0.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F },
    };
    const std::vector<std::array<float, 2>> uvs{
        { 0.0F, 0.0F },
        { 1.0F, 0.0F },
        { 0.0F, 1.0F },
    };
    const std::vector<uint16_t> indices{ 0U, 1U, 2U };
    const auto positionBytes = BytesOf(positions);
    const auto uvBytes = BytesOf(uvs);
    const auto indexBytes = BytesOf(indices);
    auto request = MakeRequest(positionBytes, uvBytes, indexBytes);

    Fast::Oot3d::GrassGeometryRegistry registry(4U);
    const auto first = registry.Resolve(request);
    ASSERT_NE(first.Geometry, nullptr);
    const uint64_t firstVersion = first.Geometry->ContentVersion;
    const uint64_t firstAnchorVersion = first.Geometry->AnchorVersion;
    request.ContentVersion = 8U;
    request.FrameId = 12U;
    const auto updated = registry.Resolve(request);
    ASSERT_NE(updated.Geometry, nullptr);
    EXPECT_FALSE(updated.CacheHit);
    EXPECT_NE(updated.Geometry->ContentVersion, firstVersion);
    EXPECT_EQ(updated.Geometry->AnchorVersion, firstAnchorVersion);
    const auto stats = registry.Stats();
    EXPECT_EQ(stats.Misses, 2U);
    EXPECT_EQ(stats.Updates, 1U);
    EXPECT_EQ(stats.Entries, 1U);

    auto changedUvs = uvs;
    changedUvs[0][0] = 0.5F;
    const auto changedUvBytes = BytesOf(changedUvs);
    request.TexCoord0.Bytes = changedUvBytes;
    request.ContentVersion = 9U;
    const auto uvUpdate = registry.Resolve(request);
    ASSERT_NE(uvUpdate.Geometry, nullptr);
    EXPECT_EQ(uvUpdate.Geometry->AnchorVersion, firstAnchorVersion);

    auto changedPositions = positions;
    changedPositions[0][0] = 50.0F;
    const auto changedPositionBytes = BytesOf(changedPositions);
    request.Position.Bytes = changedPositionBytes;
    request.ContentVersion = 10U;
    const auto geometryUpdate = registry.Resolve(request);
    ASSERT_NE(geometryUpdate.Geometry, nullptr);
    EXPECT_NE(geometryUpdate.Geometry->AnchorVersion, firstAnchorVersion);
}
