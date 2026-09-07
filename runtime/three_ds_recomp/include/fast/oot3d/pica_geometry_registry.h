#pragma once

#include "fast/oot3d/pica_nri_vertex_input.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Fast::Oot3d {

struct PicaPreparedGeometryBinding {
    uint32_t Binding = 0;
    uint64_t Offset = 0;
    uint64_t Size = 0;
    uint32_t Stride = 0;
};

struct PicaPreparedGeometry {
    uint64_t Identity = 0;
    uint64_t ContentVersion = 0;
    uint64_t StructuralSignature = 0;
    uint64_t LastUsedFrame = 0;
    std::vector<uint8_t> Payload;
    std::vector<PicaPreparedGeometryBinding> SourceBindings;
    std::vector<PicaPreparedGeometryBinding> PackedBindings;
    uint64_t IndexOffset = 0;
    uint32_t VertexOrIndexCount = 0;
    bool Indexed = false;
    bool PackedValid = false;
    std::string PackedError;
};

struct PicaGeometryRequest {
    uint64_t Identity = 0;
    uint64_t ContentVersion = 0;
    bool IdentityAvailable = false;
    uint64_t FrameId = 0;
    std::span<const PicaNriSourceVertexStream> VertexStreams;
    std::span<const PicaNriSourceVertexAttribute> VertexAttributes;
    std::span<const uint8_t> IndexBytes;
    bool Indexed = false;
    bool IndicesAre16Bit = false;
    uint32_t VertexCount = 0;
    bool BuildPackedStreams = true;
};

struct PicaGeometryRegistryStats {
    uint64_t Hits = 0;
    uint64_t Misses = 0;
    uint64_t Updates = 0;
    uint64_t DynamicBuilds = 0;
    uint64_t Evictions = 0;
    uint64_t PackedBuildFailures = 0;
    size_t Entries = 0;
};

struct PicaGeometryResolveResult {
    const PicaPreparedGeometry* Geometry = nullptr;
    bool CacheHit = false;
    bool Cacheable = false;
};

class PicaGeometryRegistry final {
  public:
    explicit PicaGeometryRegistry(size_t capacity = 2048U);
    ~PicaGeometryRegistry();
    PicaGeometryRegistry(const PicaGeometryRegistry&) = delete;
    PicaGeometryRegistry& operator=(const PicaGeometryRegistry&) = delete;

    PicaGeometryResolveResult Resolve(const PicaGeometryRequest& request);
    std::vector<uint64_t> TakeEvictedIdentities();
    [[nodiscard]] PicaGeometryRegistryStats Stats() const;
    void Clear();

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d
