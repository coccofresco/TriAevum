#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/renderer3ds/pica_gi_geometry.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Extension {

struct BrixelizerSceneStats {
    uint64_t Updates = 0;
    uint64_t CollectedInstances = 0;     // last collection
    uint64_t RejectedInstances = 0;      // last collection: arena or budget exhausted
    uint32_t StaticInstances = 0;        // resident
    uint32_t DynamicInstances = 0;       // last update
    uint64_t DynamicTriangles = 0;       // last update
    uint64_t StaticInstancesCreated = 0; // total
    uint64_t StaticInstancesEvicted = 0; // total
    uint32_t ResidentMeshes = 0;
    uint64_t ArenaVertexBytes = 0;       // in use
    uint64_t ArenaIndexBytes = 0;        // in use
    uint64_t GpuScratchBytes = 0;
    // Delayed GPU readback of the Brixelizer context (not this update).
    uint32_t FreeBricks = 0;
    uint32_t StaticBricksAllocated = 0;
    uint32_t DynamicBricksAllocated = 0;
    // Largest per-cascade demand seen so far (sizes the update budgets).
    uint32_t PeakReferences = 0;
    uint32_t PeakTriangles = 0; // bytes of triangle swap
    uint32_t PeakBricks = 0;
    uint64_t BudgetOverflows = 0; // updates whose demand exceeded a scratch budget
    // GPU time of the update (timestamp queries): last, maximum and sum over
    // GpuSamples measured updates. Debug-view updates are included.
    double GpuMilliseconds = 0.0;
    double GpuMillisecondsMax = 0.0;
    double GpuMillisecondsTotal = 0.0;
    uint64_t GpuSamples = 0;
};

// Developer inspection of the field: Brixelizer's ray-marched debug
// visualization rendered with the scene camera into a private image, then
// written to `Path` (binary PPM) once the GPU has finished. Matrices are the
// FidelityFX clip-space ones used by the other FidelityFX passes
// (PicaSurfaceCoordinates::FidelityFxClip), column-major.
struct BrixelizerDebugView {
    std::array<float, 16> Projection{};
    std::array<float, 16> WorldToClip{};
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Mode = 3; // FFX_BRIXELIZER_TRACE_DEBUG_MODE_*: 0 distance, 3 normals, 5 cascade
    std::string Path;
};

// Provider-private owner of the FidelityFX Brixelizer context: the sparse
// distance field of the scene's native opaque geometry, built from
// Renderer3ds::PicaGiInstance records of one native frame at a time.
//
// Rigid placements seen in two consecutive native frames become static
// instances and persist (surfaces the camera no longer sees stay in the
// field); moving, skinned or unproven content is resubmitted every update as
// dynamic instances. Static placements not seen for a while are evicted.
// The field is centred on the camera. Nothing here touches native targets.
class BrixelizerScene final {
  public:
    static constexpr uint32_t kCascadeCount = 8;
    // Edge of a cascade-0 voxel in native world units; each cascade doubles.
    static constexpr float kBaseVoxelSize = 4.0F;
    // First merged (static + dynamic) cascade: the one ray marching uses.
    static constexpr uint32_t kMergedCascadeBase = 2 * kCascadeCount;
    // Diffuse GI contexts (one per composed scene surface) that trace this
    // field through its FidelityFX backend.
    static constexpr uint32_t kMaxGiSurfaces = 3;

    BrixelizerScene();
    ~BrixelizerScene();
    BrixelizerScene(const BrixelizerScene&) = delete;
    BrixelizerScene& operator=(const BrixelizerScene&) = delete;

    // Whether this build and device can run Brixelizer (compiled SDK, R8
    // storage images, the descriptor-indexing features its shaders use).
    [[nodiscard]] static bool Supported(VkInstance instance, VkPhysicalDevice physicalDevice, std::string& reason);

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t frameSlots);
    void Shutdown();

    // Geometry of native frame `sourceFrame`, from the first execution that
    // records it (`executionKey`, e.g. its render-target namespace). Later
    // executions of the same native frame are ignored.
    [[nodiscard]] bool AcceptsExecution(uint64_t sourceFrame, uint64_t executionKey);
    void Submit(const Renderer3ds::PicaGiInstance& instance);

    // Builds the field from the pending native frame, if any. Records compute
    // work into `commandBuffer` outside any render pass; returns false when
    // nothing was pending or the update failed (see UnavailableReason).
    bool Update(VkCommandBuffer commandBuffer, uint32_t frameSlot,
                const std::array<float, 3>& center, const BrixelizerDebugView* debug = nullptr);
    // Scene change or state load: every instance leaves the field.
    void Reset();

    [[nodiscard]] bool Available() const noexcept;
    [[nodiscard]] const BrixelizerSceneStats& Stats() const noexcept;
    [[nodiscard]] const std::string& UnavailableReason() const noexcept;

  private:
    friend struct BrixelizerSceneGiAccess;
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Extension

#endif
