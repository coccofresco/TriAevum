#pragma once

#include "fast/oot3d/pica_attachment_contract.h"
#include "fast/renderer/extension_contract.h"
#include "fast/renderer/extension_resource_graph.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Fast::Oot3d {

using EffectStage = ::Fast::Renderer::ExtensionStage;
using EffectContractKind = ::Fast::Renderer::ExtensionContractKind;

enum class EffectResource : uint8_t {
    PicaSceneFrame,
    NativeSceneView,
    SceneColor,
    NativeDepth,
    NativeShadow2D,
    NormalGuide,
    MaterialGuide,
    RigidMotionGuide,
    AmbientGuide,
    ExtensionGeometry,
    DirectionalShadowHistory,
    DirectionalShadowMap,
    HierarchicalDepth,
    AmbientOcclusion,
    ReflectionColor,
    OutlineColor,
    MotionVectors,
    ReactiveMask,
    LinearWorkingColor,
    CompositeColor,
    TemporalColor,
    UpscaledColor,
    AntiAliasedColor,
    PresentationOutput,
    FogGuide,
    OutlineGeometryGuide,
    // The composed scene colour of a declared native boundary: the terminal
    // scene output a native consumer (immutable copy, native screen effect)
    // samples at its own order boundary. A boundary pass materializes the
    // remaining scene stages into it, so every scene effect participates
    // through the same declared graph edge instead of a per-effect list.
    SceneComposition,
    // Provider-owned persistent distance field of the scene's native opaque
    // geometry (Brixelizer). Semantic: no image transitions in the graph.
    SceneDistanceField,
    // Diffuse albedo of the opaque scene surface (auxiliary PICA output,
    // written only while global illumination requests it).
    DiffuseAlbedoGuide,
    // Diffuse irradiance traced through the scene distance field (linear,
    // RGBA16F); the composite multiplies it by the diffuse albedo.
    DiffuseIllumination,
    // Specular radiance traced through the scene distance field (linear,
    // RGBA16F); a reflection source resolved through the material guide.
    SpecularIllumination,
    // Persistent depth backing restored into dirty final pages before actors.
    DirectionalShadowStaticCache,
    DirectionalShadowStaticHistory,
    // The display's scene before native UI: its target at the boundary before
    // UI, transferred like the display. An upscaler reconstructs it and lays
    // the display's UI over its output, so the UI never enters the upscaler.
    SceneBeforeUi,
    // Shadow casters of the executing composition sequence, prepared from its
    // published draws before native order reaches them. Semantic and
    // imported: a pass at the opaque boundary reads this sample's casters
    // without waiting for, or reordering, the draws that record them.
    PreparedShadowCasters,
    // Persistent cube-map depth atlas of point lights, read by receivers, and
    // the atlas a producer writes for the current sample.
    PointShadowHistory,
    PointShadowMap,
    // Screen-space contact shadows of the directional shadow light (R8, one
    // per target): the visibility receivers read, and the one the shadow map
    // producer writes for the current sample from its camera depth.
    ContactShadowHistory,
    ContactShadowMap,
    Count,
};
// Resource sets are 32-bit masks in the barrier and residency plans.
static_assert(static_cast<uint32_t>(EffectResource::Count) <= 64U);

[[nodiscard]] ::Fast::Renderer::ExtensionResourceIdentity
BuildOot3dEffectResourceIdentity(EffectResource resource) noexcept;
[[nodiscard]] std::optional<EffectResource>
ResolveOot3dEffectResourceIdentity(
    const ::Fast::Renderer::ExtensionResourceIdentity& identity) noexcept;

using EffectResourceAccess = ::Fast::Renderer::ExtensionResourceAccess;

struct EffectResourceUse {
    EffectResource Resource = EffectResource::SceneColor;
    EffectResourceAccess Access = EffectResourceAccess::Read;
    bool operator==(const EffectResourceUse&) const = default;
};

[[nodiscard]] constexpr bool ReadsEffectResource(EffectResourceAccess access) noexcept {
    return ::Fast::Renderer::ReadsExtensionResource(access);
}

[[nodiscard]] constexpr bool WritesEffectResource(EffectResourceAccess access) noexcept {
    return ::Fast::Renderer::WritesExtensionResource(access);
}

// The declared linear working domain of the display graph. A pass that converts
// scene colour into it is a domain conversion rather than a scene effect, so a
// scene that a native boundary already resolved is still converted for the
// stages that read this domain instead of receiving encoded colour.
[[nodiscard]] constexpr bool IsLinearWorkingDomain(
    EffectResource resource) noexcept {
    return resource == EffectResource::LinearWorkingColor;
}

// Auxiliary scene data is consumed by scene or presentation stages for their
// own algorithms and is never baked into scene colour, so a boundary that
// resolves the scene leaves it re-derivable. Everything else the graph produces
// is colour: a new scene pass's colour output is resolved by the boundary
// without being declared here.
[[nodiscard]] constexpr bool IsAuxiliarySceneData(
    EffectResource resource) noexcept {
    switch (resource) {
        case EffectResource::PicaSceneFrame:
        case EffectResource::NativeSceneView:
        case EffectResource::PreparedShadowCasters:
        case EffectResource::ExtensionGeometry:
        case EffectResource::NativeDepth:
        case EffectResource::NativeShadow2D:
        case EffectResource::DirectionalShadowHistory:
        case EffectResource::DirectionalShadowMap:
        case EffectResource::DirectionalShadowStaticCache:
        case EffectResource::DirectionalShadowStaticHistory:
        case EffectResource::PointShadowHistory:
        case EffectResource::PointShadowMap:
        case EffectResource::ContactShadowHistory:
        case EffectResource::ContactShadowMap:
        case EffectResource::HierarchicalDepth:
        case EffectResource::MotionVectors:
        case EffectResource::ReactiveMask:
        case EffectResource::NormalGuide:
        case EffectResource::MaterialGuide:
        case EffectResource::RigidMotionGuide:
        case EffectResource::AmbientGuide:
        case EffectResource::FogGuide:
        case EffectResource::OutlineGeometryGuide:
        case EffectResource::DiffuseAlbedoGuide:
        case EffectResource::Count:
            return true;
        default:
            return false;
    }
}

struct EffectPass {
    std::string Name;
    std::vector<std::string> DependsOn;
    bool Enabled = true;
    EffectStage Stage = EffectStage::AfterOpaque;
    EffectContractKind Contract = EffectContractKind::ComposerPass;
    std::vector<EffectResourceUse> Resources;
    // Late native anchor for an auxiliary export with no same-frame consumers.
    std::optional<EffectStage> FallbackStage;
};

struct CompiledEffectPass {
    std::string Name;
    EffectStage Stage = EffectStage::AfterOpaque;
    EffectContractKind Contract = EffectContractKind::ComposerPass;
    std::vector<EffectResourceUse> Resources;
    std::optional<EffectStage> FallbackStage;

    [[nodiscard]] bool ReadsResource(
        EffectResource resource) const noexcept;
};

struct EffectResourceLifetime {
    EffectResource Resource = EffectResource::SceneColor;
    size_t FirstUse = 0;
    size_t LastUse = 0;
    uint32_t ReadCount = 0;
    uint32_t WriteCount = 0;
};

struct EffectResourceBarrier {
    EffectResource Resource = EffectResource::SceneColor;
    std::optional<size_t> ProducerPass;
    size_t ConsumerPass = 0;
    EffectResourceAccess Before = EffectResourceAccess::Write;
    EffectResourceAccess After = EffectResourceAccess::Read;
};

struct CompiledEffectGraph {
    std::vector<std::string> Order;
    std::vector<CompiledEffectPass> Passes;
    std::vector<EffectResourceLifetime> ResourceLifetimes;
    std::vector<EffectResourceBarrier> Barriers;
    std::vector<EffectResource> Exports;
    ::Fast::Renderer::CompiledExtensionResourceGraph PortableResources;
    PicaAttachmentRequirements Attachments;
    std::string Error;
    [[nodiscard]] bool Valid() const {
        return Error.empty();
    }
    [[nodiscard]] const CompiledEffectPass* FindPass(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<EffectResourceLifetime> FindLifetime(EffectResource resource) const noexcept;
    [[nodiscard]] bool ExportsResource(EffectResource resource) const noexcept;
    [[nodiscard]] size_t DeclaredBindingCount() const noexcept;
};

class EffectGraph {
  public:
    bool Add(EffectPass pass);
    bool Export(EffectResource resource);
    void Clear();
    [[nodiscard]] CompiledEffectGraph Compile() const;

  private:
    std::unordered_map<std::string, EffectPass> mPasses;
    std::vector<std::string> mInsertionOrder;
    std::vector<EffectResource> mExports;
};

enum class PicaExtensionPass : uint8_t {
    Guides,
    DirectionalShadowLighting,
    DirectionalShadowMap,
    AmbientOcclusion,
    Reflections,
    TemporalReconstruction,
    InteractiveGrass,
    ToonOutline,
    SceneDistanceField,
    PointShadowLighting,
    PointShadowMap,
    Count,
};

[[nodiscard]] std::string_view PicaExtensionPassName(
    PicaExtensionPass pass) noexcept;

[[nodiscard]] bool PicaExtensionPassEnabled(
    const CompiledEffectGraph& graph,
    PicaExtensionPass pass) noexcept;

// Builds the frame-wide declaration graph used to determine which auxiliary
// PICA outputs the enabled renderer extensions actually require.
[[nodiscard]] CompiledEffectGraph BuildPicaExtensionGraph(const PicaAttachmentFeatureRequests& requests);

} // namespace Fast::Oot3d
