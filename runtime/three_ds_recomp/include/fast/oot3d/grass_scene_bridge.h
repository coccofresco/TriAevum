#pragma once

#include "fast/oot3d/grass_shading_environment.h"
#include "fast/oot3d/grass_surface_extractor.h"
#include "fast/oot3d/grass_overlay_mask.h"

#include "fast/oot3d/world_debug_lines.h"
#include <array>
#include <chrono>
#include <future>
#include <optional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Fast::Oot3d {

struct GrassSceneMesh {
    uint64_t SubmissionId = 0;
    uint64_t GeometryId = 0;
    uint64_t AnchorVersion = 0;
    uint64_t ContentVersion = 0;
    uint64_t InstanceId = 0;
    // The same instance in whichever render target it was drawn into: the
    // draw identity and its occurrence within that target. World placement
    // and overlay versions use it, so a scene redrawn into another target
    // (a pause page) keeps its placements. Zero: use InstanceId.
    uint64_t WorldInstanceId = 0;
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
    uint64_t DrawOrder = 0;
    uint64_t SequenceId = 0;
    uint64_t NativeTextureIdentity = 0;
    uint64_t ConfiguredRuleHash = 0;
    uint32_t ConfiguredRuleId = 0;
    // Non-null only for room overlays; these are never indexed as grass sources.
    std::shared_ptr<const GrassOverlayAlpha> Overlay;
    std::shared_ptr<const std::vector<float>> Alphas;
    uint64_t CoordinateVersion = 0;
    // Native draw memo already resolved the immutable catalog identity.
    bool ObservedTextureResolved = false;
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

// Frames between placement changes driven by covering overlays.
inline constexpr uint64_t kGrassOverlaySettleFrames = 20;

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
    [[nodiscard]] std::vector<GrassOverlayFootprint> ObservedMasks() const;
    void HighlightMask(std::optional<GrassTextureSelector> texture);
    [[nodiscard]] WorldDebugLineSet MaskHighlight(const GrassSceneScope& scope) const;
    [[nodiscard]] std::shared_ptr<const GrassOverlaySet> OverlaysFor(const GrassSceneMesh& source);

  private:
    struct MaskMeasurement {
        uint64_t Instance = 0, Version = 0, Epoch = 0;
        std::vector<GrassOverlayFootprint> Footprints;
    };
    void CollectMaskMeasurement() const;
    void QueueMaskMeasurement(const GrassSceneMesh& source, const std::vector<const GrassSceneMesh*>& overlays,
                              std::shared_ptr<const GrassOverlaySet> set);
    mutable std::future<MaskMeasurement> mMaskMeasurement;
    mutable std::unordered_map<uint64_t, MaskMeasurement> mMaskMeasurements;
    uint64_t mMeasurementEpoch = 0;
    mutable uint64_t mMaskStatisticsFrame = 0;
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
    struct OverlayHistory {
        uint64_t CoordinateVersion = 0, AnchorVersion = 0, Namespace = 0, LastFrame = 0;
        std::array<float, 16> ModelToWorld{};
        bool Animated = false;
    };
    std::unordered_map<uint64_t, OverlayHistory> mOverlayHistory;
    std::unordered_map<uint64_t, std::shared_ptr<const GrassOverlaySet>> mOverlaySets;
    // A covering overlay change applies at once; another within the settle
    // window waits until it holds that long, so an animated mask keeps its
    // last settled placement instead of rebuilding the source every frame.
    struct PendingOverlayPlacement {
        uint64_t Version = 0, Since = 0;
    };
    std::unordered_map<uint64_t, PendingOverlayPlacement> mPendingOverlayPlacements;
    std::unordered_map<uint64_t, GrassSceneScope> mOverlaySetScopes;
    uint64_t mLatestFrame = 0;
    std::optional<GrassTextureSelector> mHighlightedMask;
    std::chrono::steady_clock::time_point mHighlightUntil;
    size_t mOverlayCount = 0;
    // Only transforms survive draw-liveness pruning, never stale draw payloads.
    std::unordered_map<uint64_t, StaticAnchor> mStaticAnchors;
    uint64_t mAnchorUse = 0;
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>>
        mInstancesByTexture;
};

} // namespace Fast::Oot3d
