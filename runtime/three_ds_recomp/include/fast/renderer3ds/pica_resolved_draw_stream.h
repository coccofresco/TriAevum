#pragma once

#include "fast/renderer3ds/pica_composition.h"
#include "fast/renderer3ds/pica_scene_payloads.h"
#include "fast/renderer3ds/pica_scene_semantics.h"
#include "fast/renderer3ds/pica_vertex_input_layout.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Fast::Renderer3ds {

inline constexpr uint32_t kPicaResolvedDrawStreamSchemaVersion = 1U;

struct PicaResolvedVertexLayout {
    uint64_t StructuralKey = 0U;
    std::vector<PicaNriPackedVertexBinding> Bindings;
    std::vector<PicaNriPackedVertexAttribute> Attributes;
};

// One renderer-resolved PICA draw. It contains platform GPU semantics and
// stable resource references only; title actor, asset and gameplay identities
// are supplied through a separate title adapter capability.
struct PicaResolvedDrawRecord {
    uint64_t SubmissionId = 0U;
    uint32_t CommandListAddress = 0U;
    uint32_t CommandListOffsetWords = 0U;
    PicaCompositionDomain CompositionDomain = PicaCompositionDomain::Unknown;
    PicaCompositionAttribution Composition;
    PicaSceneResolvedRasterState Raster;
    PicaSceneRenderTargetState RenderTarget;

    uint32_t CanonicalDescriptorSchemaVersion = 0U;
    uint64_t CanonicalVertexProgramId = 0U;
    uint64_t CanonicalFragmentProgramId = 0U;
    uint64_t CanonicalRasterStateId = 0U;
    uint64_t CanonicalPipelineId = 0U;
    uint64_t CanonicalDynamicStateId = 0U;
    uint64_t CanonicalFullRegisterStateId = 0U;

    uint64_t GeometryIdentity = 0U;
    uint64_t GeometryContentVersion = 0U;
    bool GeometryIdentityAvailable = false;

    const PicaSceneShaderProgram* VertexShader = nullptr;
    const PicaSceneShaderProgram* FragmentShader = nullptr;
    const PicaSceneShaderProgram* EffectiveVertexShader = nullptr;
    const PicaSceneShaderProgram* EffectiveFragmentShader = nullptr;
    const PicaResolvedVertexLayout* VertexLayout = nullptr;
    PicaSceneResolvedMaterialState Material;

    PicaSceneBufferResource UniformBuffer;
    uint64_t VertexUniformOffset = 0U;
    uint64_t VertexUniformSize = 0U;
    uint64_t VertexUniformContentVersion = 0U;
    uint64_t PreviousVertexUniformOffset = 0U;
    uint64_t PreviousVertexUniformSize = 0U;
    uint64_t PreviousVertexUniformContentVersion = 0U;
    bool PreviousVertexUniformHistoryAvailable = false;
    uint64_t FragmentUniformOffset = 0U;
    uint64_t FragmentUniformSize = 0U;
    uint64_t FragmentUniformContentVersion = 0U;
    PicaSceneBufferResource GeometryBuffer;
    uint32_t FirstVertexBinding = 0U;
    uint32_t VertexBindingCount = 0U;
    bool Indexed = false;
    uint64_t IndexOffset = 0U;
    uint32_t VertexOrIndexCount = 0U;
    int32_t BaseVertex = 0;
    std::array<PicaSceneTextureBinding, 3U> Textures{};
    uint8_t BoundTextureMask = 0U;

    bool PerspectiveProjection = false;
    PicaNativeLightingState NativeLighting;
    PicaNativeFragmentLightingState NativeFragmentLighting;
    PicaNativeDepthState NativeDepth;
    PicaNativeFogState NativeFog;
    PicaNativeTransformState NativeTransform;
    PicaNativeSkeletonState NativeSkeleton;
};

// Frame-lifetime, allocation-free view over renderer-owned draw metadata and
// GPU bindings. The title adapter owns storage; consumers never copy geometry,
// uniforms, textures or per-draw records.
struct PicaResolvedDrawStreamView {
    uint32_t SchemaVersion = 0U;
    uint64_t FrameId = 0U;
    std::span<const PicaResolvedDrawRecord> Draws;
    std::span<const PicaSceneVertexBufferBinding> VertexBindings;

    [[nodiscard]] bool Available() const noexcept {
        if (SchemaVersion != kPicaResolvedDrawStreamSchemaVersion ||
            FrameId == 0U) {
            return false;
        }
        for (const auto& draw : Draws) {
            const size_t first = draw.FirstVertexBinding;
            const size_t count = draw.VertexBindingCount;
            if (first > VertexBindings.size() ||
                count > VertexBindings.size() - first) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::span<const PicaSceneVertexBufferBinding>
    BindingsFor(const PicaResolvedDrawRecord& draw) const noexcept {
        const size_t first = draw.FirstVertexBinding;
        const size_t count = draw.VertexBindingCount;
        if (first > VertexBindings.size() ||
            count > VertexBindings.size() - first) {
            return {};
        }
        return VertexBindings.subspan(first, count);
    }
};

} // namespace Fast::Renderer3ds
