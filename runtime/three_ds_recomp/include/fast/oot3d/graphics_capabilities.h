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
    NriNisUpscaler,
    NriFsrUpscaler,
    NriXessUpscaler,
    NriDlssUpscaler,
    PresentTearing,
    ExclusiveFullscreen,
};

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
