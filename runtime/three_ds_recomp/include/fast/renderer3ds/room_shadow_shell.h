#pragma once

#include "fast/renderer3ds/virtual_shadow_map.h"
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <string>

namespace Fast::Renderer3ds {

enum class ShadowCasterFaces : uint8_t { Native, BothFaces, InteriorShells };
inline constexpr uint32_t kRoomShadowSectorCount = 64;
inline constexpr uint32_t kRoomShadowRasterSize = 512;
inline constexpr uint32_t kRoomShadowShellVersion = 3;
inline constexpr size_t kMaximumRoomShadowTriangles = 500000;
inline constexpr uint64_t kMaximumRoomInteriorVoxels = 64ULL * 1024 * 1024;
struct RoomInteriorSunStats {
    uint64_t Voxels = 0, PeakWorkingBytes = 0, VoxelSamples = 0, RaySteps = 0, InteriorVoxels = 0;
    uint32_t ClosingSteps = 0;
};

struct RoomShadowTriangle {
    std::array<ShadowVector, 3> Positions;
    bool CullBack = true;
    bool AlphaTest = false;
    bool Opaque = true;
};

struct RoomShadowShell {
    uint64_t Fingerprint = 0;
    uint64_t ContentVersion = 0;
    uint32_t RasterSize = kRoomShadowRasterSize;
    bool Interior = true;
    std::vector<ShadowVector> Positions;
    std::vector<uint32_t> Indices;
    std::vector<uint64_t> ShellMasks;
    ShadowBounds Bounds;
};

// Immutable guest-frame publication. A missing mesh means pending/unsupported,
// never an empty room. Without draw-to-room provenance replacement is atomic
// across the loaded rooms: retain native architecture until all are ready.
struct RoomShadowShellInstance {
    uint64_t Fingerprint = 0;
    std::shared_ptr<const RoomShadowShell> Mesh;
    bool Interior = true;
    bool operator==(const RoomShadowShellInstance&) const = default;
};
struct RoomShadowShellScene {
    uint64_t ScopeIdentity = 0;
    std::vector<RoomShadowShellInstance> Rooms;
    [[nodiscard]] bool Ready() const noexcept;
    bool operator==(const RoomShadowShellScene&) const = default;
};

// Y-up Fibonacci hemisphere: y=(s+0.5)/64, azimuth=s*pi*(3-sqrt(5)),
// x=sqrt(1-y*y)*cos(azimuth), z=sqrt(1-y*y)*sin(azimuth).
[[nodiscard]] const std::array<ShadowVector, kRoomShadowSectorCount>& RoomShadowDirections();
[[nodiscard]] uint32_t RoomShadowSector(const ShadowVector& towardLight) noexcept;
// 12-unit interior volume, 6-neighbour sealing-knee sweep (2..20). Four
// samples per triangle look toward the source; omit only when >=3 never
// enter interior. Winding independent, with no restart after leave/re-entry.
// Exterior builds retain geometry and publish zero masks. Bounds/work limits
// or cancellation return null, preserving the native two-sided fallback.
[[nodiscard]] std::shared_ptr<const RoomShadowShell>
BuildRoomShadowShell(uint64_t fingerprint, std::span<const RoomShadowTriangle> triangles,
                     uint32_t rasterSize = kRoomShadowRasterSize, std::stop_token stop = {}, bool interior = true,
                     RoomInteriorSunStats* stats = nullptr);
// The same welded mesh with masks computed earlier for identical content (a
// shipped interior-sun table): null unless there is exactly one mask per
// welded triangle. Exterior meshes accept only zero masks.
[[nodiscard]] std::shared_ptr<const RoomShadowShell>
AssembleRoomShadowShell(uint64_t fingerprint, std::span<const RoomShadowTriangle> triangles,
                        std::span<const uint64_t> masks, uint32_t rasterSize = kRoomShadowRasterSize,
                        bool interior = true);

// Little-endian, versioned local cache. Header and payload are checksummed;
// counts, coordinates, indices, fingerprint and exact length are validated.
[[nodiscard]] std::vector<uint8_t> EncodeRoomShadowShell(const RoomShadowShell& mesh);
[[nodiscard]] std::shared_ptr<const RoomShadowShell> DecodeRoomShadowShell(std::span<const uint8_t> bytes,
                                                                           uint64_t fingerprint, bool interior = true);
[[nodiscard]] std::filesystem::path RoomShadowShellPath(const std::filesystem::path& cacheDirectory,
                                                        uint64_t fingerprint, bool interior = true);
[[nodiscard]] std::shared_ptr<const RoomShadowShell> LoadRoomShadowShell(const std::filesystem::path& cacheDirectory,
                                                                         uint64_t fingerprint, bool interior = true);
bool SaveRoomShadowShell(const std::filesystem::path& cacheDirectory, const RoomShadowShell& mesh,
                         std::string* error = nullptr);

// GPU expansion is performed once on upload: all three vertices carry the
// same two 32-bit mask words, requiring no shaderInt64 or geometry shader.
struct RoomShadowShellVertex {
    ShadowVector Position;
    std::array<uint32_t, 2> Mask;
};
[[nodiscard]] std::vector<RoomShadowShellVertex> RoomShadowShellVertices(const RoomShadowShell& mesh);
// Stable static-page identity, including the selected sector and raster state.
[[nodiscard]] VirtualShadowCaster RoomShadowCaster(const RoomShadowShell& mesh, uint32_t sector,
                                                   uint64_t rasterState = 0) noexcept;
// Same binding 30 transform and root-constant prefix as native page casters.
[[nodiscard]] std::string_view RoomShadowShellVertexShader();

} // namespace Fast::Renderer3ds
