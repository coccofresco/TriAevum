#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace Fast::Oot3d {

enum class GrassRenderStatus : uint8_t {
    Disabled,
    ContractUnavailable,
    PipelineUnavailable,
    CameraUnavailable,
    NoRules,
    NoBudget,
    NoMasks,
    NoMatchingTexture,
    NoScopedSurfaces,
    PlacementPending,
    NoAnchors,
    Culled,
    Drawn,
    Error,
};

struct GrassRenderTelemetrySnapshot {
    uint64_t FrameId = 0;
    size_t AvailableMeshes = 0;
    size_t AvailableTextures = 0;
    size_t ConfiguredRules = 0;
    size_t ReadyMasks = 0;
    size_t MatchingMeshes = 0;
    size_t ScopedMeshes = 0;
    size_t Placements = 0;
    size_t PlacementCacheHits = 0;
    size_t PlacementCacheMisses = 0;
    size_t PendingPlacements = 0;
    size_t WorldPlacementCacheEntries = 0;
    size_t Clusters = 0;
    uint64_t VisibilityNodesTested = 0;
    uint64_t CandidateClusters = 0;
    uint64_t WorldPlacementCacheHits = 0;
    uint64_t WorldPlacementCacheMisses = 0;
    uint64_t AsyncPlacementBuilds = 0;
    uint64_t AsyncPlacementFailures = 0;
    uint64_t ExtractedAnchors = 0;
    uint64_t EvaluatedAnchors = 0;
    uint32_t VisibleBlades = 0;
    uint32_t DrawCalls = 0;
    uint32_t ClusterDrawInstances = 0;
    uint32_t ClusterRepresentedBlades = 0;
    uint32_t PreparedDrawClusters = 0;
    uint32_t LargeDrawClusters = 0;
    uint32_t CullingWorkers = 0;
    uint64_t UploadedBytes = 0;
    uint64_t DynamicUploadedBytes = 0;
    uint64_t StaticUploadedBytes = 0;
    uint64_t LastMissGeometryId = 0;
    uint64_t LastMissContentVersion = 0;
    double PlacementMilliseconds = 0.0;
    double AsyncPlacementBuildMilliseconds = 0.0;
    double SelectionMilliseconds = 0.0;
    double UploadMilliseconds = 0.0;
    double CpuMilliseconds = 0.0;
    bool Executed = false;
    bool GpuCompaction = false;
    GrassRenderStatus Status = GrassRenderStatus::Disabled;
};

[[nodiscard]] const char* GrassRenderStatusName(
    GrassRenderStatus status) noexcept;

// Renderer-thread measurements retained independently from the transient
// scene bridge, so the settings UI can inspect the last completed grass pass.
class GrassRenderTelemetry final {
  public:
    static GrassRenderTelemetry& Instance();

    void Publish(const GrassRenderTelemetrySnapshot& snapshot);
    [[nodiscard]] GrassRenderTelemetrySnapshot Snapshot() const;
    void Reset();

  private:
    mutable std::mutex mMutex;
    GrassRenderTelemetrySnapshot mSnapshot;
};

} // namespace Fast::Oot3d
