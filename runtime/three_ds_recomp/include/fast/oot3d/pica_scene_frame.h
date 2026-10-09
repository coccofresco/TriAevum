#pragma once

#include "fast/oot3d/native_frame_composer.h"
#include "fast/oot3d/pica_nri_vertex_input.h"
#include "fast/oot3d/pica_scene_semantics.h"
#include "fast/renderer3ds/pica_resolved_draw_stream.h"
#include "fast/renderer3ds/pica_shader_source_identity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fast::Oot3d {

using ::Fast::Renderer3ds::PicaSceneBufferResource;
using ::Fast::Renderer3ds::PicaSceneCullMode;
using ::Fast::Renderer3ds::PicaSceneFrontFace;
using ::Fast::Renderer3ds::PicaSceneShaderProgram;
using ::Fast::Renderer3ds::PicaSceneShaderStage;
using ::Fast::Renderer3ds::PicaSceneTextureBinding;
using ::Fast::Renderer3ds::PicaSceneTextureImageFormat;
using ::Fast::Renderer3ds::PicaSceneTopology;
using ::Fast::Renderer3ds::PicaSceneVertexBufferBinding;
using PicaSceneVertexLayout =
    ::Fast::Renderer3ds::PicaResolvedVertexLayout;
using PicaSceneDrawRecord =
    ::Fast::Renderer3ds::PicaResolvedDrawRecord;
using ::Fast::Renderer3ds::kPicaResolvedDrawStreamSchemaVersion;
using ::Fast::Renderer3ds::PicaResolvedDrawStreamView;

using ::Fast::Renderer3ds::kPicaSceneRenderTargetStateSchemaVersion;
using ::Fast::Renderer3ds::kPicaSceneResolvedMaterialStateSchemaVersion;
using ::Fast::Renderer3ds::kPicaSceneResolvedRasterStateSchemaVersion;
using ::Fast::Renderer3ds::PicaSceneGpuImageReference;
using ::Fast::Renderer3ds::PicaSceneRenderTargetState;
using ::Fast::Renderer3ds::PicaSceneResolvedMaterialState;
using ::Fast::Renderer3ds::PicaSceneResolvedRasterState;

struct PicaSceneDrawRecordDesc {
    uint64_t SubmissionId = 0;
    uint32_t CommandListAddress = 0;
    uint32_t CommandListOffsetWords = 0;
    ::Fast::Renderer3ds::PicaCompositionDomain CompositionDomain =
        ::Fast::Renderer3ds::PicaCompositionDomain::Unknown;
    ::Fast::Renderer3ds::PicaCompositionAttribution Composition;
    std::optional<std::array<float, 16>> LightingWorldToView;
    std::shared_ptr<const ::Fast::Renderer3ds::PicaSceneDirectionalLights> SceneDirectionals;
    // Point lights the draw's lighting sees: the shared scene snapshot and its
    // own selected native sources (in shader slot order).
    std::shared_ptr<const ::Fast::Renderer3ds::PicaScenePointLights> ScenePoints;
    std::array<::Fast::Renderer3ds::PicaNativeLightSource, 4> NativeLightSources{};
    ::Fast::Renderer3ds::VirtualShadowCaster ShadowCaster;
    PicaSceneResolvedRasterState Raster;
    PicaSceneRenderTargetState RenderTarget;

    uint32_t CanonicalDescriptorSchemaVersion = 0;
    uint64_t CanonicalVertexProgramId = 0;
    uint64_t CanonicalFragmentProgramId = 0;
    uint64_t CanonicalRasterStateId = 0;
    uint64_t CanonicalPipelineId = 0;
    uint64_t CanonicalDynamicStateId = 0;
    uint64_t CanonicalFullRegisterStateId = 0;

    uint64_t VertexShaderKey = 0;
    std::string_view VertexShaderSource;
    uint64_t FragmentShaderKey = 0;
    std::string_view FragmentShaderSource;
    uint64_t EffectiveVertexShaderKey = 0;
    std::string_view EffectiveVertexShaderSource;
    uint64_t EffectiveFragmentShaderKey = 0;
    std::string_view EffectiveFragmentShaderSource;
    uint64_t GeometryIdentity = 0;
    uint64_t GeometryContentVersion = 0;
    bool GeometryIdentityAvailable = false;

    std::span<const PicaNriPackedVertexBinding> VertexLayoutBindings;
    std::span<const PicaNriPackedVertexAttribute> VertexLayoutAttributes;
    PicaSceneResolvedMaterialState Material;

    PicaSceneBufferResource UniformBuffer;
    uint64_t VertexUniformOffset = 0;
    uint64_t VertexUniformSize = 0;
    uint64_t PreviousVertexUniformOffset = 0;
    uint64_t PreviousVertexUniformSize = 0;
    bool PreviousVertexUniformHistoryAvailable = false;
    uint64_t FragmentUniformOffset = 0;
    uint64_t FragmentUniformSize = 0;
    uint64_t SurfaceLightingUniformOffset = 0;
    uint64_t SurfaceLightingUniformSize = 0;
    PicaSceneBufferResource GeometryBuffer;
    std::span<const PicaSceneVertexBufferBinding> VertexBindings;
    bool Indexed = false;
    uint64_t IndexOffset = 0;
    uint32_t VertexOrIndexCount = 0;
    int32_t BaseVertex = 0;
    std::span<const PicaSceneTextureBinding> Textures;
    // Bound only when the fragment program samples the native lighting LUT.
    PicaSceneTextureBinding LightingLut;

    bool PerspectiveProjection = false;
    bool DirectionalShadowCaster = false;
    ::Fast::Renderer3ds::PicaVertexShaderHookLayout VertexShaderHooks;
    // Offsets of the canonical fragment program, when its producer published them.
    ::Fast::Renderer3ds::PicaShaderHookLayout FragmentShaderHooks;
    std::span<const uint8_t> PackedVertexUniforms;
    std::span<const uint8_t> PackedPreviousVertexUniforms;
    std::span<const uint8_t> PackedFragmentUniforms;
    // Produced with the immutable shader source, not reconstructed per draw.
    ::Fast::Renderer3ds::PicaShaderSourceIdentity VertexSourceIdentity;
    ::Fast::Renderer3ds::PicaShaderSourceIdentity FragmentSourceIdentity;
    ::Fast::Renderer3ds::PicaShaderSourceIdentity EffectiveVertexSourceIdentity;
    ::Fast::Renderer3ds::PicaShaderSourceIdentity EffectiveFragmentSourceIdentity;
};

enum class PicaSceneRecordStatus : uint8_t {
    Recorded,
    FrameNotActive,
    IncompleteShader,
    ShaderIdentityConflict,
    InvalidTextureBinding,
    InvalidVertexLayout,
    InvalidUniformRange,
    InvalidGeometryRange,
    InvalidRasterState,
    InvalidRenderTargetState,
    InvalidMaterialState,
    TooManyBindings,
};

struct PicaSceneFrameStats {
    uint32_t DrawCount = 0;
    uint32_t TemporalSampleCount = 0;
    uint32_t SyntheticTemporalSampleCount = 0;
    uint8_t TemporalSampleMultiplier = 0;
    uint8_t TemporalSampleOrdinal = 0;
    uint32_t SceneDomainDrawCount = 0;
    uint32_t UiDomainDrawCount = 0;
    uint32_t ScreenDomainDrawCount = 0;
    uint32_t UnknownDomainDrawCount = 0;
    uint32_t OpaqueWorldLayerDrawCount = 0;
    uint32_t TransparentWorldLayerDrawCount = 0;
    uint32_t AtmosphereLayerDrawCount = 0;
    uint32_t UiLayerDrawCount = 0;
    uint32_t UnknownLayerDrawCount = 0;
    uint32_t NativeCmbPassProvenanceDrawCount = 0;
    uint32_t NativeControlFlowProvenanceDrawCount = 0;
    uint32_t NativeUiLifecycleProvenanceDrawCount = 0;
    uint32_t UnknownProvenanceDrawCount = 0;
    uint32_t ResolvedRasterStateCount = 0;
    uint32_t ResolvedRenderTargetStateCount = 0;
    uint32_t ResolvedRenderTargetGpuResourceCount = 0;
    uint32_t ResolvedMaterialStateCount = 0;
    uint32_t DirectionalShadowCasterCount = 0;
    uint32_t DirectionalShadowNativeLightCasterCount = 0;
    uint32_t NativeLightingStateCount = 0;
    uint32_t NativeFragmentLightingStateCount = 0;
    uint32_t NativeFragmentLightingEnabledDrawCount = 0;
    uint32_t NativeFogStateCount = 0;
    uint32_t NativeTransformStateCount = 0;
    uint32_t PreviousNativeTransformStateCount = 0;
    uint32_t NativeSkeletonStateCount = 0;
    uint32_t PreviousNativeSkeletonStateCount = 0;
    uint32_t RejectedDrawCount = 0;
    uint32_t BoundTextureCount = 0;
    size_t ShaderCatalogEntries = 0;
    size_t VertexLayoutCatalogEntries = 0;
    size_t CanonicalPipelineEntries = 0;
    size_t CanonicalFullRegisterStateEntries = 0;
};

// A frame-lifetime, zero-copy view of resolved PICA draws. Bulk geometry and
// uniform payloads remain in renderer-owned GPU buffers; only immutable draw
// metadata is retained. Shader sources and layouts are interned across frames.
class PicaSceneFrame final {
  public:
    static constexpr uint32_t kSchemaVersion = 13U;

    PicaSceneFrame();
    ~PicaSceneFrame();
    PicaSceneFrame(const PicaSceneFrame&) = delete;
    PicaSceneFrame& operator=(const PicaSceneFrame&) = delete;

    void BeginFrame(uint32_t frameSlot, uint64_t frameId);
    [[nodiscard]] bool SetTemporalSample(NativeFrameTemporalSample sample) noexcept;
    PicaSceneRecordStatus Record(const PicaSceneDrawRecordDesc& desc);
    void Reset();

    [[nodiscard]] bool Active() const noexcept;
    [[nodiscard]] uint32_t FrameSlot() const noexcept;
    [[nodiscard]] uint64_t FrameId() const noexcept;
    [[nodiscard]] const NativeFrameTemporalSample& TemporalSample() const noexcept;
    [[nodiscard]] std::span<const PicaSceneDrawRecord> Draws() const noexcept;
    [[nodiscard]] PicaResolvedDrawStreamView
    ResolvedDrawStream() const noexcept;
    [[nodiscard]] std::span<const PicaSceneVertexBufferBinding>
    VertexBindings(const PicaSceneDrawRecord& draw) const noexcept;
    [[nodiscard]] bool DirectionalShadowCaster(
        size_t drawIndex) const noexcept;
    // Frame evidence for the composition policy: did this frame's scene
    // (perspective world) draws render into that colour target? The renderer
    // resolves a UI-attributed draw on such a target as screen-space frame
    // content rather than as a placed UI element.
    [[nodiscard]] bool HasSceneDrawOnColorTarget(
        uint32_t colorPhysicalAddress) const noexcept;
    [[nodiscard]] PicaSceneFrameStats Stats() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d
