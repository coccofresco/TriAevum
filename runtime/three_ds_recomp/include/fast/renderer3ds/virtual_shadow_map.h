#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace Fast::Renderer3ds {

inline constexpr uint32_t kVirtualShadowPageTexels = 128;
inline constexpr uint32_t kVirtualShadowPageBorder = 3;
inline constexpr uint32_t kVirtualShadowPhysicalPageTexels = kVirtualShadowPageTexels + 2 * kVirtualShadowPageBorder;
inline constexpr uint32_t kVirtualShadowGridPages = 8;
inline constexpr uint32_t kVirtualShadowLevelCount = 10;
inline constexpr uint32_t kVirtualShadowTableEntries =
    kVirtualShadowGridPages * kVirtualShadowGridPages * kVirtualShadowLevelCount;
inline constexpr uint32_t kVirtualShadowEmptyPage = 0xffffffffU;

using ShadowMatrix = std::array<float, 16>;
using ShadowVector = std::array<float, 3>;

struct ShadowBounds {
    ShadowVector Minimum{};
    ShadowVector Maximum{};
    bool Available = false;
};

// Why a caster has, or lacks, a content version. Diagnostic only: membership
// in the static layer depends on ContentVersion and Dynamic alone.
enum class VirtualShadowCasterVersionState : uint8_t {
    // No transform program, unreadable uniforms or vertices: no bounds.
    Unavailable = 0,
    // Rigid, homogeneous positions with cacheable geometry: versioned.
    Rigid = 1,
    // Geometry without a stable identity and content version.
    Uncacheable = 2,
    // Skinned by the vertex program: the pose is not part of the version.
    Skinned = 3,
    // Positions with W != 1.
    NonHomogeneous = 4,
};

struct VirtualShadowCaster {
    ShadowBounds WorldBounds;
    // Includes geometry, the world-space deformation, and raster state. Zero
    // means that reuse cannot be proved and this caster invalidates its pages.
    uint64_t ContentVersion = 0;
    // Drawn again in every frame into the composed layer only. Native actor
    // geometry is dynamic unless the title adapter made its owner eligible
    // for the static shadow cache (PicaCompositionAttribution::
    // StaticShadowCacheEligible); an eligible rigid caster that moves changes
    // its ContentVersion, which refreshes the static pages it covers.
    // Unproven deformation (ContentVersion == 0) is always dynamic.
    bool Dynamic = false;
    // Opaque native owner, shared with captured composition metadata.
    uint64_t OwnerIdentity = 0;
    VirtualShadowCasterVersionState VersionState = VirtualShadowCasterVersionState::Unavailable;
    // Diagnostic only: the version of the geometry and its world pose alone,
    // before material or pass state is mixed into ContentVersion.
    uint64_t PoseVersion = 0;
};

struct VirtualShadowLevel {
    int32_t OriginX = 0;
    int32_t OriginY = 0;
    float PageWorldSize = 0.0F;
    uint32_t Reserved = 0;
};
static_assert(sizeof(VirtualShadowLevel) == 16);

struct VirtualShadowLayout {
    // XY are world-anchored light-plane coordinates; Z is normalized depth.
    ShadowMatrix WorldToLight{};
    ShadowVector LightDirection{};
    ShadowVector CameraLight{};
    std::array<VirtualShadowLevel, kVirtualShadowLevelCount> Levels{};
    uint32_t LevelCount = 0;
    uint32_t AtlasResolution = 0;
    float DepthExtent = 0.0F;
    float MaximumDistance = 0.0F;

    [[nodiscard]] bool Valid() const noexcept {
        return LevelCount != 0;
    }
};

[[nodiscard]] VirtualShadowLayout
BuildVirtualShadowLayout(const ShadowVector& cameraWorld, const ShadowVector& directionTowardLight,
                         uint32_t atlasResolution, float maximumDistance,
                         const VirtualShadowLayout* previousLayout = nullptr) noexcept;

[[nodiscard]] ShadowBounds TransformShadowBounds(const ShadowBounds& bounds, const ShadowMatrix& transform) noexcept;

struct VirtualShadowPageKey {
    int32_t X = 0;
    int32_t Y = 0;
    uint32_t Level = 0;
    bool operator==(const VirtualShadowPageKey&) const = default;
};

struct VirtualShadowPageUpdate {
    VirtualShadowPageKey Key;
    uint32_t PhysicalPage = 0;
    uint32_t FirstStaticCaster = 0;
    uint32_t StaticCasterCount = 0;
    uint32_t FirstDynamicCaster = 0;
    uint32_t DynamicCasterCount = 0;
    bool RefreshStatic = false;
};

struct VirtualShadowPageStatistics {
    uint32_t Requested = 0;
    uint32_t Resident = 0;
    uint32_t Reused = 0;
    uint32_t Empty = 0;
    uint32_t Evicted = 0;
    uint32_t Unmapped = 0;
    uint32_t StaticUpdated = 0;
    uint32_t StaticReused = 0;
    // Static pages a construction budget left for later frames.
    uint32_t Pending = 0;
};

// A layout constructed ahead of its display: its static pages are drawn and
// composed without dynamic casters, at most StaticPageBudget per Prepare in
// residency priority order (coarse levels first). Its first display then
// composes only the pages that dynamic casters reach.
struct VirtualShadowConstruction {
    uint32_t StaticPageBudget = 0;
};

// CPU-owned residency shared by graph-owned static and composed depth atlases.
// Every update restores static depth before drawing current dynamic casters,
// including pages whose last dynamic caster disappeared. Prepare never claims
// either image valid until recording succeeds and Commit is called.
class VirtualShadowMapCache final {
  public:
    void Prepare(const VirtualShadowLayout& layout, std::span<const VirtualShadowCaster> casters, uint64_t frameId,
                 const VirtualShadowConstruction* construction = nullptr);
    void Commit() noexcept;
    void Invalidate() noexcept;
    // The composed pages moved to another atlas region: every page is composed
    // again from its retained static depth.
    void InvalidateComposition() noexcept;
    [[nodiscard]] const std::array<uint32_t, kVirtualShadowTableEntries>& Table() const noexcept {
        return mTable;
    }
    [[nodiscard]] std::span<const VirtualShadowPageUpdate> Updates() const noexcept {
        return mUpdates;
    }
    [[nodiscard]] std::span<const uint32_t> StaticCasters(const VirtualShadowPageUpdate& page) const noexcept {
        return std::span(mStaticCasterIndices).subspan(page.FirstStaticCaster, page.StaticCasterCount);
    }
    [[nodiscard]] std::span<const uint32_t> DynamicCasters(const VirtualShadowPageUpdate& page) const noexcept {
        return std::span(mDynamicCasterIndices).subspan(page.FirstDynamicCaster, page.DynamicCasterCount);
    }
    [[nodiscard]] const VirtualShadowPageStatistics& Statistics() const noexcept {
        return mStatistics;
    }

  private:
    struct KeyHash {
        size_t operator()(const VirtualShadowPageKey& key) const noexcept;
    };
    struct PhysicalPage {
        VirtualShadowPageKey Key;
        uint64_t Signature = 0;
        uint64_t PendingSignature = 0;
        uint64_t StaticSignature = 0;
        uint64_t PendingStaticSignature = 0;
        uint64_t LastUsed = 0;
        bool Occupied = false;
        bool Valid = false;
        bool StaticValid = false;
    };
    ShadowMatrix mWorldToLight{};
    uint32_t mResolution = 0;
    std::vector<PhysicalPage> mPages;
    std::unordered_map<VirtualShadowPageKey, uint32_t, KeyHash> mResidency;
    std::array<uint32_t, kVirtualShadowTableEntries> mTable{};
    std::vector<VirtualShadowPageUpdate> mUpdates;
    std::vector<uint32_t> mStaticCasterIndices;
    std::vector<uint32_t> mDynamicCasterIndices;
    std::vector<ShadowBounds> mLightBounds;
    // Casters that can reach each level's grid, in caster order.
    std::vector<uint32_t> mLevelCasters;
    VirtualShadowPageStatistics mStatistics;
    bool mConstruction = false;
};

} // namespace Fast::Renderer3ds
