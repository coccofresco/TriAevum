#include "fast/oot3d/grass_geometry_registry.h"

#include "fast/oot3d/grass_primitive_topology.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fast::Oot3d {
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

template <typename Value> void HashValue(uint64_t& hash, const Value& value) noexcept {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    for (size_t index = 0; index < sizeof(Value); ++index) {
        hash = (hash ^ bytes[index]) * kFnvPrime;
    }
}

void HashBytes(uint64_t& hash, std::span<const uint8_t> bytes) noexcept {
    for (const uint8_t value : bytes) {
        hash = (hash ^ value) * kFnvPrime;
    }
}

size_t ComponentBytes(::Oot3d::Renderer::PicaVertexFormat format) noexcept {
    using Format = ::Oot3d::Renderer::PicaVertexFormat;
    switch (format) {
        case Format::SignedByte:
        case Format::UnsignedByte:
            return 1U;
        case Format::SignedShort:
            return 2U;
        case Format::Float:
            return 4U;
    }
    return 0U;
}

float ReadComponent(const uint8_t* source, ::Oot3d::Renderer::PicaVertexFormat format) noexcept {
    using Format = ::Oot3d::Renderer::PicaVertexFormat;
    switch (format) {
        case Format::SignedByte:
            return static_cast<float>(static_cast<int8_t>(*source));
        case Format::UnsignedByte:
            return static_cast<float>(*source);
        case Format::SignedShort: {
            int16_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            return static_cast<float>(value);
        }
        case Format::Float: {
            float value = 0.0F;
            std::memcpy(&value, source, sizeof(value));
            return value;
        }
    }
    return 0.0F;
}

bool ReadAttribute(const GrassGeometryAttributeStream& stream, uint32_t vertexIndex, std::span<float> output) noexcept {
    if (stream.ByteStride == 0U || output.size() > stream.ComponentCount) {
        return false;
    }
    const size_t componentBytes = ComponentBytes(stream.Format);
    const size_t vertexOffset = static_cast<size_t>(vertexIndex) * stream.ByteStride;
    if (componentBytes == 0U || vertexOffset > std::numeric_limits<size_t>::max() - stream.ByteOffset) {
        return false;
    }
    const size_t offset = vertexOffset + stream.ByteOffset;
    if (output.size() > (std::numeric_limits<size_t>::max() - offset) / componentBytes ||
        offset + output.size() * componentBytes > stream.Bytes.size()) {
        return false;
    }
    for (size_t component = 0; component < output.size(); ++component) {
        output[component] = ReadComponent(stream.Bytes.data() + offset + component * componentBytes, stream.Format);
    }
    return true;
}

uint64_t StructuralSignature(const GrassGeometryRequest& request) noexcept {
    uint64_t hash = kFnvOffset;
    const auto hashStream = [&hash](const GrassGeometryAttributeStream& stream) {
        HashValue(hash, stream.ByteStride);
        HashValue(hash, stream.Format);
        HashValue(hash, stream.ComponentCount);
        HashValue(hash, stream.ByteOffset);
        HashValue(hash, stream.Bytes.size());
    };
    hashStream(request.Position);
    hashStream(request.TexCoord0);
    HashValue(hash, request.IndexBytes.size());
    HashValue(hash, request.Indexed);
    HashValue(hash, request.IndicesAre16Bit);
    HashValue(hash, request.BaseVertex);
    HashValue(hash, request.VertexCount);
    HashValue(hash, request.Topology);
    return hash == 0U ? 1U : hash;
}

uint64_t CombinedContentVersion(const GrassGeometryRequest& request, uint64_t structuralSignature,
                                uint64_t textureCoordinateVersion) noexcept {
    uint64_t hash = kFnvOffset;
    HashValue(hash, request.ContentVersion);
    HashValue(hash, structuralSignature);
    HashValue(hash, textureCoordinateVersion);
    return hash == 0U ? 1U : hash;
}

uint64_t DynamicGeometryId(const GrassGeometryRequest& request, uint64_t structuralSignature) noexcept {
    uint64_t hash = kFnvOffset;
    HashValue(hash, structuralSignature);
    HashBytes(hash, request.Position.Bytes);
    if (request.TexCoord0.Bytes.data() != request.Position.Bytes.data() ||
        request.TexCoord0.Bytes.size() != request.Position.Bytes.size()) {
        HashBytes(hash, request.TexCoord0.Bytes);
    }
    HashBytes(hash, request.IndexBytes);
    return hash == 0U ? 1U : hash;
}

bool BuildPreparedGeometry(const GrassGeometryRequest& request, uint64_t structuralSignature,
                           uint64_t textureCoordinateVersion, GrassPreparedGeometry& prepared) {
    if (!request.TextureCoordinates.Applied() || request.Position.ComponentCount < 3U ||
        request.TexCoord0.ComponentCount < 2U || request.Position.ByteStride == 0U ||
        request.TexCoord0.ByteStride == 0U) {
        return false;
    }
    const uint32_t availableVertices =
        std::min<uint32_t>(static_cast<uint32_t>(request.Position.Bytes.size() / request.Position.ByteStride),
                           static_cast<uint32_t>(request.TexCoord0.Bytes.size() / request.TexCoord0.ByteStride));
    if (availableVertices < 3U || availableVertices > 65536U) {
        return false;
    }

    auto vertices = std::make_shared<std::vector<GrassSourceVertex>>();
    uint64_t anchorVersion = kFnvOffset;
    vertices->resize(availableVertices);
    for (uint32_t vertex = 0; vertex < availableVertices; ++vertex) {
        std::array<float, 2> rawUv{};
        if (!ReadAttribute(request.Position, vertex, (*vertices)[vertex].Position) ||
            !ReadAttribute(request.TexCoord0, vertex, rawUv)) {
            return false;
        }
        (*vertices)[vertex].Uv = ApplyPicaGrassTextureCoordinateTransform(request.TextureCoordinates, rawUv);
    }

    std::vector<uint32_t> sourceIndices;
    if (request.Indexed) {
        const size_t indexBytes = request.IndicesAre16Bit ? 2U : 1U;
        if (request.IndexBytes.empty() || request.IndexBytes.size() % indexBytes != 0U) {
            return false;
        }
        const size_t indexCount = request.IndexBytes.size() / indexBytes;
        sourceIndices.reserve(indexCount);
        for (size_t index = 0; index < indexCount; ++index) {
            uint32_t value = request.IndexBytes[index * indexBytes];
            if (request.IndicesAre16Bit) {
                uint16_t wide = 0;
                std::memcpy(&wide, request.IndexBytes.data() + index * 2U, sizeof(wide));
                value = wide;
            }
            const int64_t adjusted = static_cast<int64_t>(value) + request.BaseVertex;
            if (adjusted < 0 || adjusted >= availableVertices) {
                return false;
            }
            sourceIndices.push_back(static_cast<uint32_t>(adjusted));
        }
    } else {
        sourceIndices.resize(std::min(request.VertexCount, availableVertices));
        for (uint32_t index = 0; index < sourceIndices.size(); ++index) {
            sourceIndices[index] = index;
        }
    }

    auto indices =
        std::make_shared<std::vector<uint32_t>>(ExpandGrassPrimitiveIndices(sourceIndices, request.Topology));
    if (indices->size() < 3U) {
        return false;
    }

    prepared = {};
    for (const uint32_t index : *indices) {
        HashValue(anchorVersion, index);
        for (const float component : (*vertices)[index].Position) {
            HashValue(anchorVersion, component);
        }
    }
    prepared.AnchorVersion = anchorVersion;
    prepared.GeometryId = request.IdentityAvailable && request.Identity != 0U
                              ? request.Identity
                              : DynamicGeometryId(request, structuralSignature);
    prepared.ContentVersion = CombinedContentVersion(request, structuralSignature, textureCoordinateVersion);
    prepared.LastUsedFrame = request.FrameId;
    prepared.Vertices = std::move(vertices);
    prepared.Indices = std::move(indices);
    return true;
}

struct RegistryKey {
    uint64_t Identity = 0;
    uint64_t TextureCoordinateVersion = 0;
    uint64_t StructuralSignature = 0;

    bool operator==(const RegistryKey&) const = default;
};

struct RegistryKeyHash {
    size_t operator()(const RegistryKey& key) const noexcept {
        uint64_t hash = kFnvOffset;
        HashValue(hash, key.Identity);
        HashValue(hash, key.TextureCoordinateVersion);
        HashValue(hash, key.StructuralSignature);
        return static_cast<size_t>(hash);
    }
};

} // namespace

struct GrassGeometryRegistry::Impl {
    explicit Impl(size_t capacity) : Capacity(std::max<size_t>(capacity, 1U)) {
    }

    size_t Capacity = 1U;
    std::unordered_map<RegistryKey, GrassPreparedGeometry, RegistryKeyHash> Entries;
    GrassPreparedGeometry Transient;
    GrassGeometryRegistryStats Statistics;
};

GrassGeometryRegistry::GrassGeometryRegistry(size_t capacity) : mImpl(std::make_unique<Impl>(capacity)) {
}

GrassGeometryRegistry::~GrassGeometryRegistry() = default;

GrassGeometryResolveResult GrassGeometryRegistry::Resolve(const GrassGeometryRequest& request) {
    const uint64_t structuralSignature = StructuralSignature(request);
    const uint64_t textureCoordinateVersion = PicaGrassTextureCoordinateTransformVersion(request.TextureCoordinates);
    const uint64_t contentVersion = CombinedContentVersion(request, structuralSignature, textureCoordinateVersion);
    const bool cacheable = request.IdentityAvailable && request.Identity != 0U && request.ContentVersion != 0U;
    if (!cacheable) {
        ++mImpl->Statistics.DynamicBuilds;
        if (!BuildPreparedGeometry(request, structuralSignature, textureCoordinateVersion, mImpl->Transient)) {
            return {};
        }
        return { &mImpl->Transient, false, false };
    }

    const RegistryKey key{
        request.Identity,
        textureCoordinateVersion,
        structuralSignature,
    };
    auto found = mImpl->Entries.find(key);
    if (found != mImpl->Entries.end() && found->second.ContentVersion == contentVersion) {
        found->second.LastUsedFrame = request.FrameId;
        ++mImpl->Statistics.Hits;
        mImpl->Statistics.Entries = mImpl->Entries.size();
        return { &found->second, true, true };
    }

    GrassPreparedGeometry prepared;
    if (!BuildPreparedGeometry(request, structuralSignature, textureCoordinateVersion, prepared)) {
        return {};
    }
    ++mImpl->Statistics.Misses;
    if (found != mImpl->Entries.end()) {
        ++mImpl->Statistics.Updates;
        found->second = std::move(prepared);
    } else {
        if (mImpl->Entries.size() >= mImpl->Capacity) {
            const auto oldest =
                std::min_element(mImpl->Entries.begin(), mImpl->Entries.end(), [](const auto& left, const auto& right) {
                    return left.second.LastUsedFrame < right.second.LastUsedFrame;
                });
            if (oldest != mImpl->Entries.end()) {
                mImpl->Entries.erase(oldest);
                ++mImpl->Statistics.Evictions;
            }
        }
        found = mImpl->Entries.emplace(key, std::move(prepared)).first;
    }
    mImpl->Statistics.Entries = mImpl->Entries.size();
    return { &found->second, false, true };
}

GrassGeometryRegistryStats GrassGeometryRegistry::Stats() const {
    auto stats = mImpl->Statistics;
    stats.Entries = mImpl->Entries.size();
    return stats;
}

void GrassGeometryRegistry::Clear() {
    mImpl->Entries.clear();
    mImpl->Transient = {};
    mImpl->Statistics = {};
}

} // namespace Fast::Oot3d
