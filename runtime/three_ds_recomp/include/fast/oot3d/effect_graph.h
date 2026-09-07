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
    Count,
};

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

struct EffectPass {
    std::string Name;
    std::vector<std::string> DependsOn;
    bool Enabled = true;
    EffectStage Stage = EffectStage::AfterOpaque;
    EffectContractKind Contract = EffectContractKind::ComposerPass;
    std::vector<EffectResourceUse> Resources;
};

struct CompiledEffectPass {
    std::string Name;
    EffectStage Stage = EffectStage::AfterOpaque;
    EffectContractKind Contract = EffectContractKind::ComposerPass;
    std::vector<EffectResourceUse> Resources;

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
