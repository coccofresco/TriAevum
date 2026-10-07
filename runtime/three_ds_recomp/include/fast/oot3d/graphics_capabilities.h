#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Fast::Oot3d {

enum class GraphicsCapability : uint8_t {
    NriInterop,
    NriDirectionalShadows,
    SampledSceneColor,
    SampledDepth,
    ValidViewMetadata,
    WorldOverlayBoundary,
    WorldGeometryInsertionPoint,
    DecodedStaticMesh,
    GrassSceneInstances,
    StableTextureIdentity,
    TexturePixelsOnDemand,
    LinkGrassInteractor,
    GrassComputeInteraction,
    NormalGuide,
    MaterialGuide,
    Msaa2x,
    Msaa4x,
    Msaa8x,
    MultisampledDepthResolve,
    MotionVectors,
    LinearHdrWorkingColor,
    TemporalHistory,
    Smaa1x,
    FidelityFxSssr,
    FidelityFxCacao,
    NriNisUpscaler,
    NriFsrUpscaler,
    NriXessUpscaler,
    NriDlssUpscaler,
    PresentTearing,
    ExclusiveFullscreen,
    NativeSurfaceLighting,
    BrixelizerGlobalIllumination,
    ImmerseMxao,
    // Screen-space diffuse GI (fast/renderer/diffuse_gi_pass.h on this device).
    ScreenSpaceGlobalIllumination,
    // Title-owned, set by the title frontend: the title publishes its scene
    // view (frustum, camera, scene), which the view-dependent effects need.
    TitleSceneView,
    // Title-owned: the title applies the global scene FOV to its projection.
    TitleSceneFov,
};

// Last enumerator, for code that visits every capability.
inline constexpr GraphicsCapability kLastGraphicsCapability =
    GraphicsCapability::TitleSceneFov;

struct GraphicsCapabilityState {
    bool Available = false;
    std::string Reason;
};

class GraphicsCapabilities {
  public:
    void Set(GraphicsCapability capability, bool available,
             std::string reason = {});
    [[nodiscard]] bool Has(GraphicsCapability capability) const;
    [[nodiscard]] const GraphicsCapabilityState&
    Get(GraphicsCapability capability) const;

  private:
    std::unordered_map<GraphicsCapability, GraphicsCapabilityState> mStates;
};

} // namespace Fast::Oot3d
