#pragma once

#include "fast/oot3d/grass_surface_extractor.h"
#include "fast/oot3d/pica_grass_texture_coordinates.h"
#include "oot3d/renderer/pica_render_backend.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace Fast::Oot3d {

struct GrassGeometryAttributeStream {
    std::span<const uint8_t> Bytes;
    uint32_t ByteStride = 0;
    ::Oot3d::Renderer::PicaVertexFormat Format = ::Oot3d::Renderer::PicaVertexFormat::Float;
    uint8_t ComponentCount = 0;
    uint16_t ByteOffset = 0;
};

struct GrassGeometryRequest {
    uint64_t Identity = 0;
    uint64_t ContentVersion = 0;
    bool IdentityAvailable = false;
    uint64_t FrameId = 0;
    GrassGeometryAttributeStream Position;
    GrassGeometryAttributeStream TexCoord0;
    std::span<const uint8_t> IndexBytes;
    bool Indexed = false;
    bool IndicesAre16Bit = false;
    int32_t BaseVertex = 0;
    uint32_t VertexCount = 0;
    ::Oot3d::Renderer::PicaTopology Topology = ::Oot3d::Renderer::PicaTopology::TriangleList;
    PicaGrassTextureCoordinateTransform TextureCoordinates;
};

struct GrassPreparedGeometry {
    uint64_t GeometryId = 0;
    uint64_t AnchorVersion = 0;
    uint64_t ContentVersion = 0;
    uint64_t LastUsedFrame = 0;
    std::shared_ptr<const std::vector<GrassSourceVertex>> Vertices;
    std::shared_ptr<const std::vector<uint32_t>> Indices;
};

struct GrassGeometryRegistryStats {
    uint64_t Hits = 0;
    uint64_t Misses = 0;
    uint64_t Updates = 0;
    uint64_t DynamicBuilds = 0;
    uint64_t Evictions = 0;
    size_t Entries = 0;
};

struct GrassGeometryResolveResult {
    const GrassPreparedGeometry* Geometry = nullptr;
    bool CacheHit = false;
    bool Cacheable = false;
};

class GrassGeometryRegistry final {
  public:
    explicit GrassGeometryRegistry(size_t capacity = 2048U);
    ~GrassGeometryRegistry();
    GrassGeometryRegistry(const GrassGeometryRegistry&) = delete;
    GrassGeometryRegistry& operator=(const GrassGeometryRegistry&) = delete;

    [[nodiscard]] GrassGeometryResolveResult Resolve(const GrassGeometryRequest& request);
    [[nodiscard]] GrassGeometryRegistryStats Stats() const;
    void Clear();

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d
