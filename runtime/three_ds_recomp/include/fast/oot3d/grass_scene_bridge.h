#pragma once

#include "fast/oot3d/grass_shading_environment.h"
#include "fast/oot3d/grass_surface_extractor.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Fast::Oot3d {

struct GrassSceneMesh {
    uint64_t GeometryId = 0;
    uint64_t AnchorVersion = 0;
    uint64_t ContentVersion = 0;
    uint64_t InstanceId = 0;
    uint64_t RenderTargetNamespace = 0;
    uint32_t FramebufferColorPhysicalAddress = 0;
    uint64_t FrameId = 0;
    uint64_t TextureHash = 0;
    uint64_t ObservedTextureHash = 0;
    uint16_t TextureWidth = 0;
    uint16_t TextureHeight = 0;
    uint8_t MapperSlot = 0;
    GrassTextureWrap MaterialWrapS = GrassTextureWrap::Repeat;
    GrassTextureWrap MaterialWrapT = GrassTextureWrap::Repeat;
    std::array<float, 16> ModelToWorld{};
    // Exact model-to-clip transform captured from the rigid PICA draw. This
    // remains camera-dependent and is refreshed every frame, independently
    // from the persistent world anchor used by placement and interaction.
    std::array<float, 16> PicaModelToClip{};
    bool PicaModelToClipAvailable = false;
    bool TransformBakedIntoVertices = true;
    // Static PICA room draws contain a camera-dependent model-to-view matrix.
    // Once converted to world space, retain the first anchor while the same
    // instance is observed, including gaps between presentation frames.
    bool PreserveWorldAnchor = false;
    GrassShadingEnvironment Shading;
    std::shared_ptr<const std::vector<GrassSourceVertex>> Vertices;
    std::shared_ptr<const std::vector<uint32_t>> Indices;
};

struct GrassSceneScope {
    uint64_t FrameId = 0;
    uint64_t RenderTargetNamespace = 0;
    uint32_t FramebufferColorPhysicalAddress = 0;
};

struct GrassSceneMatch {
    size_t MatchingMeshes = 0;
    std::vector<GrassSceneMesh> ScopedMeshes;
};

class GrassSceneBridge final {
  public:
    static GrassSceneBridge& Instance();
    void Publish(GrassSceneMesh mesh);
    void RemoveInstance(uint64_t instanceId);
    void RemoveRoom(uint64_t roomId);
    void PruneBeforeFrame(uint64_t frameId);
    [[nodiscard]] std::vector<GrassSceneMesh> Matching(
        const GrassTextureSelector& selector,
        const GrassSceneScope* scope = nullptr) const;
    [[nodiscard]] GrassSceneMatch Match(
        const GrassTextureSelector& selector,
        const GrassSceneScope* scope = nullptr) const;
    [[nodiscard]] size_t Size() const;
    [[nodiscard]] std::vector<GrassTextureSelector> AvailableTextures() const;
    void Clear();

  private:
    struct StaticAnchor {
        uint64_t GeometryId;
        uint64_t AnchorVersion;
        uint64_t TextureHash;
        uint64_t RenderTargetNamespace;
        uint64_t LastUse;
        uint16_t TextureWidth;
        uint16_t TextureHeight;
        uint8_t MapperSlot;
        std::array<float, 16> ModelToWorld;
    };
    void ResolveStaticAnchor(GrassSceneMesh& mesh);
    void IndexMesh(const GrassSceneMesh& mesh);
    void UnindexMesh(const GrassSceneMesh& mesh);

    mutable std::mutex mMutex;
    std::unordered_map<uint64_t, GrassSceneMesh> mMeshes;
    // Only transforms survive draw-liveness pruning, never stale draw payloads.
    std::unordered_map<uint64_t, StaticAnchor> mStaticAnchors;
    uint64_t mAnchorUse = 0;
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>>
        mInstancesByTexture;
};

} // namespace Fast::Oot3d
